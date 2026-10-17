// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// Chrome's own startup, in the engine DLL: ChromeMainDelegate, as
// chrome.dll runs it, with what a UWP app on a phone needs on top. The
// browser and renderer clients are the engine's subclasses of Chrome's
// (the phone's user agent and web preferences, the codec policy, the
// renderer's memory hooks, the engine's own stages of startup).

#ifndef VENETIUM_W10M_CHROME_VW_CHROME_MAIN_DELEGATE_H_
#define VENETIUM_W10M_CHROME_VW_CHROME_MAIN_DELEGATE_H_

#include <memory>
#include <optional>
#include <string>

#include "chrome/app/chrome_main_delegate.h"

namespace venetium_w10m {

class VwChromeRendererClient;

class VwChromeMainDelegate : public ChromeMainDelegate {
 public:
  explicit VwChromeMainDelegate(const StartupTimestamps& timestamps);
  VwChromeMainDelegate(const VwChromeMainDelegate&) = delete;
  VwChromeMainDelegate& operator=(const VwChromeMainDelegate&) = delete;
  ~VwChromeMainDelegate() override;

 protected:
  // ChromeMainDelegate:
  void SandboxInitialized(const std::string& process_type) override;
  std::optional<int> PreBrowserMain() override;
  bool ShouldHandleConsoleControlEvents() override;
  content::ContentBrowserClient* CreateContentBrowserClient() override;
  content::ContentRendererClient* CreateContentRendererClient() override;

 private:
  std::unique_ptr<VwChromeRendererClient> renderer_client_;
};

}  // namespace venetium_w10m

#endif  // VENETIUM_W10M_CHROME_VW_CHROME_MAIN_DELEGATE_H_
