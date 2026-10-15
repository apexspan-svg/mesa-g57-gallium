#!/bin/sh
# CI cross-build: Panfrost Gallium (kbase/JM, G57) + EGL/GLES for Android arm64.
# Adapted from JimVulkan/mali-panfrost build.sh (two stages: host CLC tools,
# then NDK cross-compile). Runs on a Linux x86_64 GitHub runner.
#
# usage: ./build-android-ci.sh [path-to-android-ndk]
#   NDK r29 tested. BUILD_DIR overrides the Android build dir (default: build-android).
set -eu

cd "$(dirname "$0")"

NDK="${1:-${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}}"
if [ -z "$NDK" ] || [ ! -d "$NDK" ]; then
   echo "error: pass the Android NDK path or set ANDROID_NDK_HOME" >&2
   exit 1
fi

case "$(uname -s)-$(uname -m)" in
   Linux-x86_64) HOST=linux-x86_64 ;;
   *) echo "error: build on Linux x86_64; stage 1 needs LLVM" >&2; exit 1 ;;
esac

BIN="$NDK/toolchains/llvm/prebuilt/$HOST/bin"
if [ ! -x "$BIN/aarch64-linux-android31-clang" ]; then
   echo "error: no API 31 arm64 compiler in $BIN" >&2
   exit 1
fi

for tool in meson ninja python3 pkg-config flex bison; do
   if ! command -v "$tool" >/dev/null 2>&1; then
      echo "error: $tool not found in PATH" >&2
      exit 1
   fi
done

# --- stage 1: host tools -------------------------------------------------------
HOSTB=build-host
if [ ! -f "$HOSTB/build.ninja" ]; then
   meson setup "$HOSTB" \
      -Dbuildtype=debugoptimized \
      -Dmesa-clc=enabled -Dprecomp-compiler=enabled \
      -Dgallium-drivers=panfrost -Dvulkan-drivers= -Dplatforms= -Dllvm=enabled \
      -Dopengl=false -Dgles1=disabled -Dgles2=disabled -Degl=disabled -Dglx=disabled -Dgbm=disabled \
      -Dtools= -Dvideo-codecs= \
      -Dpanfrost-kmds=kbase,panthor \
      -Dallow-fallback-for=libdrm --force-fallback-for=libdrm,expat,zlib \
      -Dlibdrm:default_library=static
fi
ninja -C "$HOSTB" src/compiler/clc/mesa_clc src/compiler/spirv/vtn_bindgen2 \
   src/panfrost/clc/panfrost_compile
mkdir -p "$HOSTB/bin"
cp "$HOSTB/src/compiler/clc/mesa_clc" "$HOSTB/src/compiler/spirv/vtn_bindgen2" \
   "$HOSTB/src/panfrost/clc/panfrost_compile" "$HOSTB/bin/"
PATH="$PWD/$HOSTB/bin:$PATH"
export PATH

# --- stage 2: the driver -------------------------------------------------------
BUILD="${BUILD_DIR:-build-android}"
mkdir -p "$BUILD" "$BUILD/pkgconfig-empty"

cat > "$BUILD/cross.ini" <<EOF
[binaries]
c = '$BIN/aarch64-linux-android31-clang'
cpp = '$BIN/aarch64-linux-android31-clang++'
ar = '$BIN/llvm-ar'
strip = '$BIN/llvm-strip'
pkg-config = 'pkg-config'

[properties]
# Keep the host's .pc files out of the cross build.
pkg_config_libdir = ['$PWD/$BUILD/pkgconfig-empty']
cpp_link_args = ['-static-libstdc++']

[host_machine]
system = 'android'
cpu_family = 'aarch64'
cpu = 'aarch64'
endian = 'little'
EOF

if [ ! -f "$BUILD/build.ninja" ]; then
   meson setup "$BUILD" --cross-file "$BUILD/cross.ini" \
      -Dbuildtype=debugoptimized -Db_ndebug=true \
      -Dplatforms=android -Dplatform-sdk-version=31 -Dandroid-stub=true -Dandroid-strict=false \
      -Dandroid-libbacktrace=disabled \
      -Dgallium-drivers=panfrost -Dvulkan-drivers= -Dvulkan-layers= -Dtools= -Dvideo-codecs= \
      -Degl=enabled -Dopengl=true -Dgles1=disabled -Dgles2=enabled -Dglx=disabled -Dgbm=disabled \
      -Degl-lib-suffix=_mesa -Dgles-lib-suffix=_mesa \
      -Dmesa-clc=system -Dprecomp-compiler=system \
      -Dpanfrost-kmds=kbase,panthor \
      -Dllvm=disabled -Dzstd=disabled -Dlmsensors=disabled -Dperfetto=false \
      -Dallow-fallback-for=libdrm,perfetto --force-fallback-for=expat,libdrm,zlib \
      -Dlibdrm:default_library=static -Dexpat:default_library=static -Dzlib:default_library=static
fi

LIBS="src/egl/libEGL_mesa.so src/gallium/targets/dri/libgallium_dri.so
      src/mesa/glapi/es2api/libGLESv2_mesa.so"
# shellcheck disable=SC2086
ninja -C "$BUILD" $LIBS

mkdir -p "$BUILD/stripped" dist
for lib in $LIBS; do
   "$BIN/llvm-strip" -o "$BUILD/stripped/$(basename "$lib")" "$BUILD/$lib"
done
(cd "$BUILD/stripped" && zip -9 "$OLDPWD/dist/panfrost-g57-kbase-android.zip" ./*)
echo "packaged: dist/panfrost-g57-kbase-android.zip"
