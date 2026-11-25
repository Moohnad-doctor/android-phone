#!/usr/bin/env bash
# The formatting is rough and I'm not going to bother. Pull requests that fix it are welcome; complaints without one help neither me nor the end user.
# Build the myth phone APK.
# Usage: tools/phone/apk.sh [debug|release]
set -euo pipefail

MODE="${1:-debug}"
case "$MODE" in
  debug|release) ;;
  *) echo "usage: $0 [debug|release]" >&2; exit 2 ;;
esac
TASK="assemble$(tr '[:lower:]' '[:upper:]' <<< "${MODE:0:1}")${MODE:1}"

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
PHONE="$ROOT/android-phone"
TOOLS="$HOME/stremiobrew-build/android"
KEYS="$TOOLS/keys"
PROPS="$KEYS/phone.properties"

# Prefer the runner's preinstalled SDK; retain the historical local-build fallback.
export ANDROID_HOME="${ANDROID_HOME:-$TOOLS/sdk}"
export ANDROID_SDK_ROOT="$ANDROID_HOME"
NDK="${NDK:-${ANDROID_NDK_ROOT:-$ANDROID_HOME/ndk/27.2.12479018}}"
export NDK ANDROID_NDK_HOME="$NDK" ANDROID_NDK_ROOT="$NDK"
GRADLE="$TOOLS/gradle-8.11.1/bin/gradle"

[ -d "$ANDROID_HOME" ] || { echo "error: Android SDK not found at $ANDROID_HOME" >&2; exit 1; }
[ -d "$NDK" ] || { echo "error: Android NDK not found at $NDK" >&2; exit 1; }
[ -x "$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/clang" ] || { echo "error: Linux Android NDK toolchain not found under $NDK" >&2; exit 1; }
[ -x "$GRADLE" ] || { echo "error: Gradle not found at $GRADLE" >&2; exit 1; }

# Gradle 8.11 / AGP 8.7 require a supported JDK; CI pins Temurin 17.
jdk_major() {
  "$1/bin/java" -version 2>&1 | sed -n 's/.*version "\([0-9]*\).*/\1/p' | head -1
}
JDK=""
for c in \
  "${JAVA_HOME:-}" \
  "$(command -v java >/dev/null 2>&1 && dirname "$(dirname "$(readlink -f "$(command -v java)")")" || true)" \
  "$(brew --prefix openjdk 2>/dev/null || true)/libexec/openjdk.jdk/Contents/Home" \
  "$(brew --prefix openjdk@17 2>/dev/null || true)/libexec/openjdk.jdk/Contents/Home" \
  "$(/usr/libexec/java_home -v 23 2>/dev/null || true)" \
  "$(/usr/libexec/java_home -v 17 2>/dev/null || true)"; do
  [ -n "$c" ] && [ -x "$c/bin/java" ] || continue
  major="$(jdk_major "$c")"
  if [ "$major" -ge 17 ] && [ "$major" -le 23 ]; then JDK="$c"; break; fi
done
[ -n "$JDK" ] || { echo "error: no supported JDK found (install or set JAVA_HOME)" >&2; exit 1; }
export JAVA_HOME="$JDK"
echo "JDK: $JAVA_HOME"

# Local SDK/NDK locations for Gradle (generated per runner; never committed).
printf 'sdk.dir=%s\nndk.dir=%s\n' "$ANDROID_HOME" "$NDK" > "$PHONE/local.properties"

