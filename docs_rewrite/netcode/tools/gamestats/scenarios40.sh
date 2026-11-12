#!/bin/bash
# The 40 scripted scenario cases (10 saber, 6 force, 24 weapon) at sv_fps 20 and 100, SEED=1 and FAST=1, several cases
# at a time; then the comparison of 20 against 100 Hz for each case, and every case's statistics in one file
# (<runs>/scenarios40_<prefix>.txt) to compare two platforms or two builds with diff, and against the recorded counts
# (scenarios40_golden.txt).
# Usage (Windows, Git Bash; gamestats_windows.sh's build and game folders):
#   scenarios40.sh <prefix> <jampgame dll> [cases at a time, 15]
# Usage (Linux; gamestats.sh's arguments in the environment):
#   DED=<openjkded> BASE=<fs_basepath> scenarios40.sh <prefix> <jampgame .so> [cases at a time, 2]
# About 10 minutes with 15 cases at a time on the owner's machine; on the VPS (1 vCPU, 2 GB) 2 cases (4 servers).
set -u
PREFIX=$1
MODULE=$2
HERE=$(cd "$(dirname "$0")" && pwd)
case "$(uname -s)" in
	MINGW*|MSYS*) WINDOWS=1; PAR=${3:-15}; RUNS="${GAMESTATS_DIR:-C:/Users/$USERNAME/AppData/Local/Temp/gamestats}" ;;
	*) WINDOWS=0; PAR=${3:-2}; RUNS="${GAMESTATS_DIR:-/tmp/gamestats}" ;;
esac
PYTHON=$(command -v python3 || command -v python)

cases() {
	for s in fast medium strong dual staff; do for d in idle attack; do echo "4|saber 96 $s $d||saber_${s}_${d}"; done; done
	for f in lightning grip drain rage protect absorb; do echo "3|force 48 $f|g_forcePowerDisable 0|force_${f}"; done
	for w in 4 5 6 7 8 9 10 11 12 13 14 15; do for a in 0 1; do echo "3|weapon 48 $w $a|g_weaponDisable 0|weapon_${w}_${a}"; done; done
}

one() {
	local min scn pre name
	IFS='|' read -r min scn pre name <<< "$1"
	if [ "$WINDOWS" = 1 ]; then
		SEED=1 FAST=1 PREMAP="$pre" SCENARIO="$scn" bash "$HERE/gamestats_windows.sh" "$min" \
			"${PREFIX}_${name}_20|20|$MODULE|bot_debugClientRate 1" "${PREFIX}_${name}_100|100|$MODULE|bot_debugClientRate 1" \
			> /dev/null 2>&1
	else
		SEED=1 FAST=1 PREMAP="$pre" SCENARIO="$scn" bash "$HERE/gamestats.sh" "$DED" "$BASE" "$MODULE" "$min" \
			"${PREFIX}_${name}_20|20|bot_debugClientRate 1" "${PREFIX}_${name}_100|100|bot_debugClientRate 1" > /dev/null 2>&1
	fi
	echo "done $name"
}
export -f one
export PREFIX MODULE HERE WINDOWS DED="${DED:-}" BASE="${BASE:-}"

cases | xargs -P "$PAR" -I{} bash -c 'one "$@"' _ {}
tags=()
for c in $(cases | cut -d'|' -f4); do
	"$PYTHON" -I "$HERE/gamestats_scn.py" "$RUNS" "${PREFIX}_${c}_20" "${PREFIX}_${c}_100" | head -1
	tags+=("${PREFIX}_${c}_20" "${PREFIX}_${c}_100")
done
"$PYTHON" -I "$HERE/gamestats_scn.py" --dump "$RUNS" "${PREFIX}_" "${tags[@]}" > "$RUNS/scenarios40_${PREFIX}.txt"
echo "statistics: $RUNS/scenarios40_${PREFIX}.txt"
# the recorded counts (scenarios40_golden.txt: the same on Windows and Linux, clang, GCC and MSVC, 12 November 2026)
if diff <(tr -d '\r' < "$HERE/scenarios40_golden.txt") <(tr -d '\r' < "$RUNS/scenarios40_${PREFIX}.txt") > /dev/null; then
	echo "golden: SAME"
else
	echo "golden: DIFF (diff $HERE/scenarios40_golden.txt $RUNS/scenarios40_${PREFIX}.txt)"
fi
