// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// venetium_w10m_run: from the shell's configuration to content::ContentMain.
//
// The shell calls this on a thread of its own, which becomes Chromium's
// browser UI thread for the rest of the run. Everything a desktop Chromium
// would learn from its command line, its process type and the shell
// namespace (SHGetKnownFolderPath, absent on a phone) is put in place first.

#include <windows.h>

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <string>
#include <vector>

#include "base/base_switches.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "content/public/app/content_main.h"
#include "content/public/common/content_switches.h"
#include "gpu/config/gpu_switches.h"
#include "sandbox/policy/switches.h"
#include "sandbox/win/src/sandbox_types.h"
#include "third_party/blink/public/common/switches.h"
#include "ui/gl/gl_switches.h"
#include "venetium_w10m/app/vw_main_delegate.h"
#include "venetium_w10m/browser/vw_engine.h"
#include "venetium_w10m/browser/vw_panel.h"
#include "venetium_w10m/browser/vw_present_guard.h"
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

// The GPU, for the log and for choosing how frames reach the screen: its
// Direct3D 11 feature level (Windows 10 Mobile phones stop at 9_3 or reach
// 11_x) and its adapter. Returns the level, or 0 without a device.
unsigned ProbeD3D11() {
  using Microsoft::WRL::ComPtr;
  static const D3D_FEATURE_LEVEL kLevels[] = {
      D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
      D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_9_3};
  ComPtr<ID3D11Device> device;
  D3D_FEATURE_LEVEL level = {};
  HRESULT hr = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                   D3D11_CREATE_DEVICE_BGRA_SUPPORT, kLevels,
                                   std::size(kLevels), D3D11_SDK_VERSION,
                                   &device, &level, nullptr);
  if (hr == E_INVALIDARG) {
    hr = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                             D3D11_CREATE_DEVICE_BGRA_SUPPORT, kLevels + 1,
                             std::size(kLevels) - 1, D3D11_SDK_VERSION,
                             &device, &level, nullptr);
  }
  if (FAILED(hr)) {
    NoteF("gpu: no Direct3D 11 device, 0x%08lx", static_cast<unsigned long>(hr));
    return 0;
  }
  DXGI_ADAPTER_DESC desc = {};
  ComPtr<IDXGIDevice> dxgi_device;
  ComPtr<IDXGIAdapter> adapter;
  if (SUCCEEDED(device.As(&dxgi_device)) &&
      SUCCEEDED(dxgi_device->GetAdapter(&adapter))) {
    adapter->GetDesc(&desc);
  }
  NoteF("gpu: Direct3D 11 feature level 0x%x, %ls (vendor 0x%04x, device "
        "0x%04x, %u MB dedicated, %u MB shared)",
        static_cast<unsigned>(level), desc.Description,
        static_cast<unsigned>(desc.VendorId),
        static_cast<unsigned>(desc.DeviceId),
        static_cast<unsigned>(desc.DedicatedVideoMemory >> 20),
        static_cast<unsigned>(desc.SharedSystemMemory >> 20));
  return static_cast<unsigned>(level);
}

bool HasSwitch(const std::string& switches, const char* name) {
  return switches.find(name) != std::string::npos;
}

