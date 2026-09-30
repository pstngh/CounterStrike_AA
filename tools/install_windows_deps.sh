#!/bin/bash
# Installs the Windows x64 builds of SDL2 and FreeType into the MinGW-w64
# sysroot, where cmake/toolchains/windows-x64-mingw.cmake finds them.
set -euo pipefail

sdl2_version=2.32.10
freetype_version=2.13.3
sysroot=/usr/x86_64-w64-mingw32
toolchain="$(cd "$(dirname "$0")/.." && pwd)/cmake/toolchains/windows-x64-mingw.cmake"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
cd "$work"

curl -fsSL "https://github.com/libsdl-org/SDL/releases/download/release-$sdl2_version/SDL2-devel-$sdl2_version-mingw.tar.gz" | tar xz
sudo make -C "SDL2-$sdl2_version" cross CROSS_PATH=/usr ARCHITECTURES=x86_64-w64-mingw32

# RmlUi needs only FreeType's core, linked statically.
curl -fsSL "https://download.sourceforge.net/freetype/freetype-$freetype_version.tar.xz" | tar xJ
cmake -S "freetype-$freetype_version" -B freetype-build -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$sysroot" -DBUILD_SHARED_LIBS=OFF \
    -DFT_DISABLE_ZLIB=ON -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_PNG=ON \
    -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BROTLI=ON
cmake --build freetype-build
sudo cmake --install freetype-build
