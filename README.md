# venetium-w10m

**Venetium for Windows 10 Mobile**: Chromium 150 — the engine of
[Venetium](https://github.com/hamed7ir/venetium), Chromium for old 32-bit ARM
Windows tablets — as a UWP browser for Lumia-era phones, with Chrome's own
interface laid out as Chrome for Android's. Brought to the phone the way
[gecko-w10m](https://github.com/Computershik73/gecko-w10m) brings Gecko there.

> **Status: usable, early.** It runs every day on an HP Elite x3 (Direct3D
> feature level 11_1) and on a Lumia 650 (feature level 9_3, 1 GB of RAM).
> Expect rough edges; logs help (see *Reporting a problem*).

## What it does

- **Chrome's interface**, as on an Android phone: Home, the address bar, a new
  tab, the tab count (a grid of tabs) and the menu; the address bar takes the
  whole toolbar while typing; the phone's Back key closes a menu, a dialog or
  the tab grid, leaves fullscreen or the address bar, then goes back in the
  tab's history.
- **GPU everywhere it can**: compositing and rasterization through ANGLE on
  Direct3D 11, on feature level 11 phones and on feature level 9_3 ones
  (Lumia 550/650 class, Adreno 304), with a fallback to software when the
  driver refuses.
- **Hardware video**: H.264 on the phone's decoder (up to 1080p30 at feature
  level 9_3, where sites are told 60 frames a second is not smooth); VP9 and
  AV1 are turned down so sites send H.264.
- **JavaScript JIT**: V8's compilers with executable memory from the
  `codeGeneration` capability.
- **A built-in ad and tracker blocker** (Venetium's): EasyList and
  EasyPrivacy through Chromium's subresource filter, on every site.
- **Memory**: one process in a 32-bit address space; background tabs are
  unloaded before the phone's limit is reached, and memory pressure is told to
  Chromium from the phone's own numbers.

## Install

1. **Windows 10 Mobile 1703 (15063) or newer**; tested on 15254.
2. **Interop unlock**: the package declares restricted capabilities
   (`codeGeneration` for the JIT).
3. **Trust the certificate** `Venetium.cer` (the same signer as Gecko for
   Windows 10 Mobile: a phone that installs gecko-w10m installs this).
4. Install the `.appx` (Device Portal, or an appx installer on the phone).
   A newer build installs over an older one and keeps the profile.

## Reporting a problem

Menu → *Windows 10 Mobile* → *Copy logs to Pictures* puts `venetium.log` and
`venetium-previous.log` into `Pictures\Venetium logs`. By default the log
holds only what happens once or rarely (start, errors, crashes); for slowness
add `--vw-diagnostics` in *Engine switches (restart)*: long tasks, processor
use by thread, memory every 2 s, video and shader statistics. Remove it after:
each line is a write to the phone's flash.

## Why it is not just Venetium in an appx

Venetium is a desktop Win32 program. A Windows 10 Mobile app has no child
processes, no window messages (`CreateWindow` and `PostMessage` are stubs),
no Direct3D 9, no `shell32`, and executable memory only with the
`codeGeneration` capability. So:

| Desktop Venetium | venetium-w10m |
|---|---|
| `chrome.exe` + `chrome.dll`, renderer, GPU, network, utility processes | `venetium_engine.dll` in a UWP app: **one process**, `--single-process`, everything on threads |
| Chrome's Views in HWNDs | the same Views on **one Aura root without an HWND**, drawn into the app's `SwapChainPanel`; a thin XAML shell for the panel, touch, keyboard and the Back key |
| ANGLE on Direct3D 9 (Tegra) | ANGLE on **Direct3D 11**, feature level 9_3 included |
| V8 JIT | the same, with executable memory from the `codeGeneration` capability |
| sandbox, crashpad, installer | not built: the app container is the sandbox |

Design notes: [docs/PORTING-W10M.md](docs/PORTING-W10M.md) (from the
start of the port; [docs/BUILD-STATUS.md](docs/BUILD-STATUS.md) is its
log of early bring-up).

## Layout

```
engine/venetium_w10m/   the engine: //venetium_w10m in the Chromium tree
  capi/                 venetium_w10m_capi.h -- the C interface the shell uses
  app/                  start-up: the command line, presentation, paths
  chrome/               Chrome's interface in the engine: main delegate,
                        browser/renderer clients, the host, the tab grid
  browser/              the panel, window manager, memory, hang watch
  renderer/             the renderer client (codec answers)
  common/               log bridge, user agent, delay-load hook, divide
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
runs GN and ninja, makes the ad blocker's list (`tools/build-filters.sh`), and
packs and signs the appx (`tools/build-appx.sh`). The package lands in
`app/VenetiumW10m/AppPackages/`.

Signing uses gecko-w10m's certificate by default (same Publisher,
`CN=Computershik`); `VW_PFX` names another. `tools/phone.py` installs over
Device Portal and fetches the logs.

## Credits

[Venetium](https://github.com/hamed7ir/venetium) and its rt2 toolchain by
hamed7ir, at whose request this port is made; [Supermium](https://github.com/win32ss/supermium)
by win32ss; Chromium by The Chromium Authors; the Windows 10 Mobile approach
from [gecko-w10m](https://github.com/Computershik73/gecko-w10m).

## License

GPL-3.0 for this repository's code (as Venetium; see [LICENSE](LICENSE));
patches to Chromium are under Chromium's BSD license; Chromium and its third
parties keep their own licenses.
