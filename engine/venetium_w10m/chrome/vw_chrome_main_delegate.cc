// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/chrome/vw_chrome_main_delegate.h"

#include <utility>

#include "base/check.h"
#include "base/win/resource_exhaustion.h"
#include "chrome/common/profiler/main_thread_stack_sampling_profiler.h"
#include "venetium_w10m/chrome/vw_chrome_browser_client.h"
#include "venetium_w10m/chrome/vw_chrome_renderer_client.h"
#include "venetium_w10m/common/vw_log.h"

namespace venetium_w10m {

namespace {

// Chrome's own handler shows a message box and ends the process: a window
// class that cannot be registered means the session's atoms are used up on
// the desktop. On the phone there are no window classes at all, and what
// asked for one (a hidden window, a message window) goes on without it.
void WindowClassUnavailable() {
  static bool noted = false;
  if (!noted) {
    noted = true;
    Note("chrome: no window class to register here (no window system); "
         "going on without the window");
  }
}

}  // namespace

VwChromeMainDelegate::VwChromeMainDelegate(const StartupTimestamps& timestamps)
    : ChromeMainDelegate(timestamps) {}

VwChromeMainDelegate::~VwChromeMainDelegate() = default;

void VwChromeMainDelegate::SandboxInitialized(const std::string& process_type) {
  ChromeMainDelegate::SandboxInitialized(process_type);
  // Chrome's logging has just been set up, and has taken the log message
  // handler: warnings and errors go to the shell's log again.
  ForwardChromiumLogs();
}

std::optional<int> VwChromeMainDelegate::PreBrowserMain() {
  std::optional<int> exit_code = ChromeMainDelegate::PreBrowserMain();
  base::win::SetOnResourceExhaustedFunction(&WindowClassUnavailable);
  return exit_code;
}

bool VwChromeMainDelegate::ShouldHandleConsoleControlEvents() {
  // No console on a phone, and SetConsoleCtrlHandler may not be there.
  return false;
}

content::ContentBrowserClient*
VwChromeMainDelegate::CreateContentBrowserClient() {
  // As ChromeMainDelegate's, with the engine's subclass.
  chrome_content_browser_client_ = std::make_unique<VwChromeBrowserClient>();
  CHECK(sampling_profiler_);
  chrome_content_browser_client_->SetSamplingProfiler(
      std::move(sampling_profiler_));
  return chrome_content_browser_client_.get();
}

content::ContentRendererClient*
VwChromeMainDelegate::CreateContentRendererClient() {
  renderer_client_ = std::make_unique<VwChromeRendererClient>();
  return renderer_client_.get();
}

}  // namespace venetium_w10m