# Native libraries: one set per ABI, extracted by Gradle (useLegacyPackaging).
JNI="$PHONE/app/src/main/jniLibs"
ABI_LIST="${TARGET_ABIS:-arm64-v8a,armeabi-v7a}"
IFS=',' read -r -a ABIS <<< "$ABI_LIST"
for ABI in "${ABIS[@]}"; do
  mkdir -p "$JNI/$ABI"
  rm -f "$JNI/$ABI"/*.so
  B="$ROOT/build/phone-$ABI"
  for f in "$B/deps/lib/libSDL2.so" "$B/deps/lib/libcurl.so" "$B/libmain.so" "$B/libsbengine.so" "$B/libsbdl.so"; do
    if [ ! -s "$f" ]; then
      echo "error: required native library is missing: $f" >&2
      exit 1
    fi
    cp "$f" "$JNI/$ABI/"
  done
  # the seek-preview decoder (tools/phone/build-prev.sh): only when asked (SB_PREVIEW_LIB=1), shipping it is the owner's call
  if [ "${SB_PREVIEW_LIB:-0}" = 1 ]; then PL="$ROOT/build/phone-prev/$ABI/libsbprev.so"; if [ -f "$PL" ]; then cp "$PL" "$JNI/$ABI/"; else echo "note: no $PL (run tools/phone/build-prev.sh)" >&2; fi; fi
done

# Release keystore, created once.
if [ "$MODE" = release ] && [ ! -f "$PROPS" ]; then
  mkdir -p "$KEYS"
  STORE="$KEYS/phone.jks"
  PASS="$(openssl rand -hex 12)"
  "$JDK/bin/keytool" -genkeypair -keystore "$STORE" -storepass "$PASS" -keypass "$PASS" \
    -alias phone -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Stremio Origin"
  ( umask 077
    printf 'storeFile=%s\nstorePassword=%s\nkeyAlias=phone\nkeyPassword=%s\n' \
      "$STORE" "$PASS" "$PASS" > "$PROPS" )
  chmod 600 "$PROPS"
  echo "created keystore: $STORE"
fi

"$GRADLE" -p "$PHONE" "$TASK" -PtargetAbis="$ABI_LIST" --console=plain

VERSION="$(sed -n "s/.*versionName '\([^']*\)'.*/\1/p" "$PHONE/app/build.gradle" | head -1)"
OUTDIR="$PHONE/app/build/outputs/apk/$MODE"
# one APK per processor: 64-bit phones take arm64-v8a, older ones armeabi-v7a
GITREV=$(git -C "$ROOT" rev-parse --short=10 HEAD 2>/dev/null || echo unknown)$(git -C "$ROOT" diff --quiet HEAD -- src 2>/dev/null || echo "+$(git -C "$ROOT" diff HEAD -- src | shasum | cut -c1-6)")
for ABI in "${ABIS[@]}"; do
  D="$ROOT/dist/dbg/$GITREV/$ABI"
  mkdir -p "$D"
  for f in libmain.dbg.so libsbengine.dbg.so; do
    [ -f "$ROOT/build/phone-$ABI/$f" ] && cp "$ROOT/build/phone-$ABI/$f" "$D/"
  done
done
[ "$MODE" = release ] && { R="$ROOT/dist/dbg/release-$VERSION-$GITREV"; mkdir -p "$R"; cp -R "$ROOT/dist/dbg/$GITREV/." "$R/"; }
ls -dt "$ROOT"/dist/dbg/*/ 2>/dev/null | grep -v '/release-' | tail -n +6 | while read -r d; do rm -rf "${d:?}"; done
SUF="-$MODE"; [ "$MODE" = release ] && SUF=""
mkdir -p "$ROOT/dist"
if [ -z "$SUF" ]; then
  rm -f "$ROOT/dist/myth-"*"-arm64-v8a.apk" "$ROOT/dist/myth-"*"-armeabi-v7a.apk"
else
  rm -f "$ROOT/dist/myth-"*"$SUF.apk"
fi
n=0
for ABI in "${ABIS[@]}"; do
  for f in "$OUTDIR/app-$ABI-$MODE.apk" "$OUTDIR/app-$ABI-$MODE-unsigned.apk"; do
    [ -f "$f" ] || continue
    DEST="$ROOT/dist/myth-$VERSION-$ABI$SUF.apk"
    cp "$f" "$DEST"
    n=$((n + 1))
    echo "APK: $DEST ($(ls -lh "$DEST" | awk '{print $5}'))"
    break
  done
done
[ "$n" -gt 0 ] || { echo "error: no APK found in $OUTDIR" >&2; exit 1; }
