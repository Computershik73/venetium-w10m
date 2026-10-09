# Build environment shared by the tools/*.sh scripts. Source it, do not run it.
# Every location has a default and can be overridden by setting the variable
# before the script runs:
#
#   VW_SRC             the Chromium tree                         (C:/vw/src)
#   VW_OUT             output directory, relative to VW_SRC       (out/w10m)
#   VW_LLVM            rt2: clang-cl, lld-link (LLVM 23.1.1 + ARM32 patches)
#                                                                 (C:/vw/rt2/rt2-23.1.1-rt2-arm32-win)
#   VW_RUST_TOOLCHAIN  rustup toolchain the engine's Rust is built with
#                                                                 (nightly-2026-04-10: Chromium 150's pin)
#   VW_BINDGEN         bindgen.exe, libclang.dll, rustfmt.exe in bin/ (C:/vw/bindgen)
#   VW_GN              gn.exe at the revision DEPS pins           (C:/vw/tools/gn/gn.exe)
#   VW_NINJA           ninja.exe                                  (ninja on PATH)
#   VW_VC              MSVC toolset with ARM32 libraries           (newest under VS 2022 that has them)
#   VW_SDK             Windows Kits 10                            (C:/Program Files (x86)/Windows Kits/10)
#   VW_SDK_VERSION     SDK for headers, libraries and tools        (10.0.22621.0: the last with ARM32 libs)
#   VW_SDK_LIB         SDK the shell links against                 (10.0.14393.0 if installed: 1607 phones)
#   VW_JOBS            parallel jobs                               (CPUs - 2)
#   VW_ATLMFC_DIR      an atlmfc directory with 32-bit ARM ATL, when VW_VC has none
#                                                                 (VS 2017's 14.16, if installed)
#   VW_PYTHON          Python for GN and the build's scripts        (Python 3.12: the python3 on
#                                                                 PATH may be the Store alias)
#
# The Chromium tree's path is short on purpose: Chromium's deepest generated
# paths plus a long prefix pass MAX_PATH for tools that have no long-path
# manifest (rc.exe, midl).

VW_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export VW_ROOT
export VW_SRC="${VW_SRC:-C:/vw/src}"
export VW_OUT="${VW_OUT:-out/w10m}"
export VW_LLVM="${VW_LLVM:-C:/vw/rt2/rt2-23.1.1-rt2-arm32-win}"
export VW_RUST_TOOLCHAIN="${VW_RUST_TOOLCHAIN:-nightly-2026-04-10}"
export VW_BINDGEN="${VW_BINDGEN:-C:/vw/bindgen}"
export VW_GN="${VW_GN:-C:/vw/tools/gn/gn.exe}"
export VW_NINJA="${VW_NINJA:-$(command -v ninja)}"
export VW_SDK="${VW_SDK:-C:/Program Files (x86)/Windows Kits/10}"
export VW_SDK_VERSION="${VW_SDK_VERSION:-10.0.22621.0}"
# The shell links against the oldest SDK there is, so the package starts on
# 1607 phones (gecko-w10m); the engine uses VW_SDK_VERSION's ARM32 libraries.
if [ -z "$VW_SDK_LIB" ]; then
  if [ -f "$VW_SDK/Lib/10.0.14393.0/um/arm/WindowsApp.lib" ]; then
    VW_SDK_LIB=10.0.14393.0
  else
    VW_SDK_LIB="$VW_SDK_VERSION"
  fi
fi
export VW_SDK_LIB
export VW_RUST_TARGET="thumbv7a-uwp-windows-msvchf"

# The rustup toolchain directory itself is the sysroot GN is pointed at.
if [ -z "$VW_RUST_SYSROOT" ]; then
  VW_RUST_SYSROOT="$(rustc "+$VW_RUST_TOOLCHAIN" --print sysroot 2> /dev/null | tr '\\' '/')"
fi
export VW_RUST_SYSROOT

# MSVC: headers and libraries for clang-cl, and cl.exe for the shell. It must
# have the ARM32 libraries, which VS 2022 17.14 (14.44) is the last to ship.
if [ -z "$VW_VC" ]; then
  for cl in "C:/Program Files/Microsoft Visual Studio/2022/"*/VC/Tools/MSVC/*/bin/Hostx64/arm/cl.exe; do
    [ -f "$cl" ] && VW_VC="${cl%/bin/Hostx64/arm/cl.exe}"
  done
fi
if [ -z "$VW_VC" ] || [ ! -f "$VW_VC/bin/Hostx64/arm/cl.exe" ]; then
  echo "No MSVC toolset with ARM32 tools found; install the VS 2022 'MSVC ARM build tools' or set VW_VC." >&2
  return 1 2> /dev/null || exit 1
fi
export VW_VC

# ATL: Chromium's base/win and Windows accessibility code need it. VS 2022 17.14
# may be installed without it; VS 2017's 14.16 toolset has ARM32 ATL libraries.
if [ -z "$VW_ATLMFC_DIR" ] && [ ! -d "$VW_VC/atlmfc/include" ]; then
  for d in "C:/Program Files (x86)/Microsoft Visual Studio/2017/"*/VC/Tools/MSVC/14.16.*/atlmfc; do
    [ -f "$d/lib/arm/atls.lib" ] && VW_ATLMFC_DIR="$d"
  done
fi
if [ -n "$VW_ATLMFC_DIR" ]; then
  export VW_ATLMFC_DIR="$(cygpath -w "$VW_ATLMFC_DIR")"
fi

if [ -z "$VW_JOBS" ]; then
  n="$(nproc 2> /dev/null || echo 4)"
  VW_JOBS=$(( n > 2 ? n - 2 : 1 ))
fi
export VW_JOBS

if [ -z "$VW_PYTHON" ]; then
  for p in "$LOCALAPPDATA/Programs/Python/Python312/python.exe"            "C:/Users/$USERNAME/AppData/Local/Programs/Python/Python312/python.exe"            "C:/Python312/python.exe"; do
    p="$(cygpath -u "$p" 2> /dev/null || echo "$p")"
    [ -f "$p" ] && VW_PYTHON="$(cygpath -m "$p")" && break
  done
fi
export VW_PYTHON="${VW_PYTHON:-$(cygpath -m "$(command -v python)")}"

# Git Bash would otherwise rewrite every "/switch" argument into a path; paths
# handed to Windows tools go through cygpath -w instead.
export MSYS2_ARG_CONV_EXCL="*"

# Chromium's scripts are Python 3 and expect a Windows python on PATH.
export PYTHONDONTWRITEBYTECODE=1
export DEPOT_TOOLS_WIN_TOOLCHAIN=0
