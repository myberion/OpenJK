#!/bin/bash
# Decompression benchmark of deflate libraries on the game's own pk3 files (surveys/compression-libraries.md): zlib
# 1.2.8 and its minizip, which the game uses, zlib 1.4, zlib-ng, Chromium's and Cloudflare's zlib, libdeflate, ISA-L,
# miniz, Wuffs and stb_image's decoder, each inflating every entry of the game's assets, the output checked against
# the CRC-32 of the archive's directory; then the archive readers (minizip as the engine drives it, miniz, minizip-ng,
# libzip, PhysicsFS, and a directory reader of 100 lines with libdeflate or Wuffs) opening the archives and reading
# every entry.
# Windows, Git Bash, with llvm-mingw's clang, CMake and Ninja in the PATH. The candidates are downloaded into the work
# folder, each in a folder of its own, and built there. zlib 1.2.8, minizip and Wuffs are taken from the repository
# (from the last commit that holds them once they are removed).
# Usage: zipbench.sh <work folder> <game base folder holding assets*.pk3> [inflate|crc|archives|all]
set -eu
mkdir -p "$1"
WORK=$(cd "$1" && (pwd -W 2> /dev/null || pwd))	# a Windows path where there is one: CMake does not read Git Bash's
BASE=$2
WHAT=${3:-all}
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../../.." && pwd)
CHROMIUM=ec453dc9abac1ddf39ced752e72a7a80dcda1d91	# chromium/src/third_party/zlib, main on 14 November 2026
CLOUDFLARE=3944b7d59cf50aa144d8bba69809edea072161d4	# cloudflare/zlib, branch gcc.amd64 on 14 November 2026
mkdir -p "$WORK"/{old,zlib14,zlibng,libdeflate,miniz,isal,nasm,chromium,cloudflare,minizipng,libzip,physfs,stb,wuffs}
cd "$WORK"

# the last commit that holds a path of the repository (the commit before the one that removed it)
holding() {
	local c
	c=$(git -C "$REPO" rev-list -1 HEAD -- "$1")
	git -C "$REPO" cat-file -e "$c:$1" 2> /dev/null || c="$c^"
	echo "$c"
}

get() { [ -s "$2" ] || curl -sSL --fail -o "$2" "$1"; }
untar() { [ -d "$1/src" ] || { mkdir "$1/src" && tar xzf "$1/src.tar.gz" -C "$1/src" --strip-components="${2:-1}"; }; }
get https://github.com/madler/zlib/archive/refs/tags/v1.4.tar.gz zlib14/src.tar.gz
get https://github.com/zlib-ng/zlib-ng/archive/refs/tags/2.3.3.tar.gz zlibng/src.tar.gz
get https://github.com/ebiggers/libdeflate/archive/refs/tags/v1.26.tar.gz libdeflate/src.tar.gz
get https://github.com/richgel999/miniz/archive/refs/tags/3.1.2.tar.gz miniz/src.tar.gz
get https://github.com/intel/isa-l/archive/refs/tags/v2.32.1.tar.gz isal/src.tar.gz
get https://www.nasm.us/pub/nasm/releasebuilds/2.16.03/win64/nasm-2.16.03-win64.zip nasm/nasm.zip
get https://github.com/zlib-ng/minizip-ng/archive/refs/tags/4.2.2.tar.gz minizipng/src.tar.gz
get https://github.com/nih-at/libzip/archive/refs/tags/v1.12.1.tar.gz libzip/src.tar.gz
get https://github.com/icculus/physfs/archive/refs/tags/release-3.2.0.tar.gz physfs/src.tar.gz
get https://raw.githubusercontent.com/nothings/stb/master/stb_image.h stb/stb_image.h
get "https://chromium.googlesource.com/chromium/src/third_party/zlib/+archive/$CHROMIUM.tar.gz" chromium/src.tar.gz
get "https://github.com/cloudflare/zlib/archive/$CLOUDFLARE.tar.gz" cloudflare/src.tar.gz
for d in zlib14 zlibng libdeflate miniz isal minizipng libzip physfs cloudflare; do untar $d; done
untar chromium 0
[ -f nasm/nasm-2.16.03/nasm.exe ] || unzip -q -o nasm/nasm.zip -d nasm
[ -f wuffs/wuffs-v0.4.c ] || git -C "$REPO" show "$(holding lib/wuffs/wuffs-v0.4.c):lib/wuffs/wuffs-v0.4.c" > wuffs/wuffs-v0.4.c

