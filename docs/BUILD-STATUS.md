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

## Engine dependencies — building

The first full build of Chromium's content layer for 32-bit ARM Windows with
rt2 is running. Problems met and fixed on the way, each now part of the build:

* rt2's lld-link has no libxml2: the SDK's `mt.exe` must be on PATH.
* VS 2022 without ATL: VS 2017's 14.16 ATL (with ARM32 libraries) is used
  (`VW_ATLMFC_DIR`, patch 0005).
* The DevTools frontend (a TypeScript bundle) is not built (patch 0003);
  Blink's inspector overlay still needs esbuild.
* ANGLE's `d3dcompiler_47.dll` copy is skipped: the phone's OS has it (0004).
* `use_v8_context_snapshot = false`: the context snapshot would build Blink, cc
  and WebRTC a second time for the x86 snapshot toolchain.
* WebNN's TFLite/LiteRT off: XNNPACK's pthreadpool calls an MSVC intrinsic
  clang lacks (`_MulUnsignedHigh`).

## Engine (`engine/venetium_w10m`) — written, untested

Each source is compiled on its own as soon as the headers it needs exist
(the build above generates them): 12 of the 21 compile cleanly so far; the
other 9 wait for generated mojom and buildflag headers. What it does:
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

## Shell (`app/VenetiumW10m`) — built, untested

Compiles with MSVC 14.44 ARM and C++/WinRT; packs and signs into an appx
(`tools/build-appx.sh`). Address bar, tab switcher, menu (new tab, forward,
desktop site, search engine, JIT, log), dialogs, file picker, downloads
notice, keyboard sink, back button.

## Next

1. Finish the dependency build; build `venetium_engine.dll`; fix what breaks.
2. Package and run on a phone (15063/15254 first) with the log; fix what
   breaks at load and at startup (api-set forwarders, delay-loaded DLLs).
3. Turn the JIT on (menu → JIT) and see what the W^X layer costs.
4. GPU compositing: ANGLE Direct3D 11 (feature level 9_3) presenting into a
   `SwapChainPanel`, as gecko-w10m's WebRender does.
5. Audio output (WASAPI through `ActivateAudioInterfaceAsync`), hardware H.264.
