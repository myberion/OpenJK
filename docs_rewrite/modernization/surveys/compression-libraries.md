# Compression libraries: reading the pk3 files, the candidates measured on the game's archives

Written on 14 November 2026, after the image libraries ([image-libraries.md](image-libraries.md)). The owner asked for the same work on compression: an inventory of everything that compresses or decompresses in the engine, the candidates measured on the game's own data, then a choice, with the wishes of the image work (very fast; one library for several formats if one does it well; one file that knows the library; no file twice between `code/` and `codemp/`; the old libraries removed) and the rule of the same day that no library comes from the system any more. Nothing is written in the engine: this page ends on options.

Each source is labelled as in the other surveys: **primary** (the project's own documentation or source code), **secondary**, **community**. Every time below comes from `tools/zipbench/` (`zipbench.sh`, `bench.cpp`, `archives.cpp`), built with clang on the owner's Windows machine, one thread on one core, on the four `assets*.pk3` of the game; the script was run from an empty folder (it downloads and builds every candidate) and the figures are those of that run.

## 1. Summary

1. The engine decompresses in one place: the pk3 files, which are ZIP archives, read through minizip on zlib 1.2.8 (2013). Nothing else calls zlib, and nothing in the engine writes compressed data through a library. Linux builds link the system's zlib; minizip is always the repository's copy, of 2010.
2. On every entry of the game's archives (22,614 deflated files, 1.87 GB once inflated), libdeflate inflates 1.59 times as fast as zlib 1.2.8, Chromium's zlib 1.40, ISA-L 1.34, zlib-ng 1.30. The newest zlib (1.4), miniz and Wuffs are at the speed of zlib 1.2.8; stb_image's decoder is slower.
3. The checksum weighs as much as the choice of the decoder: zlib 1.2.8 computes the CRC-32 at 2.2 GB per second, 835 ms for the game's data, more than a third of what it spends inflating; libdeflate, ISA-L and zlib-ng compute it at 88 GB per second (21 ms).
4. End to end, from the files on disk, the game's present path reads the whole of the assets in 3.70 s. A reader of the archive's directory written for the engine (100 lines in the benchmark) with libdeflate reads it in 1.67 s (2.2 times as fast) and opens the four archives in 3.5 ms instead of 81. The same reader with Wuffs, which is already in the repository: 2.59 s (1.4 times). minizip brought up to date on zlib-ng, with the engine's code unchanged: 2.37 s (1.56 times).
5. No candidate fails on any entry: every output has the CRC-32 the archive's directory holds.
6. libdeflate inflates a whole entry in one call and has no streaming: an entry read in pieces (a video, a music track) would be inflated whole when it is opened. The largest entry of the game is 19 MB.
7. What it means in game is small: decompression is about 9 % of a player model's load once the driver's compression is off, and the fastest option removes a little more than half of it (about 1 ms of 18.8 per model, estimated). The other reasons to change are the owner's rule (the Linux build takes zlib from the system), the age of the reader of files that come from servers, and the archives opened more than 20 times as fast.
8. Recommended: the engine's own directory reader with libdeflate (option A of section 5). Decided (owner, 14 November 2026): the streaming is kept, so that no file read in pieces is inflated whole when it is opened: zlib-ng, the fastest released decoder that streams and needs no assembler (option F, section 6).

## 2. What the engine uses today

Read in this tree (`code/`, `codemp/`, `codeJK2/`, `shared/`):

**Reading the pk3 files**, the only use of zlib:

- `codemp/qcommon/files.cpp` and `code/qcommon/files.cpp` include `<minizip/unzip.h>` and call 12 of its functions (`unzOpen`, `unzGetGlobalInfo`, `unzGoToFirstFile`, `unzGoToNextFile`, `unzGetCurrentFileInfo`, `unzGetOffset`, `unzSetOffset`, `unzOpenCurrentFile`, `unzReadCurrentFile`, `unzCloseCurrentFile`, `unzClose`, and in the macOS-only `FS_LoadMachOBundle`, `unzLocateFile`).
- An archive is opened by `FS_LoadZipFile`: it walks the directory twice (the names' total length, then the entries), keeps for each file its position in the directory and its size, and hashes the entries' CRC-32 values for the pure-server checksums (`codemp/qcommon/files.cpp:2113`). The CRC values come from the directory; no file is inflated for them.
- A file is opened by `unzSetOffset` and `unzOpenCurrentFile`, which read its directory entry and its local header again, and read by `unzReadCurrentFile` (`FS_Read`, `:1715`): minizip reads the archive 16 KB at a time, inflates into the caller's buffer and computes the CRC-32 of what it delivers.
- A seek backwards in a file of an archive inflates it again from its start (`FS_Seek`, `:1835`); a seek forwards inflates and drops what is skipped.
- A file opened with `uniqueFILE` (cinematics, music, the journal) opens the archive a second time (`:1456`).
- Most callers read a file whole (`FS_ReadFile`). Read in pieces from an archive: the RoQ videos (`client/cl_cin.cpp`), the streamed music (`client/snd_dma.cpp:4031`), the bots' AAS files, which also seek (`botlib/be_aas_file.cpp:308`).
- `codemp/client/cl_parse.cpp:31` and `codemp/server/sv_client.cpp:30` include `zlib.h` and use nothing of it (the macro they test, `USE_INTERNAL_ZLIB`, is defined nowhere).

**The libraries**:

- `lib/zlib`: zlib 1.2.8 (April 2013), 12,800 lines. Built with the project on Windows only: `UseInternalZlib` defaults to OFF elsewhere (`CMakeLists.txt:68`), so a Linux build links the system's zlib (1.2.13 on Debian 12; primary: packages.debian.org).
- `lib/minizip`: `unzip.c` and `ioapi.c` of zlib 1.2.8's `contrib/minizip` ("Version 1.1, February 14h, 2010"), 3,000 lines, always the repository's copy because it is changed to allocate through the engine (`openjk_minizip_malloc`, `qcommon/z_memman_pc.cpp`).
- Linked by the multiplayer client and dedicated server (`codemp/CMakeLists.txt:189`) and the single-player engine (`code/CMakeLists.txt:368`), which use them, and by the three renderers (`codemp/rd-vanilla/CMakeLists.txt:114`, `codemp/rd-rend2/CMakeLists.txt:120`, `code/rd-vanilla/CMakeLists.txt:57`), which link minizip and call nothing of it.

**Other code that inflates or deflates**, since the image work:

- Wuffs (`lib/wuffs`), in the renderers: its deflate, zlib, CRC-32 and Adler-32 modules decode the PNG files (`shared/rd-common/tr_image_codec.cpp`).
- stb_image_write (`lib/stb`), in the renderers: its own deflate compressor writes the PNG screenshots. It is the only compressed data the engine writes with a library.
- rend2 compiles `stb_image.h` (`codemp/rd-rend2/tr_image_stb.cpp`), which holds a zlib decoder of its own.

**What writes compressed data without a library**: the single-player saved games, run-length coded by the engine's own 100 lines (`SavedGame::compress` and `decompress`, `code/qcommon/ojk_saved_game.cpp:826`, switched by `sv_compress_saved_games`). The demos, the configuration files and the network messages are not compressed by a general-purpose coder.

**Other coders, another family, not in this survey**: the legacy network coder (`codemp/qcommon/huffman.cpp`, kept beside netcodec until the end of the plan), the MP3 decoder (`codemp/mp3code/`), the RoQ video decoder (`cl_cin.cpp`), the compressed bones of the ghoul2 animations (`G2_bones.cpp`).

**The data**: the game's four archives hold 22,789 files, 22,614 deflated and 175 stored, 1,222 MB in the archives and 1,866 MB once inflated. No ZIP64, no encryption, no other method. The largest file is 19.0 MB (a video); the largest map 18.0 MB. Three kinds of data, which the measurements keep apart:

| kind | files | in the archives | inflated | what deflate does to it |
|---|---|---|---|---|
| maps, models, TGA (`.bsp`, `.gla`, `.glm`, `.md3`, `.tga`) | 1,319 | 185 MB | 778 MB | shrinks it to a quarter |
| JPEG, PNG, MP3, video, WAV | 18,623 | 1,032 MB | 1,070 MB | nearly nothing: the data is compressed already, the decoder copies literal bytes |
| the rest (scripts, text, menus) | 2,847 | 5 MB | 17 MB | shrinks it to a third |

## 3. The candidates

Decoders of deflate, the format of every compressed entry:

| library | version read | form | decodes | streaming | compresses | licence | notes |
|---|---|---|---|---|---|---|---|
| zlib (today) | 1.2.8 (2013) | C sources | deflate, zlib, gzip | yes | yes | zlib | |
| zlib | 1.4 (4 November 2026) | C sources, CMake | the same | yes | yes | zlib | its change log since 1.2.8 lists fixes, among them "Fix several vulnerabilities in contrib/minizip" (1.4) and "Reject overflows of zip header fields in minizip" (1.3.1); no faster inflate (primary: ChangeLog) |
| zlib-ng | 2.3.3 (February 2026) | C sources with SIMD code chosen at run time, CMake, 34,800 lines | the same | yes | yes | zlib | zlib's interface under its own names or, as a build option, under zlib's; fuzzed by OSS-Fuzz (primary: README) |
| Chromium's zlib | the `main` branch of 14 November 2026 | a folder of Chromium, built by GN; its CMake file has no Windows build | the same | yes | yes | BSD 3-clause and zlib | not a released library: no version, no release notes |
| Cloudflare's zlib | the `gcc.amd64` branch of 14 November 2026 | C sources, CMake | the same | yes | yes | zlib | a fork of zlib 1.2.8, no release since |
| libdeflate | 1.26 (August 2026) | C sources with SIMD code chosen at run time, CMake; 8,600 lines for decompression and the CRC | deflate, zlib, gzip | **no**: "There is currently no support for streaming" | yes (whole buffer) | MIT | "whole-buffer DEFLATE-based compression and decompression"; needs a C11 compiler, Visual Studio 2019 16.8 or later, "MSVC is supported fairly well now too"; libFuzzer targets in its repository (primary: README, NEWS) |
| ISA-L (igzip) | 2.32.1 | C and x86 assembly, 60,500 lines for igzip and the CRC | deflate, zlib, gzip | yes | yes | BSD 3-clause | needs NASM 2.14.01 or later to build on x86-64 (primary: README) |
| miniz | 3.1.2 | four C files, 9,400 lines | deflate, zlib | yes | yes | MIT | also reads and writes ZIP archives |
| Wuffs (in the repository) | 0.4, tag `v0.4.0-alpha.12` | one C file, the modules chosen by macros | deflate, zlib, gzip, bzip2, LZMA, XZ, lzip, and the images | yes | **no** | MIT or Apache 2.0 | memory-safe by construction (image-libraries.md); no reader of ZIP archives |
| stb_image's decoder | 2.32 | one header | zlib, deflate | no | no | MIT or public domain | |
| zlib-rs | 0.6.8 | Rust, with a C interface | deflate, zlib, gzip | yes | yes | zlib | its authors measure it ahead of zlib-ng and Chromium's zlib (primary: their post of 2025); needs the Rust tool chain on every build machine: not built here |

Readers of ZIP archives, the container:

| library | version read | on top of | other methods in a ZIP | licence | notes |
|---|---|---|---|---|---|
| minizip (today) | of zlib 1.2.8 | zlib | none here | zlib | |
| minizip | of zlib 1.4 | zlib, or zlib-ng built with zlib's names | none | zlib | the same interface as today's; "Read multiple bytes instead of byte-by-byte in minizip unzip.c" (1.3) |
| minizip-ng | 4.2.2 | zlib or zlib-ng | bzip2, LZMA, XZ, Zstandard, PPMd, each with its library; encryption | zlib | 17,000 lines; another interface, with a layer that imitates minizip's; fuzzed by OSS-Fuzz (primary: README) |
| libzip | 1.12.1 | zlib | bzip2, LZMA, Zstandard, each with its library; encryption | BSD 3-clause | 22,000 lines; fuzzed by OSS-Fuzz |
| miniz | 3.1.2 | its own decoder | none | MIT | |
| PhysicsFS | 3.2.0 | its own copy of miniz's decoder | reads ZIP, 7z, ISO 9660 and the archives of other games (Quake, Doom, Build, Descent) | zlib | a file system for games, modelled on Quake III's by its author's description: search path, one tree from several archives; 25,200 lines |
| a directory reader of the engine's own | | any decoder above | none | | the directory of a ZIP is a list of fixed records: 100 lines in the benchmark (`tools/zipbench/pk3.h`, `archives.cpp`), for plain ZIP without ZIP64 |

Not measured:

- libarchive (3.8.11; tar, ZIP, 7z, RAR and others): its interface walks an archive entry by entry in order (primary: its examples), and the engine opens files by name.
- zlib-rs, and the Rust decoders (miniz_oxide, zune-inflate, fdeflate): a second tool chain.
- tinf, puff, lodepng's decoder: small decoders written for size, not speed.
- rapidgzip, pigz: several threads on one large gzip stream, not on 22,000 small entries.
- The compressors of other formats (Zstandard, LZ4, Brotli, LZMA): nothing the engine reads is in them. They would matter only if the engine wrote compressed data of its own (the demos are the candidate: their size at 100 messages per second is an open point of plan step 14 part 11), which is another question.

Licences: the project is under the GNU GPL version 2. The zlib, MIT and BSD licences can be combined with it; Wuffs is taken under its MIT licence.

What other engines read their archives with (secondary, from their repositories): ioquake3 a copy of minizip 1.01e on zlib 1.2.3 (2005); Godot zlib 1.3.2 with its `contrib/minizip`; id's own release of Quake III an `unzip.c` of 1998. No engine found that had replaced the decoder of its ZIP reader for speed. libdeflate is used where whole buffers are inflated: OpenEXR, libtiff (through GDAL: "35-50 %" faster), and 24 packages of Arch Linux depend on it.

## 4. Measurements

**Inflating**: every deflated entry of the four archives, from memory into a buffer of its size, as a reader that knows both sizes from the directory can do (ms: the sum over the 22,614 entries of the best of 5 runs; MB of output per second). Each output is checked against the CRC-32 of the directory with a CRC function of the benchmark's own.

| decoder | all, ms | MB/s | against zlib 1.2.8 | maps, models, TGA | JPEG, PNG, MP3, video | the rest |
|---|---|---|---|---|---|---|
| zlib 1.2.8 (this tree) | 2,296 | 812 | 1.00 | 864 ms | 1,412 ms | 20.1 ms |
| libdeflate 1.26 | 1,448 | 1,287 | 1.59 times as fast | 399 | 1,038 | 10.7 |
| Chromium's zlib | 1,636 | 1,139 | 1.40 | 485 | 1,139 | 12.3 |
| ISA-L 2.32.1 | 1,709 | 1,091 | 1.34 | 434 | 1,264 | 11.5 |
| zlib-ng 2.3.3 | 1,771 | 1,053 | 1.30 | 478 | 1,280 | 12.8 |
| Cloudflare's zlib | 1,823 | 1,022 | 1.26 | 530 | 1,279 | 13.4 |
| miniz 3.1.2 | 2,294 | 812 | 1.00 | 800 | 1,474 | 20.8 |
| Wuffs 0.4 alpha 12 | 2,310 | 807 | 0.99 | 665 | 1,628 | 17.9 |
| zlib 1.4 | 2,342 | 796 | 0.98 | 893 | 1,428 | 20.6 |
| stb_image 2.32 | 2,703 | 689 | 0.85 | 953 | 1,728 | 22.2 |

No decoder fails and no output has a wrong CRC. On the data deflate shrinks (maps, models), libdeflate is 2.2 times as fast as zlib 1.2.8 and Wuffs 1.3 times; on the data that is compressed already, which is most of the bytes, the gaps are small (libdeflate 1.36 times) and Wuffs is 13 % slower than zlib 1.2.8. A first run of the same benchmark gave the same figures within 2 %.

**The CRC-32** of the same 1.87 GB (the best of 5 runs per entry):

| library | ms | GB/s |
|---|---|---|
| miniz 3.1.2 | 2,638 | 0.7 |
| zlib 1.2.8 (this tree) | 835 | 2.2 |
| zlib 1.4 | 215 | 8.7 |
| Wuffs 0.4 alpha 12 | 87 | 21 |
| Chromium's zlib, Cloudflare's zlib | 83, 82 | 23 |
| zlib-ng 2.3.3, libdeflate 1.26, ISA-L 2.32.1 | 21 | 88 |

minizip computes it on every byte it delivers: with zlib 1.2.8 it adds 36 % to the inflating, with the three fastest under 2 %.

**Archive readers, end to end**: the four archives opened and their entries listed (as `FS_LoadZipFile`; the best of 5), then every file read whole from the disk, which is in the system's cache (as `FS_ReadFile`; the best of 3 passes over the 22,789 files). Each reader's output is checked against the directory's CRC values. "direct" is the benchmark's own directory reader: the compressed bytes of an entry read in one call, inflated in one call, the CRC checked with the library's function.

| reader | open and list, ms | read all, ms | MB/s | against the game |
|---|---|---|---|---|
| minizip + zlib 1.2.8, driven as the engine drives it | 80.6 | 3,698 | 505 | 1.00 |
| direct + libdeflate | 3.5 | 1,672 | 1,116 | 2.21 times as fast |
| direct + ISA-L | 3.6 | 1,936 | 964 | 1.91 |
| direct + Chromium's zlib | 3.5 | 1,935 | 964 | 1.91 |
| direct + zlib-ng | 3.6 | 2,002 | 932 | 1.85 |
| minizip-ng 4.2.2 + zlib-ng | 6.9 | 2,149 | 868 | 1.72 |
| minizip of zlib 1.4 + zlib-ng | 43.4 | 2,370 | 787 | 1.56 |
| direct + Wuffs | 3.5 | 2,586 | 721 | 1.43 |
| libzip 1.12.1 + zlib 1.4 | 42.3 | 3,049 | 612 | 1.21 |
| minizip of zlib 1.4 + zlib 1.4 | 42.3 | 3,112 | 599 | 1.19 |
| direct + zlib 1.2.8 | 3.8 | 3,411 | 547 | 1.08 |
| PhysicsFS 3.2.0 | 284.4 | 3,410 | 547 | 1.08 |
| miniz 3.1.2 | 8.3 | 5,242 | 356 | 0.71 |

What the table shows beyond the decoders: the engine's way of opening an archive costs 81 ms for the four (it reads each of the 22,789 directory entries twice, a few bytes at a time) where one read of the directory costs 3.5 ms; minizip itself, with the same zlib, costs 8 % over the direct reader; PhysicsFS builds a tree of every folder when it mounts (284 ms). Two earlier runs of this benchmark gave the same order, each figure within 5 %.

**The memory** of a decoder's state: libdeflate 11.6 KB, kept for the process; zlib 1.2.8 7.1 KB per open file, plus a window of 32 KB when a read does not end the entry, plus minizip's 16 KB buffer; Wuffs 41.8 KB.

**What it means for the game.** The profile taken after the image work (ten player models, the driver's compression off, 18.8 ms per model) had 5.7 % of its samples in `inflate_fast` and 3.6 % in `crc32`: 1.75 ms per model. Estimated from the ratios above on JPEG data, not measured in the client: with libdeflate 0.8 ms (about 0.95 ms less per model, 5 %), with zlib-ng 1.0 ms (4 %), with Wuffs 1.3 ms (2.5 %). A map is where the decoders differ most (2.2 times on a `.bsp`): the largest map inflates in 20 ms with zlib 1.2.8 plus 8 ms of CRC, in 9 ms with libdeflate, of a map load of 0.89 s. The archives' opening is paid at every start and at every restart of the file system (a connection to a pure server): 81 ms to 3.5 ms with the game's four archives, more on an installation with hundreds of pk3 files (community: Quake3e's issue 12 reports cold starts of 2.2 and 5.0 s with 1,000 pk3 files, by the way they are indexed). All of the game's data, read once: 3.7 s to 1.7 s.

## 5. Options

| option | reading (CPU) | memory | build | work in the engine | what else |
|---|---|---|---|---|---|
| A: a directory reader of the engine's own + libdeflate | all the assets in 1.67 s instead of 3.70 (2.2 times); archives opened in 3.5 ms instead of 81; a seek in a file becomes free | an entry read in pieces is inflated whole at its opening: its size while it is open (a video up to 19 MB, a music track up to 4.3 MB in the game's assets); 11.6 KB of decoder | `lib/libdeflate` at 1.26, 8,600 lines of C for decompression and the CRC, no assembler; `lib/zlib` and `lib/minizip` removed, 15,800 lines | the largest: one new shared file (the directory, the reading, the library), and the minizip calls of each `files.cpp` replaced (about 20 lines in the multiplayer one); the reader parses files that come from servers, so it is written with every bound checked and given a fuzz target, as netcodec's decoders | libdeflate also compresses whole buffers, should the engine write compressed data one day; MSVC and GCC not measured here |
| B: the same reader + Wuffs | 2.59 s (1.4 times); on the players' images and sounds its decoder is 13 % slower than zlib 1.2.8, the gain is the CRC and the reader | 41.8 KB of decoder; streaming possible, so no entry need be inflated whole | no new library: `lib/wuffs` compiled a second time, into the engine and the dedicated server, with its deflate and CRC-32 modules; `lib/zlib` and `lib/minizip` removed | as A | one library for the images and the archives; memory-safe by construction, which fits files sent by servers; no compressor |
| C: minizip of zlib 1.4 + zlib-ng 2.3.3 | 2.37 s (1.56 times); archives opened in 43 ms | as today | `lib/zlib` replaced by zlib-ng (34,800 lines, its own CMake build with the SIMD variants), `lib/minizip` brought to 1.4 with the engine's allocator change made again | the smallest: the engine's code does not change | the fixes of 13 years of minizip; streaming and compression kept; the archives' opening only halves |
| D: minizip-ng 4.2.2 + zlib-ng | 2.15 s (1.72 times); archives opened in 7 ms | not measured | two libraries, 51,800 lines | the calls of each `files.cpp` rewritten for another interface | could read entries in bzip2, LZMA, XZ or Zstandard if their libraries were added: methods no pk3 uses |
| F: a directory reader of the engine's own + zlib-ng 2.3.3 | 2.00 s (1.85 times) with every file inflated in one call; archives opened in 3.6 ms; a file read in pieces is inflated as it is read, as today | not measured; zlib's kind of state per open file | `lib/zlib` replaced by zlib-ng (34,800 lines, its own CMake build with the SIMD variants), `lib/minizip` removed | as A | streaming and compression kept; reading in pieces through this reader is not measured (minizip on zlib-ng, 16 KB at a time: 2.37 s) |
| E: zlib 1.4 and its minizip, built with the project on Linux too | 3.11 s (1.19 times, all of it the CRC and minizip's reads) | as today | `lib/zlib` and `lib/minizip` replaced by their present versions | none | the least change that follows the owner's rule and brings the fixes |

Left out: ISA-L (1.91 times, but NASM on every build machine, the reason libjpeg-turbo was not taken, and 60,500 lines); Chromium's zlib (1.91 times, but a folder of a browser with no release); miniz and PhysicsFS (slower than today, or no faster); libzip (the speed of option E with another interface).

Recommended: A. Speed is the first wish and it is the fastest by a clear margin, with the smallest library; the code it adds to the engine is small and replaces 15,800 lines of libraries; its one cost, the entry inflated whole when it is opened for reading in pieces, is memory the size of a video or a music track for the time it plays, and it makes seeking free where today a seek backwards inflates the file again. B is the choice if one library for the images and the archives, or its kind of safety, counts more than speed: it reads the players' images and sounds no faster than today. C is the choice if the engine's file code must not be touched.

To decide with it:

- A or B: whether an entry may be inflated whole at its opening whatever its size (a pk3 from a server can hold a larger file than the game's 19 MB), or only up to a limit above which the reader refuses or streams (streaming needs B's decoder or a second one).
- Whether the reader checks the CRC-32 of every file it delivers, as minizip does (21 ms for all the game's data with libdeflate, 87 ms with Wuffs).
- The two unused `zlib.h` inclusions and the renderers' link to minizip go with any option.
- The single-player saved games' run-length coder is the engine's own code and uses no library: kept as it is unless asked.

## 6. The owner's choices

- 14 November 2026: the survey asked for, with downloads allowed; many libraries to be looked at, speed first, several formats in one library if possible.
- 14 November 2026: the streaming is kept (owner: the safe way, to avoid freezes as far as possible). Inflating a file whole at its opening was estimated at 18 ms for the largest video and 4 ms for the largest music track, more from a cold hard disk; with the streaming that cost stays spread over the reading. That rules out libdeflate (option A) and leaves zlib-ng, the fastest released decoder measured that streams without an assembler: option F, the engine's own directory reader with zlib-ng, or option C, minizip of zlib 1.4 with zlib-ng.
- 14 November 2026: option F, the engine's own directory reader with zlib-ng; `lib/minizip` goes with `lib/zlib`. The reader checks the CRC-32 of every file it delivers; the code is shared between the multiplayer and the single-player engines as far as it can be.
- Written the same day (`shared/qcommon/q_pk3.cpp`, the only file that knows zlib-ng, one copy for both engines; `lib/zlib` and `lib/minizip` removed; [../progress/step-14-fixed-tick.md](../progress/step-14-fixed-tick.md) section 16). Measured with the reader built alone (`tools/zipbench`, `check`): all of the game's files in 2.08 s, 1.78 times as fast as minizip on zlib 1.2.8, and 2.27 s read 8 KB at a time; the four directories in 0.5 ms. It is above the 2.00 s of section 4's direct reader, which inflated each file in one call from its whole compressed data: the engine's reader takes the archive 64 KB at a time, as a file read in pieces needs. minizip computed the CRC-32 and the engine never looked at it: a file whose CRC-32 is wrong is now refused.

## 7. What was not read or verified

- One machine, one compiler (clang of llvm-mingw, `-O3`). GCC, the server's compiler, and MSVC, the Windows client's, are not measured; libdeflate's README says its work goes first to GCC and Clang.
- The times in the client are estimated from a profile taken before this survey and from the ratios; nothing was integrated or profiled.
- The game's own archives only: pk3 files made by players or sent by servers (other tools, ZIP64, odd flags, entries stored with a data descriptor) are not in the corpus, and the benchmark's directory reader refuses what is not plain ZIP.
- No candidate was given malformed data. The safety columns are what each project states.
- The fixes listed in zlib's change log for minizip were not read one by one to see which apply to the reading side the game uses.
- Chromium's zlib was built from its files with its x86-64 settings, outside its own build system.
- Reading the archive through a memory mapping instead of `fread` was not tried.
- zlib-rs and libarchive were not built.

## 8. Sources

- zlib, change log of 1.3 to 1.4: https://github.com/madler/zlib/blob/v1.4/ChangeLog
- zlib-ng, README and releases: https://github.com/zlib-ng/zlib-ng
- libdeflate, README and NEWS: https://github.com/ebiggers/libdeflate
- ISA-L, README: https://github.com/intel/isa-l
- miniz: https://github.com/richgel999/miniz
- Wuffs, README and its benchmarks page (deflate 1.4 times zlib 1.2.11 on 100 KB of text, 0.8 on 1 KB; version 0.3): https://github.com/google/wuffs/blob/main/doc/benchmarks.md
- Chromium's zlib: https://chromium.googlesource.com/chromium/src/third_party/zlib
- Cloudflare's zlib: https://github.com/cloudflare/zlib
- zlib-rs, "zlib-rs is faster than C" (its authors' measure): https://trifectatech.org/blog/zlib-rs-is-faster-than-c/
- minizip-ng: https://github.com/zlib-ng/minizip-ng
- libzip: https://github.com/nih-at/libzip
- PhysicsFS: https://icculus.org/physfs/
- libarchive, examples: https://github.com/libarchive/libarchive/wiki/Examples
- Comparisons by others (community): zlib-ng's discussions 871 and 2351 (libdeflate and ISA-L ahead of zlib-ng on decompression, about three times stock zlib): https://github.com/zlib-ng/zlib-ng/discussions/871 , https://github.com/zlib-ng/zlib-ng/discussions/2351
- ioquake3's zlib and its minizip: https://github.com/ioquake/ioq3/blob/main/code/zlib/zlib.h , https://bugzilla.icculus.org/show_bug.cgi?id=3780
- Godot's third-party list: https://github.com/godotengine/godot/blob/master/thirdparty/README.md
- Quake3e, issue 12 (the time to index 1,000 pk3 files): https://github.com/ec-/Quake3e/issues/12
- libdeflate in libtiff (GDAL pull request 3068) and its dependents in Arch Linux: https://github.com/OSGeo/gdal/pull/3068 , https://archlinux.org/packages/extra/x86_64/libdeflate/
- Debian 12, the package `zlib1g-dev`: https://packages.debian.org/bookworm/zlib1g-dev
