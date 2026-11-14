# SnapVector / Sys_SnapVector rounding: findings and one change to make

Date: 10 November 2026. Branch: `feature/poc-netcode` (fix commit `230ed6518`).
Audience: the agent that edits the OpenJK tree. Everything needed is in this file.

## What to do

One code change, then doc updates.

1. In `shared/qcommon/q_math.c`, make `SnapVector` force round-to-nearest around its three `lrintf` calls, the same way `Sys_SnapVector` already does in `shared/sys/snapvector.cpp`.
2. Update `docs_rewrite/modernization/progress/step-06-cpp-codec.md` (section F) with the points listed under "Doc updates".

Do not change `Sys_SnapVector`: commit `230ed6518` is correct as it is.

## Why

Target rule for both functions, on every platform and compiler: round to nearest, exact halves to the even integer, result converted through an integer (so -0.5 gives +0). This is what 1.01 did with `fld` / `fistp`.

- `Sys_SnapVector` after `230ed6518`: matches 1.01 everywhere, in every test. Nothing to do.
- `SnapVector` after `230ed6518`: matches the target in the default FPU state, but `lrintf` follows the current rounding mode and `SnapVector` does not set it. With the caller in round-toward-zero mode it truncates (measured on every compiler below). The engine does not leave the mode changed anywhere, so this is a robustness fix, not a visible bug.

## The change

`q_math.c` is a C file compiled into each game module. Replace the current `SnapVector` with:

```c
#if defined(_MSC_VER)
#include <float.h>
#pragma fenv_access (on)
#else
#include <fenv.h>
#endif

// To nearest with ties to even, through an integer (so -0.5 gives +0), on every platform and compiler:
// what 1.01's fld / fistp did on Windows. The rounding mode is forced, as in Sys_SnapVector.
void SnapVector( float *v )
{
#if defined(_MSC_VER)
	unsigned int oldcontrol;
	unsigned int newcontrol;

	_controlfp_s(&oldcontrol, 0, 0);
	_controlfp_s(&newcontrol, _RC_NEAR, _MCW_RC);

	v[0] = (float)lrintf(v[0]);
	v[1] = (float)lrintf(v[1]);
	v[2] = (float)lrintf(v[2]);

	_controlfp_s(&newcontrol, oldcontrol, _MCW_RC);
#else
	int oldround = fegetround();
	fesetround(FE_TONEAREST);

	v[0] = (float)lrintf(v[0]);
	v[1] = (float)lrintf(v[1]);
	v[2] = (float)lrintf(v[2]);

	fesetround(oldround);
#endif
}
#if defined(_MSC_VER)
#pragma fenv_access (off)
#endif
```

Notes for the edit:

- Put the `#include` lines with the file's other includes; keep the `#pragma fenv_access (on)` / `(off)` pair tight around the function so the rest of `q_math.c` keeps its normal floating-point optimisation under MSVC.
- This exact function was tested (see "Evidence", last table). It compiled as C and as C++ with MSVC 19.51 and GCC 13.3.
- Not tested: Clang, macOS, ARM. Check that the game, cgame and ui modules still build on the project's usual presets.

## Evidence

Method: each function run on every float of [-4096, 4096) on a 1/256 grid (2,097,152 values, every half included) and compared with an integer-arithmetic reference. "Even" means 0 differences from ties-to-even over the grid; "away" means it differs on the 4,096 exact halves; "trunc" means it equals truncation toward zero.

### Original 1.01 binaries

| Binary | Compiler found in the binary | `Sys_SnapVector` code | Result |
|---|---|---|---|
| `jampDed.exe` v1.0.1.0 (linked 6 Nov 2003) | MSVC 7.0 build 9466, linker 7.0, no Intel objects | at 0x458120: `fld` / `fistp` / `fild` / `fstp` three times | even |
| `linuxjampded` | Intel C++ 5.0.1 Build 010730D0, NASM 0.98, GCC 2.95.3 start-up objects | at 0x814ed00: `unix/snapvector.nasm` byte for byte, loads control word 0x037F | even |

- The machine code of both routines was extracted and executed on the grid.
- `linuxjampded` records its compile lines: the repository makefile flags plus `-DFINAL_BUILD` (and `-tp p6 -long_double`).
- `jampDed.exe` has no `fldcw` outside the CRT area at the end of the image: the engine does not change the control word itself.
- The Windows routine relies on the caller's rounding mode (it truncates if the mode is chop); the Linux routine forces its own.

### Sys_SnapVector

