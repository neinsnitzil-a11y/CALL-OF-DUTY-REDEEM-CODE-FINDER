#!/usr/bin/env bash
# Build the Android APK of COD Redemption Hunter (C++ / Qt 6 for Android) on Linux (Debian/Ubuntu/Kali) or WSL.
# Installs everything it needs into ~/.cod-hunter-android (no sudo; only python3 must already be there):
#   JDK 17, Android SDK command-line tools, platform 34, build-tools 34, NDK 26.1, Qt 6.8.3 for Android + desktop Qt (host tools).
# Output: dist/COD_Redemption_Hunter-<version>-arm64.apk
#
#   ./build_android.sh            debug-signed APK (installs on any phone with "unknown sources" allowed)
#   ./build_android.sh --release  release APK, signed with your keystore:
#        ANDROID_KEYSTORE=/path/my.jks ANDROID_KEY_ALIAS=me ANDROID_KEYSTORE_PASS=... ./build_android.sh --release
set -euo pipefail
cd "${HUNTER_ROOT:-$(dirname "$0")}"
ROOT="$PWD"
# Tools and the build live in your Linux home, not next to the source: under WSL, /mnt/c is very slow.
TOOLS="${HUNTER_ANDROID_TOOLS:-$HOME/.cod-hunter-android}"
BUILD="$TOOLS/build"
QT_VER=6.8.3
NDK_VER=26.1.10909125
SDK_PLATFORM=android-34
BUILD_TOOLS=34.0.0
CMDLINE_ZIP=commandlinetools-linux-11076708_latest.zip
NINJA_VER=1.12.1
CMAKE_VER=3.30.5
RELEASE=0
[ "${1:-}" = "--release" ] && RELEASE=1
mkdir -p "$TOOLS/bin"
export PATH="$TOOLS/bin:$PATH"
say() { printf '\n\033[1;36m== %s\033[0m\n' "$*"; }

# ── 1. host tools, no sudo needed ─────────────────────────────────
command -v python3 >/dev/null || { echo "python3 is missing. Install it once with:  sudo apt install -y python3"; exit 1; }
fetch() {  # fetch URL FILE — curl if there is one, else Python
  if command -v curl >/dev/null; then curl -fL --retry 3 -o "$2" "$1"
  else python3 -c 'import sys,urllib.request; urllib.request.urlretrieve(sys.argv[1], sys.argv[2])' "$1" "$2"; fi
}
unzip_to() {  # unzip_to ZIP DIR — keeps the executable bits
  python3 - "$1" "$2" <<'PY'
import sys, zipfile, os
z = zipfile.ZipFile(sys.argv[1])
for info in z.infolist():
    path = z.extract(info, sys.argv[2])
    mode = (info.external_attr >> 16) & 0o777
    if mode: os.chmod(path, mode)
PY
}
if ! command -v ninja >/dev/null; then
  say "Downloading ninja $NINJA_VER"
  arch=$(uname -m); zipn=ninja-linux.zip; [ "$arch" = "aarch64" ] && zipn=ninja-linux-aarch64.zip
  fetch "https://github.com/ninja-build/ninja/releases/download/v$NINJA_VER/$zipn" "$TOOLS/ninja.zip"
  unzip_to "$TOOLS/ninja.zip" "$TOOLS/bin" && chmod +x "$TOOLS/bin/ninja" && rm "$TOOLS/ninja.zip"
fi
cmake_ok() { command -v cmake >/dev/null && cmake --version | head -1 | awk '{split($3,v,"."); exit !(v[1]>3 || (v[1]==3 && v[2]>=21))}'; }
if ! cmake_ok; then
  say "Downloading CMake $CMAKE_VER"
  arch=$(uname -m); [ "$arch" = "aarch64" ] || arch=x86_64
  fetch "https://github.com/Kitware/CMake/releases/download/v$CMAKE_VER/cmake-$CMAKE_VER-linux-$arch.tar.gz" "$TOOLS/cmake.tgz"
  rm -rf "$TOOLS/cmake" && mkdir -p "$TOOLS/cmake" && tar -xzf "$TOOLS/cmake.tgz" -C "$TOOLS/cmake" --strip-components=1 && rm "$TOOLS/cmake.tgz"
  ln -sf "$TOOLS/cmake/bin/cmake" "$TOOLS/bin/cmake"
