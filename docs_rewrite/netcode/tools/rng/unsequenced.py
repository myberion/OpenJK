"""Statements with two or more random draws that C does not sequence: the draws are not separated by &&, || or ?: (in
VectorSet, by its top-level commas, which the macro turns into comma operators), so a compiler may evaluate them in
either order (clang and MSVC evaluate a call's arguments in opposite orders). Prints them for review; a product of two
draws of the same range gives the same bits in either order. Usage: python -I unsequenced.py <dir>..."""
import os
import re
import sys

DRAW = re.compile(r"(?<![\w.>:])(Q_irand|Q_flrand|Q_RandInt|Q_RandFloat|Q_RandU32|Q_random|Q_crandom|erandom|"
                  r"PM_irand_timesync)\s*\(")


def strip(s):
    """The code with comments and strings blanked (same length), and #if 0 blocks kept as code."""
    out, i, n = list(s), 0, len(s)
    while i < n:
        c = s[i]
        if c == "/" and i + 1 < n and s[i + 1] in "/*":
            j = s.find("\n", i) if s[i + 1] == "/" else s.find("*/", i + 2) + 2
            j = n if j < 0 or j == 1 else j
            for k in range(i, j):
                if out[k] != "\n":
                    out[k] = " "
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and s[j] != c and s[j] != "\n":
                j += 2 if s[j] == "\\" else 1
            for k in range(i + 1, min(j, n)):
                out[k] = " "
            i = j + 1
        else:
            i += 1
    return "".join(out)


for d in sys.argv[1:]:
    for dp, _, files in os.walk(d):
        for f in sorted(files):
            if not f.endswith((".c", ".cpp")):
                continue
            path = os.path.join(dp, f)
            raw = open(path, "rb").read().decode("latin-1")
            s = strip(raw)
            start = 0
            depth = 0
            for i, ch in enumerate(s):
                if ch in "([":
                    depth += 1
                elif ch in ")]":
                    depth -= 1
                elif ch in ";{}" and depth == 0:
                    stmt = s[start:i]
                    draws = list(DRAW.finditer(stmt))
                    if len(draws) >= 2:
                        # sequenced if a sequence point (&&, ||, ?, top-level comma) separates every pair of draws
                        # VectorSet( v, x, y, z ) expands to comma operators: its top-level commas are sequence points
                        is_set = re.match(r"\s*VectorSet4?\s*\(", stmt) is not None
                        level, depths = 0, []
                        for chs in stmt:
                            if chs in "([":
                                level += 1
                            elif chs in ")]":
                                level -= 1
                            depths.append(level)

                        def sequenced(a, b):
                            x = stmt[a.end():b.start()]
                            if re.search(r"&&|\|\||\?", x):
                                return True
                            return is_set and any(stmt[k] == "," and depths[k] == 1 for k in range(a.end(), b.start()))

                        if not all(sequenced(a, b) for a, b in zip(draws, draws[1:])):
                            line = raw.count("\n", 0, start) + 1 + len(stmt) - len(stmt.lstrip("\n"))
                            text = " ".join(stmt.split())
                            print("%s:%d: %s" % (path.replace("\\", "/"), line, text[:200]))
                    start = i + 1
