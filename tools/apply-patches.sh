#!/bin/bash
# Applies the patch series to a Chromium 150.0.7871.222 tree unpacked from
# Google's tarball (tools/fetch-source.sh), and records each stage in a git
# repository of the files it touches, for tools/export-patches.sh:
#
#   pristine   Chromium as unpacked (only the touched files are tracked)
#   supermium  + Supermium 150's patches (win32ss/supermium main, 2026-09-30),
#              in the order its README gives
#   venetium   + Venetium's ARM32 Windows patches (../venetium/patches, the
#              subset in patches/series), its superseded 0024, and 0014 redone
#              for Supermium's 09-30 typo
#   (HEAD)     + this port's patches/w10m/*.patch
#
# Needs ../venetium beside this repository and the Supermium patches in
# $VW_SUPERMIUM (default C:/vw/dl/supermium; tools/fetch-source.sh gets them).
set -e
source "$(dirname "$0")/env.sh"
SRC="$(cygpath -u "$VW_SRC")"
VENETIUM="${VW_VENETIUM:-$VW_ROOT/../venetium}"
SUPERMIUM="$(cygpath -u "${VW_SUPERMIUM:-C:/vw/dl/supermium}")"
SERIES="$VW_ROOT/patches/series"

cd "$SRC"
if [ -d .git ] && git rev-parse -q --verify venetium > /dev/null; then
  echo "the tree already has the venetium stage; applying patches/w10m only"
else
  git init -q
  git config core.autocrlf false
  git config core.longpaths true
  git config advice.addIgnoredFile false
  echo '*' > .git/info/exclude
  echo '* -text' > .git/info/attributes

  # Every file any patch of the series touches, as it is now.
  touched="$(mktemp)"
  {
    grep -v '^#' "$SERIES" | while read -r kind name; do
      case "$kind" in
        supermium) cat "$SUPERMIUM/$name.patch" ;;
        venetium) cat "$VENETIUM/patches/$name" ;;
      esac
    done
    cat "$VW_ROOT"/patches/w10m/*.patch
  } | grep -E '^(diff --git a/|--- a/)' |
      sed -E 's#^diff --git a/([^ ]+) b/.*#\1#; s#^--- a/##' | sed 's/\t.*//' |
      sort -u | while read -r f; do [ -f "$f" ] && echo "$f"; done > "$touched"
  git add -f --pathspec-from-file="$touched"
  git -c user.name=vw -c user.email=vw@localhost commit -q -m "pristine: Chromium $(sed -n 's/^MAJOR=//p' chrome/VERSION).0.$(sed -n 's/^BUILD=//p' chrome/VERSION).$(sed -n 's/^PATCH=//p' chrome/VERSION)"
  git tag pristine

  stage=""
  while read -r kind name; do
    case "$kind" in
      ''|'#'*) continue ;;
      supermium) patch="$SUPERMIUM/$name.patch" ;;
      venetium) patch="$VENETIUM/patches/$name" ;;
      fix) continue ;;
      *) echo "unknown series entry: $kind $name" >&2; exit 1 ;;
    esac
    if [ "$stage" != "$kind" ] && [ -n "$stage" ]; then
      git -c user.name=vw -c user.email=vw@localhost commit -q -m "$stage patches"
      git tag "$stage"
    fi
    stage="$kind"
    git apply --index --whitespace=nowarn "$patch"
    echo "  $kind $name"
  done < <(sed 's/\r$//' "$SERIES")

  # Venetium 0014, redone: Supermium's 09-30 set calls ::CryptUnrotectMemory.
  sed -i 's/::CryptUnrotectMemory(buffer.data(), buffer.size(),/::CryptUnprotectMemory(buffer.data(), buffer.size(),/' \
    crypto/process_bound_string.cc
  git add -f crypto/process_bound_string.cc
  git -c user.name=vw -c user.email=vw@localhost commit -q -m "venetium patches"
  git tag venetium
fi

for p in "$VW_ROOT"/patches/w10m/*.patch; do
  [ -f "$p" ] || continue
  git -c user.name=vw -c user.email=vw@localhost am -q --3way --keep-cr "$p"
  echo "  w10m $(basename "$p")"
done
echo "tree at $(git log --oneline -1)"
