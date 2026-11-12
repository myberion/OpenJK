"""Copies from an llvm-project checkout the LLVM libc headers that shared/qcommon/q_libm.cpp needs: every header
reachable from the functions' libc/shared/math headers, following each quoted #include whatever its #if, so that every
compiler and target finds what its branches include. Usage: python -I update.py <llvm-project>/libc
Then record the commit in README.md."""
import os
import re
import shutil
import sys

FUNCS = ["%s%s" % (f, s) for f in "sin cos tan asin acos atan atan2 log log2 pow".split() for s in ("", "f")]
FUNCS += ["rsqrtf", "sinpif", "cospif", "tanpif"]  # float only in LLVM libc
# the correctly rounded text to float parser behind LLVM libc's strtof / strtod (not in its shared interface)
EXTRA = ["src/__support/str_to_float.h"]

src = sys.argv[1]
dst = os.path.dirname(os.path.abspath(__file__))
todo = ["shared/libc_common.h"] + ["shared/math/%s.h" % f for f in FUNCS] + EXTRA
seen = set()
include = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)
while todo:
    rel = os.path.normpath(todo.pop()).replace("\\", "/")
    if rel in seen:
        continue
    seen.add(rel)
    text = open(os.path.join(src, rel), "rb").read().decode("utf-8")
    for name in include.findall(text):
        # libc resolves quoted includes from its root, from the including file's folder, or from include/
        for cand in (name, os.path.join(os.path.dirname(rel), name), "include/" + name):
            if os.path.isfile(os.path.join(src, cand)):
                todo.append(cand)
                break

for top in ("hdr", "include", "shared", "src"):
    shutil.rmtree(os.path.join(dst, top), ignore_errors=True)
for rel in sorted(seen):
    os.makedirs(os.path.dirname(os.path.join(dst, rel)), exist_ok=True)
    shutil.copyfile(os.path.join(src, rel), os.path.join(dst, rel))
shutil.copyfile(os.path.join(src, "LICENSE.TXT"), os.path.join(dst, "LICENSE.TXT"))
print(len(seen), "headers,", sum(os.path.getsize(os.path.join(dst, r)) for r in seen), "bytes")
