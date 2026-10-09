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

 private:
  std::unique_ptr<VwBrowserContext> browser_context_;
  std::unique_ptr<VwBrowser> browser_;
};

// Ends the browser's main loop (venetium_w10m_quit). UI thread.
void QuitBrowserMainLoop();

}  // namespace venetium_w10m

#endif  // VENETIUM_W10M_BROWSER_VW_BROWSER_MAIN_PARTS_H_
