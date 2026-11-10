#!/bin/sh
# build-from-source.sh — build the VST3-renku SDK, the VST3 hosts, and the example
# plug-ins, and install the hosts + example plug-ins. vst3host runs on the Media
# Kit; vst3jackhost is built too if jack (libjack) is installed.
set -e

HERE=$(cd "$(dirname "$0")" && pwd)

pkgman install -y cmake ninja gcc make pkgconfig
export PKG_CONFIG_PATH="/boot/system/non-packaged/lib/pkgconfig:/boot/system/lib/pkgconfig:$PKG_CONFIG_PATH"

cd "$HERE"
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build

# Install the host and the example plug-in bundles into the non-packaged tree.
mkdir -p /boot/system/non-packaged/bin /boot/system/non-packaged/add-ons/media/VST3
HOST=$(find build -type f -name vst3host | head -1)
[ -n "$HOST" ] && cp "$HOST" /boot/system/non-packaged/bin/vst3host
JACKHOST=$(find build -type f -name vst3jackhost | head -1)
[ -n "$JACKHOST" ] && cp "$JACKHOST" /boot/system/non-packaged/bin/vst3jackhost
find build -type d -name '*.vst3' -exec cp -r {} /boot/system/non-packaged/add-ons/media/VST3/ \;

echo ">> Installed vst3host${JACKHOST:+, vst3jackhost} and example plug-ins under /boot/system/non-packaged."
echo ">> The SDK static libs remain in build/lib/Release for building JackDAW/plug-ins."
