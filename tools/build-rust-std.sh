#!/bin/bash
# Build the Rust standard library from source for the engine's ARM32 target and
# install it into the toolchain's sysroot, where Chromium's find_std_rlibs.py
# (rustc --print target-libdir) finds it. The target ships no prebuilt std.
#
# The target is thumbv7a-uwp-windows-msvchf (tools/rust-targets/), not the
# built-in thumbv7a-uwp-windows-msvc: the two differ in llvm-target, and rustc
# passes homogeneous float aggregates in VFP registers only when that string
# ends in "hf" -- a test meant for gnueabihf that no stock MSVC triple passes.
# On the stock target a struct of four floats goes in r1-r3 and the stack
# while clang-cl puts it in s0-s3. (Found in gecko-w10m; the same applies to
# every Rust/C++ boundary in Chromium.) The "uwp" vendor makes std use the app
# partition's APIs, which is what a Windows 10 Mobile app container has.
#
# Run again whenever VW_RUST_TOOLCHAIN changes: std built by one rustc cannot
# be linked by another.
set -e
source "$(dirname "$0")/env.sh"

TARGET="$VW_RUST_TARGET"
TC="+$VW_RUST_TOOLCHAIN"
SYSROOT="$(cygpath -u "$VW_RUST_SYSROOT")"
DEST="$SYSROOT/lib/rustlib/$TARGET"
echo "toolchain: $(rustc "$TC" -V)"
echo "sysroot:   $SYSROOT"

mkdir -p "$DEST/lib"
cp "$VW_ROOT/tools/rust-targets/$TARGET.json" "$DEST/target.json"

# C parts of std's dependencies (none by default on MSVC targets, but cc-rs
# is asked for a compiler anyway) go through rt2's clang-cl.
export CC_thumbv7a_uwp_windows_msvchf="$(cygpath -w "$VW_LLVM/bin/clang-cl.exe")"
export CFLAGS_thumbv7a_uwp_windows_msvchf="--target=thumbv7-windows-msvc"
export AR_thumbv7a_uwp_windows_msvchf="$(cygpath -w "$VW_LLVM/bin/llvm-lib.exe")"
export RUSTFLAGS="-Cembed-bitcode=yes -Zunstable-options"
# rustc finds a custom target through RUST_TARGET_PATH or the sysroot; the
# sysroot copy above is what the Chromium build will use, this is for cargo.
export RUST_TARGET_PATH="$(cygpath -w "$VW_ROOT/tools/rust-targets")"

WORK="${TMPDIR:-/tmp}/vw-stdbuild"
rm -rf "$WORK" && mkdir -p "$WORK" && cd "$WORK"
cargo "$TC" new --bin stdbuild > /dev/null 2>&1
cd stdbuild
printf '[profile.release]\npanic = "abort"\n' >> Cargo.toml
# test (and getopts with it) is part of the set Chromium's find_std_rlibs.py
# insists on; panic_abort because the target aborts on panic.
cargo "$TC" build --release -Z build-std=std,panic_abort,test \
  -Z build-std-features= --target "$TARGET" 2>&1 | tail -20 || true

rm -f "$DEST/lib/"*.rlib
cp target/"$TARGET"/release/deps/*.rlib "$DEST/lib/"

# Chromium's GN declares libprofiler_builtins.rlib among the sysroot's files
# (build/rust/std/BUILD.gn, skip_stdlib_files) and copies it into the build's
# own sysroot. It is only linked under -Cprofile-generate, which this build
# never uses, and building the real one needs compiler-rt's sources, which
# rust-src does not carry. An empty no_std crate of that name stands in.
echo '#![no_std]' > profiler_builtins.rs
# (A custom target is an unstable feature of rustc: -Zunstable-options.)
rustc "$TC" -Zunstable-options --crate-type=rlib \
  --crate-name=profiler_builtins --edition=2021 \
  -Copt-level=2 --target "$TARGET" profiler_builtins.rs \
  -o "$DEST/lib/libprofiler_builtins-0000000000000000.rlib"
echo "Installed $(ls "$DEST/lib/"*.rlib | wc -l) rlibs into $DEST/lib:"
ls "$DEST/lib/" | sed 's/-[0-9a-f]*\.rlib$//' | sort | tr '\n' ' '
echo
