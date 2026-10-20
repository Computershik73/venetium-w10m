// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// Chrome as the shell's requests reach it (VwChromeBridge), on the UI
// thread, for as long as Chrome's browser windows exist.
//
// On a phone the browser is the app: Chrome never runs without a window
// here. A keep-alive holds the run open when the last one closes, and the
// last tab closing opens a new one, as Chrome for Android does; the run
// ends only when the shell asks (venetium_w10m_quit).

#ifndef VENETIUM_W10M_CHROME_VW_CHROME_HOST_H_
#define VENETIUM_W10M_CHROME_VW_CHROME_HOST_H_

#include <map>
#include <memory>
#include <set>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/views/focus/focus_manager.h"
#include "venetium_w10m/browser/vw_chrome_bridge.h"

class ScopedKeepAlive;

namespace venetium_w10m {

class VwChromeHost : public VwChromeBridge,
                     public BrowserCollectionObserver,
                     public content::WebContentsObserver,
                     public views::FocusChangeListener {
 public:
  VwChromeHost();
  VwChromeHost(const VwChromeHost&) = delete;
  VwChromeHost& operator=(const VwChromeHost&) = delete;
  ~VwChromeHost() override;

  // Chrome's first window is up (PostBrowserStart).
  void Started();

  // VwChromeBridge:
  void OpenUrl(const GURL& url) override;
  bool Back() override;
  void UpdateBackState() override;
  void Suspending() override;
  void Resumed() override;
  int DiscardableTabs(base::TimeDelta spare) override;
  int DiscardBackgroundTabs(int limit,
                            base::TimeDelta spare,
                            const char* cause) override;
  void Quit() override;

  // BrowserCollectionObserver:
  void OnBrowserCreated(BrowserWindowInterface* browser) override;
  void OnBrowserClosed(BrowserWindowInterface* browser) override;
  void OnBrowserActivated(BrowserWindowInterface* browser) override;

  // content::WebContentsObserver, for the tab in front:
  void NavigationEntryCommitted(
      const content::LoadCommittedDetails& load_details) override;
  void NavigationListPruned(
      const content::PrunedDetails& pruned_details) override;
  void NavigationEntriesDeleted() override;

  // views::FocusChangeListener, for the browser windows: the address bar
  // taking or leaving the focus changes what the back button does.
  void OnDidChangeFocus(views::View* focused_before,
                        views::View* focused_now) override;
  void OnFocusManagerDestroying(views::FocusManager* focus_manager) override;

 private:
  BrowserWindowInterface* ActiveBrowser();
  // What the back button would do now; with |act|, does it.
  bool BackStep(bool act);
  // Watches the tab in front of the active browser for its history.
  void WatchActiveTab();
  // The last browser window closed by itself: a new one, with a new tab.
  void ReopenIfEmpty();

  std::unique_ptr<ScopedKeepAlive> keep_alive_;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      browsers_{this};
  std::map<BrowserWindowInterface*, base::CallbackListSubscription>
      active_tab_changes_;
  std::set<raw_ptr<views::FocusManager>> focus_managers_;
  bool started_ = false;
  bool quitting_ = false;
  base::WeakPtrFactory<VwChromeHost> weak_factory_{this};
};

}  // namespace venetium_w10m

#endif  // VENETIUM_W10M_CHROME_VW_CHROME_HOST_H_
