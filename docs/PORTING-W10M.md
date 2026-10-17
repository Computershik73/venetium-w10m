# Porting Venetium (Chromium 150) to Windows 10 Mobile

Venetium is Chromium 150 for 32-bit ARM Windows tablets (Surface RT / Surface 2,
Windows 10 build 15035), built on Supermium. This project takes the same engine to
**Windows 10 Mobile** (Lumia phones and the like, builds 10586–15254), using the
approach that already works for Gecko in [gecko-w10m](../../gecko-w10m): a UWP app
whose shell is XAML and whose engine is a single-process browser engine drawing into
a buffer the shell puts on screen.

## Why Venetium cannot simply be repackaged

Venetium is a desktop Win32 program. On a phone every one of these is missing:

| Desktop Venetium relies on | Windows 10 Mobile has | This port |
|---|---|---|
| `CreateProcess` for renderer, GPU, utility, network processes | no process creation for a UWP app | **single process**: `--single-process`, `--in-process-gpu`, network/storage/audio in-process |
| HWNDs: Aura's `WinWindow`, message-only windows for the UI message pump, clipboard, display enumeration | `CreateWindow`, `PostMessage`, `MsgWaitForMultipleObjects` are **stubs** (measured in gecko-w10m 0088) | an `aura::WindowTreeHost` **without a window** (like `//headless`), an event-driven UI pump, a screen fed by the shell |
| Direct3D 9 through ANGLE (Tegra) | **no Direct3D 9**; Direct3D 11 at feature level 9_3 (Adreno 3xx) | first: software compositing into the shell's bitmap; then ANGLE D3D11 on a `SwapChainPanel` |
| `kernel32.dll`, `advapi32.dll`, `ole32.dll`, `version.dll`, `dbghelp.dll` imported by name | **absent** on 1607 phones; the functions live in `kernelbase`/`combase`/api-sets | gecko-w10m's forwarder shims (`tools/gen-apiset-shims.py`), applied to the engine DLL after link |
| V8 JIT with RWX pages from `VirtualAlloc` | only `VirtualAllocFromApp` + `VirtualProtectFromApp`, with the `codeGeneration` capability, **W^X** | `--jitless` first; JIT through the `*FromApp` allocators next |
| sandbox, crashpad, installer, `chrome_elf` | not possible inside an app container | not built |

## Architecture

```
VenetiumW10m.exe (cl.exe, C++/WinRT, XAML)          venetium_engine.dll (clang-cl, Chromium tree)
┌──────────────────────────────────────┐            ┌────────────────────────────────────────────┐
│ App / MainPage: address bar, tabs,   │  C ABI     │ capi/  venetium_w10m_* exports              │
│ back/forward/reload, progress        │◄──────────►│ app/   ContentMainDelegate, ContentClient   │
│ EngineView: Image (WriteableBitmap), │ frame_copy │ browser/ BrowserMainParts, BrowserContext,  │
│   touch / keys / text sink / keyboard│ input_*    │   tabs = WebContents, WindowTreeHost        │
│ Log, clipboard, file picker,         │ tab_*      │   without HWND, screen, input → aura        │
│ dialogs, permissions                 │ events     │ viz: software output device → frame buffer  │
└──────────────────────────────────────┘            │ renderer/ ContentRendererClient             │
                                                    └────────────────────────────────────────────┘
```

* **The engine is a content-layer embedder**, not `chrome.dll`. Chrome's desktop UI
  (views, tab strip, omnibox) is built on HWND frames and is meant for a mouse; on a
  phone the shell draws the browser chrome in XAML, as Firefox for Android and the
  iOS browsers do. The embedder is modelled on `//headless`, Chromium's production
  embedder that already runs without a window — `content_shell` is `testonly` and
  pulls test support into the binary.
* **Single process.** `content::RenderProcessHost::SetRunRendererInProcess`, the
  GPU thread in-process, the network service in-process. Utility services run on
  in-process threads, which `--single-process` already arranges.
* **No HWND anywhere.** `VwWindowTreeHost` derives from `aura::WindowTreeHost`
  directly and reports `gfx::kNullAcceleratedWidget`. Viz then composites into
  memory; a hook in `OutputSurfaceProviderImpl` gives it a software output device
  that publishes every frame into a buffer `venetium_w10m_frame_copy` hands to the
  shell — the same contract the gecko-w10m shell uses with Gecko.
* **Input** arrives through the C ABI on the XAML thread and is posted to the
  browser UI thread as `ui::TouchEvent` / `ui::MouseEvent` / `ui::KeyEvent` into the
  host's event sink, so Aura's gesture recogniser, `RenderWidgetHostViewAura` and
  Blink see a touch screen. Text from the on-screen keyboard goes in through the
  focused `ui::TextInputClient`.
