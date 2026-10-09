// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// The browser on its UI thread: the window (VwPlatform), the tabs, which one
// is on screen, and the answers to what the shell asks of them. Lives from
// BrowserMainParts::PreMainMessageLoopRun to PostMainMessageLoopRun.

#ifndef VENETIUM_W10M_BROWSER_VW_BROWSER_H_
#define VENETIUM_W10M_BROWSER_VW_BROWSER_H_

#include <stdint.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "url/gurl.h"
#include "venetium_w10m/browser/vw_dialogs.h"
#include "venetium_w10m/browser/vw_engine.h"

namespace content {
class WebContents;
}

namespace venetium_w10m {

class VwBrowserContext;
class VwPlatform;
class VwTab;

class VwBrowser {
 public:
  VwBrowser(VwBrowserContext* context, const EngineConfig& config);
  VwBrowser(const VwBrowser&) = delete;
  VwBrowser& operator=(const VwBrowser&) = delete;
  ~VwBrowser();

  VwPlatform* platform() { return platform_.get(); }
  VwDialogs* dialogs() { return &dialogs_; }
  VwBrowserContext* context() { return context_; }

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
  void AppStateChanged(bool suspended);
  void MemoryPressure(int32_t level);

  // From the tabs.
  void OnTabStateChanged(VwTab* tab);
  void OnFullscreenChanged(VwTab* tab, bool on);

 private:
  VwTab* AddTab(std::unique_ptr<VwTab> tab, bool select);
  void PublishSelection();

  raw_ptr<VwBrowserContext> context_;
  std::unique_ptr<VwPlatform> platform_;
  VwDialogs dialogs_;
  std::map<int32_t, std::unique_ptr<VwTab>> tabs_;
  // Tabs in the order they were selected, newest last: closing the visible
  // tab goes back to the one seen before it.
  std::vector<int32_t> recent_;
  int32_t selected_ = 0;
  base::WeakPtrFactory<VwBrowser> weak_factory_{this};
};

}  // namespace venetium_w10m

#endif  // VENETIUM_W10M_BROWSER_VW_BROWSER_H_