# a CMake build in Release with clang; a failure is reported and the script goes on without that library
cm() {	# <name> <source folder> <cmake options...>
	local name=$1 src=$2
	shift 2
	[ -f "$name/build/done" ] && return
	mkdir -p "$name"
	if cmake -S "$src" -B "$name/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
		-DBUILD_SHARED_LIBS=OFF -DCMAKE_POLICY_VERSION_MINIMUM=3.5 "$@" > "$name/build.log" 2>&1 && cmake --build "$name/build" >> "$name/build.log" 2>&1; then
		touch "$name/build/done"
	else
		echo "BUILD FAILED: $name (see $WORK/$name/build.log)"
	fi
}

# zlib 1.2.8 and minizip as the game has them, built as the game builds them
if [ ! -f old/CMakeLists.txt ]; then
	git -C "$REPO" archive "$(holding lib/zlib/CMakeLists.txt)" lib/zlib lib/minizip | tar x -C old
	printf 'cmake_minimum_required(VERSION 3.10)\nproject(old C)\nadd_subdirectory(lib/zlib)\nadd_subdirectory(lib/minizip)\n' > old/CMakeLists.txt
fi
cm old old
cm zlib14 zlib14/src -DZLIB_BUILD_TESTING=OFF -DZLIB_BUILD_SHARED=OFF -DZLIB_BUILD_MINIZIP=OFF -DZLIB_INSTALL=OFF
cm zlibng zlibng/src -DZLIB_COMPAT=OFF -DZLIB_ENABLE_TESTS=OFF -DZLIBNG_ENABLE_TESTS=OFF -DWITH_GTEST=OFF -DBUILD_TESTING=OFF
cm libdeflate libdeflate/src -DLIBDEFLATE_BUILD_SHARED_LIB=OFF -DLIBDEFLATE_BUILD_GZIP=OFF
cm miniz miniz/src -DBUILD_EXAMPLES=OFF -DBUILD_TESTS=OFF -DBUILD_FUZZERS=OFF
cm isal isal/src "-DCMAKE_ASM_NASM_COMPILER=$WORK/nasm/nasm-2.16.03/nasm.exe" -DISAL_BUILD_TESTS=OFF -DISAL_BUILD_PERF_TESTS=OFF \
	-DISAL_BUILD_ISAL_SHIM=OFF
cm cloudflare cloudflare/src

