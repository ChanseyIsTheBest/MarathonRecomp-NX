#!/usr/bin/env bash
# Cross-builds a minimal FFmpeg (avcodec + avutil, XMAFRAMES decoder only) for
# Nintendo Switch / devkitA64 and installs it into thirdparty/ffmpeg-core/switch/.
#
# The prebuilt .a files are NOT committed to the repo (see .gitignore); run this
# once, and again only when bumping the FFmpeg version. Driven automatically by
# tools/build-switch.sh, but can be run on its own.
set -euo pipefail

: "${DEVKITPRO:=/opt/devkitpro}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 8)}"
FFMPEG_TAG="${FFMPEG_TAG:-n7.1}"   # must match thirdparty/ffmpeg-core/include public headers

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work="${FFMPEG_WORKDIR:-$root_dir/build/ffmpeg-switch}"
dest="$root_dir/thirdparty/ffmpeg-core/switch"
patch="$root_dir/tools/switch/ffmpeg-xmaframes.patch"

export PATH="$DEVKITPRO/devkitA64/bin:$PATH"
command -v aarch64-none-elf-gcc >/dev/null || { echo "devkitA64 gcc not found; set DEVKITPRO." >&2; exit 2; }

echo "== FFmpeg $FFMPEG_TAG: fetch =="
if [ ! -d "$work/.git" ]; then
  rm -rf "$work"; mkdir -p "$(dirname "$work")"
  git clone --depth 1 --branch "$FFMPEG_TAG" https://github.com/FFmpeg/FFmpeg.git "$work"
fi
cd "$work"

echo "== FFmpeg: apply XMAFRAMES decoder patch =="
if git apply --check "$patch" 2>/dev/null; then
  git apply "$patch"
elif git apply --reverse --check "$patch" 2>/dev/null; then
  echo "  already applied"
else
  echo "  WARNING: patch neither applies nor is applied cleanly; continuing" >&2
fi

echo "== FFmpeg: configure (minimal avcodec+avutil, xmaframes only) =="
if [ ! -f ffbuild/config.mak ]; then
  ARCH="-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE -ffunction-sections -fdata-sections -D__SWITCH__"
  ./configure \
    --prefix="$work/install" \
    --enable-cross-compile --cross-prefix=aarch64-none-elf- \
    --arch=aarch64 --cpu=cortex-a57 --target-os=none \
    --enable-pic --enable-static --disable-shared \
    --disable-programs --disable-doc --disable-autodetect --disable-network --disable-debug \
    --disable-avdevice --disable-avfilter --disable-swscale --disable-swresample --disable-postproc --disable-avformat \
    --disable-everything --enable-decoder=xmaframes \
    --extra-cflags="$ARCH -I$DEVKITPRO/libnx/include -I$DEVKITPRO/portlibs/switch/include" \
    --extra-ldflags="-L$DEVKITPRO/libnx/lib -L$DEVKITPRO/portlibs/switch/lib -fPIE -specs=$DEVKITPRO/libnx/switch.specs" \
    --pkg-config=false
fi

echo "== FFmpeg: build + install =="
make -j"$JOBS"
make install

echo "== FFmpeg: stage into $dest =="
mkdir -p "$dest/lib" "$dest/include"
cp install/lib/libavcodec.a install/lib/libavutil.a "$dest/lib/"
cp -r install/include/* "$dest/include/"
echo "== Done: $(ls "$dest/lib") =="
