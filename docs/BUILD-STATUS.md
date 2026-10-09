# Build status

Updated as the port moves. Each claim says how it is known: **built** (the
compiler and linker said so), **checked** (compared against something), or
**untested** (written, not yet run).

## Toolchain and tree — built/checked

| Item | State |
|---|---|
| Chromium 150.0.7871.222 lite tarball, unpacked (565,589 files) | sha256 checked against Google's |
| Supermium 150's 11 patches (2026-09-30), Venetium 0001–0030 minus 0006/0014/0021, superseded 0024 | apply cleanly (0014 redone for Supermium's own 09-30 fix, which has a typo) |
| rt2 (LLVM 23.1.1 + 6 ARM32 patches) | sha256 checked against its release |
| `clang_rt.builtins-{arm,i386}.lib` from compiler-rt 23.1.1's generic sources; x86_64 from LLVM 22 | built (120 objects each) |
| Rust nightly-2026-04-10 (Chromium 150's pin), std for `thumbv7a-uwp-windows-msvchf` (VFP-register float aggregates, app-container std) | built (19 rlibs + profiler_builtins stand-in) |
| bindgen 0.73.2 + LLVM 22's libclang, rustfmt | built |
| gn (DEPS' revision), rc.exe, node 24.12.0, esbuild 0.25.1, gperf, DirectX-Headers, webauthn, perl | fetched, checksums/revisions as DEPS pins them |
| Codec configurations for Windows ARM32 (dav1d, libaom, libvpx, ffmpeg) | 23 of 24 files byte-identical to the hashes Venetium recorded; ffmpeg's `config.h` differs only in build-host tool probes (`HAVE_PERL`, `HAVE_XMLLINT`) no source uses |
| `gn gen` rooted at `//venetium_w10m` | 8,842 targets; 37,813 build steps for the engine's dependencies |

## Engine — built

`venetium_engine.dll` links (129.5 MB, ARMNT, `/APPCONTAINER`): Chromium's
content layer, Blink, V8 and the network service for 32-bit ARM Windows,
built with the public rt2. Its only load-time imports are `kernel32.dll` and
`ntdll.dll`; every other system DLL (49 of them, `user32`, `gdi32`, `ole32`,
`shell32`, `d3d11`, `dxgi`, `dwrite`, `mf*` among them) is delay-loaded, so
what a phone lacks becomes a failing call, not a failed load. The appx with
it is 74.7 MB (`tools/build-appx.sh`).

Problems met and fixed on the way, each now part of the build:

* rt2's lld-link has no libxml2: the SDK's `mt.exe` must be on PATH.
* VS 2022 without ATL: VS 2017's 14.16 ATL (with ARM32 libraries) is used
  (`VW_ATLMFC_DIR`, patch 0005).
* The DevTools frontend (a TypeScript bundle) is not built (patch 0003);
  Blink's inspector overlay still needs esbuild.
* ANGLE's `d3dcompiler_47.dll` copy is skipped: the phone's OS has it (0004).
* `use_v8_context_snapshot = false`: the context snapshot would build Blink, cc
  and WebRTC a second time for the x86 snapshot toolchain.
* WebNN's TFLite/LiteRT off; pthreadpool is still built (other users), and
  its `fxdiv.h` calls an MSVC intrinsic the public rt2's clang lacks
  (`_MulUnsignedHigh`): patch 0009 takes the portable branch, as Venetium's
  0025 did before rt2.1.
* MIDL's checked-in outputs exist for x86, x64 and arm64 only; the arm ones
  are generated the way `midl.py` asks and carried as patch 0010.
* `use_dawn = false` (no D3D12 on a phone): two places in the GPU service
  assumed Skia's Dawn backend on Windows (0011).
* Without Dawn, Windows' shared images still hold a Dawn texture cache and
  reference a few Dawn classes on Dawn-only paths: the cache is built on its
  own and the rest stubbed as unreachable (0016).
* WebNN's ONNX Runtime backend got fp16 only through TFLite (0013).
* The public rt2 leaves `_AddressOfReturnAddress` and `_ReadWriteBarrier`
  undefined on thumbv7: the frame address and a full fence instead (0014,
  0015), as Venetium did before rt2.1.
* Generator scripts on a Windows host: a `python3.bat` shim for `.bat`
  launchers (`tools/ninja.sh`), rollup's native Windows module and the
  removal of Linux's `esbuild` ELF from DevTools' tree (`tools/setup-tree.sh`).

## Engine (`engine/venetium_w10m`) — written, untested

All sources compile and link (**built**). What it does:
single-process content embedder; Aura host without an HWND; software output
device → frame buffer → `venetium_w10m_frame_copy`; touch/mouse/wheel/key/text
input into Aura; tabs with window.open/close, fullscreen, back button; mobile
viewport, touch pointer, mobile Chrome user agent and client hints;
cookies/cache on disk, flushed on suspend; downloads; JavaScript dialogs and
file chooser through the shell; the clipboard both ways (in memory in the
engine; what a page copies goes to the phone's, the phone's is sent in when
the app comes to the front or it changes); delay-load failures turned into
failing calls.

Patches to Chromium for the phone's runtime (written, untested on a device):
UI message pump without a message window (0006), no comctl32 or device window
at browser start (0006), window creation failure non-fatal (0007), viz
software frames to the embedder (0002), V8's JIT under W^X (0008: code pages
read-execute, a vectored exception handler flipping the page that is written
to read-write and back when it runs, through `VirtualAllocFromApp` and
`VirtualProtectFromApp`; in PartitionAlloc, where Chromium's V8 gets its
pages, and in V8's own platform code). 0008 compiles for ARM.

From a review of the startup path for what an app container on the phone
lacks (0012, written, untested): no screen DC (`GetDC(NULL)` no longer
fatal; system fonts are Segoe UI 12 DIPs without GDI; system DPI 96), no
kernel32.dll or version.dll on 1607 (the kernel32 version from
`RtlGetVersion`), no classic COM STA without a hidden window (STA worker
threads stay in the MTA). `--disable-software-rasterizer` keeps GL off
(otherwise `--disable-gpu` falls back to WebGL on WARP through ANGLE).
Delay-load failures return each function's own failure value (`E_NOTIMPL`,
`ERROR_CALL_NOT_IMPLEMENTED` where 0 means success, else 0).

Reviewed before its first run by five reviewers and adversarial verifiers
(threading and lifetime, content API use, input and frames, the patches, the
shell): 27 distinct defects confirmed and fixed, among them the network service never
being created (`IsShuttingDown` true during startup), pages laid out in
pixels instead of DIPs, an encrypting cookie store without a provider
(`NOTREACHED`), COM worker threads spinning on `MsgWaitForMultipleObjectsEx`
(a delay-load substitute now waits on the handles), and the keyboard sink
unable to take focus. The keyboard now comes up through a
`ui::VirtualKeyboardController` (only when the user taps a field) and the
page is inset while it is up.

## Shell (`app/VenetiumW10m`) — built, untested

Compiles with MSVC 14.44 ARM and C++/WinRT; packs and signs into an appx
(`tools/build-appx.sh`). Address bar, tab switcher, menu (new tab, forward,
desktop site, search engine, JIT, log), dialogs, file picker, downloads
notice, keyboard sink, back button.

## Next

1. Run on a phone (15063/15254 first) with the log; fix what
   breaks at load and at startup (api-set forwarders, delay-loaded DLLs).
2. Turn the JIT on (menu → JIT) and see what the W^X layer costs.
3. GPU compositing: ANGLE Direct3D 11 (feature level 9_3) presenting into a
   `SwapChainPanel`, as gecko-w10m's WebRender does.
4. Audio output (WASAPI through `ActivateAudioInterfaceAsync`), hardware H.264.