fi
# aqtinstall (downloads Qt) through a standalone pip — no venv or system pip needed
AQT() { PYTHONPATH="$TOOLS/pylib" python3 -m aqt "$@"; }
if ! PYTHONPATH="$TOOLS/pylib" python3 -c 'import aqt' 2>/dev/null; then
  say "Installing aqtinstall"
  fetch https://bootstrap.pypa.io/pip/pip.pyz "$TOOLS/pip.pyz"
  python3 "$TOOLS/pip.pyz" install -q --break-system-packages --target "$TOOLS/pylib" aqtinstall
fi
echo "ninja $(ninja --version) · $(cmake --version | head -1)"

# ── 2. JDK 17 (Gradle for Qt 6.8 needs 17) ────────────────────────
java_major() { "$1" -version 2>&1 | sed -n 's/.*version "\([0-9]*\).*/\1/p' | head -1; }
if [ -n "${JAVA_HOME:-}" ] && [ "$(java_major "$JAVA_HOME/bin/java")" = "17" ]; then :
elif command -v java >/dev/null && [ "$(java_major java)" = "17" ]; then JAVA_HOME="$(dirname "$(dirname "$(readlink -f "$(command -v java)")")")"
elif [ -x "$TOOLS/jdk/bin/java" ]; then JAVA_HOME="$TOOLS/jdk"
else
  say "Downloading JDK 17 (Eclipse Temurin)"
  arch=$(uname -m); [ "$arch" = "aarch64" ] && arch=aarch64 || arch=x64
  fetch "https://api.adoptium.net/v3/binary/latest/17/ga/linux/$arch/jdk/hotspot/normal/eclipse" "$TOOLS/jdk.tar.gz"
  mkdir -p "$TOOLS/jdk" && tar -xzf "$TOOLS/jdk.tar.gz" -C "$TOOLS/jdk" --strip-components=1 && rm "$TOOLS/jdk.tar.gz"
  JAVA_HOME="$TOOLS/jdk"
fi
export JAVA_HOME PATH="$JAVA_HOME/bin:$PATH"
echo "JDK: $JAVA_HOME"

# ── 3. Android SDK + NDK ──────────────────────────────────────────
export ANDROID_SDK_ROOT="${ANDROID_SDK_ROOT:-$TOOLS/sdk}"
export ANDROID_HOME="$ANDROID_SDK_ROOT"
SDKM="$ANDROID_SDK_ROOT/cmdline-tools/latest/bin/sdkmanager"
if [ ! -x "$SDKM" ]; then
  say "Downloading Android command-line tools"
  fetch "https://dl.google.com/android/repository/$CMDLINE_ZIP" "$TOOLS/cmdline.zip"
  rm -rf "$TOOLS/cmdline-tmp" && unzip_to "$TOOLS/cmdline.zip" "$TOOLS/cmdline-tmp"
  mkdir -p "$ANDROID_SDK_ROOT/cmdline-tools" && rm -rf "$ANDROID_SDK_ROOT/cmdline-tools/latest"
  mv "$TOOLS/cmdline-tmp/cmdline-tools" "$ANDROID_SDK_ROOT/cmdline-tools/latest"
  rm -rf "$TOOLS/cmdline.zip" "$TOOLS/cmdline-tmp"
fi
if [ ! -d "$ANDROID_SDK_ROOT/ndk/$NDK_VER" ] || [ ! -d "$ANDROID_SDK_ROOT/platforms/$SDK_PLATFORM" ] || [ ! -d "$ANDROID_SDK_ROOT/build-tools/$BUILD_TOOLS" ]; then
  say "Installing SDK platform, build-tools and NDK (a few GB the first time)"
  yes | "$SDKM" --sdk_root="$ANDROID_SDK_ROOT" --licenses >/dev/null || true
  "$SDKM" --sdk_root="$ANDROID_SDK_ROOT" "platform-tools" "platforms;$SDK_PLATFORM" "build-tools;$BUILD_TOOLS" "ndk;$NDK_VER"
