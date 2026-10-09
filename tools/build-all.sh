#!/bin/bash
# From nothing to a signed appx:
#   fetch-source -> apply-patches -> setup-tree -> clang builtins, Rust std ->
#   gn gen -> ninja venetium_w10m -> build-appx
# Each step can be run on its own; each skips what is already done.
set -e
TOOLS="$(cd "$(dirname "$0")" && pwd)"
source "$TOOLS/env.sh"

echo "=== fetch the sources and toolchains"
bash "$TOOLS/fetch-source.sh"

echo "=== apply the patch series"
bash "$TOOLS/apply-patches.sh"

echo "=== link the engine into the tree"
SRC="$(cygpath -u "$VW_SRC")"
if [ ! -e "$SRC/venetium_w10m/BUILD.gn" ]; then
  powershell.exe -NoProfile -Command "New-Item -ItemType Junction -Path '$(cygpath -w "$SRC/venetium_w10m")' -Target '$(cygpath -w "$VW_ROOT/engine/venetium_w10m")'" > /dev/null
fi

echo "=== what gclient's hooks would have fetched"
bash "$TOOLS/setup-tree.sh"

echo "=== clang builtins for rt2, Rust std for the ARM32 UWP target"
[ -f "$(cygpath -u "$VW_LLVM")/lib/clang/23/lib/windows/clang_rt.builtins-arm.lib" ] ||
  bash "$TOOLS/build-clang-builtins.sh"
[ -f "$(cygpath -u "$VW_RUST_SYSROOT")/lib/rustlib/$VW_RUST_TARGET/target.json" ] ||
  bash "$TOOLS/build-rust-std.sh"

echo "=== gn gen"
bash "$TOOLS/gen.sh"

echo "=== build the engine"
bash "$TOOLS/ninja.sh" venetium_w10m

echo "=== package"
bash "$TOOLS/build-appx.sh"
