#!/usr/bin/env bash
# Cross-builds a minimal FFmpeg (avcodec + avutil, XMAFRAMES decoder only) for
# Nintendo Switch / devkitA64 and installs it into thirdparty/ffmpeg-core/switch/.
#
# The prebuilt .a files are NOT committed to the repo (see .gitignore); run this
# once, and again only when bumping the FFmpeg version. Driven automatically by
# tools/build-switch.sh, but can be run on its own.
set -euo pipefail

# devkitPro. Git Bash exports DEVKITPRO=/opt/devkitpro even where that folder does not exist.
if [ -z "${DEVKITPRO:-}" ] || [ ! -d "$DEVKITPRO" ]; then
  for devkitpro_candidate in /opt/devkitpro /c/devkitPro; do
    if [ -d "$devkitpro_candidate" ]; then DEVKITPRO="$devkitpro_candidate"; break; fi
  done
fi
: "${DEVKITPRO:=/opt/devkitpro}"
# The shell form goes into PATH; the native compiler gets the Windows form (C:/devkitPro), in its flags and
# in the DEVKITPRO variable libnx's switch.specs reads at link time (configure links test programs).
DEVKITPRO_SHELL="$DEVKITPRO"
DEVKITPRO_NATIVE="$DEVKITPRO"
if command -v cygpath >/dev/null 2>&1; then
  DEVKITPRO_SHELL="$(cygpath -u "$DEVKITPRO")"
  DEVKITPRO_NATIVE="$(cygpath -m "$DEVKITPRO")"
fi
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 8)}"
FFMPEG_TAG="${FFMPEG_TAG:-n7.1}"   # must match thirdparty/ffmpeg-core/include public headers
# Host compiler for FFmpeg's build helpers: MSYS2 CLANGARM64 on an arm64 PC, CLANG64 on an x86_64 one, from
# C:/msys64 or devkitPro's C:/devkitPro/msys2. tools/build-switch.sh passes its own choice.
if [ -z "${CLANGARM64:-}" ]; then
  host_is_arm64=false
  case "$(uname -m 2>/dev/null) ${PROCESSOR_ARCHITECTURE:-} ${PROCESSOR_IDENTIFIER:-}" in
    *aarch64*|*arm64*|*ARM64*|*ARMv8*) host_is_arm64=true ;;
  esac
  host_cc_dirs=(/c/devkitPro/msys2/clang64/bin /c/msys64/clang64/bin)
  if $host_is_arm64; then
    host_cc_dirs=(/c/msys64/clangarm64/bin /c/devkitPro/msys2/clangarm64/bin "${host_cc_dirs[@]}")
  fi
  CLANGARM64="${host_cc_dirs[0]}"
  for host_cc_dir in "${host_cc_dirs[@]}"; do
    if [ -x "$host_cc_dir/clang.exe" ]; then CLANGARM64="$host_cc_dir"; break; fi
  done
fi
HOST_CC="${HOST_CC:-$CLANGARM64/clang.exe}"

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work="${FFMPEG_WORKDIR:-$root_dir/build/ffmpeg-switch}"
dest="$root_dir/thirdparty/ffmpeg-core/switch"
patch="$root_dir/tools/switch/ffmpeg-xmaframes.patch"

export PATH="$CLANGARM64:$DEVKITPRO_SHELL/devkitA64/bin:$PATH"
export DEVKITPRO="$DEVKITPRO_NATIVE"
command -v aarch64-none-elf-gcc >/dev/null || { echo "devkitA64 gcc not found; set DEVKITPRO." >&2; exit 2; }
[ -x "$HOST_CC" ] || { echo "Host clang not found: $HOST_CC (set CLANGARM64 or HOST_CC)." >&2; exit 2; }

# A temporary folder both configure (POSIX path) and the native devkitA64 compiler (Windows path) can use.
# Under a shell whose msys runtime differs from make's (Git for Windows bash driving devkitPro's make),
# TMP/TEMP do not reach the recipes and GCC falls back to C:\Windows. They are passed to make explicitly.
ffmpeg_tmp="$root_dir/build/tmp"
mkdir -p "$ffmpeg_tmp"
export TMPDIR="$ffmpeg_tmp"
make_tmp_args=()
if command -v cygpath >/dev/null 2>&1; then
  ffmpeg_tmp_win="$(cygpath -m "$ffmpeg_tmp")"
  make_tmp_args=(TMP="$ffmpeg_tmp_win" TEMP="$ffmpeg_tmp_win")
fi

echo "== FFmpeg $FFMPEG_TAG: fetch =="
if [ ! -d "$work/.git" ]; then
  rm -rf "$work"; mkdir -p "$(dirname "$work")"
  git clone --depth 1 --branch "$FFMPEG_TAG" https://github.com/FFmpeg/FFmpeg.git "$work"
fi
cd "$work"

echo "== FFmpeg: apply XMAFRAMES decoder patch =="
if git apply --check --ignore-space-change --whitespace=nowarn "$patch" 2>/dev/null; then
  git apply --ignore-space-change --whitespace=nowarn "$patch"
elif git apply --reverse --check --ignore-space-change --whitespace=nowarn "$patch" 2>/dev/null; then
  echo "  already applied"
else
  echo "  ERROR: FFmpeg patch neither applies nor is already applied" >&2
  exit 2
fi

echo "== FFmpeg: configure (minimal avcodec+avutil, xmaframes only) =="
if [ ! -f ffbuild/config.mak ]; then
  ARCH="-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE -ffunction-sections -fdata-sections -D__SWITCH__"
  ./configure \
    --prefix="$work/install" \
    --enable-cross-compile --cross-prefix=aarch64-none-elf- \
    --host-cc="$HOST_CC" \
    --arch=aarch64 --cpu=cortex-a57 --target-os=none \
    --enable-pic --enable-static --disable-shared \
    --disable-programs --disable-doc --disable-autodetect --disable-network --disable-debug \
    --disable-avdevice --disable-avfilter --disable-swscale --disable-swresample --disable-postproc --disable-avformat \
    --disable-everything --enable-decoder=xmaframes \
    --extra-cflags="$ARCH -I$DEVKITPRO_NATIVE/libnx/include -I$DEVKITPRO_NATIVE/portlibs/switch/include" \
    --extra-ldflags="-L$DEVKITPRO_NATIVE/libnx/lib -L$DEVKITPRO_NATIVE/portlibs/switch/lib -fPIE -specs=$DEVKITPRO_NATIVE/libnx/switch.specs" \
    --pkg-config=false
fi

echo "== FFmpeg: build + install =="
make -j"$JOBS" "${make_tmp_args[@]}"
make install "${make_tmp_args[@]}"

echo "== FFmpeg: stage into $dest =="
# Only files whose content changed are copied: 'make install' rewrites every file, and new timestamps on
# the libraries relinked the app (a whole LTO link with SWITCH_LTO=1) and on the headers recompiled their users.
stage_file() { # $1 source, $2 destination
  if ! cmp -s "$1" "$2"; then
    mkdir -p "$(dirname "$2")"
    cp -f "$1" "$2"
    echo "  staged ${2#"$dest/"}"
  fi
}
stage_file install/lib/libavcodec.a "$dest/lib/libavcodec.a"
stage_file install/lib/libavutil.a "$dest/lib/libavutil.a"
while IFS= read -r -d '' header; do
  stage_file "$header" "$dest/include/${header#install/include/}"
done < <(find install/include -type f -print0)
echo "== Done: $(ls "$dest/lib") =="