fi
export ANDROID_NDK_ROOT="$ANDROID_SDK_ROOT/ndk/$NDK_VER"

# ── 4. Qt for Android (+ the desktop Qt it needs for host tools) ──
QT_DIR="$TOOLS/qt"
if [ ! -x "$QT_DIR/$QT_VER/android_arm64_v8a/bin/qt-cmake" ] || [ ! -d "$QT_DIR/$QT_VER/gcc_64" ]; then
  say "Downloading Qt $QT_VER for Android with aqtinstall"
  AQT install-qt linux desktop "$QT_VER" linux_gcc_64 -O "$QT_DIR"
  AQT install-qt linux android "$QT_VER" android_arm64_v8a -O "$QT_DIR"
fi
QT_ANDROID="$QT_DIR/$QT_VER/android_arm64_v8a"
QT_HOST="$QT_DIR/$QT_VER/gcc_64"

# ── 4b. libraries Qt's host tools (moc, rcc, androiddeployqt) need ──
# Minimal installs (Kali, WSL images) often lack libglib-2.0 & co. Fetch them as plain files into
# $TOOLS/hostlibs with "apt-get download" (no sudo); only if that's impossible, fall back to sudo apt.
HOSTLIBS="$TOOLS/hostlibs"
export LD_LIBRARY_PATH="$HOSTLIBS/usr/lib/x86_64-linux-gnu:$HOSTLIBS/lib/x86_64-linux-gnu:$HOSTLIBS/usr/lib/aarch64-linux-gnu:$QT_HOST/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
missing_libs() {
  for t in "$QT_HOST/libexec/rcc" "$QT_HOST/libexec/moc" "$QT_HOST/libexec/uic" "$QT_HOST/bin/androiddeployqt" \
           "$QT_HOST/lib/libQt6Core.so.6"; do
    [ -e "$t" ] && ldd "$t" 2>/dev/null | awk '/not found/{print $1}'
  done | sort -u
}
pkg_for() {  # library file → Debian/Ubuntu/Kali package names to try
  case "$1" in
    libglib-2.0.so*|libgthread-2.0.so*|libgobject-2.0.so*|libgio-2.0.so*) echo "libglib2.0-0t64 libglib2.0-0";;
    libpcre2-8.so*) echo "libpcre2-8-0";;
    libpcre2-16.so*) echo "libpcre2-16-0";;
    libzstd.so*) echo "libzstd1";;
    libffi.so*) echo "libffi8";;
    libdouble-conversion.so*) echo "libdouble-conversion3";;
    libb2.so*) echo "libb2-1";;
    libsystemd.so*) echo "libsystemd0";;
    libmount.so*) echo "libmount1";;
    libblkid.so*) echo "libblkid1";;
    libselinux.so*) echo "libselinux1";;
    libz.so*) echo "zlib1g";;
    libgssapi_krb5.so*|libkrb5.so*|libk5crypto.so*|libkrb5support.so*) echo "libgssapi-krb5-2 libkrb5-3 libk5crypto3 libkrb5support0";;
    libxkbcommon.so*) echo "libxkbcommon0";;
    libfontconfig.so*) echo "libfontconfig1";;
    libfreetype.so*) echo "libfreetype6";;
    libEGL.so*|libGL.so*|libOpenGL.so*|libGLX.so*) echo "libegl1 libgl1 libopengl0 libglx0 libglvnd0";;
    libX11.so*|libxcb.so*) echo "libx11-6 libxcb1";;
    libdbus-1.so*) echo "libdbus-1-3";;
    *) echo "";;
  esac
}
fetch_host_libs() {
  local libs pkgs=() tried=0 dir="$TOOLS/debs"
  for round in 1 2 3 4; do
    libs=$(missing_libs)
    [ -z "$libs" ] && return 0
    say "Fetching libraries Qt's tools need: $(echo $libs)"
    pkgs=()
    for l in $libs; do for p in $(pkg_for "$l"); do pkgs+=("$p"); done; done
    [ ${#pkgs[@]} -eq 0 ] && break
    mkdir -p "$dir" "$HOSTLIBS"
    ( cd "$dir" && for p in "${pkgs[@]}"; do apt-get download "$p" >/dev/null 2>&1 && echo "  got $p"; done )
    if ! ls "$dir"/*.deb >/dev/null 2>&1 && [ $tried = 0 ]; then
      tried=1  # the package lists may never have been downloaded on a fresh minimal install
      echo "  (refreshing the package list — this one step asks for your Linux password)"
      sudo apt-get update -qq || true
      ( cd "$dir" && for p in "${pkgs[@]}"; do apt-get download "$p" >/dev/null 2>&1 && echo "  got $p"; done )
    fi
    for d in "$dir"/*.deb; do [ -e "$d" ] && dpkg-deb -x "$d" "$HOSTLIBS" && rm -f "$d"; done
  done
  libs=$(missing_libs)
  [ -z "$libs" ] && return 0
  echo
  echo "Qt's build tools still can't find: $(echo $libs)"
  echo "Install them once with your package manager, e.g.:"
  echo "    sudo apt install -y libglib2.0-0t64 || sudo apt install -y libglib2.0-0"
  echo "then run this script again."
  exit 1
}
fetch_host_libs

# ── 5. configure + build the APK ──────────────────────────────────
say "Building the APK"
SIGN=()
if [ $RELEASE = 1 ]; then
  : "${ANDROID_KEYSTORE:?set ANDROID_KEYSTORE to your .jks for --release}"
  : "${ANDROID_KEY_ALIAS:?set ANDROID_KEY_ALIAS}"
  : "${ANDROID_KEYSTORE_PASS:?set ANDROID_KEYSTORE_PASS}"
  export QT_ANDROID_KEYSTORE_PATH="$ANDROID_KEYSTORE" QT_ANDROID_KEYSTORE_ALIAS="$ANDROID_KEY_ALIAS"
  export QT_ANDROID_KEYSTORE_STORE_PASS="$ANDROID_KEYSTORE_PASS" QT_ANDROID_KEYSTORE_KEY_PASS="${ANDROID_KEY_PASS:-$ANDROID_KEYSTORE_PASS}"
  SIGN=(-DQT_ANDROID_SIGN_APK=ON)
fi
"$QT_ANDROID/bin/qt-cmake" -S "$ROOT" -B "$BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=$([ $RELEASE = 1 ] && echo Release || echo RelWithDebInfo) \
  -DQT_HOST_PATH="$QT_HOST" \
  -DANDROID_SDK_ROOT="$ANDROID_SDK_ROOT" -DANDROID_NDK_ROOT="$ANDROID_NDK_ROOT" \
  -DQT_ANDROID_ABIS=arm64-v8a -DQT_ANDROID_BUILD_ALL_ABIS=OFF \
  -DHUNTER_WITH_WEBENGINE=OFF -DHUNTER_BUILD_TESTS=OFF "${SIGN[@]}"
cmake --build "$BUILD" --parallel --target apk

VER=$(sed -n 's/^project(CODRedemptionHunter VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
mkdir -p dist
APK=$(find "$BUILD/android-build" -name '*.apk' -path '*outputs/apk*' | sort | tail -1)
[ -n "$APK" ] || { echo "No APK produced — see the output above."; exit 1; }
cp "$APK" "dist/COD_Redemption_Hunter-$VER-arm64.apk"
say "Done: dist/COD_Redemption_Hunter-$VER-arm64.apk"
echo "Install: adb install -r dist/COD_Redemption_Hunter-$VER-arm64.apk   (or copy it to the phone and open it)"
echo "Note: on Android, scanning runs while the app is open — use ntfy phone push from the PC app for 24/7 alerts."
