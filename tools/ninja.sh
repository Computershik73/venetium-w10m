#!/bin/bash
# Runs ninja in the engine's build directory with the environment the build's
# scripts expect. Arguments go to ninja: tools/ninja.sh venetium_engine
set -e
source "$(dirname "$0")/env.sh"
cd "$(cygpath -u "$VW_SRC")"
export PATH="$(dirname "$(cygpath -u "$VW_PYTHON")"):$PATH"
# rt2's lld-link has no libxml2 of its own and merges manifests with the SDK's
# mt.exe, which it looks for on PATH (lld-link runs with ninja's environment).
export PATH="$(cygpath -u "$VW_SDK/bin/$VW_SDK_VERSION/x64"):$PATH"
exec "$VW_NINJA" -C "$VW_OUT" -j "$VW_JOBS" "$@"
