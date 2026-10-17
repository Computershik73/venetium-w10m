// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// The engine's own stages in Chrome's startup and shutdown, run after
// Chrome's parts at each stage:
//   PreEarlyInitialization  the frame sink, before viz's thread exists
//   ToolkitInitialized      the phone's screen, before Chrome would make a
//                           desktop one from the monitors there are not
//   PreProfileInit          the window Chrome's interface goes into (the
//                           panel, input, the window manager), the
//                           clipboard, the shell as opener of URLs and files
//   PostBrowserStart        the shell's queued requests run from here
//   PostMainMessageLoopRun  all of it goes, after Chrome's windows

#ifndef VENETIUM_W10M_CHROME_VW_CHROME_EXTRA_PARTS_H_
#define VENETIUM_W10M_CHROME_VW_CHROME_EXTRA_PARTS_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "chrome/browser/chrome_browser_main_extra_parts.h"

namespace venetium_w10m {

class VwBrowser;
class VwChromeHost;

class VwChromeExtraParts : public ChromeBrowserMainExtraParts {
 public:
  VwChromeExtraParts();
  VwChromeExtraParts(const VwChromeExtraParts&) = delete;
  VwChromeExtraParts& operator=(const VwChromeExtraParts&) = delete;
  ~VwChromeExtraParts() override;

  // ChromeBrowserMainExtraParts:
  void PreEarlyInitialization() override;
  void ToolkitInitialized() override;
  void PreProfileInit() override;
  void PostBrowserStart() override;
  void PostMainMessageLoopRun() override;

 private:
  std::unique_ptr<VwBrowser> browser_;
  // Owned by |browser_|.
  raw_ptr<VwChromeHost> host_ = nullptr;
};

}  // namespace venetium_w10m

#endif  // VENETIUM_W10M_CHROME_VW_CHROME_EXTRA_PARTS_H_
