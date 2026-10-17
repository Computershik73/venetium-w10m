// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// The browser on its UI thread: the window (VwPlatform), the tabs, which one
// is on screen, and the answers to what the shell asks of them. Lives from
// BrowserMainParts::PreMainMessageLoopRun to PostMainMessageLoopRun.
//
// When Chrome's own interface runs, Chrome has the tabs: this keeps the
// window, the clipboard and the memory arbiter, and passes what the shell
// asks of tabs to Chrome (VwChromeBridge). It lives from Chrome's
// PreProfileInit to PostMainMessageLoopRun then.

#ifndef VENETIUM_W10M_BROWSER_VW_BROWSER_H_
#define VENETIUM_W10M_BROWSER_VW_BROWSER_H_

#include <stdint.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "url/gurl.h"
#include "venetium_w10m/browser/vw_chrome_bridge.h"
#include "venetium_w10m/browser/vw_clipboard.h"
#include "venetium_w10m/browser/vw_dialogs.h"
#include "venetium_w10m/browser/vw_engine.h"
#include "venetium_w10m/browser/vw_memory.h"

namespace content {
class WebContents;
}

namespace venetium_w10m {

class VwBrowserContext;
class VwPlatform;
class VwTab;

class VwBrowser {
 public:
  // |chrome|: Chrome's own interface runs, and has the tabs; |context| is
  // null then.
  VwBrowser(VwBrowserContext* context,
            const EngineConfig& config,
            std::unique_ptr<VwChromeBridge> chrome = nullptr);
  VwBrowser(const VwBrowser&) = delete;
  VwBrowser& operator=(const VwBrowser&) = delete;
  ~VwBrowser();

  VwPlatform* platform() { return platform_.get(); }
  VwDialogs* dialogs() { return &dialogs_; }
  VwClipboard* clipboard() { return &clipboard_; }
  VwBrowserContext* context() { return context_; }
  VwMemory* memory() { return &memory_; }
  // Null unless Chrome's own interface runs.
  VwChromeBridge* chrome() { return chrome_.get(); }

  // A new tab under |id| (from VwEngine::NextTabId), showing |url| unless it
  // is empty.
  VwTab* CreateTab(int32_t id, const GURL& url, bool select, int32_t opener);
  // A WebContents a page opened (window.open).
  VwTab* AdoptContents(std::unique_ptr<content::WebContents> contents,
                       int32_t opener,
                       bool select);
  void CloseTab(int32_t id);
  // From inside the tab's own callbacks: closes on a later turn.
  void CloseTabSoon(int32_t id);
  void SelectTab(int32_t id);
  VwTab* tab(int32_t id);
  VwTab* selected();

  // Address-bar text: a URL, or a search.
  void NavigateText(int32_t id, const std::string& text);
  static GURL ResolveText(const std::string& text);

  // The phone's back button (see venetium_w10m_back_button).
  void BackButton();

  void Resize(const gfx::Size& content_pixels);
  void SetDesktopMode(bool desktop);
  // The shell's keyboard covers |pixels| at the bottom (0: hidden).
  void SetKeyboardInset(int32_t pixels);
  void AppStateChanged(bool suspended);
  // The phone's own word on memory (see venetium_w10m_memory_pressure).
  void MemoryPressure(int32_t level);

  // Background tabs whose pages can be unloaded now: not the tab on screen
  // nor one bound to it as opener and opened, not one that left the screen
  // less than |spare| ago, and none that VwTab::CanDiscard keeps.
  int UnloadableTabs(base::TimeDelta spare);
  // Unloads the pages of up to |limit| of them, the ones seen longest ago
  // first; |cause| is what is short, for the log. Each stays a tab and
  // loads its page again when it is selected. Returns how many were
  // unloaded. What they held is free only once the renderer has taken them
  // apart and collected its garbage (VwMemory sees to that).
  int UnloadBackgroundTabs(int limit, base::TimeDelta spare, const char* cause);
  // One line in the log on why nothing was unloaded.
  void NoteKeptTabs(base::TimeDelta spare);

  // From the tabs.
  void OnTabStateChanged(VwTab* tab);
  void OnFullscreenChanged(VwTab* tab, bool on);

 private:
  VwTab* AddTab(std::unique_ptr<VwTab> tab, bool select);
  void PublishSelection();
  // A window came, went, or changed in the platform's window manager.
  void OnWindowsChanged();
  // The tab on screen opened |tab| (window.open) or was opened by it, and
  // the two can still reach each other from script.
  bool TiedToSelected(VwTab* tab);
  // Why |tab|'s page cannot be unloaded now, or null if it can.
  const char* WhyKept(VwTab* tab, base::TimeDelta spare);

  // First: the clipboard is the thread's before a page can touch it.
  VwClipboard clipboard_;
  raw_ptr<VwBrowserContext> context_;
  std::unique_ptr<VwPlatform> platform_;
  VwDialogs dialogs_;
  // After the platform: Chrome's windows are in its root.
  std::unique_ptr<VwChromeBridge> chrome_;
  std::map<int32_t, std::unique_ptr<VwTab>> tabs_;
  // Tabs in the order they were selected, newest last: closing the visible
  // tab goes back to the one seen before it.
  std::vector<int32_t> recent_;
  int32_t selected_ = 0;
  VwMemory memory_{this};
  base::WeakPtrFactory<VwBrowser> weak_factory_{this};
};

}  // namespace venetium_w10m

#endif  // VENETIUM_W10M_BROWSER_VW_BROWSER_H_
