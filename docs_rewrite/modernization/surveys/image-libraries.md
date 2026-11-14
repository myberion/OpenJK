# Image libraries: one decoder for JPEG, PNG and TGA, the candidates measured on the game's images

Written on 14 November 2026, during plan step 14 part 9b ([../PLAN.md](../PLAN.md)). The freeze when players' models are loaded during a game is, for a quarter, the reading and decoding of their images ([asset-loading.md](asset-loading.md)). The owner asked for better JPEG and PNG libraries before the rest of that part, with two wishes: one library for the image formats if one does it well, and very fast. The same day the owner decided that no library comes from the system any more: each is vendored at a pinned version and built with the project on Windows and Linux, as LLVM libc is for the math functions.

Each source is labelled as in the other surveys: **primary** (the project's own documentation or source code), **secondary**, **community**. Every time below comes from `tools/imagebench/` (`imagebench.sh`, `bench.cpp`), built with clang on the owner's Windows machine, one thread on one core; each file is decoded to RGBA8, the best of 5 runs per file, summed.

## 1. Summary

1. Today Windows decodes JPEG with IJG libjpeg 9a and Linux with the distribution's libjpeg, which on Debian is libjpeg-turbo: the two platforms do not draw the same pixels. PNG goes through libpng on both.
2. Wuffs decodes the three formats of the game in one library: as fast as libjpeg-turbo on JPEG (2.4 times libjpeg 9a on the game's 4,271 JPEG files), twice as fast as libpng on its 1,134 PNG files, and its JPEG pixels are those of libjpeg-turbo to the bit on every file. It is one C file, needs no assembler, and its code cannot overflow a buffer by construction.
3. libjpeg-turbo is 9 % faster than Wuffs on JPEG, needs the NASM assembler on every build machine for that speed (1.45 times libjpeg 9a without it), and does JPEG only.
4. stb_image also does the three formats, slower (2.0 times libjpeg 9a, 1.3 times libpng), and its own header sends security-minded users to Wuffs.
5. No candidate fails on any of the game's 5,875 images.
6. Wuffs does not write images: the screenshots and the video capture need a writer (stb_image_write, or libjpeg-turbo).
7. Recommended: Wuffs for all decoding. Its JPEG decoder exists only in version 0.4, not yet declared stable: the risk to weigh. Nothing is decided.

## 2. What the engine uses today

Read in this tree:

- The loaders are in `codemp/rd-common/`: `tr_image_jpg.cpp` (libjpeg: rows of RGB, widened to RGBA), `tr_image_png.cpp` (libpng; it refuses anything but 8-bit RGB or RGBA and any size that is not a power of two), `tr_image_tga.cpp` (the engine's own code: types 2, 3 and 10). A name is tried with its own extension first, then as `.jpg`, `.png` and `.tga` in that order (`tr_image_load.cpp`).
- The same files write images: `RE_SaveJPG` and `RE_SaveJPGToBuffer` (JPEG screenshots and the video capture, `rd-vanilla/tr_init.cpp`), `RE_SavePNG` (PNG screenshots).
- The libraries: `lib/jpeg-9a` (IJG libjpeg 9a, plain C), `lib/libpng` (1.6.53), `lib/zlib` (1.2.8, also used for the pk3 files). They are built with the project only on Windows: `UseInternalJPEG`, `UseInternalPNG` and `UseInternalZlib` default to OFF elsewhere (`CMakeLists.txt`), so a Linux build links the system's. On Debian 12 `libjpeg-dev` is `libjpeg62-turbo-dev` (primary: packages.debian.org).
- The game's assets hold 4,271 JPEG files (387 MB), 1,134 PNG (204 MB) and 470 TGA (115 MB); the players' models, 1,009 JPEG and 198 PNG.

## 3. The candidates

| library | version read | decodes | writes | form | licence | notes |
|---|---|---|---|---|---|---|
| IJG libjpeg (today, Windows) | 9a | JPEG | JPEG | C sources | IJG | plain C, no SIMD |
| libpng (today) | 1.6.53 | PNG | PNG | C sources, needs zlib | libpng | |
| libjpeg-turbo | 3.2.1 (27 October 2026) | JPEG | JPEG | C sources and assembly, CMake | IJG and BSD 3-clause | libjpeg's interface; its speed on x86 comes from assembly code that needs NASM or Yasm to build (primary: its build output here) |
| libspng | 0.7.4 (May 2023) | PNG | PNG | two files, needs zlib or miniz | BSD 2-clause | |
| stb_image, stb_image_write | 2.32 | JPEG, PNG, TGA and others | JPEG, PNG, TGA | one header each | MIT or public domain | its header: "If security is a priority, consider using Wuffs instead" (primary) |
| Wuffs | 0.4, tag `v0.4.0-alpha.12` | JPEG, PNG, TGA, BMP, GIF, WebP, QOI and others | nothing | one C file of 3.7 MB, the wanted formats chosen by macros | MIT or Apache 2.0 | "safe with respect to buffer overflows, integer arithmetic overflows and null pointer dereferences", checked when the library itself is compiled; its GIF decoder ships in Chrome since 2021; its README names 0.3 (April 2023) as the latest stable version, and 0.3 has no JPEG decoder (primary: README, the release files) |

Not measured: SDL_image, FreeImage and DevIL (they wrap the libraries above or stb_image), libvips and OpenImageIO (frameworks far larger than the need).

Licences: the project is under the GNU GPL version 2. The IJG, BSD, MIT and libpng licences can be combined with it; Wuffs is taken under its MIT licence, not the Apache one.

## 4. Measurements

Every image of the game's assets, decoded to RGBA8 (ms: the sum over the files of the best of 5 decodings; megapixels per second):

| JPEG, 4,271 files | ms | MP/s | against libjpeg 9a |
|---|---|---|---|
| libjpeg 9a (this tree) | 8,032 | 148 | 1.00 |
| stb_image | 3,973 | 300 | 2.02 times as fast |
| Wuffs | 3,327 | 358 | 2.41 |
| libjpeg-turbo | 3,052 | 390 | 2.63 |

| PNG, 1,134 files | ms | MP/s | against libpng |
|---|---|---|---|
| libpng (this tree) | 2,460 | 73 | 1.00 |
| libspng | 2,329 | 77 | 1.06 times as fast |
| stb_image | 1,886 | 96 | 1.30 |
| Wuffs | 1,238 | 146 | 1.99 |

TGA, 470 files: 21.7 ms with stb_image, 20.7 ms with Wuffs. The format is not compressed or run-length coded: its decoding does not count.

The images of the players' models alone (the script's default set: 1,009 JPEG and 198 PNG). JPEG: libjpeg 9a 1,692 ms, libjpeg-turbo without its assembly code 1,170 ms (1.45 times as fast), stb_image 798 ms (2.12), Wuffs 672 ms (2.52), libjpeg-turbo 646 ms (2.62). PNG: libpng 551 ms, libspng 521 ms (1.06), stb_image 435 ms (1.27), Wuffs 290 ms (1.90).

The pixels:

- PNG and TGA: every decoder gives the same pixels on every file (1,134 and 470; the TGA files compared between stb_image and Wuffs, not with the engine's own loader).
- JPEG: Wuffs gives the pixels of libjpeg-turbo on the 4,271 files. libjpeg 9a gives them on 424 only: it upsamples the colour planes another way, and on the players' images a channel differs by up to 71 levels of 255 (0.18 on average). stb_image differs from libjpeg-turbo by up to 3 levels (0.02 on average).
- No decoder refused a file.

What it means for the game: all of its images decode in 4.6 s with Wuffs instead of 10.5 s; for the freeze of part 9b, where reading and decoding take 14.6 ms per model once the driver's compression is off, about 7 ms less per model (estimated from the ratio, not measured in the client).

## 5. Options

| option | decoding | build | what else |
|---|---|---|---|
| A: Wuffs for JPEG, PNG and TGA | JPEG 2.4 times as fast as today on Windows, PNG 2.0 times; the pixels of libjpeg-turbo, so Windows draws what Linux draws today | one C file compiled with the renderer; no assembler; MSVC, GCC and Clang are supported by its header, MSVC not tried here | memory-safe by construction, which matters for skins a server sends; no writer; version 0.4 is not declared stable |
| B: libjpeg-turbo for JPEG, Wuffs or libpng for PNG | JPEG 2.6 times as fast (9 % above Wuffs) | NASM on every build machine (the Windows VM, the Debian VM, this one), or 1.45 times only; two or three libraries | libjpeg's interface: the present loader and writer stay as they are |
| C: stb_image for the three | JPEG 2.0 times as fast, PNG 1.3 times | one header | its author recommends Wuffs where security counts |
| D: the present libraries, built with the project on Linux too | none | the option switched on | Linux would draw the pixels of libjpeg 9a |

For the writers (JPEG screenshots, video capture, PNG screenshots), with A or C: stb_image_write (one header, not measured), or libjpeg and libpng kept for writing only; with B, libjpeg-turbo writes JPEG.

Recommended: A, with stb_image_write for the writers, which takes libjpeg and libpng out of the multiplayer build (they stay in the repository for the single-player trees, which are out of scope). It is the owner's wish (one library, fast), it needs nothing installed, and its JPEG pixels are already those of the Linux build. Its risk is its version: the JPEG decoder is in 0.4 only, whose tags are still named alpha; against it, the output is equal to libjpeg-turbo's on every JPEG of the game, and the library's kind of safety does not depend on its age. If that status is not acceptable: B.

To decide with it:

- whether the PNG loader keeps its two refusals (sizes that are not powers of two, kinds other than 8-bit RGB and RGBA);
- zlib: 1.2.8 stays for the pk3 files either way; a replacement (zlib-ng, libdeflate) is another question, not measured.

## 6. The owner's choices

- 14 November 2026: no system library; the JPEG and PNG libraries replaced before the rest of part 9b.
- The library: not chosen yet.

## 7. What was not read or verified

- One machine, one compiler (clang of llvm-mingw). GCC and MSVC are not measured, and Wuffs was not compiled with MSVC.
- The times are those of the decoders alone, on files already in memory: not the engine's loaders, which also read the pk3.
- The TGA files were not decoded by the engine's own loader in the bench: stb_image and Wuffs agree with each other, not proven equal to it.
- stb_image_write was not measured or tried.
- The game's assets only: skins made by players (other encoders, progressive JPEG, odd PNG kinds) are not in the corpus.
- Wuffs's and libjpeg-turbo's own benchmark claims were read (Wuffs: PNG 1.2 to 2.7 times libpng; libjpeg-turbo: 1.9 to 4.3 times libjpeg 8d on x86-64, for its version 1.5), not reproduced beyond this page's measure.

## 8. Sources

- Wuffs, README and release files: https://github.com/google/wuffs
- libjpeg-turbo, licences and "Performance": https://github.com/libjpeg-turbo/libjpeg-turbo , https://libjpeg-turbo.org/About/Performance
- stb_image and stb_image_write: https://github.com/nothings/stb
- libspng: https://github.com/randy408/libspng
- Debian 12, the package `libjpeg-dev`: https://packages.debian.org/bookworm/libjpeg-dev
