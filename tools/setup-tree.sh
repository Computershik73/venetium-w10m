#!/bin/bash
# The parts of a Chromium checkout that gclient's hooks would have fetched,
# for a tree unpacked from Google's source tarball (tools/fetch-source.sh).
# The tarball is made on Linux, so every dependency DEPS limits to Windows
# checkouts is missing from it as well. Idempotent: run it again any time.
#
#   build/toolchain/win/rc/win/rc.exe     Chromium's resource compiler (GCS, by the sha1 in the tree)
#   third_party/node/win/node.exe         Node at the version update_node_binaries names (nodejs.org,
#                                         checked against its SHASUMS256)
#   third_party/devtools-frontend/src/third_party/esbuild/esbuild.exe
#                                         Blink's inspector overlay is bundled with it (CIPD)
#   third_party/gperf, microsoft_dxheaders/src, microsoft_webauthn/src, perl
#                                         Windows-only git dependencies, at DEPS' revisions
#   third_party/rust-toolchain/bin/rustc.exe
#                                         named as an input by //build/rust even with a custom
#                                         toolchain; a copy of the toolchain's rustc
#   third_party/llvm-build/Release+Asserts
#                                         a junction to rt2: build/toolchain/win/rc/rc.py runs the
#                                         resource preprocessor from there, whatever clang_base_path says
#   codec configurations for win/arm      dav1d, libaom, libvpx and ffmpeg (tools/gen-codec-configs.py,
#                                         deps/ffmpeg), the files Venetium's deps/arm-inputs.tsv names
#   the i686 Rust std                     V8's ARM snapshot is made by an x86 build of V8, which has Rust
#   gn.exe                                at DEPS' gn_version (CIPD, into $VW_GN)
set -e
source "$(dirname "$0")/env.sh"
SRC="$(cygpath -u "$VW_SRC")"
cd "$SRC"

# --- rc.exe ------------------------------------------------------------------
rc="build/toolchain/win/rc/win/rc.exe"
want="$(cat "$rc.sha1")"
if [ ! -f "$rc" ] || [ "$(sha1sum "$rc" | cut -d' ' -f1)" != "$want" ]; then
  curl -sfL -o "$rc" "https://storage.googleapis.com/chromium-browser-clang/rc/$want"
fi
echo "rc.exe       $(sha1sum "$rc" | cut -d' ' -f1)"

# --- node.exe ----------------------------------------------------------------
node_version="$(sed -n 's/^NODE_VERSION="\(v[0-9.]*\)".*/\1/p' third_party/node/update_node_binaries)"
node="third_party/node/win/node.exe"
mkdir -p "$(dirname "$node")"
if [ ! -f "$node" ] || [ "$("$node" --version 2> /dev/null)" != "$node_version" ]; then
  curl -sfL -o "$node.new" "https://nodejs.org/dist/$node_version/win-x64/node.exe"
  sum="$(curl -sfL "https://nodejs.org/dist/$node_version/SHASUMS256.txt" | grep ' win-x64/node.exe$' | cut -d' ' -f1)"
  if [ "$(sha256sum "$node.new" | cut -d' ' -f1)" != "$sum" ]; then
    echo "node.exe $node_version: checksum mismatch" >&2
    rm -f "$node.new"
    exit 1
  fi
  mv "$node.new" "$node"
fi
echo "node.exe     $("$node" --version)"

# --- esbuild.exe -------------------------------------------------------------
esbuild_dir="third_party/devtools-frontend/src/third_party/esbuild"
esbuild_version="$(python - <<'PY'
import re
t = open("third_party/devtools-frontend/src/DEPS", encoding="utf-8").read()
m = re.search(r"'third_party/esbuild'.*?'version':\s*'([^']+)'", t, re.S)
print(m.group(1))
PY
)"
if [ ! -f "$esbuild_dir/esbuild.exe" ]; then
  curl -sfL -o "$esbuild_dir/esbuild.zip" \
    "https://chrome-infra-packages.appspot.com/dl/infra/3pp/tools/esbuild/windows-amd64/+/$esbuild_version"
  python -c "import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])" \
    "$esbuild_dir/esbuild.zip" "$esbuild_dir"
  rm -f "$esbuild_dir/esbuild.zip"
fi
# The tarball carries Linux's esbuild (an ELF file named "esbuild"); the
# DevTools scripts run ".../esbuild", which on Windows must find the .exe.
# (Not rm: MSYS takes "esbuild" for esbuild.exe once the ELF file is gone.)
python - "$esbuild_dir/esbuild" <<'PY'
import os, sys
p = sys.argv[1]
if os.path.isfile(p) and open(p, "rb").read(4) == b"ELF":
    os.remove(p)
PY
echo "esbuild.exe  $("$esbuild_dir/esbuild.exe" --version)"

