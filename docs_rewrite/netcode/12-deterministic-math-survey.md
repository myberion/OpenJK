# 12. Math functions identical on every platform: libraries, what other projects do, and the options for step 14

Written on 12 November 2026, during plan step 14, before part 3 (the tick loop). The owner asked on 12 November 2026 that Linux and Windows give exactly the same results everywhere (rounding, random numbers, math functions, code generation), even where 1.01's own builds differed, and that the math library be the fastest and the most precise available, an external library allowed, after a real survey. This page records why the results differ today, what the game calls, what other projects do, the candidate libraries with their accuracy, speed, licence and portability, the options with their costs, and the tests that settle the choice. The measurements that started it are in [progress/step-14-fixed-tick.md](progress/step-14-fixed-tick.md) section 10.

Each source is labelled as in 10 and 11:

- **primary**: the project's own documentation, source code, paper or talk slides; a published measurement by the people who made it;
- **secondary**: a summary written by someone else;
- **community**: forum posts, issue comments, unofficial ports.

## 12.1 Summary

- The C library's transcendental functions (`sin`, `cos`, `atan2`, `acos`, `log`, `pow`, ...) are not specified to the bit: IEEE 754-2019 only recommends correct rounding, C does not require it. Measured here: glibc and the UCRT differ on every transcendental function tested (step 14 section 10). Documented: the UCRT ships with Windows, so its results can change with the Windows version (Microsoft, primary), and glibc picks a code path by CPU at run time, so the same Linux binary can answer differently on two processors (Gladman, Innocente, Mather, Ozaki, Zimmermann, primary).
- Games that need identical results everywhere stop calling the C library: Factorio (2014), Box2D v3 and Jolt Physics write their own approximations with no fused multiply-add; Rapier and Avian call a portable software library (a Rust port of musl); Java's `StrictMath` requires the results of `fdlibm` (all primary).
- A newer answer exists since 2022 and reached production in 2025 and 2026: **correctly rounded** functions. A correctly rounded function returns the floating-point number nearest the exact result, so every correct implementation returns the same bits, whatever the compiler, the CPU, fused multiply-add or the code path. glibc 2.42 and 2.43 took CORE-MATH's code for many functions; Microsoft's compiler ships LLVM libc's correctly rounded functions behind `/Zc:cmath` (experimental, compiler 19.52, 14.52 build tools, November 2026); V8 moved to LLVM libc (all primary except V8, secondary).
- Two libraries cover every function the game calls with correct rounding or close to it: **CORE-MATH** (MIT; every function correctly rounded; C code written for GCC and Clang: 128-bit integers, builtins, attributes; its double functions call fused multiply-add without a fallback) and **LLVM libc** (Apache 2.0 with the LLVM exception, which addresses GPL v2 combinations; header-only C++ since May 2026; built by MSVC, GCC and Clang, with code paths with and without fused multiply-add; every float function and the double `sin`, `cos`, `tan`, `acos`, `asin`, `log` correctly rounded, double `pow` since 19 September 2026; double `atan` and `atan2` within 1 ulp).
- Speed: the published numbers put correctly rounded float functions at 12 to 27 cycles of reciprocal throughput, as fast as glibc or faster, but with fused multiply-add enabled; nobody publishes numbers for the baseline x86-64 (SSE2) this branch builds for, nor for 32-bit x86. The game calls these functions a few tens of thousands of times per second at most (estimate), so the library's speed is a fraction of a percent of a core either way; it must still be measured (12.9).
- Recommended: LLVM libc's header-only functions, vendored at a fixed commit and called under names of our own, with the float functions used wherever the argument is a float; CORE-MATH used in the test program as an independent reference. To confirm with the tests of 12.9 before integrating. The owner decides (12.8).

## 12.2 Why the results differ today

