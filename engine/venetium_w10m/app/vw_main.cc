// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// venetium_w10m_run: from the shell's configuration to content::ContentMain.
//
// The shell calls this on a thread of its own, which becomes Chromium's
// browser UI thread for the rest of the run. Everything a desktop Chromium
// would learn from its command line, its process type and the shell
// namespace (SHGetKnownFolderPath, absent on a phone) is put in place first.

#include <windows.h>

#include <string>
#include <vector>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/path_service.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "content/public/app/content_main.h"
#include "content/public/common/content_switches.h"
#include "sandbox/policy/switches.h"
#include "third_party/blink/public/common/switches.h"
#include "venetium_w10m/app/vw_main_delegate.h"
#include "venetium_w10m/browser/vw_engine.h"
#include "venetium_w10m/capi/venetium_w10m_capi.h"
#include "venetium_w10m/common/vw_log.h"

namespace venetium_w10m {

namespace {

base::FilePath PathOf(const wchar_t* text) {
  return text ? base::FilePath(text) : base::FilePath();
}

// The engine DLL's own module handle: ContentMain wants the instance the
// resources (and DIR_MODULE) belong to, which is this DLL, not the exe.
HINSTANCE ThisModule() {
  HMODULE module = nullptr;
  ::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&ThisModule), &module);
  return module;
}

void SetUpCommandLine(const EngineConfig& config) {
  // On Windows this reads GetCommandLineW: the shell's exe and nothing else.
  base::CommandLine::Init(0, nullptr);
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();

  // One process: an app cannot start others. The renderer, the GPU thread
  // and every utility service run on threads of this one.
  command_line->AppendSwitch(switches::kSingleProcess);
  command_line->AppendSwitch(switches::kInProcessGPU);
  command_line->AppendSwitch(sandbox::policy::switches::kNoSandbox);
  // No HWND for the GPU to draw in yet: software raster and compositing,
  // frames to the shell's bitmap (vw_frame_sink).
  command_line->AppendSwitch(switches::kDisableGpu);
  command_line->AppendSwitch(switches::kDisableGpuCompositing);
  // RenderWidgetHostViewAura's child HWND for accessibility and legacy
  // input: no HWNDs here.
  command_line->AppendSwitch(switches::kDisableLegacyIntermediateWindow);
  command_line->AppendSwitch(switches::kEnableViewport);
  command_line->AppendSwitchASCII(switches::kTouchEventFeatureDetection,
                                  "enabled");
  // Pages kept alive for back/forward cost memory a phone does not have.
  command_line->AppendSwitchASCII(switches::kDisableFeatures,
                                  "BackForwardCache");
  if (!config.jit) {
    // V8 without a JIT: no executable memory needed at all, which is all an
    // app container without the codeGeneration capability allows.
    command_line->AppendSwitchASCII(blink::switches::kJavaScriptFlags,
                                    "--jitless");
  }

  // The shell's extras, "--name[=value]" separated by spaces.
  for (const std::string& token :
       base::SplitString(config.switches, " ", base::TRIM_WHITESPACE,
                         base::SPLIT_WANT_NONEMPTY)) {
    std::string_view arg(token);
    if (!base::StartsWith(arg, "--")) {
      continue;
    }
    arg.remove_prefix(2);
    const size_t equals = arg.find('=');
    if (equals == std::string_view::npos) {
      command_line->AppendSwitch(std::string(arg));
    } else {
      command_line->AppendSwitchASCII(std::string(arg.substr(0, equals)),
                                      std::string(arg.substr(equals + 1)));
    }
  }
  NoteF("command line: %s",
        base::WideToUTF8(command_line->GetCommandLineString()));
}

// Every directory base would ask the shell namespace for. A phone's shell32
// is missing, and an app container's paths are the package's anyway.
void SetUpPaths(const EngineConfig& config) {
  const base::FilePath local_state = config.profile_dir.DirName();
  const base::FilePath temp =
      config.temp_dir.empty() ? local_state.Append(L"Temp") : config.temp_dir;
  base::PathService::Override(base::DIR_TEMP, temp);
  base::PathService::Override(base::DIR_HOME, local_state);
  base::PathService::Override(base::DIR_USER_DESKTOP, local_state);
  base::PathService::Override(base::DIR_LOCAL_APP_DATA, local_state);
  base::PathService::Override(base::DIR_ROAMING_APP_DATA, local_state);
  base::PathService::Override(base::DIR_COMMON_APP_DATA, local_state);
  base::PathService::Override(base::DIR_IE_INTERNET_CACHE, config.cache_dir);
  if (!config.install_dir.empty()) {
    base::PathService::Override(base::DIR_ASSETS, config.install_dir);
  }
}

}  // namespace

}  // namespace venetium_w10m

extern "C" VW_EXPORT int32_t venetium_w10m_run(
    const venetium_w10m_config* config) {
  using namespace venetium_w10m;
  if (!config) {
    return -1;
  }
  EngineConfig engine_config;
  engine_config.install_dir = PathOf(config->install_dir);
  engine_config.profile_dir = PathOf(config->profile_dir);
  engine_config.cache_dir = PathOf(config->cache_dir);
  engine_config.temp_dir = PathOf(config->temp_dir);
  engine_config.size = gfx::Size(config->width, config->height);
  engine_config.scale = config->scale > 0 ? config->scale : 1.0;
  engine_config.jit = config->jit != 0;
  engine_config.switches = config->switches ? config->switches : "";
  engine_config.initial_url = config->initial_url ? config->initial_url : "";
  if (engine_config.profile_dir.empty()) {
    Note("run: no profile directory");
    return -2;
  }
  if (engine_config.cache_dir.empty()) {
    engine_config.cache_dir =
        engine_config.profile_dir.Append(FILE_PATH_LITERAL("Cache"));
  }

  VwEngine* engine = VwEngine::Get();
  engine->Configure(engine_config);
  // A screen the shell reported before this call stays in the config.
  engine_config = engine->config();
  NoteF("run: %dx%d px at %.2f, jit %d", engine_config.size.width(),
        engine_config.size.height(), engine_config.scale,
        engine_config.jit ? 1 : 0);

  SetUpCommandLine(engine_config);
  SetUpPaths(engine_config);

  VwMainDelegate delegate;
  content::ContentMainParams params(&delegate);
  params.instance = ThisModule();
  params.sandbox_info = nullptr;
  const int result = content::ContentMain(std::move(params));
  NoteF("run: ContentMain returned %d", result);
  return result;
}
