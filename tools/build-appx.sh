#!/bin/bash
# Builds the UWP shell, stages the engine beside it and packs, signs and
# delivers Venetium for Windows 10 Mobile (ARM32) into app/VenetiumW10m/AppPackages.
#
# The shell is code-only C++/WinRT XAML compiled by MSVC's ARM cl.exe (clang
# has no C++ exception handling on 32-bit ARM Windows, and C++/WinRT throws).
# The engine is venetium_engine.dll from the Chromium build ($VW_SRC/$VW_OUT).
# MSBuild is not involved: makeappx packs and signtool signs (as gecko-w10m).
#
# Every path handed to a Windows tool goes through cygpath -w.
set -e
source "$(dirname "$0")/env.sh"
PATH="/usr/bin:$PATH"

ROOT="$VW_ROOT"
APP="$ROOT/app/VenetiumW10m"
OUT="$APP/AppPackages"
STAGE="$OUT/stage"
OBJ="$OUT/obj"
ENGINE_OUT="$(cygpath -u "$VW_SRC")/$VW_OUT"

VC="$VW_VC"
SDK="$VW_SDK"
SDKV="$VW_SDK_VERSION"
SDKV_LIB="$VW_SDK_LIB"
CL="$VC/bin/Hostx64/arm/cl.exe"
LINK="$VC/bin/Hostx64/arm/link.exe"
BIN="$SDK/bin/$SDKV/x64"

export INCLUDE="$(cygpath -w "$VC/include");$(cygpath -w "$SDK/Include/$SDKV/ucrt");$(cygpath -w "$SDK/Include/$SDKV/shared");$(cygpath -w "$SDK/Include/$SDKV/um");$(cygpath -w "$SDK/Include/$SDKV/winrt");$(cygpath -w "$SDK/Include/$SDKV/cppwinrt")"
# The umbrella library decides which api-set DLLs the shell imports: link
# against the oldest SDK the package should start on (VW_SDK_LIB).
export LIB="$(cygpath -w "$VC/lib/arm/store");$(cygpath -w "$VC/lib/arm");$(cygpath -w "$SDK/Lib/$SDKV_LIB/ucrt/arm");$(cygpath -w "$SDK/Lib/$SDKV_LIB/um/arm")"
export PATH="$(cygpath -u "$VC/bin/Hostx64/x64"):$PATH"

# --- version: 150.<build>.<patch>.<counter> ------------------------------------
COUNTER="$APP/build-number.txt"
[ -f "$COUNTER" ] || echo 0 > "$COUNTER"
BUILD="$(( $(tr -cd '0-9' < "$COUNTER") + 1 ))"
echo "$BUILD" > "$COUNTER"
CHROME_VERSION="$(cygpath -u "$VW_SRC")/chrome/VERSION"
MAJOR="$(sed -n 's/^MAJOR=//p' "$CHROME_VERSION" | tr -d '\r')"
CBUILD="$(sed -n 's/^BUILD=//p' "$CHROME_VERSION" | tr -d '\r')"
CPATCH="$(sed -n 's/^PATCH=//p' "$CHROME_VERSION" | tr -d '\r')"
VERSION="$MAJOR.$CBUILD.$CPATCH.$BUILD"
python - "$(cygpath -w "$APP/Package.appxmanifest")" "$VERSION" <<'PY'
import re, sys
path, version = sys.argv[1], sys.argv[2]
text = open(path, encoding="utf-8").read()
text = re.sub(r'(<Identity[^>]*?Version=")[0-9.]+(")',
              lambda m: m.group(1) + version + m.group(2), text, count=1, flags=re.S)
open(path, "w", encoding="utf-8", newline="\n").write(text)
PY
echo "=== Venetium $VERSION ==="
PKG="$OUT/Venetium_${VERSION}_ARM.appx"

for try in 1 2 3 4 5; do
  rm -rf "$STAGE" 2> /dev/null
  [ -e "$STAGE" ] || break
  sleep 2
done
[ -e "$STAGE" ] && { echo "cannot remove $STAGE" >&2; exit 1; }
mkdir -p "$STAGE/Assets" "$OBJ"

# --- the shell ---------------------------------------------------------------
echo "=== compile the shell (cl.exe ARM, C++/WinRT) ==="
SRCS="App.cpp MainPage.cpp client/EngineView.cpp client/Log.cpp client/Settings.cpp engine/EngineHost.cpp"
OBJS=()
# The precompiled header: C++/WinRT's headers are most of every unit.
"$CL" /nologo /c /Yc"pch.h" "$(cygpath -w "$APP/pch.cpp")" \
  "/Fp$(cygpath -w "$OBJ/pch.pch")" "/Fo$(cygpath -w "$OBJ/pch.obj")" \
  /std:c++20 /EHsc /GR- /O2 /utf-8 /bigobj /MT \
  /D_ARM_ /DWIN32 /D_WIN32 /DNOMINMAX /DUNICODE /D_UNICODE \
  /DWINAPI_FAMILY=WINAPI_FAMILY_APP /D_WIN32_WINNT=0x0A00 \
  "/I$(cygpath -w "$APP")" "/I$(cygpath -w "$ROOT/engine/venetium_w10m/capi")"
