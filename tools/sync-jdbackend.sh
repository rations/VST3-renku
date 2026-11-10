#!/bin/sh
# sync-jdbackend.sh - copy JackDAW's audio backends into haiku/jdbackend/.
#
# vst3host (and NAMku's standalone app) run on the same audio backends as
# JackDAW (https://github.com/rations/jackdaw-renku): its Media Kit node, its
# JACK add-on and the loader that finds the add-on. The files are copied byte
# for byte and never edited here: fix them in JackDAW, then run this again.
#
# usage: tools/sync-jdbackend.sh [path to a jackdaw-renku checkout]
#        (default: ../jackDAW-haiku next to this repo)
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC=${1:-"$ROOT/../jackDAW-haiku"}
DEST="$ROOT/haiku/jdbackend"

[ -f "$SRC/src/backend/mediakit/mediakit_backend.cpp" ] ||
	{ echo "!! no JackDAW checkout at $SRC" >&2; exit 1; }

COMMIT=$(git -C "$SRC" rev-parse HEAD)
DIRTY=$(git -C "$SRC" status --porcelain -- src/backend/mediakit src/backend/jack \
	src/engine/audio_backend.h src/engine/audio_backend.c \
	src/engine/rt_ringbuffer.h src/engine/rt_ringbuffer.c)
[ -z "$DIRTY" ] || { echo "!! uncommitted backend changes in $SRC; commit them first" >&2; exit 1; }

mkdir -p "$DEST/backend/mediakit" "$DEST/backend/jack" "$DEST/engine"
cp "$SRC/src/backend/mediakit/mediakit_backend.cpp" "$DEST/backend/mediakit/"
cp "$SRC/src/backend/jack/jack_backend.c" "$DEST/backend/jack/"
cp "$SRC/src/engine/audio_backend.h" "$SRC/src/engine/audio_backend.c" \
	"$SRC/src/engine/rt_ringbuffer.h" "$SRC/src/engine/rt_ringbuffer.c" "$DEST/engine/"
cp "$SRC/LICENSE" "$DEST/LICENSE"

cat > "$DEST/PROVENANCE" <<EOT
These files are copied unchanged from JackDAW (jackdaw-renku), MIT licence
(see LICENSE here), by tools/sync-jdbackend.sh. Do not edit them in this
repository: change them in JackDAW and sync again.

Source:  https://github.com/rations/jackdaw-renku
Commit:  $COMMIT
Files:   src/backend/mediakit/mediakit_backend.cpp -> backend/mediakit/
         src/backend/jack/jack_backend.c           -> backend/jack/
         src/engine/audio_backend.h                -> engine/
         src/engine/audio_backend.c                -> engine/
         src/engine/rt_ringbuffer.h                -> engine/
         src/engine/rt_ringbuffer.c                -> engine/
EOT
echo ">> synced haiku/jdbackend from $SRC at $COMMIT"
