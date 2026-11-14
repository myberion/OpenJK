#!/bin/sh
# Builds and runs mathbench with GCC or Clang. Usage: build.sh <tag> <c compiler> <c++ compiler> [extra flags...]
#   build.sh gcc12-x64 gcc g++            build.sh gcc12-x86 gcc g++ -m32
#   build.sh clang-x64 clang clang++      build.sh clang-x86 i686-w64-mingw32-clang i686-w64-mingw32-clang++
# Needs the sources in build-netcodec/mathlibs (core-math, llvm-project/libc). Writes the objects, the program and
# its output (<tag>.txt) to build-netcodec/mathbench/<tag>/. NOCM=1 leaves CORE-MATH out (GCC on 32-bit x86 has no
# 128-bit integer type, which CORE-MATH needs).
set -e
TAG=$1; CC=$2; CXX=$3; shift 3
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../../.." && pwd)
M=$ROOT/build-netcodec/mathlibs
OUT=$ROOT/build-netcodec/mathbench/$TAG
mkdir -p "$OUT"
FLAGS="-O2 -ffp-contract=off $*"
CM=$M/core-math/src
$CXX $FLAGS -std=c++17 -fno-exceptions -fno-rtti -I "$M/llvm-project/libc" -c "$HERE/lm.cpp" -o "$OUT/lm.o"
# the game's entry point, with the vendored LLVM libc (q_platform.h wants an ARCH_STRING outside Windows)
case "$(uname -s)" in MINGW*|MSYS*) ARCHDEF= ;; *) ARCHDEF='-DARCH_STRING="mathbench"' ;; esac
$CXX $FLAGS -std=c++17 -fno-exceptions -fno-rtti $ARCHDEF -I "$ROOT/lib/llvm-libc" \
	-c "$ROOT/shared/qcommon/q_libm.cpp" -o "$OUT/q_libm.o"
$CC $FLAGS $ARCHDEF -c "$ROOT/shared/qcommon/q_rand.c" -o "$OUT/q_rand.o"
OBJS="$OUT/lm.o $OUT/q_libm.o $OUT/q_rand.o"
DEFS=
if [ "$NOCM" != 1 ]; then
	DEFS=-DHAVE_CM
	for f in binary32/sin/sinf binary32/cos/cosf binary32/tan/tanf binary32/asin/asinf binary32/acos/acosf \
		binary32/atan/atanf binary32/atan2/atan2f binary32/log/logf binary32/pow/powf binary64/sin/sin binary64/cos/cos \
		binary64/tan/tan binary64/acos/acos binary64/atan/atan binary64/atan2/atan2 binary64/pow/pow \
		binary32/sinpi/sinpif binary32/cospi/cospif binary32/tanpi/tanpif binary32/rsqrt/rsqrtf; do
		o=$OUT/cm_$(basename $f).o
		$CC $FLAGS -c "$CM/$f.c" -o "$o"
		OBJS="$OBJS $o"
	done
fi
EXE=$OUT/mathbench
# C++ objects without exceptions or RTTI link with the C compiler; libm on Linux
$CC $FLAGS $DEFS "$HERE/mathbench.c" $OBJS -o "$EXE" -lm
"$EXE" | tee "$OUT/$TAG.txt"
