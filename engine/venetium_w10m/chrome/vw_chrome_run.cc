// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/chrome/vw_chrome_run.h"

#include <memory>
#include <string>
#include <utility>

#include "base/base_switches.h"
#include "base/command_line.h"
#include "base/debug/dump_without_crashing.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/sampling_heap_profiler/poisson_allocation_sampler.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/app/startup_timestamps.h"
#include "chrome/chrome_elf/chrome_elf_main.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/install_static/install_details.h"
#include "chrome/install_static/install_modes.h"
#include "chrome/install_static/install_util.h"
#include "components/sync/base/command_line_switches.h"
#include "content/public/app/content_main.h"
#include "content/public/common/content_switches.h"
#include "sandbox/win/src/sandbox_types.h"
#include "ui/base/ui_base_switches.h"
#include "venetium_w10m/browser/vw_engine.h"
#include "venetium_w10m/chrome/vw_chrome_main_delegate.h"
#include "venetium_w10m/common/vw_log.h"

namespace venetium_w10m {

namespace {

// Chrome's profile lives beside the engine's own (which the shell's
// interface uses): LocalState\User Data, with its one profile "Default".
base::FilePath UserDataDir(const EngineConfig& config) {
  return config.profile_dir.DirName().Append(FILE_PATH_LITERAL("User Data"));
}

// A feature list switch adds to what is already there: a switch given twice
// keeps only its last value.
void AddToFeatureList(base::CommandLine* command_line,
                      const char* name,
                      const std::string& features) {
  std::string value = command_line->GetSwitchValueASCII(name);
  if (!value.empty()) {
    value += ",";
  }
  value += features;
  command_line->AppendSwitchASCII(name, value);
}

}  // namespace

void AddChromeSwitches(const EngineConfig& config,
                       base::CommandLine* command_line) {
  command_line->AppendSwitchPath(switches::kUserDataDir, UserDataDir(config));
  command_line->AppendSwitchASCII(switches::kProfileDirectory, "Default");
  // No first-run window, import, What's New, search engine choice or
  // default-browser prompt: the app is the browser.
  command_line->AppendSwitch(switches::kNoFirstRun);
  command_line->AppendSwitch(switches::kNoDefaultBrowserCheck);
  command_line->AppendSwitch(switches::kDisableDefaultApps);
  // Nothing fetched behind the user's back, and no components downloaded
  // and unpacked into the profile: a phone's data, flash and address space.
  command_line->AppendSwitch(switches::kDisableBackgroundNetworking);
  command_line->AppendSwitch(switches::kDisableComponentUpdate);
  command_line->AppendSwitch(syncer::kDisableSync);
  command_line->AppendSwitch(switches::kDisableDomainReliability);
  command_line->AppendSwitch(switches::kNoPings);
  // A phone ends a suspended app; that is not a crash to apologise for.
  command_line->AppendSwitch(switches::kHideCrashRestoreBubble);
  // Chrome's toolbar and tabs sized for fingers. Chrome reads it once.
  command_line->AppendSwitchASCII(switches::kTopChromeTouchUi,
                                  switches::kTopChromeTouchUiEnabled);
  // A phone's toolbar, as Chrome for Android's: no profile button beside
  // the address bar (Supermium's switch; one profile, no sign-in here).
  command_line->AppendSwitchASCII("show-avatar-button", "never");
  // No system proxy to poll for (WinHTTP's is a stub here).
  command_line->AppendSwitch(switches::kNoProxyServer);
  // A renderer that is in this process cannot be ended for a bad message.
  command_line->AppendSwitch(switches::kDisableKillAfterBadIPC);
  // Chrome's notifications are popups of its own on Windows 10 before 1803.
  command_line->AppendSwitch(switches::kDisableNotifications);
  AddToFeatureList(
      command_line, switches::kDisableFeatures,
      // One process, one profile: a profile is not destroyed with its last
      // window (that shuts the in-process renderer down for good).
      "DestroyProfileOnBrowserClose,"
      // Windows' location permission (1803+), occlusion through window
      // events, clipboard monitoring through a message window: none here.
      "WinSystemLocationPermission,CalculateNativeWinOcclusion,"
      "PlatformClipboardMonitor,WebUsbDeviceDetection,WindowsSystemTracing,"
      // What costs memory, flash or data a phone has little of.
      "PreloadTopChromeWebUI,Glic,GlicWebContentsWarming,PersistentHistograms,"
      "OptimizationHints,OptimizationTargetPrediction,"
      "OptimizationGuideModelExecution,OptimizationGuideOnDeviceModel,"
      "PageContentAnnotations,HistoryEmbeddings,SegmentationPlatform,"
      "MediaRouter,DialMediaRouteProvider,LensOverlay,LensStandalone,"
      "NtpLogo,NtpCustomizeChromeAutoOpen,"
      // Google's AI Mode in the address bar and its suggestions: a page
      // that needs a desktop's memory, and Google as the search engine.
      "AimEnabled,AiModeOmniboxEntryPoint");
  // Tracing and video capture as utilities would hold the one utility
  // thread a single process has for good; in the browser they do not.
  AddToFeatureList(command_line, switches::kEnableFeatures,
                   "TracingServiceInProcess,"
                   "RunVideoCaptureServiceInBrowserProcess,"
                   // The tab switcher's pictures (VwTabGrid): Chrome keeps
                   // each tab's as it goes to the background. Venetium 0056
                   // turns them off on ARM32 tablets, which have no use for
                   // them.
                   "TabHoverCardImages");
  // A URL from outside (the shell's first one): a tab of its own.
  if (!config.initial_url.empty()) {
    command_line->AppendArg(config.initial_url);
  }
}

void SetUpChromePaths(const EngineConfig& config) {
  const base::FilePath local_state = config.profile_dir.DirName();
  const base::FilePath downloads =
      local_state.Append(FILE_PATH_LITERAL("Downloads"));
  base::PathService::Override(chrome::DIR_DEFAULT_DOWNLOADS, downloads);
  base::PathService::Override(chrome::DIR_DEFAULT_DOWNLOADS_SAFE, downloads);
  base::PathService::Override(chrome::DIR_USER_DOCUMENTS, local_state);
  // Spelling dictionaries are downloaded; the package's folder is read-only.
  base::PathService::Override(
      chrome::DIR_APP_DICTIONARIES,
      local_state.Append(FILE_PATH_LITERAL("Dictionaries")));
  base::CreateDirectory(UserDataDir(config));
}

int RunChrome(HINSTANCE instance) {
  // What chrome.exe and chrome_elf.dll do before chrome.dll runs: the
  // install's details (this package, per-user, no channel: system-level
  // would wake app-bound encryption and browser isolation), the process
  // type, and where a dump without crashing goes.
  install_static::InitializeProcessType();
  auto details = std::make_unique<install_static::PrimaryInstallDetails>();
  details->set_mode(&install_static::kInstallModes[0]);
  details->set_channel(std::wstring());
  details->set_system_level(false);
  install_static::InstallDetails::SetForProcess(std::move(details));
  base::debug::SetDumpWithoutCrashingFunction(&DumpProcessWithoutCrash);
  // What ChromeMain does before ContentMain.
  base::PoissonAllocationSampler::Init();

  StartupTimestamps timestamps;
  timestamps.exe_entry_point_ticks = base::TimeTicks::Now();
  VwChromeMainDelegate delegate(timestamps);
  content::ContentMainParams params(&delegate);
  params.instance = instance;
  // As for the engine's own run: no broker, no target, an unsandboxed
  // browser process.
  sandbox::SandboxInterfaceInfo sandbox_info = {};
  params.sandbox_info = &sandbox_info;
  Note("run: Chrome's own interface");
  const int result = content::ContentMain(std::move(params));
  NoteF("run: ContentMain returned %d", result);
  return result;
}

}  // namespace venetium_w10m
