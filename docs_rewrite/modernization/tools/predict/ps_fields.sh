#!/bin/bash
# Names the offsets of the cgame command predictmiss (plan step 14, part 10): the layout of playerState_t as the
# compiler sees it, one line per 32-bit word, "<offset> <field>". With the predictmiss.txt the command writes as
# argument, prints it with the fields' names instead of their offsets, the per-field lines sorted by their count.
# Usage, from anywhere (this machine, Git Bash, llvm-mingw clang): ps_fields.sh [predictmiss.txt]
export PATH="/c/Logiciels/llvm-mingw-20261104-ucrt-x86_64/bin:$PATH"
R="$(cd "$(dirname "$0")/../../../.." && pwd -W 2>/dev/null || pwd)"
T=$(mktemp -d)
printf '#include "qcommon/q_shared.h"\nint size = sizeof( playerState_t );\n' > "$T/ps.c"
# the dump: "<offset> | <two spaces per depth><type> <name>", a nested struct followed by its members; a field's words are
# those up to the next line's offset
clang -x c -fsyntax-only -Xclang -fdump-record-layouts -I "$R/codemp" -I "$R/shared" "$T/ps.c" 2> "$T/err.txt" |
	awk '/^ *0 \| struct playerState_s$/ { on = 1; next }
	     on && /\[sizeof=/ { sub(/.*sizeof=/, ""); sub(/,.*/, ""); off[++n] = $0; exit }
	     on && / \| / {
	         o = $1; line = $0; sub(/^[^|]*\| /, "", line)
	         depth = match(line, /[^ ]/); sub(/^ */, "", line)
	         k = split(line, w, " ")
	         off[++n] = o; dep[n] = depth; nam[n] = w[k]
	     }
	     END {
	         for (i = 1; i < n; i++) {
	             path[dep[i]] = nam[i]
	             if (dep[i + 1] > dep[i]) continue                    # a nested struct: its members follow
	             full = ""
	             for (d = 1; d <= dep[i]; d++) if (d in path && d % 2 == dep[i] % 2) full = full (full == "" ? "" : ".") path[d]
	             words = (off[i + 1] - off[i]) / 4
	             for (j = 0; j < words; j++) print off[i] + 4 * j, full (words > 1 ? "[" j "]" : "")
	         }
	     }' > "$T/fields.txt"
[ -s "$T/fields.txt" ] || { echo "no layout:"; cat "$T/err.txt"; rm -rf "$T"; exit 1; }
if [ -n "$1" ]; then
	grep '^predictmiss:' "$1"
	awk 'NR == FNR { name[$1] = $2; next }
	     /^field +[0-9]+ / { rest = $0; sub(/^field +[0-9]+ +[0-9]+ +/, "", rest); printf "%-36s %6d  %s\n", name[$2], $3, rest }' \
		"$T/fields.txt" "$1" | sort -k2,2nr
	awk 'NR == FNR { name[$1] = $2; next }
	     /^  diff +[0-9]+ / { rest = $0; sub(/^  diff +[0-9]+ +/, "", rest); printf "  %-34s %s\n", name[$2], rest; next }
	     /^miss |^  start/ { print }' "$T/fields.txt" "$1"
else
	cat "$T/fields.txt"
fi
rm -rf "$T"
