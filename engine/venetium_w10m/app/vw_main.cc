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

#include <atomic>
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
#include "partition_alloc/oom_callback.h"
#include "sandbox/policy/switches.h"
#include "sandbox/win/src/sandbox_types.h"
#include "third_party/blink/public/common/switches.h"
#include "ui/gl/gl_switches.h"
#include "ui/gl/w10m_panel.h"
#include "venetium_w10m/app/vw_main_delegate.h"
#include "venetium_w10m/browser/vw_engine.h"
#include "venetium_w10m/browser/vw_interface_guard.h"
#include "venetium_w10m/browser/vw_panel.h"
#include "venetium_w10m/browser/vw_present_guard.h"
#include "venetium_w10m/capi/venetium_w10m_capi.h"
#include "venetium_w10m/common/vw_log.h"

#if defined(VW_CHROME_INTERFACE)
#include "venetium_w10m/chrome/vw_chrome_run.h"
#include "venetium_w10m/browser/vw_hang_watch.h"
#endif

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

// Chromium's hardware video decoder saying where it is (ui/gl/w10m_panel.h),
// on the GPU thread: a run that ends between kStarting and what follows it
// is counted against hardware video decoding (vw_present_guard.h).
void VideoDecoding(gl::W10mVideoEvent event) {
  switch (event) {
    case gl::W10mVideoEvent::kStarting: {
      // And from here it is not also counted against GPU compositing, which
      // has drawn the page this video is on: a launch that opens a video at
      // once (within the five seconds GPU compositing is given to prove
      // itself) would otherwise charge a driver that only cannot decode
      // to both, and two such launches would leave both alone. Nothing
      // unless GPU compositing has shown a frame.
      // Not for the first of a run: that one is the driver being asked what
      // it decodes, once, when the renderer starts (RenderMediaClient asks
      // for the supported configurations at once, media element or none),
      // and it is over in a moment. Proving GPU compositing there would let
      // it off its five seconds in every launch whose first frame came
      // before the question.
      static std::atomic<bool> asked{false};
      if (asked.exchange(true) &&
          !VwEngine::Get()->gpu_compositing_failed()) {
        PresentGuard::Get()->Proven(PresentKind::kAngle);
      }
      PresentGuard::Get()->VideoTrying();
      break;
    }
    case gl::W10mVideoEvent::kWorked:
      PresentGuard::Get()->VideoWorked();
      break;
    case gl::W10mVideoEvent::kEnded:
      PresentGuard::Get()->VideoEnded();
      break;
    case gl::W10mVideoEvent::kLost:
      PresentGuard::Get()->VideoLost();
      break;
  }
}

bool HasSwitch(const std::string& switches, const char* name) {
  return switches.find(name) != std::string::npos;
}

// Chrome's own interface, unless the switches ask for the shell's
// ("--vw-ui=shell"), or runs with it have ended before its window showed
// (vw_interface_guard.h). Only an engine built with Chrome's browser layer
// has one to give.
bool ChromeInterface(const std::string& switches,
                     const base::FilePath& profile_dir) {
#if defined(VW_CHROME_INTERFACE)
  if (HasSwitch(switches, "--vw-ui=shell")) {
    return false;
  }
  if (interface_guard::GaveUp(profile_dir)) {
    static bool noted = false;
    if (!noted) {
      noted = true;
      NoteF("interface: the shell's: %d runs with Chrome's ended before its "
            "window showed (a new build tries Chrome's again)",
            interface_guard::kTries);
    }
    return false;
  }
  return true;
#else
  return false;
#endif
}

