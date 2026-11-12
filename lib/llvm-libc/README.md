# LLVM libc math functions (vendored subset)

The headers of LLVM libc's header-only math functions (`libc/shared/math`) and of its text to float parser
(`src/__support/str_to_float.h`, behind its `strtof` and `strtod`) that `shared/qcommon/q_libm.cpp` uses, and the
headers they include. They give correctly rounded results for every float function and for most double functions,
so the game computes the same bits on every platform and compiler (survey and measurements:
`docs_rewrite/netcode/12-deterministic-math-survey.md`). Nothing else includes these headers: the code calls the
`Q_` functions of `shared/qcommon/q_libm.h`, and only `q_libm.cpp` knows this library.

- Source: https://github.com/llvm/llvm-project, folder `libc/`, commit `faba310ff3e76fb8fd9d279d01d7af98943ef044`
  (28 October 2026).
- Licence: Apache License 2.0 with LLVM Exceptions (`LICENSE.TXT`), whose exceptions cover combining it with GPL v2
  software.
- Files: every header reachable from the functions' `shared/math` headers, following each quoted `#include` whatever
  its `#if`, so that every compiler and target finds what its branches include; the files are unchanged.
- Update: check out llvm-project at a commit at least two weeks old, run `python -I update.py <llvm-project>/libc`
  (it replaces `hdr`, `include`, `shared` and `src`), record the commit above, then compare the results with the
  previous ones (`docs_rewrite/netcode/tools/mathlib`).
