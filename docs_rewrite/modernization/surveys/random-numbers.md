# Random numbers identical on every platform: generators, what other projects do, and the options for step 14

Written on 12 November 2026, during plan step 14, after the math functions ([deterministic-math.md](deterministic-math.md)). The owner asked for one random number generator, modern, fast and of quality, the same on every platform, chosen after a survey as for the math library. This page records what the code draws today, the candidate generators with their quality, speed, state and licence, what engines and languages use, the draws the client must predict, and the options with their costs.

Sources are labelled as in the other surveys: **primary** (the authors' own paper, code or documentation), **secondary** (a summary by someone else), **community** (forums, unofficial code).

## 1. Summary

- The game draws from three generators today: Raven's `holdrand` (an LCG copied from MSVC's `rand`, 15 bits per value) behind `Q_irand`, `Q_flrand`, `irand`, `flrand` (about 1,400 calls in the game, cgame, UI, client, botlib and renderers), the C library's `rand()` (37 calls in the game, 36 in cgame, a few in the engine), and `PM_irand_timesync`, which draws from the command's time so that the server and the client's prediction agree. glibc's and the UCRT's `rand()` give different sequences from the same seed, so a Linux and a Windows server play differently; `holdrand` is identical everywhere but of low quality (15-bit values, low bits with short periods). The engine's connection challenges are made from `rand()` and the clock, which an attacker can guess.
- Every modern candidate costs about 1 ns per value on x64, measured on this machine (section 5): speed does not separate them, nor does it matter at the game's draw rate (estimated at tens of thousands per second at most, well under 0.1 ms per second). Quality, state size, simplicity, licence and predictability do.
- Recent engines and languages use the xoshiro family (.NET 6 for `new Random()`, Rust's `SmallRng`, Lua 5.4), PCG32 (Godot), LXM (Java 17), xorshift (Unity.Mathematics) or an LCG (Unreal's `FRandomStream`); for unpredictable numbers, a stream cipher (Rust's `StdRng` uses ChaCha12). Valve's Source engine gives each user command a random seed derived from its number (`MD5_PseudoRandom( sequence_number )`), which the client and the server both use for the weapon spread; cheats use the same seed to cancel the spread.
- Recommended: **xoshiro128++** for every stream (public domain, 32-bit operations, 128-bit state, "passes all tests we are aware of", the authors' current recommendation for 32-bit output), seeded through SplitMix64; integers in a range by **Lemire's unbiased method**, floats from the top 24 bits; **the SplitMix64 finalizer of (command time, draw index)** for the draws `Pmove` makes on both sides; the engine's connection challenges from the operating system's random bytes. One entry point, as for the math functions. The owner decides (section 7).

## 2. What the code draws today

Counted on 12 November 2026 in the multiplayer code and `shared` (calls, not counting comments):

| code | `Q_irand` | `Q_flrand` | `irand` / `flrand` | `rand()` | other |
|---|---|---|---|---|---|
| game | 941 | 93 | 62 | 37 | `PM_irand_timesync` 17, `RandFloat` 5 (`w_saber.c`, `#ifdef` per platform and `g_randFix`), `srand` 2, `Rand_Init` 2, `erandom` 1, `Q_random( &seed )` 1 |
| cgame | 80 | 114 | 23 | 36 | `Rand_Init` 1, `Q_random` 1 |
| UI | 1 | 4 | | | `Rand_Init` |
| client | 7 | | 24 | 5 | `srand`, `Com_RandomBytes` |
| server | 3 | | | 2 | `srand( Com_Milliseconds() )`, `sv.checksumFeed` from `rand()` |
| qcommon | | 1 | | 2 | `srand`, `Rand_Init`, `Com_RandomBytes` (the system's random bytes, `rand()` as a fallback) |
| botlib | | 19 | | | |
| renderers (vanilla, rend2, dedicated) | 3 | 31 | 88 | 6 | `Q_random`, `Q_crandom`, `srand( 1001 )` for the noise table |

- `holdrand` (`shared/qcommon/q_math.c`): `holdrand = holdrand * 214013 + 2531011`, value `holdrand >> 17` (15 bits); `irand` maps it with `(value * range) >> 15`, so a range above 32,768 cannot be drawn and every range has a bias of up to range / 32,768; `flrand` gives 32,768 distinct values. `Q_irand` and `Q_flrand` only call `irand` and `flrand`: two names for each function.
- `rand()`: the C library's generator, different on glibc and the UCRT (step 14 section 10); its quality depends on the library (the UCRT's is the same 15-bit LCG as `holdrand`).
- Draws whose order a compiler may change: two draws in one call's arguments or as operands of one operator (step 14: the flechette's alternate fire, an NPC voice event, the Galak mech's aim, fixed; a systematic search goes with this work).
- Security: `clc.challenge = ((rand() << 16) ^ rand()) ^ Com_Milliseconds()` and the update server's challenge, seeded from the time, are predictable; the connection challenge protects the server against spoofed source addresses.

## 3. Candidate generators

| generator | state | output | quality (authors' tests unless noted) | period | licence | notes |
|---|---|---|---|---|---|---|
| **xoshiro128++** (Blackman, Vigna 2018-2019) | 128 bits | 32 bits | "passes all tests we are aware of"; the authors' 32-bit all-purpose recommendation | 2^128 − 1 | public domain (dedication in the code) | 32-bit operations only; jump function (2^64 steps) for independent streams; must not be seeded all-zero (seed through SplitMix64) |
| xoshiro128** | 128 bits | 32 bits | passes BigCrush (paper); .NET 6's generator on 32-bit | 2^128 − 1 | public domain | the `**` scrambler multiplies; the site now recommends `++` |
| xoshiro256++ / xoshiro256** | 256 bits | 64 bits | the authors' 64-bit recommendation (`++`); .NET 6 64-bit (`**`), Rust `SmallRng` 64-bit (`++`), Lua 5.4 (`**`) | 2^256 − 1 | public domain | 64-bit operations; the game needs 32-bit values |
| PCG32 (O'Neill 2014) | 64 bits + stream | 32 bits | "excellent" (author); used by Godot | 2^64 per stream | Apache 2.0 or MIT (pcg-cpp; pcg-c-basic Apache 2.0, not checked) | a 64-bit multiply per value; Apache 2.0 alone is incompatible with GPL v2, MIT is not |
| SplitMix64 (Steele, Lea, Flood 2014) | 64 bits | 64 bits | passes BigCrush; the authors of xoshiro recommend it for seeding | 2^64 | public domain implementations | its output function is a strong 64-bit hash, usable on a counter |
| SFC32 (Doty-Humphrey, PractRand) | 128 bits | 32 bits | passes PractRand (author, community reports) | at least 2^32 (a counter), average larger | public domain | chaotic, no exact period |
| RomuTrio32 (Overton 2020) | 96 bits | 32 bits | PractRand to large sizes (author) | not guaranteed (pseudo-random cycle lengths) | not copyrighted, credit asked | the fastest; a short cycle cannot be excluded by theory |
| wyrand (Wang Yi) | 64 bits | 64 bits | passes PractRand and BigCrush (author's claims, not read here) | 2^64 | Unlicense | needs a 64 × 64 → 128-bit multiply |
| LXM, L32X64MixRandom (Java 17 default) | 96 bits | 32 bits | passes TestU01 and PractRand (JEP 356) | 2^32 (2^64 − 1) | GPL v2 with classpath exception (OpenJDK code) | an LCG mixed with a xorshift: two generators per value |
| ChaCha8 / ChaCha12 / ChaCha20 (Bernstein) | 512 bits per block | 16 × 32 bits per block | cryptographic (12 and 20 rounds considered secure; Rust's `StdRng` uses 12) | 2^64 blocks per key | public domain reference | unpredictable from its outputs; about 3 times the cost of xoshiro here |
| MT19937 | 2.5 KB | 32 bits | fails linear-complexity and binary-rank tests (prng.di.unimi.it) | 2^19937 − 1 | BSD | the C++ standard fixes its sequence, but not the distributions' (`uniform_int_distribution` differs between libraries) |
| the C library's `rand()` | varies | 15 to 31 bits | varies | varies | | different sequences on glibc and the UCRT |

Counter-based functions, for the draws both sides make from a known input (the command's time and the draw's index):

| function | input / output | quality | cost here |
|---|---|---|---|
| **SplitMix64 finalizer** of `(time << 32 \| index) × golden ratio` | 64 → 64 bits | SplitMix64's output function: SplitMix64 passes BigCrush | 1.1 ns x64, 2.9 ns x86 |
| PCG hash (Jarzynski, Olano 2020) | 32 → 32 bits | on the speed / quality Pareto frontier of their TestU01 study; "should probably be your default GPU hash function" (Reed, secondary) | 1.1 ns |
| lowbias32 (Wellons) | 32 → 32 bits | the lowest bias of two-multiply 32-bit hashes in his search (community) | 1.1 ns |
| MurmurHash3 `fmix32` | 32 → 32 bits | widely used finalizer | 1.1 ns |
| Philox4x32-10 (Salmon et al. 2011) | 128 → 128 bits | passes BigCrush with a safety margin of rounds (authors); proposed for the C++ standard (P2075) | 5.2 ns |

A 32-bit hash of a 32-bit time combined with an index has to fold two inputs into 32 bits first (collisions between (time, index) pairs); the 64-bit SplitMix64 finalizer takes both without folding.

## 4. What others use

| project | generator | source |
|---|---|---|
| .NET 6 and later, `new Random()` | xoshiro256** (64-bit), xoshiro128** (32-bit) | primary: dotnet/runtime#47085, Stephen Toub's .NET performance posts |
| Rust `rand` 0.8 and later | `SmallRng`: xoshiro256++ / xoshiro128++; `StdRng` and `thread_rng`: ChaCha12; both declared non-portable between releases | primary: Rust Rand Book, rand 0.8 changelog |
| Lua 5.4 | xoshiro256** (from memory; the manual could not be fetched) | to verify |
| Java 17 | LXM family (`L32X64MixRandom` default); `SplittableRandom` is SplitMix64 | primary: JEP 356, java.util.random package |
| Godot | PCG32, documented as an implementation detail | primary: Godot documentation |
| Unity.Mathematics | xorshift with 32-bit state; seeds hashed with Wang's hash by `CreateFromIndex` | primary: Unity documentation and source |
| Unreal Engine `FRandomStream` | 32-bit LCG (`seed × 196314165 + 907633515`), "very bad quality in the lower bits" | primary: Epic's API documentation; constants from a forum quote of the source (community) |
| Valve Source engine | per user command: `random_seed = MD5_PseudoRandom( sequence_number ) & 0x7fffffff`, used for the predicted weapon spread | primary: Source SDK 2013 `in_main.cpp`; cheats compensate the spread with it (community: public cheat code) |
| V8 (JavaScript `Math.random`) | xorshift128+ | from memory, not verified here |

## 5. Measurements (12 November 2026, this machine)

`tools/rng/rngbench.c`, llvm-mingw clang 23, `-O2`: time per 32-bit value (stream generators through their state, hashes on independent inputs; each call through a function pointer, about 1 ns of the figure), best of 5 runs of 100 million values; the hash of the first million values is the same in 64-bit and 32-bit (integer operations only).

| generator | x64 ns | x86 ns |
|---|---|---|
| `holdrand` (today, 15 bits) | 1.07 | 1.08 |
| C library `rand()` (UCRT) | 3.87 | 6.20 |
| xoshiro128** | 1.09 | 1.51 |
| xoshiro128++ | 1.16 | 1.58 |
| xoshiro256** (top 32 bits) | 1.29 | 6.43 |
| xoshiro256++ (top 32 bits) | 1.34 | 4.31 |
| PCG32 | 1.15 | 2.05 |
| SplitMix64 (top 32 bits) | 1.29 | 2.50 |
| SFC32 | 1.25 | 1.40 |
| RomuTrio32 | 1.07 | 1.05 |
| wyrand (portable 128-bit product) | 1.26 | 2.37 |
| ChaCha8 | 2.99 | 3.29 |
| hash: PCG, lowbias32, fmix32 | 1.07 | 1.24 |
| hash: SplitMix64 finalizer | 1.07 | 2.85 |
| hash: Philox4x32-10 | 5.24 | 6.42 |
| a draw in [0, n) with xoshiro128**: modulo / multiply-shift / Lemire unbiased | 1.26 / 1.11 / 1.31 | 1.61 / 1.62 / 1.68 |

On the VMs (the owner's runs, 12 November 2026): the same hashes with GCC 12 (Debian) and MSVC 2026 (only the C library's `rand()` differs, glibc against the UCRT). Through the function pointer GCC takes 3.5 to 3.7 ns for xoshiro128**/++ and SFC32 and about 1 ns for the 64-bit generators; MSVC 1.3 to 2.0 ns for all. Inlined with the state in a global variable, as the game will call them:

| inlined | clang 23 | GCC 12 | MSVC 2026 |
|---|---|---|---|
| xoshiro128++ | 0.51 | 0.53 | 0.46 |
| xoshiro256++ (top 32 bits) | 0.55 | 0.71 | 4.41 |
| `holdrand` | 0.23 | 0.71 | 0.33 |

So GCC's 3.7 ns came from the call through a pointer, and xoshiro128++ is the fastest and the most even of the two on the three compilers (MSVC is slow on xoshiro256++ inlined). Reading: on x64 every candidate except ChaCha8 and Philox runs within the cost of the call; the 64-bit generators lose on 32-bit x86 (not a target). Lemire's unbiased method costs 0.2 ns more than the biased multiply-shift. To run on the VMs: `cc -O2 -o rngbench rngbench.c` (GCC, Clang), `cl /O2 rngbench.c` (MSVC); the hashes must be equal everywhere.

## 6. The draws the client predicts

`PM_irand_timesync` (17 calls in `bg_saber.c` and `bg_pmove.c`: saber chain ends, lock breaks, flip and jump attacks) must give the same numbers on the server and in cgame's prediction, so it cannot use a stream whose state only the server has. Today it draws from the command's time; step 14 stopped it from overwriting that time. The modern equivalent is a counter-based function of values both sides hold, as Source does with the command number: the SplitMix64 finalizer of (command time, index of the draw within the command). Its known limit, the same as Source's and as today's code: the client chooses its command times (within what the server accepts), so a modified client can pick times that give it favourable draws, and can know the outcome in advance. Hiding the input from the client would break prediction; the alternative is to keep such draws out of gameplay outcomes that matter (a design question for later).

## 7. Options

| option | identical everywhere | quality | CPU | memory | unpredictable | work |
|---|---|---|---|---|---|---|
| **A. xoshiro128++** streams, SplitMix64 seeding, Lemire ranges, 24-bit floats; SplitMix64 finalizer for `Pmove`'s draws (recommended) | yes (integer operations) | passes all known tests; unbiased ranges | ~1 ns per value | 16 bytes per stream | no (a linear generator; its state could in theory be rebuilt from enough exact outputs) | one entry point, about 1,400 calls renamed, the `rand()` calls replaced |
| B. as A with xoshiro256++ | yes | the same, longer period | ~1.3 ns x64, 4.3 ns x86 | 32 bytes | no | as A |
| C. as A with ChaCha8 for the server's stream | yes | cryptographic | ~3 ns per value | 64 + 64 bytes | yes | as A plus ChaCha (about 40 lines) |
| D. PCG32 | yes | excellent (author) | ~1.2 ns | 16 bytes | harder than xoshiro (author's claim) | as A; take the MIT licence of pcg-cpp |
| E. keep `holdrand`, replace only `rand()` | yes | low (15 bits, biased ranges) | ~1 ns | 4 bytes | no | the smallest |

In every option:

- One entry point, as `q_libm.h`: the game calls only its functions (a stream draw, an integer in a range, a float in a range, the counter-based draw for `Pmove`), and the generator can change in one file; `Q_irand`/`irand` and `Q_flrand`/`flrand` become one name each; `rand()`, `srand()`, `RandFloat`'s platform branches and `g_randFix` go.
- One stream per module, as today (the game, cgame, UI, the engine), seeded at start (the game from `g_debugSeed` or from the operating system's random bytes); the scenarios' `SEED` keeps replaying a run.
- The engine's connection and update challenges from `Com_RandomBytes` (the system's random bytes), not from `rand()`.
- Every random outcome changes (the scenario counts are recorded after, as planned).

Why A: speed is a non-issue, so the choice is quality and simplicity. xoshiro128++ is the generators' authors' current 32-bit recommendation, public domain, two dozen lines of 32-bit operations, and the family is what .NET and Rust ship; Lemire's method removes the bias of today's ranges for 0.2 ns. ChaCha8 (C) would also make the server's stream unpredictable from outside, for 2 ns per value; its benefit is small while the stream serves every server event in an order an observer cannot reproduce, and it does nothing for the predicted draws (section 6).

**Owner's choice (12 November 2026): A, xoshiro128++**, confirmed by the inlined timings of section 5 on the three compilers.

## 8. Integration (12 November 2026)

- **The entry point**: `shared/qcommon/q_rand.h` and `q_rand.c` (C, in `SharedCommonFiles`, so every module and the engine have their own copy and their own stream), included by `q_math.h`. `qrand_t` holds a xoshiro128++ state; `Q_RandSeed` fills it through SplitMix64 (never all zero); `Q_RandU32`; `Q_RandInt( rng, min, max )`, inclusive and unbiased by Lemire's method, the bounds in either order and the whole 32-bit range accepted; `Q_RandFloat( rng, min, max )` from the top 24 bits. The module's stream: `Rand_Init`, `Q_irand`, `Q_flrand`, `erandom`. A sequence kept in the caller's int, `Q_random` and `Q_crandom` (effects that redraw the same numbers from a seed): a Weyl sequence through Wellons' lowbias32 hash.
- **Variants removed**: `holdrand` and its 15-bit LCG, `irand` and `flrand` (151 calls renamed `Q_irand` and `Q_flrand`), `QRAND_MAX`, the LCG `Q_rand`; the C library's `rand()` and `srand()` everywhere in the multiplayer code (73 calls in the game and cgame: `rand() % n` and `rand() & (2^k - 1)` became `Q_irand( 0, n - 1 )`, unbiased, the one mask that is not of that form keeps its bits with `Q_irand( 0, 32767 ) & 99`); `RandFloat` (its Linux branch tested `__GCC__`, which no compiler defines, so on Linux it divided glibc's `rand()`, up to 2^31, by 32,768 and returned values far outside its range) and `g_randFix` (its default, 1, meant the correct range: `Q_flrand`).
- **`Pmove`'s draws**: `PM_irand_timesync` draws from a `qrand_t` in `pml`, seeded with the command's time at the start of `Pmove`, so the server and the client's prediction draw the same numbers (since step 14 part 4b, 13 November 2026: with the command's tick, its time rounded up to the tick). It keeps 1.01's effective range, `val1` to `val2 - 1` (1.01's formula, `val1 + random * (val2 - val1)` truncated, never gave `val2`, so `PM_irand_timesync( 0, 1 )` is always 0; the saber's chances were tuned with it), and `val1` when `val2 <= val1`.
- **Engine**: its stream seeded once from the system's random bytes (`Com_InitRand`; a later `Rand_Init( Sys_Milliseconds() )` that overwrote that seed is gone); the connection challenge, the update server's challenge and the pure checksum feed from `Com_RandomBytes` instead of `rand()` and the clock; `net_dropsim`, the music's random entry, the weather effects on the stream; the renderer's noise table from a `qrand_t` seeded with 1001.
- **Unsequenced draws**: a scanner (statements with two draws not separated by `&&`, `||`, `?:` or `VectorSet`'s commas) found no case left in the game after the three fixed with the math functions; in cgame three visual ones (an explosion's position, duration and size; two marks' size and lifetime), now drawn one after the other left to right; the products of two draws of the same range in ghoul2 (`Q_flrand( a, b ) * Q_flrand( a, b )`) give the same bits in either order.
- **Checks**: the game, cgame, UI, engine and dedicated server build with clang; pmove gives the same results as the game and as cgame; mathbench's `rand check` (SplitMix64 seeding, xoshiro128++, Lemire's range and the float against values computed in Python, the bounds, the uniformity of a small range) passes; the 40 scenarios are identical at 20 and 100 Hz with clang (12 cases have new counts, those where chance decides; 28 are unchanged).
- **Linux and Windows identical**: with the math functions (the math survey) and these random numbers, the 40 scenarios give the same statistics, line for line (560 lines: every case at 20 and 100 Hz), with the game module built by llvm-mingw clang 23 and by MSVC 2026 on Windows and by GCC 12 on Debian, run on the VPS (glibc 2.41, AMD EPYC); the first time a Linux and a Windows server play the same game. The counts are recorded in `tools/gamestats/scenarios40_golden.txt`; `tools/gamestats/scenarios40.sh` runs the 40 cases on either system and compares them with it.

## 9. Not read or verified

- Lua 5.4's generator (the manual could not be fetched), V8's, and wyrand's test claims.
- pcg-c-basic's licence (pcg-cpp is Apache 2.0 or MIT).
- The game's actual draw rate (an estimate).
- The quality claims are the authors' and published tests'; no statistical test suite was run here.

## 10. Sources

- S. Vigna, "A PRNG shootout" (primary): https://prng.di.unimi.it/ ; xoshiro128++ reference code with its public-domain dedication (primary): https://prng.di.unimi.it/xoshiro128plusplus.c
- D. Blackman, S. Vigna, "Scrambled Linear Pseudorandom Number Generators", ACM TOMS 47(4), 2021 (primary; referenced by the site)
- M. O'Neill, PCG (primary): https://www.pcg-random.org/ ; pcg-cpp licence (primary): https://github.com/imneme/pcg-cpp
- G. Steele, D. Lea, C. Flood, "Fast Splittable Pseudorandom Number Generators", OOPSLA 2014 (primary; SplitMix64)
- M. Overton, "Romu: Fast Nonlinear Pseudo-Random Number Generators Providing High Quality", 2020 (primary): https://arxiv.org/abs/2002.11331
- J. Salmon, M. Moraes, R. Dror, D. Shaw, "Parallel Random Numbers: As Easy as 1, 2, 3", SC11 (primary): https://www.thesalmons.org/john/random123/ ; WG21 P2075R5, Philox for C++ (primary): https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p2075r5.pdf
- B. Widynski, "Squares: A Fast Counter-Based RNG", 2020 (primary): https://arxiv.org/abs/2004.06278
- M. Jarzynski, M. Olano, "Hash Functions for GPU Rendering", JCGT 9(3), 2020 (primary): https://jcgt.org/published/0009/03/02/ ; N. Reed's summary (secondary): https://www.reedbeta.com/blog/hash-functions-for-gpu-rendering/
- D. Lemire, "Fast Random Integer Generation in an Interval", ACM TOMACS 29(1), 2019 (primary): https://doi.org/10.1145/3230636
- dotnet/runtime#47085, "Improve Random (performance, APIs, ...)" (primary): https://github.com/dotnet/runtime/pull/47085 ; "Performance Improvements in .NET 8" (primary): https://devblogs.microsoft.com/dotnet/performance-improvements-in-net-8/
- Rust Rand Book, "Our RNGs" and "Updating to 0.8" (primary): https://rust-random.github.io/book/guide-rngs.html , https://rust-random.github.io/book/update-0.8.html
- JEP 356 and java.util.random (primary): https://cr.openjdk.org/~iris/se/17/latestSpec/api/java.base/java/util/random/package-summary.html
- Godot RandomNumberGenerator (primary): https://docs.godotengine.org/en/stable/classes/class_randomnumbergenerator.html
- Unity.Mathematics.Random (primary): https://docs.unity.com/en-us/engine/6000.6/script-reference/unity/mathematics/random
- Unreal FRandomStream (primary): https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Core/FRandomStream
- Source SDK 2013, `src/game/client/in_main.cpp` (primary): https://github.com/ValveSoftware/source-sdk-2013 ; public nospread cheat code using the command seed (community): https://github.com/SimpleRealistic/styles-cheat-csgo-source/blob/master/NoSpread.cpp