# --- rollup's native module for Windows ---------------------------------------
# DevTools' node_modules (the inspector overlay is bundled with rollup) carry
# rollup's Linux binary only; npm on Windows would have installed this one.
# The npm registry's sha512 is checked.
rollup_dir="third_party/devtools-frontend/src/node_modules"
rollup_version="$(python -c "import json; print(json.load(open('$rollup_dir/rollup/package.json'))['version'])")"
rollup_win="$rollup_dir/@rollup/rollup-win32-x64-msvc"
if [ ! -f "$rollup_win/package.json" ]; then
  meta="https://registry.npmjs.org/@rollup/rollup-win32-x64-msvc/$rollup_version"
  curl -sfL -o out/vw-rollup.json "$meta"
  tgz_url="$(python -c "import json; print(json.load(open('out/vw-rollup.json'))['dist']['tarball'])")"
  integrity="$(python -c "import json; print(json.load(open('out/vw-rollup.json'))['dist']['integrity'])")"
  curl -sfL -o out/vw-rollup.tgz "$tgz_url"
  have="sha512-$(openssl dgst -sha512 -binary out/vw-rollup.tgz | base64 -w0)"
  [ "$have" = "$integrity" ] || { echo "rollup-win32-x64-msvc: $have, expected $integrity" >&2; exit 1; }
  mkdir -p "$rollup_win"
  tar -xzf out/vw-rollup.tgz -C "$rollup_win" --strip-components=1
  rm -f out/vw-rollup.tgz out/vw-rollup.json
fi
echo "rollup       $rollup_version (+ win32-x64-msvc)"

# --- Windows-only git dependencies ---------------------------------------------
python - <<'PY' | tr -d '\r' > /tmp/vw-win-deps.txt
import re
t = open("DEPS", encoding="utf-8").read()
for name in ["third_party/gperf", "third_party/microsoft_dxheaders/src",
             "third_party/microsoft_webauthn/src", "third_party/perl"]:
    body = re.search(r"'src/" + re.escape(name) + r"'\s*:\s*\{(.*?)\n  \},", t, re.S).group(1)
    url, rev = re.search(r"'url':\s*Var\('chromium_git'\)\s*\+\s*'([^']+)'\s*\+\s*'@'\s*\+\s*'([0-9a-f]+)'", body).groups()
    print(name, "https://chromium.googlesource.com" + url, rev)
PY
while read -r path url rev; do
  if [ "$(git -C "$path" rev-parse HEAD 2> /dev/null)" != "$rev" ]; then
    mkdir -p "$path"
    git -C "$path" init -q
    git -C "$path" fetch -q --depth 1 "$url" "$rev"
    git -C "$path" checkout -q FETCH_HEAD
  fi
  echo "$(printf '%-12s' "$(basename "$path")") $rev"
done < /tmp/vw-win-deps.txt

# --- rustc.exe for //build/rust's inputs ------------------------------------
rt="third_party/rust-toolchain/bin"
mkdir -p "$rt"
cp -u "$(cygpath -u "$VW_RUST_SYSROOT")/bin/rustc.exe" "$rt/rustc.exe"
echo "rustc.exe    $(rustc "+$VW_RUST_TOOLCHAIN" -V)"

# --- llvm-build junction for rc.py --------------------------------------------
llvm_build="third_party/llvm-build/Release+Asserts"
if [ ! -f "$llvm_build/bin/clang-cl.exe" ]; then
  mkdir -p third_party/llvm-build
  powershell.exe -NoProfile -Command "New-Item -ItemType Junction -Path '$(cygpath -w "$SRC/$llvm_build")' -Target '$(cygpath -w "$VW_LLVM")'" > /dev/null
fi
echo "llvm-build   -> $("$llvm_build/bin/clang-cl.exe" --version | head -1)"

# --- codec configurations ------------------------------------------------------
python "$(cygpath -w "$VW_ROOT/tools/gen-codec-configs.py")" "$(cygpath -w "$SRC")"   "$(cygpath -w "$VW_ROOT/deps/ffmpeg/Chrome/win/arm-neon")" | tail -1

# --- the x86 Rust std (V8's snapshot toolchain) ---------------------------------
rustup target add i686-pc-windows-msvc --toolchain "$VW_RUST_TOOLCHAIN" > /dev/null 2>&1
echo "rust std     i686-pc-windows-msvc for $VW_RUST_TOOLCHAIN"

# --- gn.exe ------------------------------------------------------------------
gn="$(cygpath -u "$VW_GN")"
if [ ! -f "$gn" ]; then
  rev="$(sed -n "s/.*'gn_version': 'git_revision:\([0-9a-f]*\)'.*/\1/p" DEPS)"
  mkdir -p "$(dirname "$gn")"
  curl -sfL -o "$(dirname "$gn")/gn.zip" \
    "https://chrome-infra-packages.appspot.com/dl/gn/gn/windows-amd64/+/git_revision:$rev"
  python -c "import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])" \
    "$(cygpath -w "$(dirname "$gn")/gn.zip")" "$(cygpath -w "$(dirname "$gn")")"
fi
echo "gn.exe       $("$gn" --version)"
