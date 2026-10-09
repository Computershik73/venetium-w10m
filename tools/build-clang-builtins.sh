#!/bin/bash
# clang_rt.builtins for rt2, which ships none.
#
# Chromium links clang's builtins library into every target of every toolchain
# (//build/config/clang:compiler_builtins) so that LLVM's helper calls resolve
# to C++'s strong symbols rather than Rust compiler_builtins' weak ones. This
# build has three toolchains -- the ARM32 target, the x64 host and the x86
# toolchain V8's ARM snapshot runs in (Venetium 0010) -- so it needs
# clang_rt.builtins-arm.lib, -i386.lib and -x86_64.lib under
# $VW_LLVM/lib/clang/23/lib/windows.
#
# For an MSVC target compiler-rt itself builds only the portable C sources
# (GENERIC_SOURCES in lib/builtins/CMakeLists.txt; the ARM assembly is for
# EABI/MinGW, and the MSVC ARM CRT supplies __rt_sdiv/__rt_udiv), which is
# exactly what is done here, with rt2's own clang-cl. x86_64 is copied from the
# installed official LLVM: it is host code for build tools only.
#
# Sources: compiler-rt/lib/builtins at llvmorg-23.1.1, fetched sparsely.
set -e
source "$(dirname "$0")/env.sh"

SRC_ROOT="${VW_COMPILER_RT:-C:/vw/dl/llvm/llvm-project}"
if [ ! -d "$SRC_ROOT/compiler-rt/lib/builtins" ]; then
  mkdir -p "$(dirname "$SRC_ROOT")"
  git clone -q --depth 1 --branch llvmorg-23.1.1 --filter=blob:none --sparse \
    https://github.com/llvm/llvm-project.git "$SRC_ROOT"
  git -C "$SRC_ROOT" sparse-checkout set compiler-rt/lib/builtins
fi
B="$SRC_ROOT/compiler-rt/lib/builtins"
OUTDIR="$VW_LLVM/lib/clang/23/lib/windows"
mkdir -p "$OUTDIR"

# GENERIC_SOURCES, straight from the CMake list.
SOURCES=$(python - "$B/CMakeLists.txt" <<'PY'
import re, sys
text = open(sys.argv[1], encoding="utf-8").read()
m = re.search(r"set\(GENERIC_SOURCES\n(.*?)\)", text, re.S)
print(" ".join(l.strip() for l in m.group(1).splitlines()
               if l.strip() and not l.strip().startswith("#")))
PY
)

SDK_INC="$VW_SDK/Include/$VW_SDK_VERSION"
export INCLUDE="$VW_VC/include;$SDK_INC/ucrt;$SDK_INC/um;$SDK_INC/shared"

build() {  # build <clang target> <lib suffix>
  local target="$1" suffix="$2"
  local work="${TMPDIR:-/tmp}/vw-builtins-$suffix"
  rm -rf "$work" && mkdir -p "$work"
  local objs=() n=0 skipped=()
  for s in $SOURCES; do
    local o="$work/${s%.c}.obj"
    if "$VW_LLVM/bin/clang-cl.exe" --target="$target" /nologo /c /O2 /GS- \
         /Zl /D_CRT_SECURE_NO_WARNINGS -Wno-everything \
         "$(cygpath -w "$B/$s")" "/Fo$(cygpath -w "$o")" 2> /dev/null; then
      objs+=("$(cygpath -w "$o")")
      n=$((n + 1))
    else
      skipped+=("$s")
    fi
  done
  "$VW_LLVM/bin/llvm-lib.exe" /nologo "/out:$(cygpath -w "$OUTDIR/clang_rt.builtins-$suffix.lib")" "${objs[@]}"
  echo "clang_rt.builtins-$suffix.lib: $n objects${skipped:+, skipped: ${skipped[*]}}"
}

build thumbv7-windows-msvc arm
build i686-windows-msvc i386

LLVM_OFFICIAL="${VW_LLVM_OFFICIAL:-C:/Program Files/LLVM}"
x64="$(ls "$LLVM_OFFICIAL"/lib/clang/*/lib/windows/clang_rt.builtins-x86_64.lib 2> /dev/null | tail -1)"
if [ -n "$x64" ]; then
  cp "$x64" "$OUTDIR/clang_rt.builtins-x86_64.lib"
  echo "clang_rt.builtins-x86_64.lib: copied from $x64"
else
  echo "WARNING: no official LLVM with clang_rt.builtins-x86_64.lib; host links will fail" >&2
fi