Measured on this branch (step 14 section 10, `tools/libm_hash.c`, about 2 million inputs per function in the game's ranges):

- glibc (Debian 12, GCC 12 and Clang 14 alike) and the UCRT (llvm-mingw clang) give different hashes for 16 of 17 functions; `fmodf`, exact by definition, is the only identical one.
- MSVC x64 gave the UCRT's hashes except for `pow`. Step 14 section 10 guessed that MSVC does not call the UCRT's `pow`; measured since (12.10): `cl` links the static UCRT (`/MT`, `libucrt.lib`, which follows the compiler version) by default, and its `pow` differs from the one of `ucrtbase.dll` (which follows the Windows version) that clang linked; built with `/MD`, MSVC gives clang's `pow` hash.
- Game modules built by clang and by MSVC differ on 1 scenario of 40 (the flechette's alternate fire), on the same engine and the same UCRT: the compiler changes which C library functions are called (the clang module imports `acosf`, `floorf`; the MSVC module imports `floor`, `sqrtf` and no `acosf`).

Documented:

- **The UCRT depends on the Windows version.** "Windows maintains and deploys the UCRT, so math function results can vary by Windows version" (Microsoft, `/Zc:cmath` documentation, primary). Gladman et al. (August 2026, primary): with MSVC, `/MT` uses `libucrt.lib`, which depends only on the compiler version, and `/MD` uses `ucrtbase.dll`, which depends on the operating system; the two give different results for `erff` and other functions. This branch's CMake files do not set the runtime, so MSVC targets use CMake's default `/MD`: a Windows player's results depend on their Windows version.
- **glibc depends on the CPU.** "Some libraries have a runtime dispatcher which invokes different source code for different cpus, for example using the fused multiply-add, or extensions SSE, AVX, AVX2, or AVX512. This is the case for the GNU libc and for the Intel Math library" (Gladman et al., primary). A Linux server's results can therefore change when the VPS moves to another processor.
- **glibc changes with its version.** glibc 2.42 replaced many float functions and 2.43 seven double functions by CORE-MATH's (CORE-MATH site, primary); a patch series of September 2026 does the same for `sinf`, `cosf` and `sincosf` (glibc patchwork, primary). Debian 12 has glibc 2.36; a newer distribution gives other results.
- **The compiler matters too.** Fused multiply-add contracted from `a * b + c` changes the rounding: GCC contracts by default (`-ffp-contract=fast` in its GNU modes), Clang within an expression (`-ffp-contract=on`), MSVC only with `/fp:contract` or `/fp:fast` since Visual Studio 2022 (Microsoft, primary). None of this branch's builds enables an instruction set with fused multiply-add (no `-march`, `/arch:SSE2` on 32-bit), so no build contracts today; one `-march=native` would. Compilers also evaluate calls with constant arguments themselves (GCC with MPFR, which is correctly rounded; Clang with the host's library: from memory of LLVM's `ConstantFolding.cpp`, not verified here) and may replace a pair `sin`/`cos` by `sincos`; calls under names of our own avoid both.

## 12.3 What the game calls

Inventory of 12 November 2026 (step 14 section 10), completed here. `sqrt`, `floor`, `ceil` and `fmod` are exact by definition (correctly rounded in IEEE 754 or exact), so identical everywhere.

| code | calls | runs on |
|---|---|---|
| game module (`codemp/game`) | `sin` 25, `cos` 15, `tan` 2, `acos` 2, `atan2` 1 | server; `bg_*` files also in cgame (prediction) |
| cgame (`codemp/cgame`) | `sin` 58, `cos` 24, `tan` 7, `atan2` 5, `tanf` 2, `powf` 2, `atan` 2, `sinf` 1 | client; mostly effects, its `bg_*` files are the game module's |
| `shared/qcommon/q_math.c` | `cosf` 6, `cos` 6, `sinf` 4, `atan2f` 2, `acos` 2, `logf`, `asinf`, `acosf`, `atan2` | every module and the engine |
| dedicated server's ghoul2 (`codemp/rd-dedicated`) | `pow` 4, `sin` 5, `cos` 5 (model traces, bolts, bone decay) | server |
| `codemp/server/sv_gameapi.cpp:1774-1780` | `sin`, `cos`, `atan2` (the legacy `trap` calls) | server, for a module that uses them |
| `codemp/botlib/be_aas_reach.cpp:3856` | `tan` (grapple reachability, at map load) | server; a different bit can change a bot's routes |
| collision (`codemp/qcommon/cm_*`) | `sqrt` only | both |

Most calls pass a `float` to the double function (`sin(x)` with `float x`, the result stored in a `float`): the float function (`sinf`) gives the correctly rounded float directly, faster, where the double function rounds twice. Some arguments really are double, for example `sin(pVeh->m_ucmd.serverTime * 0.001)` in `game/FighterNPC.c:791` (a time in seconds that a float would hold to about 0.1 ms after a day) and `sin((150 + pml.frametime) * 0.003)` in `game/bg_pmove.c:575`: these need the double functions. So both precisions are needed: float `sinf`, `cosf`, `tanf`, `asinf`, `acosf`, `atanf`, `atan2f`, `logf`, `powf`; double `sin`, `cos`, `tan`, `acos`, `atan`, `atan2`, `pow`. Each double call is classified (float argument or not) when the library is integrated.

## 12.4 What other projects do

| project | what it does | source |
|---|---|---|
| Factorio (Wube, 2014) | a replay recorded on one operating system desynchronised at once on another; "basic trigonometric functions (sin, cos, asin, atan, etc.) are not guaranteed to give same results across different platforms"; Kovarex wrote custom implementations giving the same results on Windows, OS X and Linux, "not about precision"; today all of the mod API's trigonometric, hyperbolic, exponential and logarithmic functions are custom | primary: Friday Facts #36 and #52; Lua API documentation |
| Box2D v3 (Erin Catto, 2024) | cross-platform determinism since 3.1, tested on MSVC, GCC and Clang, x64 and ARM; `atan2f` differs between platforms, `sinf`/`cosf` matched but are replaced anyway ("I don't trust this result will hold"); own approximations `b2Atan2` (about 0.0023 degree) and `b2ComputeCosSin`; fused multiply-add disabled (`-ffp-contract=off`), never fast-math; `sqrtf` is deterministic; "no noticeable performance impact" | primary: box2d.org "Determinism", API documentation |
| Jolt Physics | "this file exists because std::sin etc. are not platform independent": `Sin`, `Cos`, `Tan`, `ASin`, `ACos`, `ATan`, `ATan2` are its own SIMD polynomials; `CROSS_PLATFORM_DETERMINISTIC` turns fused multiply-add off, `/fp:precise` and `-ffp-contract=off`; a CI job compares simulation hashes on x64, ARM64, RISC-V and WASM | primary: `Jolt/Math/Trigonometry.h`, build documentation; the cost of about 10 % is from a third-party pull request (community) |
| Rapier, Avian (Rust physics) | `enhanced-determinism` routes the transcendental functions through `libm`, a software library (port of musl), for bit-identical results on every IEEE 754 platform | primary: rapier.rs documentation |
| Java `StrictMath` | `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `exp`, `log`, `pow`, ... must give the results of `fdlibm` 5.3, "to help ensure portability" | primary: Java SE API documentation |
| Microsoft C++ (2026) | `/Zc:cmath` replaces the UCRT's functions by LLVM libc's, statically linked, "correctly rounded results in all rounding modes", "stable across operating system versions and hardware architectures"; experimental, off by default, 14.52 build tools | primary: Microsoft Learn, C++ Team Blog (10 and 16 September 2026) |
| glibc (2025 to 2026) | CORE-MATH's correctly rounded code for many float functions (2.42) and seven double functions (2.43) | primary: CORE-MATH site |
| V8 (Chrome's JavaScript) | most of `Math` moved to LLVM libc's correctly rounded functions | secondary: a downstream pull request |
| a C++ port of a 2D game (supermariowar reference) | CORE-MATH's `sinf`, `cosf`, `atan2f` unmodified, built with `-ffp-contract=off` | community: GitHub pull request |

Standards: IEEE 754-2019 recommends correctly rounded functions without requiring them; C23 reserves the `cr_` prefix for correctly rounded versions of the `<math.h>` functions (section 7.33.8, primary); WG21 paper P3864R0 (2025) proposes the same for C++.

Two schools, then. Games written before correct rounding was practical (Factorio, Box2D, Jolt) wrote approximations: fast, small, deterministic, but less precise than the C library (Box2D's `atan2` is about 0.0023 degree off, about 340 float ulp near 1 radian; computed here from its documented bound). Since 2025, correctly rounded libraries give determinism and the best possible precision at the same time, which is what the owner asked.

## 12.5 Candidate libraries

| library | covers the game's functions | accuracy | licence | compilers and targets | fused multiply-add | state |
|---|---|---|---|---|---|---|
| **CORE-MATH** (Inria, Sibidanov, Zimmermann et al.) | all, float and double, plus many more | correctly rounded for every function, all rounding modes; float univariate functions proven by exhaustive test, bivariate and double ones checked on known hard cases | MIT | C for GCC and Clang only: `__int128` or `_BitInt(128)` (unavailable on GCC 32-bit x86), `__builtin_*`, `__attribute__`, inline assembly as a fallback; no MSVC support (a third-party binary64 snapshot builds on Windows with Visual Studio's Clang tools); tested on x86-64 Linux | float functions compute in double and need none; double functions call `__builtin_fma` on their main path with no fallback (read in `binary64/sin/sin.c`): in a build without the instruction it becomes a call to the C library's `fma`, which uses the instruction when the CPU has it (the UCRT here, inferred from the speed, 12.10) and emulates it otherwise; their correctness then rests on that `fma` | active; its code is in glibc, AMD LibM, Houdini 22 |
| **LLVM libc** (Google, with Microsoft) | all; header-only since May 2026 (`libc/shared/math.h`, 53 of 53 functions done) | every float function correctly rounded (exhaustive test) except rare `powf` inputs; double `sin`, `cos`, `tan` verified correctly rounded for all rounding modes (joint work with Zimmermann and Lefèvre, June 2026), `acos`, `asin`, `log` correctly rounded; `pow` correctly rounded since 19 September 2026 (main, after release 22.1.8, which returned -1 for one huge input); double `atan` and `atan2` 1 ulp | Apache 2.0 with LLVM exception (explicit clause for GPL v2 combinations) | C++17 headers; GCC, Clang, and MSVC (fixes merged 2025 and 2026; Microsoft builds it into its runtime); Windows x86-64 listed, 32-bit x86 not listed (needs a test); a 128-bit integer class replaces `__int128` where missing | separate paths with and without the instruction (Dekker products without it) | active, two companies maintain it |
| RLIBM (Rutgers) | float only: 16 functions (`sinf`, `cosf`, `tanf`, `asinf`, `acosf`, `atanf`, logs, exps, ...), no `atan2f`, no `powf`, no double | correctly rounded for every format up to 32 bits and all rounding modes | not stated on the pages read | research prototype | none needed | papers 2021 to 2025; not used in production |
| SLEEF | all, float and double, scalar and vector | 1.0 ulp or 3.5 ulp classes; dedicated functions return bit-identical results on every architecture | Boost 1.0 | MSVC supported on Windows, GCC on Linux, Clang experimental on Linux; no 32-bit x86 listed; generated sources (CMake) | with and without versions of the consistent functions | active; built for vector code |
| musl, FreeBSD msun, OpenLibm, fdlibm (and Arm's optimized-routines in glibc and musl) | all, as portable C | about 0.5 to 1.6 ulp (table 12.6); OpenLibm and FreeBSD double `pow` up to 636 ulp | MIT, BSD, ISC | portable C (fdlibm is C89); identical only if compiled alike: same source, no contraction, SSE2 | none | stable |
| CRlibm | double only, `pow` for round to nearest only | correctly rounded | LGPL | old (2000s), not maintained | | superseded by CORE-MATH |
| own approximations (Box2D, Jolt, Factorio) | what we write | chosen (Box2D `atan2` about 0.0023 degree) | ours | anything | must be off | |
| the C library with `/Zc:cmath` | all | LLVM libc's | | MSVC only, C++23, experimental | | does not cover GCC or Clang |

## 12.6 Accuracy of the functions the game calls

Largest error in ulp from Gladman, Innocente, Mather, Ozaki and Zimmermann, "Accuracy of Mathematical Functions in Single, Double, Double Extended, and Quadruple Precision", August 2026 (primary). Float univariate values are exact maxima (exhaustive search); bivariate and double values are the largest found. 0.500 means correctly rounded. CORE-MATH, the reference of the paper's search, is correctly rounded everywhere (its claim). GNU libc 2.44, LLVM libc 22.1.8 (before the September `pow` change), Microsoft's library of Visual Studio 2026 (`/MT`), Musl 1.2.6, OpenLibm 0.8.7.

Float:

| function | GNU libc | LLVM libc | MSVC | Musl | OpenLibm |
|---|---|---|---|---|---|
| `sinf` | 0.561 | 0.500 | 0.530 | 0.501 | 0.501 |
| `cosf` | 0.561 | 0.500 | 0.530 | 0.501 | 0.501 |
| `tanf` | 0.500 | 0.500 | 0.502 | 0.800 | 0.800 |
| `asinf` | 0.500 | 0.500 | 0.846 | 0.743 | 0.743 |
| `acosf` | 0.500 | 0.500 | 0.653 | 0.918 | 0.918 |
| `atanf` | 0.500 | 0.500 | 0.501 | 0.853 | 0.853 |
| `atan2f` | 0.500 | 0.500 | 0.584 | 1.55 | 1.55 |
| `logf` | 0.818 | 0.500 | 0.562 | 0.818 | 0.888 |
| `powf` | 0.817 | 0.501 | 1.00 | 0.817 | 0.970 |

Double:

| function | GNU libc | LLVM libc | MSVC | Musl | OpenLibm |
|---|---|---|---|---|---|
| `sin` | 0.516 | 0.500 | 0.799 | 0.831 | 0.831 |
| `cos` | 0.516 | 0.500 | 0.897 | 0.834 | 0.834 |
| `tan` | 0.619 | 0.500 | 1.32 | 1.02 | 1.02 |
| `acos` | 0.523 | 0.500 | 0.932 | 0.930 | 0.930 |
| `atan` | 0.523 | 1.00 | 0.863 | 0.861 | 0.861 |
| `atan2` | 0.524 | 0.501 | 1.00 | 1.55 | 1.55 |
| `pow` | 0.523 | wrong sign on one huge input (fixed 19 September 2026) | 0.505 | 0.525 | 636 |

The double rows `sin`, `cos` and `tan` have one empty cell in the paper's table (a library without the function, after these columns); the columns used here are the ones before it.

Precision only matters here for its own sake: none of these errors is visible in game. What matters is that every machine computes the same bits, which correct rounding gives by construction and the other libraries give only when the same code is compiled alike.

## 12.7 Speed

Published, all measured with fused multiply-add enabled:

- LLVM libc against glibc (LLVM libc documentation, primary; Ryzen 1700, Clang 12 to 14, `FMA`), reciprocal throughput in cycles: `sinf` 12 against 25, `cosf` 13 against 32, `tanf` 16 against 50, `asinf` 23 against 27, `acosf` 24 against 29, `atanf` 27 against 29, `logf` 12 against 10, `expf` 9 against 7. No double figures.
- CORE-MATH in glibc (glibc patchwork, primary): `cospif` reciprocal throughput 20.95 to 11.50 cycles on a Ryzen 9 5900X; the new `sinf` slower than glibc's by less than 3 cycles on the Box2D traces (Zimmermann, September 2026).
- Correct rounding costs little on average because more than 99.99 % of inputs return from the fast path; the rare slow path is bounded (LLVM libc aims at 10 to 20 times the average at worst) (Microsoft C++ Team Blog guest post, primary; LLVM libc documentation, primary).

Not published: any figure for the baseline x86-64 (SSE2, no fused multiply-add) this branch builds for, or for 32-bit x86. Without the instruction, LLVM libc's double functions use Dekker products and CORE-MATH's double functions call the C library's `fma`; measured on this machine in 12.10.

Share of a core (estimate, to measure): with 32 players, about 100 calls per player per 20 Hz frame (view vectors, movement, traces, the saber) give 64,000 calls per second; at 50 cycles a call, 3.2 million cycles per second, about 0.1 % of a 3 GHz core. A library twice as slow as another changes the server's CPU time by about 0.1 %. Speed therefore separates the candidates only if one is far slower.

Memory (estimate, to measure): code and tables of the 16 functions, a few tens of kilobytes per binary (CORE-MATH's double `sin` holds 170 128-bit table entries, 2.7 KB); bytes on the wire: none, for every option (the format does not change, only how values are computed).

## 12.8 Options

| option | identical results | precision | CPU (estimate) | memory | work | risks |
|---|---|---|---|---|---|---|
| **A. CORE-MATH**, vendored, ported to MSVC and 32-bit x86 | yes, by construction (correctly rounded) | the best possible, every function | measured in 12.10: float and most double functions as fast as or faster than LLVM libc; double `tan` and `pow` slower; their double functions call the C library's `fma`, emulated on a CPU without the instruction | tens of KB | port each of the 16 functions: a portable 128-bit multiply (`_umul128` on MSVC x64, 32-bit products elsewhere), the builtins and attributes as macros; keep the port in step with upstream fixes | a port error breaks correct rounding (caught by comparing with the upstream build on Linux); MSVC and 32-bit x86 not supported upstream |
| **B. LLVM libc**, header-only, vendored at a fixed commit (recommended) | yes for every correctly rounded function; double `atan` and `atan2` (1 ulp) identical as long as every build takes the same path (no fused multiply-add, no contraction), which is the case today | correctly rounded everywhere except double `atan` and `atan2` (1 ulp) and rare `powf` inputs (0.501 ulp); the float functions replace double calls with float arguments | measured in 12.10: float functions as the C library, double functions 2 to 5 times its time (10 to 21 ns on x64) | tens of KB | one C++17 file per binary exposing `extern "C"` functions under our names, the calls replaced; the headers the 16 functions include vendored (about 100, an estimate) | 32-bit x86 not tested upstream; MSVC support is recent (2025 to 2026) |
| C. B, with CORE-MATH's double `atan` and `atan2` | yes | correctly rounded everywhere | as B | as B | B plus the port of two CORE-MATH functions | as A for those two |
| D. musl or fdlibm, vendored | yes if compiled alike (same source, no contraction, SSE2) | 0.5 to 1.6 ulp (OpenLibm `pow` 636) | as the C library | tens of KB | small (portable C) | determinism rests on build discipline, not on the result's definition |
| E. own approximations (Box2D, Jolt, Factorio) | yes, with contraction off | low (about 0.0023 degree for Box2D's `atan2`), chosen per function | the fastest (a few cycles) | a few KB | write and test 16 functions | changes the game's values more than any library; against the owner's request for the most precise |
| F. SLEEF consistent functions | yes | 1.0 ulp | vector-oriented; scalar speed not published | larger build | its CMake and generated sources | no 32-bit x86, Clang on Linux experimental |

In every option:

- The game, cgame, the engine and the library are compiled with `-ffp-contract=off` (GCC, Clang) and keep `/fp:precise` (MSVC; no contraction since Visual Studio 2022). No effect today (no build enables fused multiply-add); it keeps a future `-march` from changing the game's arithmetic.
- The functions are called under names of our own (not `sin`), so that no compiler evaluates or replaces them with its own (constant folding, `sincos`, MSVC's vector math library).
- 32-bit x86 keeps SSE2 arithmetic (`-mfpmath=sse` with GCC, `/arch:SSE2` with MSVC, already set).
- The engine uses the library too: ghoul2's model traces and bolts, the botlib's `tan`, the `trap` functions.

**Owner's choice (12 November 2026): B, LLVM libc**, after the measurements of 12.10 (identical bits on every build and on CORE-MATH's). 32-bit x86 is not a requirement: it is kept while it costs nothing.

Why B: it reaches nearly the precision of A with far less risk. Every function the game calls is correctly rounded except double `atan` and `atan2`, which the game mostly calls with float arguments (they become `atanf` and `atan2f`, correctly rounded); it is built upstream by MSVC, GCC and Clang (Microsoft ships it in its runtime), and it has a path without fused multiply-add, which this branch's builds use. A gives the last fraction of an ulp on two functions (no difference at all on the game's inputs, 12.10) for a port of every function to MSVC and to GCC on 32-bit x86, and double functions that depend on the C library's `fma`. CORE-MATH stays useful as a test oracle: where both are correctly rounded they must agree bit for bit, so the test program compares them (12.9).

## 12.9 Tests that settle the choice

A test program extending `tools/libm_hash.c`, the libraries' sources in a folder of their own:

1. Per function and input range of the game (as `libm_hash.c`): one hash per library (C library, LLVM libc, CORE-MATH where it builds, the chosen option), the largest difference in ulp against CORE-MATH, and the time per call (throughput over independent inputs and latency over dependent ones, best of several runs).
2. Built and run on: llvm-mingw clang x64 and x86 here; MSVC x64 and Win32 on the Windows VM (`/MD` and `/MT`, which also checks the `pow` question of 12.2); GCC 12 and Clang 14 x64 and x86 on the Debian VM; GCC on the VPS (another CPU).
3. Pass: for the chosen library, the same hashes on every build; for correctly rounded functions, the same hashes as CORE-MATH; speed within the share estimated in 12.7.
4. Then the integration (game, cgame, engine), the 40 scenarios on clang, MSVC and GCC and at 20 and 100 Hz, as planned in step 14 section 10.

The program is `tools/mathlib/` (`mathbench.c`, `lm.cpp` for LLVM libc, `build.sh` for GCC and Clang, `build.bat` for MSVC; output in `build-netcodec/mathbench/<tag>/<tag>.txt`). The sources, downloaded with the owner's agreement on 12 November 2026, are in `build-netcodec/mathlibs/` (ignored by git, shared with the VMs): CORE-MATH at commit `4e52c9a2009e` (22 October 2026, `gitlab.inria.fr/core-math/core-math`) and llvm-project at commit `faba310ff3e7` (28 October 2026, `libc/` only, which holds the correctly rounded double `pow`).

## 12.10 First measurements (12 November 2026, this machine)

llvm-mingw clang 23.1.5, Windows 11 and its UCRT, `-O2 -ffp-contract=off`, no fused multiply-add allowed; times are one run on a machine that was not checked to be quiet, so a guide only.

- **LLVM libc and CORE-MATH return the same bits for all 18 cases** (the 16 functions, plus `sin` and `cos` of a double time): 0 results out of 2,097,152 differ per function, double `atan` and `atan2` included, which LLVM libc does not claim to round correctly. Same hashes in the 64-bit and the 32-bit builds.
- The UCRT differs from them by 1 ulp on 4 to 161,240 inputs per function in 64-bit (none on `atan2f`), and its 32-bit build differs from its 64-bit build: in a 32-bit process its float functions return the correctly rounded results on these inputs, its double functions other wrong ones. So even the same Windows answers differently in a 32-bit and a 64-bit program.
- CORE-MATH builds with clang in 64-bit and in 32-bit (clang has `_BitInt(128)` on 32-bit x86), but calls C23 `roundeven`, which the UCRT lacks (the test program supplies one through `rint`), and its `pow` and `powf` include `<x86intrin.h>`, a GCC and Clang header: two more porting points for MSVC.

Nanoseconds per call (independent calls, best of 7 runs):

| function | UCRT x64 | LLVM libc x64 | CORE-MATH x64 | UCRT x86 | LLVM libc x86 | CORE-MATH x86 |
|---|---|---|---|---|---|---|
| `sinf` | 2.9 | 3.6 | 2.6 | 9.6 | 4.8 | 5.3 |
| `cosf` | 2.6 | 3.7 | 2.5 | 9.7 | 4.7 | 5.2 |
| `tanf` | 2.2 | 4.1 | 3.2 | 10.8 | 84.6 | 6.6 |
| `asinf` | 2.8 | 2.7 | 3.3 | 41.0 | 3.3 | 5.6 |
| `acosf` | 2.5 | 2.8 | 3.3 | 36.3 | 4.2 | 5.6 |
| `atanf` | 2.7 | 2.9 | 9.8 | 33.2 | 4.4 | 10.0 |
| `atan2f` | 3.9 | 6.5 | 6.1 | 36.5 | 9.6 | 7.7 |
| `logf` | 1.6 | 2.0 | 2.3 | 9.7 | 3.3 | 3.4 |
| `powf` | 4.1 | 8.4 | 8.7 | 22.0 | 14.9 | 63.8 |
| `sin` | 3.0 | 11.0 | 6.5 | 9.3 | 18.4 | 23.9 |
| `cos` | 2.9 | 11.1 | 6.3 | 9.3 | 73.9 | 23.1 |
| `tan` | 3.7 | 15.3 | 27.2 | 9.1 | 22.7 | 86.6 |
| `acos` | 2.7 | 11.9 | 6.1 | 8.7 | 15.0 | 30.8 |
| `atan` | 2.8 | 10.0 | 5.1 | 6.9 | 12.4 | 12.7 |
| `atan2` | 5.4 | 15.9 | 9.4 | 127.1 | 20.0 | 18.3 |
| `pow` | 8.4 | 21.0 | 38.6 | 21.1 | 23.0 | 129.1 |
| `sin` of a time | 3.5 | 14.3 | 7.4 | 9.2 | 15.4 | 16.6 |
| `cos` of a time | 4.7 | 14.2 | 9.2 | 9.2 | 75.7 | 21.8 |

Reading: in 64-bit, LLVM libc's float functions cost about the UCRT's (within 1 to 4 ns); its double functions cost 2 to 5 times the UCRT's, 10 to 21 ns, where CORE-MATH's cost 5 to 9 ns except `tan` and `pow`. CORE-MATH's double `sin` at 6.5 ns with several `fma` calls means the UCRT's `fma` uses the instruction on this CPU (an inference). At the estimate of 12.7 (64,000 calls per second, all double at 15 ns), LLVM libc would cost about 1 ms per second, 0.1 % of a core.

On the VMs (12 November 2026; the owner's runs, outputs in `build-netcodec/mathbench/<tag>/`): GCC 12.2 and Clang 14.0.6 on Debian 12 (glibc 2.36), x64 and x86 (`-m32 -msse2 -mfpmath=sse`); MSVC 19.51.36260 (Visual Studio 2026) on Windows 11, x64 and x86, `/MD` and `/MT`.

- **All 10 builds give the same LLVM libc hashes for the 18 cases**, equal to the CORE-MATH hashes of its 5 builds (clang 23 x64 and x86, GCC 12 x64, Clang 14 x64 and x86; GCC 12 x86 cannot build it, MSVC was not tried). Linux and Windows, GCC, Clang and MSVC, 64-bit and 32-bit: identical.
- On the VPS (1 vCPU; the GCC 12 x64 program built on the Debian VM, copied): the same LLVM libc and CORE-MATH hashes again. The C library, same binary, does not: on the VPS glibc's `tanf`, `asinf`, `acosf`, `atanf` and `atan2f` are correctly rounded on these inputs (on the VM they differ on 42,596 to 446,992 inputs) and its double `atan` differs from the VM's (420 inputs off instead of 418). The VPS runs Debian 13 with glibc 2.41 on an AMD EPYC Genoa, the VM Debian 12 with glibc 2.36: the float functions are CORE-MATH's code, which entered glibc from 2.41; the double `atan` change comes from the version or from the code glibc picks for a CPU with fused multiply-add (not separated). So two Linux servers running the same binary compute differently with glibc. LLVM libc on the VPS: float 3.6 to 13.5 ns (glibc 3.5 to 9.8), double 17.3 to 34.1 ns (glibc 4.9 to 12.9).
- The C libraries differ from each other and from themselves: glibc 2.36 x64 differs from the correctly rounded results on 355 to 446,992 inputs per function (`atan2f` the most, which the UCRT gets right); glibc's 32-bit build differs from its 64-bit build on `tanf` and every double function (its float `asinf`, `acosf`, `atanf`, `atan2f` are correctly rounded on these inputs in 32-bit only); the UCRT behaves the same way; `/MT` and `/MD` differ on `pow` (x64).
- Speed, 64-bit: GCC 12, LLVM libc float 2.2 to 7.5 ns (glibc 2.2 to 6.3), double 10.9 to 20.8 ns (glibc 2.7 to 7.6); MSVC, LLVM libc float 2.1 to 8.4 ns (UCRT 1.9 to 4.3), double 12.5 to 22.7 ns (UCRT 2.9 to 7.9). The VMs' times are less reliable than this machine's.
- Speed, 32-bit: LLVM libc's `asinf`, `acosf` and `acos` take 68 to 172 ns with GCC 12, Clang 14 and MSVC (3 to 15 ns with clang 23). Its square root (`FPUtil/sqrt.h`) uses `__builtin_elementwise_sqrt`, which only recent Clang has, or x86-64 assembly; 32-bit x86 with other compilers falls back to the generic software square root. Same bits (the square root is exact either way); if 32-bit is kept, a one-line local change to use the SSE2 instruction removes the cost. clang 23's 32-bit `tanf` and `cos` (75 to 85 ns) stay unexplained; the other compilers run them in 6 to 28 ns.

## 12.11 Integration (12 November 2026)

What was built, after the owner's choice of B and three requests of the same day: one entry point for every math function, so that a later change of library touches one file; the existing variants replaced by it; the most precise results the code allows.

- **The entry point**: `shared/qcommon/q_libm.h`, included by `q_math.h`, so every module and the engine see it. The functions whose result IEEE 754 does not fix are out of line in `q_libm.cpp` on LLVM libc: `Q_sin`, `Q_cos`, `Q_tan`, `Q_asin`, `Q_acos`, `Q_atan`, `Q_atan2`, `Q_log`, `Q_log2`, `Q_pow` and their `f` versions, `Q_rsqrtf`, `Q_sinpif`, `Q_cospif`, `Q_tanpif` (the sine, cosine and tangent of pi times x), and three helpers in degrees computed in double and rounded once, `Q_atan2degf`, `Q_atandegf`, `Q_acosdegf`. The functions IEEE 754 fixes are inline calls to the C library, the same instruction: `Q_sqrt`, `Q_floor`, `Q_ceil`, `Q_fabs`, `Q_fmod`, `Q_round` and their `f` versions, `Q_lrintf`, `Q_isnan`, `Q_isfinite`.
- **The library**: `lib/llvm-libc/` holds the 307 headers the functions reach (every `#if` branch followed, so that every compiler finds its own), 1.5 MB, unchanged, at llvm-project commit `faba310ff3e7`; `update.py` copies them again from a newer checkout. `q_libm.cpp` is C++17 in the static library `qlibm`, linked by every binary built with `SharedCommonFiles`.
- **The calls**: about 1,100 calls in the multiplayer code and `shared` (game, cgame, UI, client, server, botlib, renderers, sound, mp3 decoder) go through the entry point, each at its former precision (a double call stays double: a float version would round the result before the rest of the expression, which is often computed in double). Not touched: the two third-party libraries inside rend2 (`stb_image.h`, MikkTSpace), display only, and the single-player code (out of scope), which only links `qlibm` because it shares `q_math.c`.
- **Variants removed**: `Q_acos`, `Q_asin` (a clamp that never applied), `Q_rsqrt` (Quake's approximation, about 0.2 % off), `Q_powf(float, int)` (an integer power: the camera of `cg_smoothCamera 0`, the default, kept as the only behaviour with `Q_powf` and the same whole exponent, so the cvar went), the float `Q_fabs` and `Q_isnan`; the integer `Q_log2` renamed `Q_ilog2` (an integer logarithm is exact: a float route could only lose precision above 2^24).
- **Precision**: angles in degrees go through half turns (`Q_sinpif( d / 180 )` instead of `sinf( d * pi / 180 )` with a float pi): `AngleVectors`, `RotatePointAroundVector`, TR_SINE trajectories, the rotation of map entities, field of view, effects, about 60 places; angles from vectors through the double helpers (`vectoangles`, `vectoyaw`, the normal encoding, the movers' arc); vector lengths and normalisation in double, rounded once. mathbench measures the half-turn path against the radians path: the radians path differs from the correctly rounded result on 1.5 million of 2.1 million angles (by up to 885 million ulp near the exact zeros, where it gives about 1e-7 instead of 0), and `1 / sqrtf` on 0.6 million of 2.1 million squared lengths (1 ulp); LLVM libc's `sinpif`, `cospif`, `tanpif`, `rsqrtf` return CORE-MATH's bits.
- **Checks**: the game, cgame, UI, engine and dedicated server build with llvm-mingw clang (rend2 has a compile error that exists on the parent commit too); the netcodec tests pass and pmove gives the same results as the game and as cgame; the 40 scenarios are identical at `sv_fps` 20 and 100, first after the plain replacement, then after the precision changes.
- **What the scenarios see**: the parent commit's game module (the C library's functions) gives the same counts as the new one on all 40 cases, on this machine: the scenarios do not see differences of the last bit. So the flechette case where the MSVC and clang modules differed (12.2) did not come from the math library. Its cause: `WP_CreateFlechetteBouncyThing` drew two random numbers in the arguments of one call (`CreateMissile( ..., 700 + Q_flrand(0, 1) * 700, 1500 + Q_flrand(0, 1) * 2000, ... )`), whose order of evaluation C leaves unspecified: clang evaluates them left to right, MSVC right to left, so the speed and the lifetime of the bouncing flechettes swapped their numbers. The two draws are now sequenced in clang's order (clang's counts unchanged), as are two other cases in the game (`NPC.c` voice event, `NPC_AI_GalakMech.c` aim); a systematic search for such cases goes with the random number step. With these fixes, the game module built by MSVC 2026 on the Windows VM gives the clang module's counts on all 40 cases (and is identical at 20 and 100 Hz), on the same engine; the game and the engine also build with GCC 12 on Debian, and the VM validations pass on GCC, Clang 14 and MSVC.
- **Text conversions** (mathbench, the C library only): parsing (`strtof`, `strtod`, `atof`, `sscanf %f`) and printing (`printf %f`, `%g`, `%.9g`, `%.17g`, `%.0f` of halves, `%.1f` of quarters) hashed on every platform, to know whether map entities, configuration files and configstrings read and write the same values on glibc and the UCRT. Measured (glibc 2.36 with GCC 12 and Clang 14, the UCRT with llvm-mingw clang, x64 and x86): every printing hash identical (both round ties to even: `%.0f` of 0.5, 1.5, 2.5 gives 0, 2, 2), `strtod` and `atof` identical; `strtof` and `sscanf %f` differ. Over the 1,048,576 strings a single one differs, `83034.89453125`, which lies exactly halfway between two floats: glibc returns the even one (`0x47a22d72`, correctly rounded, as `(float)strtod`), the UCRT the odd one (`0x47a22d73`): the UCRT's `strtof` rounds exact ties away from even. Any decimal exactly halfway between two floats is read differently, for example the integer 16777217 (2^24 + 1). The game reads map entities' origins and angles, vehicle and effect files with `sscanf` `%f` (28 places); LLVM libc has a correctly rounded parser (`internal::strtofloatingpoint`, header-only, not in its `shared` interface). Owner's choice (12 November 2026): the entry point parses text with it: `Q_strtof`, `Q_strtod`, `Q_atof` (as `strtof`, `strtod`, `atof`, `errno` included) and `Q_ScanFloats( str, out, count )` (as `sscanf( str, "%f %f ..." )`, the same counts, -1 for an empty string); the 318 `atof` calls and the 28 `sscanf %f` of the multiplayer code go through them (`src/__support/str_to_float.h` and its headers added to `lib/llvm-libc`, 313 headers, 1.7 MB). Checked: over the same 1,048,576 strings `Q_strtof` gives glibc's correctly rounded hash on Windows (clang x64 and x86), mathbench's scan check (counts and bits, the two halfway cases included) passes, the 40 scenarios are unchanged and identical at 20 and 100 Hz.

## 12.12 What was not read or verified

- Whether the VPS's double `atan` differs because of glibc 2.41 or because of the CPU's code path.
- Whether Clang folds constant math calls with the host's library (from memory of LLVM's source).
- RLIBM's licence; SLEEF's scalar speed.
- The call counts of 12.7 are an estimate, not a measurement.

## 12.13 Sources

Correctly rounded libraries:

- CORE-MATH project page (primary): https://core-math.gitlabpages.inria.fr/
- CORE-MATH source, unofficial GitHub mirror read for `src/binary32/sin/sinf.c` and `src/binary64/sin/sin.c` (community mirror of primary code): https://github.com/nak5124/core-math
- A. Sibidanov, P. Zimmermann, S. Glondu, "The CORE-MATH Project", ARITH 2022 (primary): https://inria.hal.science/hal-03721525v2/file/core-math-final.pdf
- core-math-binary64, a snapshot building on Windows with clang-cl (community): https://github.com/indigobio/core-math-binary64
- LLVM libc math documentation: goals, platforms, performance table, GPU accuracy (primary): https://libc.llvm.org/math/index.html
- LLVM libc tracking issue "Refactor Higher Math Functions to Header Only", closed 4 May 2026 (primary): https://github.com/llvm/llvm-project/issues/147386
- LLVM libc pull request #222827, double `pow` correctly rounded, merged 19 September 2026 (primary): https://github.com/llvm/llvm-project/pull/222827
- LLVM libc pull request #233984, "Fix various MSVC compatibility issues" (primary): https://github.com/llvm/llvm-project/pull/233984
- LLVM libc pull request #221012, error bounds of the double `sin`/`cos` fast path fixed, September 2026 (primary): https://github.com/llvm/llvm-project/pull/221012
- LLVM libc issue #214934, double `atan2` sign of tiny results, "not correctly rounded yet" (primary): https://github.com/llvm/llvm-project/issues/214934
- LLVM libc fortnightly math meeting notes (primary): https://discourse.llvm.org/t/fortnightly-llvm-libc-math-library-meeting/89166
- LLVM licence and its exceptions (primary): https://llvm.org/LICENSE.txt
- RLIBM project (primary): https://github.com/rutgers-apl/The-RLIBM-Project, https://github.com/rutgers-apl/rlibm-32
- SLEEF (primary): https://sleef.org/; N. Shibata, F. Petrogalli, "SLEEF: A Portable Vectorized Library of C Standard Mathematical Functions" (primary): https://arxiv.org/abs/2001.09258

Accuracy and standards:

- B. Gladman, V. Innocente, J. Mather, K. Ozaki, P. Zimmermann, "Accuracy of Mathematical Functions in Single, Double, Double Extended, and Quadruple Precision", August 2026 (primary): https://members.loria.fr/PZimmermann/papers/accuracy.pdf
- WG14 N2715, the `cr_` prefix (primary): https://www.open-std.org/jtc1/sc22/wg14/www/docs/n2715.htm
- WG21 P3864R0, correctly rounded floating-point maths functions (primary): https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3864r0.html

Microsoft:

- `/Zc:cmath` (primary): https://learn.microsoft.com/en-us/cpp/build/reference/zc-cmath
- `/fp` and contractions (primary): https://learn.microsoft.com/en-us/cpp/build/reference/fp-specify-floating-point-behavior
- "MSVC C++23: constexpr cmath with LLVM Libc", 10 September 2026 (primary): https://devblogs.microsoft.com/cppblog/msvc-c23-constexpr-cmath-with-llvm-libc/
- "Bringing Correctly Rounded Math to Production with LLVM-libc", 16 September 2026, guest post by M. Jones and T. Ly (primary): https://devblogs.microsoft.com/cppblog/bringing-correctly-rounded-math-to-production-with-llvm-libc/

glibc:

- "benchtests: Add sinf traces" and the CORE-MATH patch series (primary): https://patchwork.sourceware.org/project/glibc/patch/PAWPR08MB8982EACC87FFDE5E558DF38F8351A@PAWPR08MB8982.eurprd08.prod.outlook.com/, https://patchwork.sourceware.org/project/glibc/patch/20250131191844.2582716-13-adhemerval.zanella@linaro.org/

Games and engines:

- Factorio Friday Facts #36 and #52 (primary): https://www.factorio.com/blog/post/fff-36, https://factorio.com/blog/post/fff-52; Lua API, libraries and functions (primary): https://lua-api.factorio.com/latest/auxiliary/libraries.html
- E. Catto, "Determinism", Box2D blog, August 2024 (primary): https://box2d.org/posts/2024/08/determinism/; Box2D math API (primary): https://box2d.org/documentation/group__math.html
- Jolt Physics `Trigonometry.h` (primary): https://github.com/jrouwe/JoltPhysics/blob/master/Jolt/Math/Trigonometry.h; build options (primary): https://jrouwe.github.io/JoltPhysics/md__build_2_r_e_a_d_m_e.html; jolt-rust pull request on the cost (community): https://github.com/SecondHalfGames/jolt-rust/pull/15
- Rapier determinism (primary): https://rapier.rs/docs/user_guides/templates/determinism/
- Java `StrictMath` (primary): https://docs.oracle.com/en/java/javase/21/docs/api/java.base/java/lang/StrictMath.html
- supermariowar reference, CORE-MATH for `sin`, `cos`, `atan2` (community): https://github.com/KyleJamesWalker/supermariowar-cpp-reference/pull/5
