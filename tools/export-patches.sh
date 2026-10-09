#!/bin/bash
# Writes this port's changes to the Chromium tree as patches/w10m/*.patch.
#
# The tree ($VW_SRC) is a git repository holding only the files the patch
# series touches (tools/apply-patches.sh): tag "venetium" is Chromium + Supermium
# + Venetium, and every commit after it is either "track: <files>" -- an
# unchanged file added so the next commit can show a diff -- or a change of
# this port. Each change becomes one patch, numbered in order; track commits
# are skipped. A patch therefore applies to the tree the commits before it
# made, whether or not the files it touches were tracked at the time.
set -e
source "$(dirname "$0")/env.sh"
SRC="$(cygpath -m "$VW_SRC")"   # git.exe takes C:/..., not /c/... (MSYS2_ARG_CONV_EXCL)
DEST="$VW_ROOT/patches/w10m"
mkdir -p "$DEST"
rm -f "$DEST"/*.patch

n=0
for commit in $(git -C "$SRC" rev-list --reverse venetium..HEAD); do
  subject="$(git -C "$SRC" log -1 --format=%s "$commit")"
  case "$subject" in
    track:*) continue ;;
  esac
  n=$((n + 1))
  slug="$(echo "$subject" | tr 'A-Z' 'a-z' | sed 's/[^a-z0-9]\+/-/g; s/^-//; s/-$//' | cut -c1-60)"
  name="$(printf '%04d' "$n")-$slug.patch"
  # Full index lines, so git apply --3way can find the base blobs.
  git -C "$SRC" format-patch -1 --stdout --full-index --no-signature \
    --zero-commit "$commit" > "$DEST/$name"
  echo "  $name"
done
echo "$n patches in patches/w10m"
