// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/chrome/vw_chrome_extra_parts.h"

#include <string>
#include <utility>

#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/platform_util.h"
#include "ui/display/win/dpi.h"
#include "venetium_w10m/browser/vw_browser.h"
#include "venetium_w10m/browser/vw_browser_main_parts.h"
#include "venetium_w10m/browser/vw_engine.h"
#include "venetium_w10m/browser/vw_frame_sink.h"
#include "venetium_w10m/browser/vw_platform.h"
#include "venetium_w10m/chrome/vw_browser_native_widget.h"
#include "venetium_w10m/chrome/vw_chrome_host.h"
#include "venetium_w10m/common/vw_log.h"

namespace venetium_w10m {

namespace {

// Chrome hands out a URL of another app's, a downloaded file or the folder
// it is in; the shell opens them with the Windows Runtime launcher. Any
// thread.
void OpenThroughShell(platform_util::W10mOpen what, const std::string& target) {
  const venetium_w10m_events events = VwEngine::Get()->events();
  switch (what) {
    case platform_util::W10mOpen::kUrl:
      if (events.launch_uri) {
        events.launch_uri(target.c_str());
      }
      break;
    case platform_util::W10mOpen::kFile:
    case platform_util::W10mOpen::kFolder:
      if (events.open_path) {
        events.open_path(target.c_str(),
                         what == platform_util::W10mOpen::kFolder ? 1 : 0);
      }
      break;
  }
}

}  // namespace

VwChromeExtraParts::VwChromeExtraParts() = default;
VwChromeExtraParts::~VwChromeExtraParts() = default;

void VwChromeExtraParts::PreEarlyInitialization() {
  // Before viz's thread exists: every root surface gets the shell's output.
  InstallFrameSink();
}

void VwChromeExtraParts::ToolkitInitialized() {
  // The phone's one display, as the shell reported it. Chrome's views part
  // makes a desktop screen in PreCreateThreads unless there is one; and
  // display::win's own idea of the scale is for whatever still asks it.
  const EngineConfig config = VwEngine::Get()->config();
  VwPlatform::InstallScreen(config.screen.IsEmpty() ? config.size
                                                    : config.screen,
                            static_cast<float>(config.scale));
  display::win::SetDefaultDeviceScaleFactor(static_cast<float>(config.scale));
}

void VwChromeExtraParts::PreProfileInit() {
  // The window Chrome's interface goes into: before the first profile, whose
  // browser window opens as soon as it is loaded.
  TuneBrowserProcess();
  VwBrowserNativeWidgetFactory::Install();
  platform_util::SetW10mOpenHandler(&OpenThroughShell);
  auto host = std::make_unique<VwChromeHost>();
  host_ = host.get();
  browser_ = std::make_unique<VwBrowser>(nullptr, VwEngine::Get()->config(),
                                         std::move(host));
  Note("chrome: the window is ready for Chrome's interface");
}

void VwChromeExtraParts::PostBrowserStart() {
  host_->Started();
  VwEngine::Get()->BrowserStarted(
      browser_.get(), base::SingleThreadTaskRunner::GetCurrentDefault());
  Note("chrome: browser started");
}

void VwChromeExtraParts::PostMainMessageLoopRun() {
  Note("chrome: main loop ended");
  VwEngine::Get()->BrowserStopping();
  platform_util::SetW10mOpenHandler(nullptr);
  host_ = nullptr;
  browser_.reset();
}

}  // namespace venetium_w10m
