#!/bin/bash
# Builds lib/libwc09.a (the WC2009 HD core for iPhone/iPad, arm64) and copies the header to include/.
# Needs Xcode and cmake (brew install cmake). Xcode runs this by itself when the library is missing.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
B="$HERE/build-lib"
# Xcode's build environment can point cmake at the wrong SDK/compiler: start clean
unset SDKROOT CC CXX LD CFLAGS CXXFLAGS LDFLAGS IPHONEOS_DEPLOYMENT_TARGET MACOSX_DEPLOYMENT_TARGET

cmake -S "$ROOT" -B "$B" \
    -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_QT_SDL=OFF -DMELONDS_GLES=ON -DMELONDS_GLES_30=ON -DBUILD_WC09_MOBILE=ON \
    -DENABLE_JIT=OFF -DENABLE_GDBSTUB=OFF -DENABLE_LTO_RELEASE=OFF
cmake --build "$B" --target wc09mobile --config Release -j "$(sysctl -n hw.ncpu)"

mkdir -p "$HERE/lib" "$HERE/include"
rm -f "$HERE/lib/libwc09.a"
libtool -static -o "$HERE/lib/libwc09.a" $(find "$B" -name "*.a" -not -path "*/CMakeFiles/*")
cp "$ROOT/mobile/core/wc09_core.h" "$HERE/include/"
echo "built $HERE/lib/libwc09.a"
