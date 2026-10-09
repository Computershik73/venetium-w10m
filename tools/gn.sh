#!/bin/bash
# Runs a gn command (path, desc, refs, ls, ...) on the engine's build
# directory with the flags tools/gen.sh generated it with:
#   tools/gn.sh path //venetium_w10m:venetium_engine //some:target
set -e
source "$(dirname "$0")/env.sh"
cd "$(cygpath -u "$VW_SRC")"
export PATH="$(dirname "$(cygpath -u "$VW_PYTHON")"):$PATH"
cmd="$1"
shift
exec "$(cygpath -u "$VW_GN")" "$cmd" "$VW_OUT" --script-executable="$VW_PYTHON" \
  --root-target=//venetium_w10m --root-pattern=//venetium_w10m:* "$@"
