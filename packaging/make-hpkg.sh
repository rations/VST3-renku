#!/bin/sh
# make-hpkg.sh — build the VST3-renku packages on a running RenkuOS (x86_64):
#   vst3_renku       vst3host (Media Kit; JACK through its add-on) + the SDK example
#                    plug-ins; does not require jack
#   vst3_renku_jack  vst3jackhost (JACK); only if jack (libjack) is installed
#
# NAMku/DRUMku are packaged separately, so this build excludes them.
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
VERSION=0.4.0
REVISION=2
STAGE="$HERE/stage"
STAGE_JACK="$HERE/stage-jack"

pkgman install -y cmake ninja gcc make pkgconfig libsamplerate_devel || true
export PKG_CONFIG_PATH="/boot/system/non-packaged/lib/pkgconfig:/boot/system/lib/pkgconfig:$PKG_CONFIG_PATH"

cd "$ROOT"
rm -rf build "$STAGE" "$STAGE_JACK"
# NAMKU_DIR pointed away so the sibling NAMku is not pulled into this package.
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DNAMKU_DIR=/nonexistent
ninja -C build

# vst3_renku
mkdir -p "$STAGE/bin" "$STAGE/add-ons/media/VST3"
HOST=$(find build -type f -name vst3host | head -1)
[ -n "$HOST" ] || { echo "!! vst3host not built" >&2; exit 1; }
cp "$HOST" "$STAGE/bin/vst3host"
# vst3host --backend jack loads this; without the jack package it reports that.
if [ -f build/bin/Release/backends/jack.so ]; then
	mkdir -p "$STAGE/add-ons/vst3host"
	cp build/bin/Release/backends/jack.so "$STAGE/add-ons/vst3host/jack.so"
fi

# Copy every built .vst3 bundle except the ones with their own packages.
find build -type d -name '*.vst3' | while read -r b; do
	case "$(basename "$b")" in
		NAMku.vst3|DRUMku.vst3) continue ;;
	esac
	cp -r "$b" "$STAGE/add-ons/media/VST3/"
done

cp "$HERE/vst3_renku.PackageInfo" "$STAGE/.PackageInfo"
OUT="$HERE/vst3_renku-$VERSION-$REVISION-x86_64.hpkg"
package create -C "$STAGE" "$OUT"
echo ">> built $OUT"

# vst3_renku_jack
JACKHOST=$(find build -type f -name vst3jackhost | head -1)
if [ -z "$JACKHOST" ]; then
	echo ">> vst3jackhost not built (jack not installed): no vst3_renku_jack package"
	exit 0
fi
mkdir -p "$STAGE_JACK/bin"
cp "$JACKHOST" "$STAGE_JACK/bin/vst3jackhost"
cp "$HERE/vst3_renku_jack.PackageInfo" "$STAGE_JACK/.PackageInfo"
OUT="$HERE/vst3_renku_jack-$VERSION-$REVISION-x86_64.hpkg"
package create -C "$STAGE_JACK" "$OUT"
echo ">> built $OUT"
