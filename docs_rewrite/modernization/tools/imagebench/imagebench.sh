#!/bin/bash
# Decoding benchmark of image libraries on the game's own images (surveys/image-libraries.md): libjpeg 9a and libpng,
# which the game used, libjpeg-turbo with and without its assembly code, libspng, stb_image and Wuffs, each decoding
# every JPEG, PNG and TGA of the game's assets to RGBA8, with the pixels compared.
# Windows, Git Bash, with llvm-mingw's clang, CMake and Ninja in the PATH. The candidates are downloaded into the work
# folder, each in a folder of its own, and built there. libjpeg 9a, libpng and the engine's own TGA loader are no
# longer in the repository: they are taken from the last commit that holds them.
# Usage: imagebench.sh <work folder> <game base folder holding assets*.pk3> [skins|all]
#   skins (default): the images of models/players, pixels compared one by one
#   all: every image of the assets, pixels compared by hash
set -eu
mkdir -p "$1"
WORK=$(cd "$1" && pwd)
BASE=$2
SET=${3:-skins}
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../../.." && pwd)
mkdir -p "$WORK"/{wuffs,stb,spng,turbo,nasm,old,tga,corpus,ref,ref_turbo}
cd "$WORK"

# the last commit that holds a path of the repository (the commit before the one that removed it)
holding() {
	local c
	c=$(git -C "$REPO" rev-list -1 HEAD -- "$1")
	git -C "$REPO" cat-file -e "$c:$1" 2> /dev/null || c="$c^"
	echo "$c"
}

get() { [ -s "$2" ] || curl -sSL -o "$2" "$1"; }
get https://raw.githubusercontent.com/google/wuffs/v0.4.0-alpha.12/release/c/wuffs-v0.4.c wuffs/wuffs-v0.4.c
get https://raw.githubusercontent.com/nothings/stb/master/stb_image.h stb/stb_image.h
get https://raw.githubusercontent.com/randy408/libspng/v0.7.4/spng/spng.c spng/spng.c
get https://raw.githubusercontent.com/randy408/libspng/v0.7.4/spng/spng.h spng/spng.h
get https://github.com/libjpeg-turbo/libjpeg-turbo/archive/refs/tags/3.2.1.tar.gz turbo/turbo.tar.gz
get https://www.nasm.us/pub/nasm/releasebuilds/2.16.03/win64/nasm-2.16.03-win64.zip nasm/nasm.zip
[ -d turbo/libjpeg-turbo-3.2.1 ] || tar xzf turbo/turbo.tar.gz -C turbo
[ -f nasm/nasm-2.16.03/nasm.exe ] || unzip -q -o nasm/nasm.zip -d nasm

# libjpeg-turbo, with its assembly code (NASM) and without
turbo() {	# <build folder> <cmake options...>
	local dir=$1
	shift
	[ -f "turbo/$dir/libjpeg.a" ] && return
	cmake -S turbo/libjpeg-turbo-3.2.1 -B "turbo/$dir" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang \
		-DENABLE_SHARED=OFF -DWITH_TURBOJPEG=OFF -DWITH_TOOLS=OFF -DWITH_TESTS=OFF "$@" > /dev/null
	cmake --build "turbo/$dir" > /dev/null
}
turbo build "-DCMAKE_ASM_NASM_COMPILER=$WORK/nasm/nasm-2.16.03/nasm.exe"
turbo build_nosimd -DWITH_SIMD=0

# libjpeg 9a and libpng 1.6.53 as the game had them, with its zlib, built as the game built them
if [ ! -f old/build/lib/libpng/libbundled_libpng.a ]; then
	git -C "$REPO" archive "$(holding lib/jpeg-9a/CMakeLists.txt)" lib/jpeg-9a lib/libpng lib/zlib | tar x -C old
	printf 'cmake_minimum_required(VERSION 3.10)\nproject(old C)\nadd_subdirectory(lib/jpeg-9a)\nadd_subdirectory(lib/zlib)\nadd_subdirectory(lib/libpng)\n' > old/CMakeLists.txt
	cmake -S old -B old/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang > /dev/null
	cmake --build old/build > /dev/null
fi

# the corpus: one flat folder, the path's separators replaced
if [ -z "$(ls -A corpus)" ]; then
	python -I -c '
import glob, os, sys, zipfile
base, out, which = sys.argv[1:4]
for pk3 in sorted(glob.glob(os.path.join(base, "assets*.pk3"))):
    z = zipfile.ZipFile(pk3)
    for i in z.infolist():
        low = i.filename.lower()
        if low.endswith((".jpg", ".png", ".tga")) and (which == "all" or low.startswith("models/players/")):
            open(os.path.join(out, low.replace("/", "__")), "wb").write(z.read(i))
' "$BASE" corpus "$SET"
fi

FLAGS="-O3 -DNDEBUG -std=c++17"
clang -O3 -DNDEBUG -DSPNG_STATIC -I old/lib/zlib/include -c spng/spng.c -o spng.o 2> /dev/null
clang++ $FLAGS -DWITH_LIBJPEG '-DJPEG_NAME="libjpeg 9a"' -DWITH_LIBPNG -DWITH_SPNG -DSPNG_STATIC -DWITH_STB -DWITH_WUFFS \
	-I old/lib/jpeg-9a/include -I old/lib/libpng/include -I old/lib/zlib/include -I spng -I stb -I wuffs \
	"$HERE/bench.cpp" spng.o old/build/lib/jpeg-9a/libbundled_libjpeg.a old/build/lib/libpng/libbundled_libpng.a \
	old/build/lib/zlib/libbundled_zlib.a -static -o bench_tree.exe
clang++ $FLAGS -DWITH_LIBJPEG '-DJPEG_NAME="libjpeg-turbo 3.2.1"' -I turbo/libjpeg-turbo-3.2.1/src -I turbo/build \
	"$HERE/bench.cpp" turbo/build/libjpeg.a -static -o bench_turbo.exe
clang++ $FLAGS -DWITH_LIBJPEG '-DJPEG_NAME="turbo 3.2.1, no SIMD"' -I turbo/libjpeg-turbo-3.2.1/src -I turbo/build_nosimd \
	"$HERE/bench.cpp" turbo/build_nosimd/libjpeg.a -static -o bench_nosimd.exe

# the engine's own TGA loader against Wuffs: its file, put next to the stand-in header
cp "$HERE/tga_shim/tr_common.h" tga/
git -C "$REPO" show "$(holding codemp/rd-common/tr_image_tga.cpp):codemp/rd-common/tr_image_tga.cpp" > tga/tr_image_tga.cpp
clang++ -O2 -std=c++17 -I tga -I wuffs "$HERE/tga_check.cpp" -static -o tga_check.exe

MODE=write
[ "$SET" = all ] && MODE=hash
echo "=== against libjpeg 9a and libpng (TGA: against stb_image)"
./bench_tree.exe corpus ref $MODE
./bench_turbo.exe corpus ref_turbo $MODE | tail -n +2
./bench_nosimd.exe corpus ref_turbo | tail -n +2
echo "=== JPEG against libjpeg-turbo"
./bench_tree.exe corpus ref_turbo | grep " jpg "
echo "=== TGA: the engine's loader against Wuffs"
./tga_check.exe corpus
