#!/bin/bash
# Compiles the shaders of the Direct3D 11 compositor (feature level 9_3):
# writes components/viz/service/display/w10m_d3d11_shaders.h in the Chromium
# tree from w10m_d3d11_shaders.hlsl beside it, with the Windows SDK's fxc.exe.
# The script that does it, w10m_d3d11_shaders.py, is beside them too and has
# the six command lines (vs_4_0_level_9_3 and ps_4_0_level_9_3, /O3).
#
# Not a step of the build: the header is in the patch series like any other
# source, so that the build needs no fxc and the phone compiles nothing. Run
# this after changing the HLSL, commit both in the tree, and export the
# patches (tools/export-patches.sh).
#
#   VW_FXC   fxc.exe   ($VW_SDK/bin/$VW_SDK_VERSION/x64/fxc.exe)
#
# With an argument, a directory, fxc's listings are written there: each
# shader's level 9 code and how many instructions it takes.
set -e
source "$(dirname "$0")/env.sh"
FXC="${VW_FXC:-$VW_SDK/bin/$VW_SDK_VERSION/x64/fxc.exe}"
if [ ! -f "$FXC" ]; then
  echo "No fxc.exe at $FXC; set VW_FXC." >&2
  exit 1
fi
"$VW_PYTHON" "$VW_SRC/components/viz/service/display/w10m_d3d11_shaders.py" \
  "$FXC" "$@"