| Code | Compilers | Result | Example: 0.5, 2.5, -0.5, -224.5 |
|---|---|---|---|
| 1.01 Windows (`fld` / `fistp`) | original `jampDed.exe`; rebuilt with MSVC 6.0 SP6, MSVC 7.1, ICC 5.0.1 Windows | even | 0, 2, 0, -224 |
| 1.01 Linux (`snapvector.nasm`) | original `linuxjampded`; rebuilt and linked with ICC Classic 2021.1 and 2021.10, GCC 13.3 | even | 0, 2, 0, -224 |
| OpenJK built with MSVC (`roundfloat`) | MSVC 19.51 x86 and x64 (OpenJK's file); MSVC 6.0 SP6, MSVC 7.1, ICC 5.0.1 Windows (expression) | **away** | **1, 3, -1, -225** |
| OpenJK built with anything else (`nearbyintf`) | ICC Classic 2021.1 and 2021.10, GCC 13.3, MinGW-w64 i686 | even, but keeps -0 | 0, 2, **-0**, -224 |
| Fix `230ed6518` (`lrintf`) | MSVC 19.51 x86 and x64, ICC Classic 2021.1 and 2021.10, GCC 13.3, MinGW-w64 i686 | even | 0, 2, 0, -224 |

Conclusion: the regression is OpenJK's MSVC `roundfloat`, confirmed with the real compiler and with 2001-2003 compilers. The fix is bit-identical to 1.01 (same hash over the grid, sign of zero included).

### SnapVector

| Code | Compilers | Result | Example: 0.7, 1.5, 3.5, -0.7 |
|---|---|---|---|
| 1.01 Windows (`fld` / `fistp`, `game/q_shared.h`) | rebuilt with MSVC 6.0 SP6, MSVC 7.1, ICC 5.0.1 Windows | even | 1, 2, 4, -1 |
| 1.01 Linux (`(int)` macro, `game/q_shared.h`) | rebuilt with ICC Classic 2021.1 and 2021.10, GCC 13.3 | trunc | 0, 1, 3, 0 |
| OpenJK built with 32-bit MSVC (`fld` / `fistp`) | MSVC 19.51 x86, MSVC 6.0 SP6, MSVC 7.1, ICC 5.0.1 Windows | even | 1, 2, 4, -1 |
| OpenJK built with anything else (`(int)`) | MSVC 19.51 x64, ICC Classic 2021.1 and 2021.10, GCC 13.3, MinGW-w64 i686 | trunc | 0, 1, 3, 0 |
| Fix `230ed6518` (`lrintf`) | MSVC 19.51 x86 and x64, ICC Classic 2021.1 and 2021.10, GCC 13.3, MinGW-w64 i686 | even | 1, 2, 4, -1 |

Conclusion: 1.01 itself differed between Windows (nearest) and Linux (truncation), so no single rule matches both. The decision is to use the 1.01 Windows rule everywhere: at most 0.5 unit of error instead of 1, no bias toward the map origin, and the same rule as `Sys_SnapVector`.

### SnapVector with the caller's rounding mode set toward zero

| Code | MSVC 19.51 x86 | MSVC 19.51 x64 | ICC Classic 2021.10 (SSE2 and x87) | GCC 13.3 (x87) | MinGW-w64 i686 |
|---|---|---|---|---|---|
| `230ed6518` (`lrintf`, mode not forced) | trunc | trunc | trunc | trunc | trunc |
| Proposed function above (mode forced) | even | even | even | even | even |

In the default mode the proposed function gives the same hash as 1.01 Windows on all of these builds.

## Doc updates (`step-06-cpp-codec.md`, section F)

- Where the doc says `SnapVector` "relies on the default mode": after the change it forces round-to-nearest and restores the previous mode, like `Sys_SnapVector`.
- Compilers of the original game, now known from the binaries: Windows server MSVC 7.0 build 9466; Linux server Intel C++ 5.0.1 Build 010730D0 with NASM 0.98. The doc's "The compilers here are not the 2003 ones" can cite them.
- `fld` / `fistp` and `snapvector.nasm` giving ties-to-even is now checked on the original machine code, not only on a rebuild.
- OpenJK's `SnapVector` truncation on 64-bit MSVC and the `roundfloat` rule on MSVC x86 and x64 are measured with MSVC 19.51.
- State the cost plainly: on a Linux server the fixed `SnapVector` differs from 1.01 Linux on any fraction of 0.5 or more (events, missile launch points, dropped items move by at most 1 unit). Per the doc, no client prediction depends on it.
- Mods compile their own `q_math.c`: they keep their old `SnapVector` until they take the change. `Sys_SnapVector` is in the engine and applies to every module.

## Limits of these tests

- No rebuild with the two original compilers (MSVC 7.0, Intel C++ 5.0.1 for Linux): neither could be obtained. Their `Sys_SnapVector` is covered by the original binaries.
- 1.01's `SnapVector` lives in the game modules (`jampgamex86.dll`, `jampgamei386.so`), which were not available; its rows are rebuilds from `game/q_shared.h`.
- Windows executables were run under Wine on an x86-64 host; the FPU instructions execute natively.
- MSVC 19.51 flags used: `/O2 /fp:precise`, plus `/arch:SSE2` on x86. Not the project's exact CMake command line.
- Linux rebuilds used the 1.01 makefile flags (`-D_JK2 -D_M_IX86 -Kc++ -w -DNDEBUG -O2 -unroll -use_msasm`) with a modern ICC, not ICC 5.0.
- The pmove determinism test (`test/pmove/`) was not rerun here. Rerun `validate.sh` / `validate.bat` after the change.
