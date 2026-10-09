// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/app/vw_main_delegate.h"

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "content/public/common/content_switches.h"
#include "ui/base/resource/resource_bundle.h"
#include "venetium_w10m/app/vw_content_client.h"
#include "venetium_w10m/browser/vw_browser_client.h"
#include "venetium_w10m/browser/vw_engine.h"
#include "venetium_w10m/common/vw_log.h"
#include "venetium_w10m/renderer/vw_renderer_client.h"

namespace venetium_w10m {

VwMainDelegate::VwMainDelegate() = default;
VwMainDelegate::~VwMainDelegate() = default;

std::optional<int> VwMainDelegate::BasicStartupComplete() {
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();
  logging::LoggingSettings settings;
  // Chromium's own log file only when asked for (--enable-logging): every
  // line is a write on the phone's flash. Warnings and errors reach the
  // shell's log either way (ForwardChromiumLogs).
  if (command_line.HasSwitch(switches::kEnableLogging)) {
    settings.logging_dest = logging::LOG_TO_FILE;
    settings.log_file_path =
        VwEngine::Get()->config().profile_dir.DirName().Append(
            FILE_PATH_LITERAL("chromium.log")).value();
    settings.delete_old = logging::DELETE_OLD_LOG_FILE;
  } else {
    settings.logging_dest = logging::LOG_NONE;
  }
  logging::InitLogging(settings);
  logging::SetLogItems(/*enable_process_id=*/false, /*enable_thread_id=*/true,
                       /*enable_timestamp=*/true, /*enable_tickcount=*/false);
  ForwardChromiumLogs();
  return std::nullopt;
}

void VwMainDelegate::PreSandboxStartup() {
  // venetium_engine.pak lies beside venetium_engine.dll, which is what
  // DIR_ASSETS is on Windows.
  base::FilePath assets;
  base::PathService::Get(base::DIR_ASSETS, &assets);
  const base::FilePath pak =
      assets.Append(FILE_PATH_LITERAL("venetium_engine.pak"));
  NoteF("resources: %s", base::PathExists(pak) ? "venetium_engine.pak"
                                               : "venetium_engine.pak MISSING");
  ui::ResourceBundle::InitSharedInstanceWithPakPath(pak);
}

content::ContentClient* VwMainDelegate::CreateContentClient() {
  content_client_ = std::make_unique<VwContentClient>();
  return content_client_.get();
}

content::ContentBrowserClient* VwMainDelegate::CreateContentBrowserClient() {
  browser_client_ = std::make_unique<VwContentBrowserClient>();
  return browser_client_.get();
}

content::ContentRendererClient*
VwMainDelegate::CreateContentRendererClient() {
  renderer_client_ = std::make_unique<VwContentRendererClient>();
  return renderer_client_.get();
}

}  // namespace venetium_w10m
