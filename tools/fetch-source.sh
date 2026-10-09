#!/bin/bash
# Fetches everything the engine is built from, into C:/vw by default:
#
#   $VW_SRC              Chromium 150.0.7871.222, Google's lite source tarball
#                        (sha256 checked against Google's .hashes file)
#   $VW_SUPERMIUM        Supermium 150's patches (github.com/win32ss/supermium,
#                        main as of 2026-09-30)
#   $VW_LLVM             rt2, LLVM 23.1.1 with six ARM32 Windows patches
#                        (github.com/hamed7ir/llvm-rt1, sha256 checked)
#   rustup toolchain     $VW_RUST_TOOLCHAIN with rust-src
#   $VW_BINDGEN          bindgen-cli built with cargo, libclang.dll and rustfmt
#
# About 2 GB downloaded; the tree takes ~11 GB unpacked. Then:
#   tools/apply-patches.sh, tools/setup-tree.sh, tools/build-all.sh
set -e
source "$(dirname "$0")/env.sh"
DL="${VW_DOWNLOADS:-C:/vw/dl}"
DLU="$(cygpath -u "$DL")"
mkdir -p "$DLU"

CHROMIUM_VERSION="${VW_CHROMIUM_VERSION:-150.0.7871.222}"
SUPERMIUM_COMMIT="${VW_SUPERMIUM_COMMIT:-main}"

# --- Chromium ----------------------------------------------------------------
SRC="$(cygpath -u "$VW_SRC")"
if [ ! -f "$SRC/chrome/VERSION" ]; then
  tarball="chromium-$CHROMIUM_VERSION-lite.tar.xz"
  base="https://commondatastorage.googleapis.com/chromium-browser-official"
  if [ ! -f "$DLU/$tarball" ]; then
    curl -fL --retry 10 -C - -o "$DLU/$tarball" "$base/$tarball"
  fi
  want="$(curl -sfL "$base/$tarball.hashes" | awk '$1 == "sha256" {print $2}')"
  have="$(sha256sum "$DLU/$tarball" | cut -d' ' -f1)"
  [ "$want" = "$have" ] || { echo "$tarball: sha256 $have, expected $want" >&2; exit 1; }
  python "$(cygpath -w "$VW_ROOT/tools/extract-tarball.py")" "$(cygpath -w "$DLU/$tarball")" "$(cygpath -w "$SRC")"
fi
echo "chromium   $(tr '\n' . < "$SRC/chrome/VERSION" | sed 's/MAJOR=//;s/MINOR=//;s/BUILD=//;s/PATCH=//;s/\.$//')"

# --- Supermium patches -----------------------------------------------------------
SUP="$(cygpath -u "${VW_SUPERMIUM:-$DL/supermium}")"
mkdir -p "$SUP"
for name in $(grep '^supermium ' "$VW_ROOT/patches/series" | awk '{print $2}'); do
  [ -f "$SUP/$name.patch" ] || curl -sfL -o "$SUP/$name.patch" \
    "https://raw.githubusercontent.com/win32ss/supermium/$SUPERMIUM_COMMIT/$name.patch"
done
echo "supermium  $(ls "$SUP"/*.patch | wc -l) patches"

# --- rt2 ---------------------------------------------------------------------
LLVM="$(cygpath -u "$VW_LLVM")"
if [ ! -f "$LLVM/bin/clang-cl.exe" ]; then
  rel="https://github.com/hamed7ir/llvm-rt1/releases/download/rt2-23.1.1"
  curl -fL -o "$DLU/rt2.tar.gz" "$rel/rt2-23.1.1-rt2-arm32-win.tar.gz"
  want="$(curl -sfL "$rel/SHA256SUMS" | awk '{print $1}')"
  [ "$(sha256sum "$DLU/rt2.tar.gz" | cut -d' ' -f1)" = "$want" ] || { echo "rt2: bad sha256" >&2; exit 1; }
  mkdir -p "$(dirname "$LLVM")"
  tar -xzf "$DLU/rt2.tar.gz" -C "$(dirname "$LLVM")"
fi
echo "rt2        $("$LLVM/bin/clang-cl.exe" --version | head -1)"

# --- Rust --------------------------------------------------------------------
rustup toolchain install "$VW_RUST_TOOLCHAIN" --profile minimal --component rust-src > /dev/null
echo "rust       $(rustc "+$VW_RUST_TOOLCHAIN" -V)"

# --- bindgen -------------------------------------------------------------------
BG="$(cygpath -u "$VW_BINDGEN")"
if [ ! -f "$BG/bin/bindgen.exe" ]; then
  stable="$(cygpath -u "$(rustc +stable --print sysroot)")"
  "$stable/bin/cargo.exe" install bindgen-cli --locked --root "$(cygpath -w "$BG")"
fi
LLVM_OFFICIAL="${VW_LLVM_OFFICIAL:-C:/Program Files/LLVM}"
cp -u "$(cygpath -u "$LLVM_OFFICIAL")/bin/libclang.dll" "$BG/bin/"
stable_bin="$(cygpath -u "$(rustc +stable --print sysroot)")/bin"
cp -u "$stable_bin/rustfmt.exe" "$stable_bin"/rustc_driver-*.dll "$stable_bin"/std-*.dll "$BG/bin/"
echo "bindgen    $("$BG/bin/bindgen.exe" --version)"
