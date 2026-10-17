// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// What the shell-facing browser (VwBrowser) asks of Chrome when Chrome's own
// interface runs: Chrome has the tabs, the history and the profile then, and
// VwBrowser keeps the rest -- the panel and input (VwPlatform), the
// clipboard, the memory arbiter. Implemented in chrome/vw_chrome_host.cc;
// this header knows nothing of Chrome. UI thread.

#ifndef VENETIUM_W10M_BROWSER_VW_CHROME_BRIDGE_H_
#define VENETIUM_W10M_BROWSER_VW_CHROME_BRIDGE_H_

#include "base/time/time.h"

class GURL;

namespace venetium_w10m {

class VwChromeBridge {
 public:
  virtual ~VwChromeBridge() = default;

  // A URL from outside (the shell's open_url, the first one of a run): a
  // new tab in front, in the window last used.
  virtual void OpenUrl(const GURL& url) = 0;

  // The phone's back button: closes a menu or a dialog, leaves fullscreen,
  // goes back in the tab, or closes a tab a page opened and returns to that
  // page. Whether it did any of it.
  virtual bool Back() = 0;

  // Something the back button would act on may have changed (a window came
  // or went, fullscreen): VwEngine::selected_can_go_back is made current.
  virtual void UpdateBackState() = 0;

  // The app is being suspended, after which the phone may end it without
  // another word: what Chrome keeps is written out now. And resumed.
  virtual void Suspending() = 0;
  virtual void Resumed() = 0;

  // The memory arbiter (vw_memory.h): background tabs whose pages can be
  // discarded, and discarding up to |limit| of them, seen longest ago first.
  virtual int DiscardableTabs(base::TimeDelta spare) = 0;
  virtual int DiscardBackgroundTabs(int limit,
                                    base::TimeDelta spare,
                                    const char* cause) = 0;

  // venetium_w10m_quit: Chrome closes its windows and the run ends.
  virtual void Quit() = 0;
};

}  // namespace venetium_w10m

#endif  // VENETIUM_W10M_BROWSER_VW_CHROME_BRIDGE_H_