OBJS+=("$(cygpath -w "$OBJ/pch.obj")")
for s in $SRCS; do
  name="$(echo "$s" | tr '/' '_' | sed 's/\.cpp$/.obj/')"
  "$CL" /nologo /c /Yu"pch.h" "/Fp$(cygpath -w "$OBJ/pch.pch")" \
    "$(cygpath -w "$APP/$s")" "/Fo$(cygpath -w "$OBJ/$name")" \
    /std:c++20 /EHsc /GR- /O2 /utf-8 /bigobj /MT \
    /D_ARM_ /DWIN32 /D_WIN32 /DNOMINMAX /DUNICODE /D_UNICODE \
    /DWINAPI_FAMILY=WINAPI_FAMILY_APP /D_WIN32_WINNT=0x0A00 \
    "/I$(cygpath -w "$APP")" "/I$(cygpath -w "$ROOT/engine/venetium_w10m/capi")"
  OBJS+=("$(cygpath -w "$OBJ/$name")")
done

echo "=== link Venetium.exe ==="
# oleaut32 delay-loaded: C++/WinRT reaches it only on error paths, and a
# phone without it would refuse the executable before wWinMain (gecko-w10m).
"$LINK" /nologo "${OBJS[@]}" "/OUT:$(cygpath -w "$STAGE/Venetium.exe")" \
  /APPCONTAINER /SUBSYSTEM:WINDOWS /MACHINE:ARM /OPT:REF /OPT:ICF \
  /DYNAMICBASE /NXCOMPAT /MANIFEST:NO \
  WindowsApp.lib OneCoreUap.lib /DELAYLOAD:oleaut32.dll delayimp.lib
echo "    $(stat -c%s "$STAGE/Venetium.exe") bytes"

# --- the package's files -------------------------------------------------------
echo "=== stage ==="
cp "$APP/Package.appxmanifest" "$STAGE/AppxManifest.xml"
cp "$APP/Assets/"*.png "$STAGE/Assets/"

if [ -f "$ENGINE_OUT/venetium_engine.dll" ]; then
  for f in venetium_engine.dll venetium_engine.pak icudtl.dat snapshot_blob.bin \
           v8_context_snapshot.bin libEGL.dll libGLESv2.dll; do
    if [ -f "$ENGINE_OUT/$f" ]; then
      cp "$ENGINE_OUT/$f" "$STAGE/$f"
      echo "    $f ($(( $(stat -c%s "$STAGE/$f") / 1024 )) KB)"
    fi
  done
else
  echo "    WARNING: no engine in $ENGINE_OUT -- packaging the shell alone" >&2
fi

# resources.pri, from the manifest and the Assets only.
PRITMP="$OUT/pri"
rm -rf "$PRITMP" && mkdir -p "$PRITMP/Assets"
cp "$STAGE/AppxManifest.xml" "$PRITMP/" && cp "$STAGE/Assets/"* "$PRITMP/Assets/"
if "$BIN/makepri.exe" createconfig /cf "$(cygpath -w "$PRITMP/priconfig.xml")" /dq en-US /pv 10.0.0 /o > /dev/null 2>&1 &&
   "$BIN/makepri.exe" new /pr "$(cygpath -w "$PRITMP")" /cf "$(cygpath -w "$PRITMP/priconfig.xml")" \
     /mn "$(cygpath -w "$PRITMP/AppxManifest.xml")" /of "$(cygpath -w "$STAGE/resources.pri")" /o > /dev/null 2>&1; then
  echo "    resources.pri"
else
  echo "    WARNING: makepri failed, no resources.pri" >&2
fi
rm -rf "$PRITMP"

# Api-set forwarders for names an older phone's OS does not resolve
# (tools/gen-apiset-shims.py, from gecko-w10m).
python "$(cygpath -w "$ROOT/tools/gen-apiset-shims.py")" "$(cygpath -w "$STAGE")" \
  "$(cygpath -w "$OUT/shims")" "$(cygpath -w "$CL")" "$(cygpath -w "$VW_LLVM/bin/lld-link.exe")" \
  "$(cygpath -w "$SDK/Lib/$SDKV_LIB/um/arm")"

find "$STAGE" -name "*.pdb" -delete 2> /dev/null || true
echo "    staged $(find "$STAGE" -type f | wc -l) files, $(du -sm "$STAGE" | cut -f1) MB"

# --- pack and sign -------------------------------------------------------------
echo "=== pack ==="
rm -f "$PKG"
"$BIN/makeappx.exe" pack /d "$(cygpath -w "$STAGE")" /p "$(cygpath -w "$PKG")" /o /nv > /dev/null

echo "=== sign ==="
# The signer's subject must be the manifest's Publisher exactly. By default
# the certificate is gecko-w10m's (the same Publisher), so a phone that
# already trusts it needs nothing new; VW_PFX names another.
CERT="${VW_PFX:-$ROOT/../gecko-w10m/app/GeckoW10m/Gecko.pfx}"
PUBLISHER="$(tr -d '\r' < "$APP/Package.appxmanifest" | sed -n '/<Identity/,/\/>/{s/.*Publisher="\([^"]*\)".*/\1/p}' | head -1)"
powershell.exe -NoProfile -ExecutionPolicy Bypass \
  -File "$(cygpath -w "$ROOT/tools/sign-appx.ps1")" \
  -Package "$(cygpath -w "$PKG")" \
  -Certificate "$(cygpath -w "$CERT")" \
  -Subject "$PUBLISHER" \
  -SdkBin "$(cygpath -w "$BIN")"
CER="${CERT%.pfx}.cer"
[ -f "$CER" ] && cp "$CER" "$OUT/Venetium.cer"

echo
echo "PACKAGE: $PKG"
ls -la "$PKG" | awk '{printf "  %.1f MB\n", $5/1048576}'