// How frames reach the screen, best first:
//   GPU compositing  ANGLE on Direct3D 11 draws the page's layers into the
//                    shell's SwapChainPanel (feature level 11_0 and up);
//   D3D11 present    the software compositor's frames, shown by the GPU
//                    through the panel (any feature level from 9_3);
//   the bitmap       frames copied out to the shell.
// The shell can ask for less. And what a run died of before its first frame
// is remembered (vw_present_guard.h): a driver that cannot do one of these
// would otherwise end every launch.
void ChoosePresentation(EngineConfig* config, uint64_t ram_mb) {
  const unsigned level = ProbeD3D11();
  config->present_guard = config->profile_dir.Append(L"present-guard");
  PresentGuard* guard = PresentGuard::Get();
  // What a verdict belongs to: this package (its directory carries the appx
  // version; the engine DLL alone is the same in a build that changes only
  // ANGLE or the shell) and the switches of the run, less the two the shell
  // adds for its Hardware graphics setting and the one that only chooses
  // which video codecs pages are offered.
  std::string made_of = base::ToLowerASCII(
      base::WideToUTF8(config->install_dir.BaseName().value()));
  for (const std::string& token :
       base::SplitString(config->switches, " ", base::TRIM_WHITESPACE,
                         base::SPLIT_WANT_NONEMPTY)) {
    if (token != "--vw-gpu=retry" && token != "--vw-present=bitmap" &&
        token != "--vw-codecs=all") {
      made_of += ' ';
      made_of += token;
    }
  }
  guard->Load(config->present_guard, made_of,
              HasSwitch(config->switches, "--vw-gpu=retry"));
  if (HasSwitch(config->switches, "--vw-present=bitmap")) {
    Note("gpu: frames through the bitmap (the shell asked for it)");
    return;
  }
  if (!level) {
    Note("gpu: frames through the bitmap (no Direct3D 11)");
    return;
  }
  if (!guard->Allowed(PresentKind::kD3D11)) {
    NoteF("gpu: frames through the bitmap: %d runs presenting through "
          "Direct3D 11 ended before their first frame (a new build, other "
          "engine switches, or Hardware graphics off and on, tries again)",
          guard->Failures(PresentKind::kD3D11));
    return;
  }
  const bool angle_capable =
      level >= 0xb000 && PanelHost::Get()->has_panel() &&
      !HasSwitch(config->switches, "--vw-gpu=off");
  config->gpu_compositing =
      angle_capable && guard->Allowed(PresentKind::kAngle);
  if (angle_capable && !config->gpu_compositing) {
    NoteF("gpu: no GPU compositing: earlier runs with it did not last "
          "(count %d; a new build, other engine switches, or Hardware "
          "graphics off and on, tries again)",
          guard->Failures(PresentKind::kAngle));
  }
  // GPU compositing falls back to presenting through Direct3D 11 within the
  // run, so a run counts against the one it starts with.
  guard->Trying(config->gpu_compositing ? PresentKind::kAngle
                                        : PresentKind::kD3D11);
  config->hardware_present = true;
  // A frame that takes a little more than a refresh costs two with two
  // buffers (gecko-w10m); a third is 14 MB at this size, which phones with
  // little memory do without.
  config->present_buffers = ram_mb > 1536 ? 3 : 2;
  if (HasSwitch(config->switches, "--vw-present-buffers=2")) {
    config->present_buffers = 2;
  } else if (HasSwitch(config->switches, "--vw-present-buffers=3")) {
    config->present_buffers = 3;
  }
  NoteF("gpu: %s; software frames through Direct3D 11 into the panel when "
        "it is there, %d buffers",
        config->gpu_compositing ? "GPU compositing (ANGLE on Direct3D 11)"
                                : "software compositing",
        config->present_buffers);
}