// How frames reach the screen, best first:
//   GPU compositing  ANGLE on Direct3D 11 draws the page's layers into the
//                    shell's SwapChainPanel (feature level 9_3 and up: ES 3
//                    from 10_0, ES 2 at 9_3, ANGLE's 9_3 from gecko-w10m);
//   the Direct3D 11  the CPU rasters, and viz draws the layers itself with
//   compositor       Direct3D 11 into the swap chain below, frame by frame
//                    where it can (a 9_3 GPU whose runs with ANGLE did not
//                    last; or --vw-gpu=d3d11, on a device held to 9_3);
//   D3D11 present    the software compositor's frames, shown by the GPU
//                    through the panel (any feature level from 9_3);
//   the bitmap       frames copied out to the shell.
// The shell can ask for less. And what a run died of before its first frame
// is remembered (vw_present_guard.h): a driver that cannot do one of these
// would otherwise end every launch.
void ChoosePresentation(EngineConfig* config, uint64_t ram_mb) {
  const unsigned level = ProbeD3D11();
  config->d3d_feature_level = level;
  config->present_guard = config->profile_dir.Append(L"present-guard");
  PresentGuard* guard = PresentGuard::Get();
  // What a verdict belongs to: this package (its directory carries the appx
  // version; the engine DLL alone is the same in a build that changes only
  // ANGLE or the shell) and the switches of the run, less the two the shell
  // adds for its Hardware graphics setting and the ones that only choose
  // which video codecs pages are offered and whether background tabs are
  // unloaded.
  std::string made_of = base::ToLowerASCII(
      base::WideToUTF8(config->install_dir.BaseName().value()));
  for (const std::string& token :
       base::SplitString(config->switches, " ", base::TRIM_WHITESPACE,
                         base::SPLIT_WANT_NONEMPTY)) {
    // Nor is the one that keeps video decoding on the CPU, nor the one that
    // leaves the device untrimmed when the app is suspended: a way out of
    // what a driver cannot do must not have a way of presenting tried again.
    if (token != "--vw-gpu=retry" && token != "--vw-present=bitmap" &&
        token != "--vw-codecs=all" && token != "--vw-discard=off" &&
        token != "--vw-video=software" && token != "--vw-trim=off" &&
        token != "--force-dark-mode") {
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
  const bool off = HasSwitch(config->switches, "--vw-gpu=off");
  // The Direct3D 11 compositor asked for by name: on any phone, and with the
  // device a 9_3 phone has, so that it is tested within the same limits.
  const bool d3d11_asked = HasSwitch(config->switches, "--vw-gpu=d3d11");
  const bool angle_capable = level >= 0x9300 &&
                             PanelHost::Get()->has_panel() && !off &&
                             !d3d11_asked;
  config->gpu_compositing =
      angle_capable && guard->Allowed(PresentKind::kAngle);
  if (angle_capable && !config->gpu_compositing) {
    NoteF("gpu: no GPU compositing: earlier runs with it did not last "
          "(count %d; a new build, other engine switches, or Hardware "
          "graphics off and on, tries again)",
          guard->Failures(PresentKind::kAngle));
  }
  if (config->gpu_compositing && level < 0xa000) {
    // ANGLE's ES 2 at 9_3 (Venetium's way on Lumias, with gecko-w10m's level
    // 9 clear shaders): WebGL 1, no WebGL 2; Chromium takes its 7 varyings.
    Note("gpu: GPU compositing at feature level 9_3, ANGLE's ES 2 "
         "(--vw-gpu=d3d11 among the engine switches: the Direct3D 11 "
         "compositor instead)");
  }
  // H.264 on the GPU's own decoder (Chromium's D3D11VideoDecoder), where the
  // compositor is on the GPU too: the decoder works on ANGLE's device and
  // hands its frames on as textures, which a software compositor cannot
  // take. Counted like a way of presenting, but from when a page first plays
  // a video, not from here (vw_present_guard.h); what the driver says of
  // H.264 is in the log from then.
  if (!config->gpu_compositing) {
    Note("video: decoded in software (the GPU's decoder needs GPU "
         "compositing to draw its frames)");
  } else if (HasSwitch(config->switches, "--vw-video=software") ||
             HasSwitch(config->switches,
                       "--disable-accelerated-video-decode")) {
    Note("video: decoded in software (the engine switches ask for it)");
  } else if (!guard->VideoAllowed()) {
    NoteF("video: decoded in software: %d runs ended while the GPU's decoder "
          "was first at work (a new build, other engine switches, or "
          "Hardware graphics off and on, tries again)",
          guard->VideoFailures());
  } else {
    config->hardware_video = true;
    gl::SetW10mVideoHandler(&VideoDecoding);
    if (level < 0xa000) {
      // Chromium's decoder on a 9_3 device: its frames copied by the video
      // processor into BGRA textures the compositor samples, up to 1080p30
      // (media/gpu/windows, Windows 10 Mobile; the renderer client).
      Note("video: at feature level 9_3, frames copied as BGRA, up to 1080p "
           "at 30 frames a second (720p up to 60)");
    }
    Note("video: H.264 goes to the GPU's decoder when a page plays one "
         "(--vw-video=software among the engine switches keeps it on the "
         "CPU)");
  }
  // Raster on the GPU as well, where the compositor is (at 9_3 too: the
  // user's "hardware acceleration, forced"), as Chrome for Android does. Chromium's blocklist
  // keeps it off Qualcomm's Windows drivers before 31.0.2.0 (crbug 1047002,
  // for laptops), which every phone's driver is; the switch overrides it. A
  // run that does not last with it counts against GPU compositing, as above.
  if (config->gpu_compositing) {
    if (HasSwitch(config->switches, "--vw-raster=cpu")) {
      Note("raster: on the CPU (the engine switches ask for it)");
    } else {
      config->gpu_raster = true;
      Note("raster: tiles and 2D canvases on the GPU (--vw-raster=cpu among "
           "the engine switches keeps them on the CPU)");
    }
  }
  const bool compositor_capable = !config->gpu_compositing &&
                                  (level < 0xa000 || d3d11_asked) &&
                                  PanelHost::Get()->has_panel() && !off;
  config->d3d_compositing =
      compositor_capable && guard->Allowed(PresentKind::kCompositor);
  config->d3d_level_9_3 = d3d11_asked;
  if (compositor_capable && !config->d3d_compositing) {
    NoteF("gpu: no Direct3D 11 compositor: earlier runs with it did not last "
          "(count %d; a new build, other engine switches, or Hardware "
          "graphics off and on, tries again)",
          guard->Failures(PresentKind::kCompositor));
  }
  // Both fall back to presenting software frames through Direct3D 11 within
  // the run, so a run counts against the one it starts with.
  guard->Trying(config->gpu_compositing   ? PresentKind::kAngle
                : config->d3d_compositing ? PresentKind::kCompositor
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
        "it is there, %d buffers%s",
        config->gpu_compositing   ? "GPU compositing (ANGLE on Direct3D 11)"
        : config->d3d_compositing ? "the Direct3D 11 compositor (the CPU "
                                    "rasters, Direct3D 11 draws the frames "
                                    "it can)"
                                  : "software compositing",
        config->present_buffers,
        config->d3d_level_9_3 ? ", the device held to feature level 9_3" : "");
}

// Runs on the thread that could not get memory, just before Chromium raises
// its out-of-memory exception (0xE0000008), which nothing handles: the last
// line of a run that ended this way says so.
void NoteOutOfMemory() {
  static std::atomic<bool> noted{false};
  if (!noted.exchange(true)) {
    Note("memory: OUT OF MEMORY -- an allocation failed, and Chromium ends "
         "the process for it");
  }
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
    // presenting into the shell's panel; tiles and 2D canvases rastered there
    // too (gpu_raster), or on the CPU. Video encode stays off the GPU: left
    // on, it has the GPU thread enumerate and activate the phone's hardware
    // encoder transforms for the first page with a media element, only to
    // list their profiles.
    command_line->AppendSwitchASCII(switches::kUseGL, "angle");
    command_line->AppendSwitchASCII(switches::kUseANGLE, "d3d11");
    if (config.gpu_raster) {
      command_line->AppendSwitch(switches::kEnableGpuRasterization);
    } else {
      command_line->AppendSwitch(switches::kDisableGpuRasterization);
      command_line->AppendSwitch(switches::kDisableAccelerated2dCanvas);
    }
    if (config.hardware_video) {
      // H.264 on the GPU's decoder, and nothing else of video there. These
      // are Chromium's own driver workarounds, taken as switches
      // (gpu/config/gpu_workaround_list.txt); "=0" after one among the
      // engine switches takes it back.
      //  - ID3D11VideoContext, not ID3D11VideoContext1: Chromium's entry for
      //    Qualcomm (SubmitDecoderBuffers1 fails on its chipsets), said
      //    again in case the vendor id is not matched.
      //  - Frames are copied out of the decoder's textures by the video
      //    processor, and the compositor samples the copies: gecko-w10m's
      //    way at feature level 9_3 (at 11 it copies with the 3D engine).
      //    The decoder's textures are then made for decoding only. Sampled
      //    where they are decoded ("=0") there is no copy to make or to
      //    keep, and nothing known about it: gecko-w10m never samples a
      //    decoder's texture on an Adreno.
      //  - No VP9, AV1 or HEVC on the GPU: pages are not offered them, and
      //    one thing at a time.
      command_line->AppendSwitch("limit_d3d11_video_decoder_to_11_0");
      command_line->AppendSwitch("disable_dxgi_zero_copy_video");
      command_line->AppendSwitch("disable_accelerated_vp9_decode");
      command_line->AppendSwitch("disable_accelerated_av1_decode");
      command_line->AppendSwitch("disable_accelerated_hevc_decode");
    } else {
      command_line->AppendSwitch(switches::kDisableAcceleratedVideoDecode);
    }
    command_line->AppendSwitch(switches::kDisableAcceleratedVideoEncode);
  } else {
    // Software raster and compositing; the frames go to the panel through
    // Direct3D 11, or to the shell's bitmap (vw_frame_sink). The Direct3D 11
    // compositor is this too, as far as Chromium knows: it is viz's software
    // renderer that draws the frames it can with Direct3D 11.
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
  // What memory pressure reaches. On Windows Chromium leaves some of those
  // that listen for it untold (kSuppressMemoryListeners and its mask, a
  // character a listener, in
  // base/memory/memory_pressure_listener_registry.cc), and one of them
  // holds much here: the manager of the discardable memory that decoded
  // images wait in (listener 9), each megabyte of it mapped twice in this
  // one process. The mask below is Chromium's with that one told again.
  // Blink is to let go of the resources no page is using, and of decoded
  // images it can decode again: off by default on Windows. And Chromium's
  // own watcher of memory is put to sleep. Every 5 s it calls less than
  // 1000 MB of RAM free in the whole phone moderate pressure, told again
  // every 10 s, and less than 400 MB critical, told every 5 s
  // (components/memory_pressure/system_memory_pressure_evaluator_win.cc): a
  // phone with 1 GB is never clear of that, and what it measures is not
  // what runs out here. That the shell measures, and VwMemory decides what
  // it is worth (browser/vw_memory.h).
  command_line->AppendSwitchASCII(
      switches::kEnableFeatures,
      "SuppressMemoryListeners:suppress_memory_listeners_mask/"
      "0200200200220200020020020002020020000002000000020,"
      "ReleaseResourceStrongReferencesOnMemoryPressure,"
      "ReleaseResourceDecodedDataOnMemoryPressure,"
      "WinMemoryPressurePeriod:period/30d");
  if (config.d3d_feature_level && config.d3d_feature_level < 0xa000) {
    // A feature level 9_3 phone does not keep 60 frames a second: sites that
    // ask MediaCapabilities are told it is not smooth above 30
    // (media/mojo/services/video_decode_perf_history.cc).
    command_line->AppendSwitchASCII("vw-max-smooth-video-fps", "30");
  }
  if (!config.jit) {
    // V8 without a JIT: no executable memory needed at all, which is all an
    // app container without the codeGeneration capability allows.
    command_line->AppendSwitchASCII(blink::switches::kJavaScriptFlags,
                                    "--jitless");
  }
  if (config.hardware_video) {
    // With video on the GPU's decoder, a call's stays on the CPU (the
    // feature's name is spelt this way): the driver has one decoder to give,
    // and a call would take it from a video or be refused it. Added to the
    // features turned off above; a switch given twice keeps its last value.
    command_line->AppendSwitchASCII(
        switches::kDisableFeatures,
        command_line->GetSwitchValueASCII(switches::kDisableFeatures) +
            ",webrtc-hw-decoding");
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

extern "C" VW_EXPORT int32_t venetium_w10m_interface(
    const char* switches,
    const wchar_t* profile_dir) {
  using namespace venetium_w10m;
  return ChromeInterface(switches ? switches : "", PathOf(profile_dir)) ? 1
                                                                        : 0;
}

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

  partition_alloc::SetPartitionAllocOomCallback(&NoteOutOfMemory);
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
  // Before Chromium starts its threads: each is counted as it is named.
  hang_watch::StartCpuAccounting();

#if defined(VW_CHROME_INTERFACE)
  if (ChromeInterface(engine_config.switches, engine_config.profile_dir)) {
    base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
    AddChromeSwitches(engine_config, command_line);
    NoteF("command line for Chrome: %s",
          base::WideToUTF8(command_line->GetCommandLineString()));
    SetUpChromePaths(engine_config);
    interface_guard::Trying(engine_config.profile_dir);
    return RunChrome(ThisModule());
  }
#endif

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
