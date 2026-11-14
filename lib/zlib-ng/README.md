# zlib-ng (vendored source tree)

zlib-ng inflates the files of the pk3 archives. It is linked by the engines and the dedicated server, built by its
own `CMakeLists.txt` from the top-level one (`openjk_add_zlib_ng`), under its own names (`zng_inflate`, `zng_crc32`,
header `zlib-ng.h`); only `shared/qcommon/q_pk3.cpp` includes it: the file systems call the functions of
`shared/qcommon/q_pk3.h` (survey and measurements: `docs_rewrite/modernization/surveys/compression-libraries.md`).

- Source: https://github.com/zlib-ng/zlib-ng at the tag `2.3.3` (commit `12731092979c6d07f42da27da673a9f6c7b13586`).
  Every file kept is unchanged. Left out: `test/`, `doc/`, `tools/`, `.github/` and the other dot files, `configure`,
  `Makefile.in`, the `cmake/toolchain-*.cmake` files, and the code of the processors the project does not target
  (`arch/arm`, `arch/loongarch`, `arch/power`, `arch/riscv`, `arch/s390`). `arch/generic` and `arch/x86` are kept:
  a build for another processor needs its folder back from the same tag.
- Licence: zlib (`LICENSE.md`), which can be combined with the GNU GPL version 2.
- Build: CMake 3.14 or later; no assembler. The SIMD variants of its functions (SSE2 to AVX-512, PCLMULQDQ for the
  CRC-32) are compiled each with its own flags and chosen when the program starts, from what the processor has.
- To update it: replace the tree with the same files of a newer tag, build, and run
  `docs_rewrite/modernization/tools/zipbench/zipbench.sh` (its `check` part reads every file of the game's archives
  through `q_pk3.cpp`).