uint64_t PhysicalMemoryMb() {
  MEMORYSTATUSEX status = {sizeof(status)};
  return ::GlobalMemoryStatusEx(&status) ? status.ullTotalPhys >> 20 : 0;
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
  if (config.gpu_compositing) {
    // The compositor on the GPU: SkiaRenderer on ANGLE's Direct3D 11,
    // presenting into the shell's panel. Raster stays on the CPU for now,
    // and video decode, video encode and 2D canvas off the GPU: one thing at
    // a time. (Video encode left on has the GPU thread enumerate and
    // activate the phone's hardware encoder transforms for the first page
    // with a media element, only to list their profiles.)
    command_line->AppendSwitchASCII(switches::kUseGL, "angle");
    command_line->AppendSwitchASCII(switches::kUseANGLE, "d3d11");
    command_line->AppendSwitch(switches::kDisableGpuRasterization);
    command_line->AppendSwitch(switches::kDisableAcceleratedVideoDecode);
    command_line->AppendSwitch(switches::kDisableAcceleratedVideoEncode);
    command_line->AppendSwitch(switches::kDisableAccelerated2dCanvas);
  } else {
    // Software raster and compositing; the frames go to the panel through
    // Direct3D 11, or to the shell's bitmap (vw_frame_sink).
    command_line->AppendSwitch(switches::kDisableGpu);
    command_line->AppendSwitch(switches::kDisableGpuCompositing);
  }
  // Without it --disable-gpu still falls back to WebGL on Direct3D's WARP
  // rasterizer through ANGLE, which loads libGLESv2 and asks for a screen
  // DC; with it the GPU thread only composites, and GL stays off.
  command_line->AppendSwitch(switches::kDisableSoftwareRasterizer);
  // TimeTicks on the performance counter: on ARM, base otherwise flips a
  // coin between it and timeGetTime's milliseconds, which animations and
  // frame timing feel.
  command_line->AppendSwitchASCII(switches::kForceHighResTimeTicks, "enabled");
  // Blink gives every desktop at least 512 MB of tiles, software compositing
  // included: far more than a 32-bit app on a phone has address space for.
  const uint64_t ram_mb = PhysicalMemoryMb();
  command_line->AppendSwitchASCII(blink::switches::kForceGpuMemAvailableMb,
                                  ram_mb && ram_mb <= 1536 ? "64" : "128");
  // Chromium rasters 256-pixel tiles on Windows whatever the density: at
  // 350% that is 54 tiles a screen, each a raster task, a shared image and
  // a quad of its own. Chrome for Android goes to 384 and 512 on dense
  // screens (blink's GenerateLayerTreeSettings); the same here, by scale.
  if (config.scale >= 2.5) {
    const char* tile = config.scale >= 3.0 ? "512" : "384";
    command_line->AppendSwitchASCII(blink::switches::kDefaultTileWidth, tile);
    command_line->AppendSwitchASCII(blink::switches::kDefaultTileHeight, tile);
  }
  // RenderWidgetHostViewAura's child HWND for accessibility and legacy
  // input: no HWNDs here.
  command_line->AppendSwitch(switches::kDisableLegacyIntermediateWindow);
  // DirectX 12 and Vulkan support is probed in a GPU process of its own --
  // in this one, in single-process mode -- through d3d12.dll, which a phone
  // has not got. And no DirectComposition: there is no HWND to compose into.
  command_line->AppendSwitch(switches::kDisableGpuProcessForDX12InfoCollection);
  command_line->AppendSwitch(switches::kDisableDirectComposition);
  command_line->AppendSwitch(switches::kEnableViewport);
  command_line->AppendSwitchASCII(switches::kTouchEventFeatureDetection,
                                  "enabled");
  // Pages kept alive for back/forward cost memory a phone does not have.
  // And a GPU channel that could not be made is not answered by shutting
  // "the GPU process" down and starting it again: in this one process that
  // is a second GPU thread beside the first.
  command_line->AppendSwitchASCII(
      switches::kDisableFeatures,
      "BackForwardCache,ShutdownForFailedChannelCreation");
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
      const std::string name(arg.substr(0, equals));
      std::string value(arg.substr(equals + 1));
      // A feature list adds to the engine's own: a switch given twice keeps
      // only its last value, and what was turned off above would be back.
      if (name == switches::kDisableFeatures ||
          name == switches::kEnableFeatures) {
        const std::string ours = command_line->GetSwitchValueASCII(name);
        if (!ours.empty()) {
          value = ours + "," + value;
        }
      }
      command_line->AppendSwitchASCII(name, value);
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
  // The DirectWrite font proxy reads font files from here; base would ask
  // SHGetKnownFolderPath(FOLDERID_Fonts).
  wchar_t windows[MAX_PATH] = {};
  if (::GetWindowsDirectoryW(windows, MAX_PATH)) {
    base::PathService::Override(base::DIR_WINDOWS_FONTS,
                                base::FilePath(windows).Append(L"Fonts"));
  }
  if (!config.install_dir.empty()) {
    base::PathService::Override(base::DIR_ASSETS, config.install_dir);
  }
  // Before the browser starts: its UI thread may not touch the disk.
  base::CreateDirectory(config.profile_dir);
  base::CreateDirectory(config.cache_dir);
  base::CreateDirectory(temp);
}

}  // namespace

}  // namespace venetium_w10m

extern "C" VW_EXPORT int32_t venetium_w10m_run(
    const venetium_w10m_config* config) {
  using namespace venetium_w10m;
  // However this returns, the shell's later requests are dropped, not queued
  // for a browser that will never run them.
  struct ExitNote {
    ~ExitNote() { VwEngine::Get()->EngineExited(); }
  } exit_note;
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

  ChoosePresentation(&engine_config, PhysicalMemoryMb());

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
  // An empty sandbox interface: no broker (it would make job objects and
  // threads for child processes there will never be) and no target. The
  // browser process is unsandboxed, which is all Sandbox::Initialize checks
  // then; a null pointer here it would dereference.
  sandbox::SandboxInterfaceInfo sandbox_info = {};
  params.sandbox_info = &sandbox_info;
  const int result = content::ContentMain(std::move(params));
  NoteF("run: ContentMain returned %d", result);
  return result;
}
