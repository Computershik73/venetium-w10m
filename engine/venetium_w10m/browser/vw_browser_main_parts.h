// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#ifndef VENETIUM_W10M_BROWSER_VW_BROWSER_MAIN_PARTS_H_
#define VENETIUM_W10M_BROWSER_VW_BROWSER_MAIN_PARTS_H_

#include <memory>

#include "base/functional/callback.h"
#include "content/public/browser/browser_main_parts.h"

namespace venetium_w10m {

class VwBrowser;
class VwBrowserContext;

class VwBrowserMainParts : public content::BrowserMainParts {
 public:
  VwBrowserMainParts();
  VwBrowserMainParts(const VwBrowserMainParts&) = delete;
  VwBrowserMainParts& operator=(const VwBrowserMainParts&) = delete;
  ~VwBrowserMainParts() override;

  VwBrowserContext* browser_context() { return browser_context_.get(); }

  // content::BrowserMainParts:
  int PreEarlyInitialization() override;
  int PreMainMessageLoopRun() override;
  void WillRunMainMessageLoop(
      std::unique_ptr<base::RunLoop>& run_loop) override;
  void PostMainMessageLoopRun() override;

  // venetium_w10m_quit, on the UI thread: the tabs go first (the in-process
  // renderer is shut down right after the main loop, and no RenderFrameHost
  // may outlive it), then the loop ends -- at once, or as soon as it runs
  // if the quit came during startup.
  void Quit();

 private:
  std::unique_ptr<VwBrowserContext> browser_context_;
  std::unique_ptr<VwBrowser> browser_;
  base::OnceClosure quit_;
  bool quit_requested_ = false;
};

// Ends the browser's main loop (venetium_w10m_quit). UI thread.
void QuitBrowserMainLoop();

// What the browser process is told before the main loop, whoever runs it
// (the shell's tabs or Chrome's interface): precise timers on a battery
// too, and a ceiling on discardable memory. UI thread.
void TuneBrowserProcess();

}  // namespace venetium_w10m

#endif  // VENETIUM_W10M_BROWSER_VW_BROWSER_MAIN_PARTS_H_
