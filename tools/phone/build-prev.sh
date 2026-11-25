#!/usr/bin/env bash
# Optional phone seek-preview decoder build (FFmpeg H.264/HEVC); not part of the fast arm64 APK CI path.
# Usage: tools/phone/build-prev.sh [arm64-v8a|armeabi-v7a]...
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
W="$HOME/stremiobrew-build/ffmpeg"
VERSION=7.1.1
SRC="$W/ffmpeg-$VERSION"
NDK_ROOT="${NDK:-${ANDROID_NDK_ROOT:-$HOME/stremiobrew-build/android/sdk/ndk/27.2.12479018}}"
NDK="$NDK_ROOT/toolchains/llvm/prebuilt/linux-x86_64/bin"
[ -f "$SRC/configure" ] || { echo "no FFmpeg source at $SRC" >&2; exit 1; }
[ -x "$NDK/llvm-ar" ] && [ -x "$NDK/llvm-strip" ] || { echo "Linux NDK tools not found at $NDK" >&2; exit 1; }
ABIS=("$@")
[ "${#ABIS[@]}" -gt 0 ] || ABIS=(arm64-v8a armeabi-v7a)
for ABI in "${ABIS[@]}"; do
  case "$ABI" in
    arm64-v8a) TC="$W/tc-atv-arm64"; ARCH=aarch64 ;;
    armeabi-v7a) TC="$W/tc-atv-armv7"; ARCH=arm ;;
    *) echo "unknown ABI: $ABI" >&2; exit 1 ;;
  esac
  B="$W/build-phone-prev2-$ABI"
  P="$W/prefix-phone-prev2-$ABI"
  mkdir -p "$B"
  XC="-O2 -fPIC -ffunction-sections -fdata-sections -ffile-prefix-map=$HOME=/src -ffile-prefix-map=$W=/build"
  if [ ! -f "$P/lib/libavcodec.a" ]; then
    (
      cd "$B"
      "$SRC/configure" --prefix="$P" --disable-autodetect --disable-shared --enable-static --enable-pic \
        --disable-programs --disable-doc --disable-debug --disable-network --disable-avdevice --disable-avfilter --disable-swscale --disable-postproc \
        --enable-avcodec --enable-avformat --enable-avutil --enable-swresample --disable-iconv --disable-zlib \
        --disable-everything --enable-decoder=h264,hevc \
        --enable-cross-compile --arch="$ARCH" --target-os=android --cc="$TC/cc" --ar="$NDK/llvm-ar" --ranlib="$NDK/llvm-ranlib" --nm="$NDK/llvm-nm" --strip="$NDK/llvm-strip" \
        --extra-cflags="$XC" >/dev/null
      make -j8 >/dev/null
      make install >/dev/null
    )
  fi
  OUT="$ROOT/build/phone-prev/$ABI"
  mkdir -p "$OUT"
  echo '{ global: sbd_prev_get; local: *; };' > "$OUT/exports.map"
  "$TC/cc" -std=c11 -O2 -fPIC -shared -fvisibility=hidden -ffunction-sections -fdata-sections -D_GNU_SOURCE -DSBD_PREVIEW -I"$P/include" -I"$ROOT/src" \
    -ffile-prefix-map="$HOME"=/src -ffile-prefix-map="$ROOT"=/app "$ROOT/tools/dts/sbdts.c" \
    -L"$P/lib" -lavformat -lavcodec -lswresample -lavutil -lm \
    -Wl,--gc-sections -Wl,--version-script="$OUT/exports.map" -Wl,-z,max-page-size=16384 -Wl,--build-id=none -o "$OUT/libsbprev.so"
  "$NDK/llvm-strip" --strip-all "$OUT/libsbprev.so"
  echo "$ABI: $(stat -c %s "$OUT/libsbprev.so") bytes -> build/phone-prev/$ABI/libsbprev.so"
done
