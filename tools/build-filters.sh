#!/bin/bash
# The built-in ad blocker's list (Venetium 0059): EasyList and EasyPrivacy
# (https://easylist.to/, GPLv3 / CC BY-SA 3.0) converted by Chromium's own
# ruleset_converter into an unindexed subresource_filter ruleset, in
# $VW_OUT/venetium_filters beside a version.txt (the lists' versions: a new one
# makes the browser index the list again, once) and Venetium's LICENSE.txt.
# tools/build-appx.sh puts the folder next to Venetium.exe, where the browser
# looks for it; without it nothing is blocked.
#
#   bash tools/build-filters.sh            downloads the lists, then converts
#   VW_FILTERS_OFFLINE=1 bash ...          converts the lists downloaded before
set -e
source "$(dirname "$0")/env.sh"
SRC="$(cygpath -u "$VW_SRC")"
OUT="$SRC/$VW_OUT"
LISTS="$(cygpath -u "${VW_DL:-C:/vw/dl}")/filters"
DEST="$OUT/venetium_filters"
VENETIUM="${VW_VENETIUM:-$VW_ROOT/../venetium}"
CONVERTER="$OUT/win_clang_x64/ruleset_converter.exe"

if [ ! -x "$CONVERTER" ]; then
  echo "=== build the converter (host tool)"
  bash "$VW_ROOT/tools/ninja.sh" vw_host_tools
fi

mkdir -p "$LISTS" "$DEST"
if [ -z "$VW_FILTERS_OFFLINE" ]; then
  for list in easylist easyprivacy; do
    echo "=== download $list.txt"
    curl -fsSL --retry 3 -o "$(cygpath -w "$LISTS/$list.txt.new")" "https://easylist.to/easylist/$list.txt"
    mv -f "$LISTS/$list.txt.new" "$LISTS/$list.txt"
  done
fi

version_of() {
  sed -n 's/^! Version: *//p' "$LISTS/$1.txt" | head -1 | tr -d '\r'
}
VERSION="easylist-$(version_of easylist).easyprivacy-$(version_of easyprivacy)"

echo "=== convert ($VERSION)"
"$CONVERTER" --input_format=filter-list --output_format=unindexed-ruleset \
  --input_files="$(cygpath -w "$LISTS/easylist.txt"),$(cygpath -w "$LISTS/easyprivacy.txt")" \
  --output_file="$(cygpath -w "$DEST/filters.dat")"
printf '%s\n' "$VERSION" > "$DEST/version.txt"
cp "$VENETIUM/package/venetium_filters/LICENSE.txt" "$DEST/LICENSE.txt"
echo "    $DEST: filters.dat $(( $(stat -c%s "$DEST/filters.dat") / 1024 )) KB, version $VERSION"
