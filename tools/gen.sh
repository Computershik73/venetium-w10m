#!/bin/bash
# Writes $VW_SRC/$VW_OUT/args.gn from args/w10m.gn and runs gn gen.
set -e
source "$(dirname "$0")/env.sh"
SRC="$(cygpath -u "$VW_SRC")"
OUT="$SRC/$VW_OUT"
mkdir -p "$OUT"

rustc_version="$(rustc "+$VW_RUST_TOOLCHAIN" -V | tr ' ()' '-  ' | tr -s ' -' '-' | sed 's/-$//')"
sed -e "s|@VW_LLVM@|$VW_LLVM|" \
    -e "s|@VW_RUST_SYSROOT@|$VW_RUST_SYSROOT|" \
    -e "s|@VW_RUSTC_VERSION@|$rustc_version|" \
    -e "s|@VW_BINDGEN@|$VW_BINDGEN|" \
    "$VW_ROOT/args/w10m.gn" > "$OUT/args.gn.new"
if ! cmp -s "$OUT/args.gn.new" "$OUT/args.gn"; then
  mv "$OUT/args.gn.new" "$OUT/args.gn"
else
  rm "$OUT/args.gn.new"
fi

# Chromium finds Visual Studio and the SDK through these (no depot_tools).
export vs2022_install="$(cygpath -w "${VW_VC%/VC/Tools/MSVC/*}")"
export WINDOWSSDKDIR="$(cygpath -w "$VW_SDK")\\"
export GYP_MSVS_VERSION=2022

cd "$SRC"
# The build's own scripts run through the same Python (ninja calls it by the
# path GN records).
export PATH="$(dirname "$(cygpath -u "$VW_PYTHON")"):$PATH"
# Only the engine and what it depends on: //BUILD.gn would pull in all of
# Chrome, its tests and their feature assertions.
"$(cygpath -u "$VW_GN")" gen "$VW_OUT" --script-executable="$VW_PYTHON" \
  --root-target=//venetium_w10m --root-pattern=//venetium_w10m:* "$@"