* **The UI message pump** on Windows (`MessagePumpForUI`) wakes through an event
  already; on W10M it must not demand a message-only window and must wait on that
  event alone (patch to `base/message_loop/message_pump_win.cc`).
* **Paths** come from the shell (`LocalState`, `LocalCache`, `TempState`) and are put
  into `base::PathService` before anything asks `SHGetKnownFolderPath`, which the
  phone does not have.

### Chrome's own interface (in progress)

As gecko-w10m moved from a XAML shell to Firefox drawing its own interface, the
engine now carries Chrome's browser layer (`//chrome`) and runs Chrome's own
startup, single-process, in the same DLL; the shell's address bar, tabs and menu
give way to Chrome's, drawn into the same panel. `--vw-ui=shell` (engine switches)
keeps the XAML interface, and two runs that end before Chrome's window has shown
bring it back by themselves (`browser/vw_interface_guard.h`).

* `chrome/vw_chrome_run.cc` does what `chrome.exe` and `chrome_elf.dll` do before
  `ChromeMain` (install details, per-user; Chrome's switches; the profile in
  `LocalState\User Data`); `chrome/vw_chrome_elf.cc` answers `chrome_elf`'s exports.
* `VwChromeMainDelegate` / `VwChromeBrowserClient` / `VwChromeRendererClient`
  subclass Chrome's; `VwChromeExtraParts` adds the engine's stages: the frame sink,
  the phone's screen, the window Chrome's interface goes into, the shell's queue.
* Every Chrome widget is a `NativeWidgetAura` under the engine's one aura root,
  as on ChromeOS (patch 0034); `browser/vw_window_manager.cc` is ash in small
  (containers, focus and activation, maximized windows, tooltips, modality).
* What the shell asks of tabs goes to `VwChromeHost` (`browser/vw_chrome_bridge.h`):
  URLs from outside, the back button, suspend, tab discarding, quit.
* Chrome's resources (`resources.pak`, `chrome_100/200_percent.pak`, two locales)
  are built into `out/w10m/vw_chrome` and packaged beside the DLL.

## The base tree

The engine is Chromium **150.0.7871.222** (Google's lite source tarball), plus

1. **Supermium 150**'s core patches (base/partition_alloc/mojo/crypto for old
   Windows builds — W10M's 10586–15254 are "old Windows 10" to Chromium 150),
2. **Venetium**'s ARM32 Windows patches (toolchain, V8 for Windows ARM32, platform
   blockers, link fixes, ffmpeg for Windows ARM) — the desktop-only ones (installer,
   `chrome_elf`, sandbox token tweaks, the Direct3D 9 path) are left out,
3. **this port's own patches** in `patches/w10m/`.

`patches/series` lists exactly what is applied, in order.

## Toolchain

* **rt2** (LLVM 23.1.1 + six ARM32 Windows patches, `github.com/hamed7ir/llvm-rt1`).
  It fixes the Thumb bit of local `__imp_` pointers and the forwarded-`musttail`
  registers of virtual-call thunks — the two miscompiles gecko-w10m had to patch out
  of its binaries after link. It does **not** carry rt2.1's fixes, so:
  * Control Flow Guard is off (`win_enable_cfg_guards = false`): lld-link records
    ARM32 guard targets without the Thumb bit.
  * libc++ is built without exceptions (Venetium's superseded 0024).
  * Integer division: the MSVC CRT's `__rt_sdiv`/`__rt_udiv`, plus rt2's aeabi shim
    for the two `__aeabi_*divmod` names some code calls directly.
* **Rust**: a nightly close to the one Chromium 150 pins, with the
  `thumbv7a-pc-windows-msvc` standard library built from source (as gecko-w10m does
  for its UWP target).
* **MSVC 14.44** (VS 2022 17.14, the last with ARM32 tools) for headers, libraries
  and the shell; **Windows SDK 10.0.22621** (the last with ARM32 libraries).

## Graphics, in order

1. Software raster + software compositing (`--disable-gpu`): Skia on the CPU, frames
   copied into a `WriteableBitmap`. Works on every phone, is the first milestone.
2. GPU raster and compositing through ANGLE's **Direct3D 11** renderer at feature
   level 9_3 (ES 2.0), presenting into a composition swap chain that the shell puts in
   a `SwapChainPanel` — what gecko-w10m's WebRender does today.
3. Hardware H.264 through D3D11 video decode (DXVA) on the Adreno.

## JavaScript

* Milestone 1 runs V8 **jitless** (Ignition only, no WebAssembly): correct, slow.
* Then the JIT: code space reserved with `VirtualAllocFromApp`, committed read-write,
  flipped to read-execute with `VirtualProtectFromApp` around every code write
  (V8's write-protection scopes), `FlushInstructionCache` after each flip. Needs the
  `codeGeneration` restricted capability, which an interop-unlocked phone honours.

## Status

See [BUILD-STATUS.md](BUILD-STATUS.md).
