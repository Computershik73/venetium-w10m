# venetium-w10m

**Venetium for Windows 10 Mobile**: Chromium 150 — the engine of
[Venetium](../venetium), Chromium for old 32-bit ARM Windows tablets — as a UWP
browser for Lumia-era phones, built the way [gecko-w10m](../gecko-w10m) brings
Gecko there.

> **Status: work in progress.** The engine, the shell and the build are written;
> the first full engine build is running. Nothing has run on a phone yet. See
> [docs/BUILD-STATUS.md](docs/BUILD-STATUS.md) for exactly what is done.

## Why it is not just Venetium in an appx

Venetium is a desktop Win32 program. A Windows 10 Mobile app has no child
processes, no window messages (`CreateWindow` and `PostMessage` are stubs),
no Direct3D 9, no `shell32`, and executable memory only through
`VirtualAllocFromApp`/`VirtualProtectFromApp`. So:

| Desktop Venetium | venetium-w10m |
|---|---|
| `chrome.dll` with Chrome's views UI | `venetium_engine.dll`: Chromium's **content layer** behind a flat C interface; the browser chrome is **XAML** |
| renderer, GPU, network, utility processes | **one process**: `--single-process`, everything on threads |
| Aura in an HWND, `MessagePumpForUI` on a message window | an Aura host **without an HWND**, an event-driven UI pump |
| ANGLE on Direct3D 9 (Tegra) | **software compositing** into the shell's bitmap first; ANGLE on Direct3D 11 FL 9_3 next |
| V8 JIT | **jitless** by default; the JIT (menu → JIT) under W^X through the `*FromApp` allocators, a fault handler flipping code pages between writable and executable |
| sandbox, crashpad, installer | not built: the app container is the sandbox |

Design: [docs/PORTING-W10M.md](docs/PORTING-W10M.md).

## Layout

```
engine/venetium_w10m/   the engine: //venetium_w10m in the Chromium tree
  capi/                 venetium_w10m_capi.h -- the C interface the shell uses
  app/                  ContentMain, the main delegate, the content client
  browser/              tabs, the window without a window, frames, input,
                        browser context, dialogs, downloads, permissions
  renderer/             the renderer client (error page)
  common/               log bridge, user agent, delay-load hook
app/VenetiumW10m/       the UWP shell (code-only C++/WinRT XAML)
patches/series          what is applied to Chromium 150.0.7871.222, in order:
                        Supermium 150, Venetium's ARM32 patches, ...
patches/w10m/           ... and this port's own patches
deps/ffmpeg/            FFmpeg's configuration for Windows ARM32 (generated)
args/w10m.gn            the GN arguments
tools/                  the build, step by step (build-all.sh runs them all)
```

## Build

On an x64 Windows machine with about 45 GB free:

| Tool | Version |
|---|---|
| Visual Studio 2022 | 17.14 (MSVC 14.44, the last with ARM32 tools), "MSVC v143 ARM build tools" |
| ATL for ARM32 | VS 2022's ATL component, or VS 2017's 14.16 toolset (found automatically) |
| Windows SDK | 10.0.26100 (headers) and 10.0.22621 (the last with ARM32 libraries); 10.0.14393 for the shell, optional |
| Python | 3.12 (`VW_PYTHON`) |
| Git for Windows, Strawberry Perl, Node.js | any recent |
| Rust | rustup; the build installs `nightly-2026-04-10` (Chromium 150's pin) |
| LLVM | 22.x installed (only `libclang.dll` for bindgen and the x64 builtins) |

```bash
bash tools/build-all.sh
```

That fetches Chromium's lite source tarball, Supermium's patches, the rt2
toolchain and the Rust nightly (`tools/fetch-source.sh`), applies the patch
series (`tools/apply-patches.sh`), supplies what gclient's hooks would have
(`tools/setup-tree.sh`), builds clang's builtins and the ARM32 UWP Rust std,
runs GN and ninja, and packs and signs the appx (`tools/build-appx.sh`). The
package lands in `app/VenetiumW10m/AppPackages/`.

Signing uses gecko-w10m's certificate by default (same Publisher,
`CN=Computershik`), so a phone that runs Gecko installs this without another
certificate; `VW_PFX` names another.

The phone must be **interop-unlocked** (the package declares
`rescap:codeGeneration`), and build 15063 or 15254 is the first target; 1607
needs gecko-w10m's legacy-name shims (`VW_LEGACY_SHIMS=1`).

## Credits

[Venetium](https://github.com/hamed7ir/venetium) and its rt2 toolchain by
hamed7ir; [Supermium](https://github.com/win32ss/supermium) by win32ss;
Chromium by The Chromium Authors; the Windows 10 Mobile approach from
gecko-w10m.

## License

GPL-3.0 for this repository's code (as Venetium and gecko-w10m); patches to
Chromium are under Chromium's BSD license; Chromium and its third parties keep
their own licenses.