# Chromium's zlib: its CMake file has no Windows build; the files of its inflate are compiled with its x86-64 settings
if [ ! -f chromium/libz.a ]; then
	mkdir -p chromium/obj
	for f in adler32 adler32_simd crc32 crc32_simd crc_folding cpu_features inffast inftrees zutil contrib/optimizations/inffast_chunk \
		contrib/optimizations/inflate; do
		clang -O3 -DNDEBUG -DINFLATE_CHUNK_SIMD_SSE2 -DADLER32_SIMD_SSSE3 -DINFLATE_CHUNK_READ_64LE -DCRC32_SIMD_SSE42_PCLMUL -DX86_WINDOWS \
			-mssse3 -msse4.2 -mpclmul -I chromium/src -c chromium/src/$f.c -o "chromium/obj/$(basename $f).o"
	done
	llvm-ar rcs chromium/libz.a chromium/obj/*.o
fi

FLAGS="-O3 -DNDEBUG -std=c++17 -I $HERE"
if [ "$WHAT" != archives ]; then
	clang++ $FLAGS -DWITH_ZLIB '-DZLIB_NAME="zlib 1.2.8"' -DWITH_LIBDEFLATE -DWITH_ISAL -DWITH_MINIZ -DWITH_WUFFS -DWITH_STB \
		-I old/lib/zlib/include -I libdeflate/src -I isal/src/include -I miniz/src -I miniz/build -I wuffs -I stb \
		"$HERE/bench.cpp" old/build/lib/zlib/libbundled_zlib.a libdeflate/build/libdeflate.a isal/build/isa-l.a \
		miniz/build/libminiz.a -static -o bench_tree.exe
	# zlib-ng's header and zlib's cannot be included together, and the zlib forks share zlib's names: a program each
	clang++ $FLAGS -DWITH_ZLIBNG -I zlibng/build "$HERE/bench.cpp" zlibng/build/libz-ng.a -static -o bench_zlibng.exe
	clang++ $FLAGS -DWITH_ZLIB '-DZLIB_NAME="zlib 1.4"' -I zlib14/build -I zlib14/src "$HERE/bench.cpp" zlib14/build/libzs.a -static -o bench_zlib14.exe
	clang++ $FLAGS -DWITH_ZLIB '-DZLIB_NAME="Chromium zlib"' -I chromium/src "$HERE/bench.cpp" chromium/libz.a -static -o bench_chromium.exe
	clang++ $FLAGS -DWITH_ZLIB '-DZLIB_NAME="Cloudflare zlib"' -I cloudflare/build -I cloudflare/src "$HERE/bench.cpp" cloudflare/build/libz.a \
		-static -o bench_cloudflare.exe
	for mode in inflate crc; do
		[ "$WHAT" = all ] || [ "$WHAT" = $mode ] || continue
		echo "=== $mode: every deflated entry of the game's archives, the best of 5 runs per entry"
		./bench_tree.exe "$BASE" $mode
		for b in zlib14 zlibng chromium cloudflare; do ./bench_$b.exe "$BASE" $mode | tail -n +2; done
	done
fi

if [ "$WHAT" = all ] || [ "$WHAT" = archives ]; then
	# the archive readers: zlib-ng once more with zlib's names (for minizip), minizip-ng, libzip, PhysicsFS
	cm zlibngc zlibng/src -DZLIB_COMPAT=ON -DZLIB_ENABLE_TESTS=OFF -DZLIBNG_ENABLE_TESTS=OFF -DWITH_GTEST=OFF -DBUILD_TESTING=OFF
	# minizip-ng 4.2.2 names zlib-ng's static library zlib-ng::zlibstatic, zlib-ng 2.3.3 exports zlib-ng::zlib: the name it wants
	[ -d zlibng/inst ] || cmake --install zlibng/build --prefix "$WORK/zlibng/inst" > /dev/null
	printf 'add_library(zlib-ng::zlibstatic STATIC IMPORTED GLOBAL)
set_target_properties(zlib-ng::zlibstatic PROPERTIES IMPORTED_LOCATION "%s/zlibng/build/libz-ng.a" INTERFACE_INCLUDE_DIRECTORIES "%s/zlibng/build")
' 		"$WORK" "$WORK" > minizipng/zlibng.cmake
	cm minizipng minizipng/src "-DCMAKE_PREFIX_PATH=$WORK/zlibng/inst" "-DCMAKE_PROJECT_INCLUDE=$WORK/minizipng/zlibng.cmake" -DMZ_COMPAT=OFF -DMZ_BZIP2=OFF -DMZ_LZMA=OFF -DMZ_PPMD=OFF -DMZ_ZSTD=OFF \
		-DMZ_FETCH_LIBS=OFF -DMZ_PKCRYPT=OFF -DMZ_WZAES=OFF -DMZ_OPENSSL=OFF -DMZ_ICONV=OFF -DMZ_DECOMPRESS_ONLY=ON
	cm libzip libzip/src "-DZLIB_LIBRARY=$WORK/zlib14/build/libzs.a" "-DZLIB_INCLUDE_DIR=$WORK/zlib14/src" -DENABLE_COMMONCRYPTO=OFF \
		-DENABLE_GNUTLS=OFF -DENABLE_OPENSSL=OFF -DENABLE_WINDOWS_CRYPTO=OFF -DENABLE_BZIP2=OFF -DENABLE_LZMA=OFF -DENABLE_ZSTD=OFF \
		-DBUILD_TOOLS=OFF -DBUILD_REGRESS=OFF -DBUILD_OSSFUZZ=OFF -DBUILD_EXAMPLES=OFF -DBUILD_DOC=OFF -DLIBZIP_DO_INSTALL=OFF
	cm physfs physfs/src -DPHYSFS_BUILD_SHARED=OFF -DPHYSFS_BUILD_TEST=OFF -DPHYSFS_BUILD_DOCS=OFF -DPHYSFS_DISABLE_INSTALL=ON
	# the minizip of zlib 1.4, against zlib 1.4 and against zlib-ng
	mkdir -p minizip14 minizip14ng
	for f in unzip ioapi; do
		clang -O3 -DNDEBUG -I zlib14/src -c zlib14/src/contrib/minizip/$f.c -o minizip14/$f.o
		clang -O3 -DNDEBUG -I zlibngc/build -c zlib14/src/contrib/minizip/$f.c -o minizip14ng/$f.o
	done

	arch() {	# <program> <options, include folders and libraries...>
		local out=$1
		shift
		clang++ $FLAGS "$HERE/archives.cpp" "$@" -static -o "$out" || echo "COMPILE FAILED: $out"
	}
	arch arch_tree.exe -DWITH_MINIZIP '-DMINIZIP_NAME="minizip + zlib 1.2.8 (the game)"' -DWITH_ZLIB -DWITH_DIRECT_ZLIB '-DZLIB_NAME="zlib 1.2.8"' \
		-DWITH_MINIZ -DWITH_LIBDEFLATE -DWITH_ISAL -DWITH_WUFFS -I old/lib/minizip/include/minizip -I old/lib/zlib/include -I libdeflate/src \
		-I isal/src/include -I miniz/src -I miniz/build -I wuffs old/build/lib/minizip/libbundled_minizip.a old/build/lib/zlib/libbundled_zlib.a \
		libdeflate/build/libdeflate.a isal/build/isa-l.a miniz/build/libminiz.a
	arch arch_zlib14.exe -DWITH_MINIZIP '-DMINIZIP_NAME="minizip 1.4 + zlib 1.4"' -I zlib14/src/contrib/minizip -I zlib14/src minizip14/unzip.o \
		minizip14/ioapi.o zlib14/build/libzs.a
	arch arch_zlibng.exe -DWITH_MINIZIP '-DMINIZIP_NAME="minizip 1.4 + zlib-ng"' -I zlib14/src/contrib/minizip -I zlibngc/build minizip14ng/unzip.o \
		minizip14ng/ioapi.o zlibngc/build/libz.a
	arch arch_mzng.exe -DWITH_MZNG -DWITH_ZLIBNG -I minizipng/src -I minizipng/build -I zlibng/build minizipng/build/libminizip-ng.a zlibng/build/libz-ng.a
	arch arch_libzip.exe -DWITH_LIBZIP -DZIP_STATIC -I libzip/src/lib -I libzip/build libzip/build/lib/libzip.a zlib14/build/libzs.a
	arch arch_physfs.exe -DWITH_PHYSFS -I physfs/src/src physfs/build/libphysfs.a
	arch arch_chromium.exe -DWITH_ZLIB -DWITH_DIRECT_ZLIB '-DZLIB_NAME="Chromium zlib"' -I chromium/src chromium/libz.a
	echo "=== archives: the four archives opened and listed (best of 5), then every entry read whole (best of 3)"
	./arch_tree.exe "$BASE"
	for b in zlib14 zlibng mzng libzip physfs chromium; do [ -f arch_$b.exe ] && ./arch_$b.exe "$BASE" | tail -n +2; done
fi
