#!/bin/bash
# Windows version of gamestats.sh for the owner's machine (llvm-mingw clang build, run under lldb: the clang dedicated
# server crashes without a console). Runs local dedicated servers (no network) with 16 saber bots and prints
# gamestats after a measured time. The runs go in $GAMESTATS_DIR/gs_<tag>; BUILD and GAME are set below.
# Usage: [FAST=0|1] [PREMAP='<cvar> <value>;...'] gamestats_windows.sh <minutes> '<tag>|<fps>|<game dll>[|<cvar value>]' ...   (all run in parallel)
# The commands go in a cfg run by +exec: the engine keeps only 32 "+" commands of its command line
# (MAX_CONSOLE_LINES); the measured time is chained in waits of 30 seconds. FAST=1 (default) sets fixedtime and
# timedemo: the server runs its frames as fast as it can, each advancing the game by one interval.
# A server that does not exit after its quit (seen in fast mode under lldb) is killed once its statistics are written.
set -u
MINUTES="$1"
shift
SCRATCH="${GAMESTATS_DIR:-C:/Users/Yberion/AppData/Local/Temp/gamestats}"
mkdir -p "$SCRATCH"
BUILD="E:/Git/jka/OpenJK_Netcode/build-netcodec/openjk-clang"
GAME="E:/Jeux/Jedi Academy default - netcodev_openjk"
export PATH="/c/Logiciels/llvm-mingw-20261104-ucrt-x86_64/x86_64-w64-mingw32/bin:/c/Logiciels/llvm-mingw-20261104-ucrt-x86_64/bin:$PATH"

# kills the dedicated server whose command line holds this text (its own fs_homepath)
kill_server() {
	powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name='openjkded.x86_64.exe'\" | Where-Object { \$_.CommandLine -like '*$1*' } | ForEach-Object { Stop-Process -Id \$_.ProcessId -Force }" > /dev/null 2>&1
}

run() {
	local tag="$1" fps="$2" dll="$3" extra="$4"
	local home="$SCRATCH/gs_${tag}"
	rm -rf "$home"
	mkdir -p "$home/base"
	cp "$dll" "$home/base/jampgamex86_64.dll"
	local chunk=$(( 30 * fps ))
	{
		echo "set sv_maxclients 20"
		echo "set sv_fps 125"
		echo "set g_gametype 0"
		echo "set fraglimit 0"
		echo "set timelimit 0"
		echo "set g_weaponDisable 524279"
		# the owner's server settings, set here because the dedicated server also reads the openjk_server.cfg of
		# fs_basepath, which differs between machines: sabers and force jump only, no saber locks, no private duels
		echo "set g_forcePowerDisable 163837"
		echo "set g_saberLocking 0"
		echo "set g_privateDuel 0"
		# PREMAP: cvars set before the map loads (latched ones), separated by ";", for example PREMAP="g_forcePowerDisable 0"
		IFS=';' read -ra pre <<< "${PREMAP:-}"
		for c in "${pre[@]}"; do echo "set $c"; done
		# the game seeds its random numbers with the milliseconds since the process started: a random wait before
		# the map loads, in frames of 8 ms whatever the rate tested (400 values, at most 3.2 s), gives each server
		# its own seed (40 values of the frame of the rate tested gave the same seed, so the same game, twice)
		echo "wait $(( RANDOM % 400 + 1 ))"
		echo "set sv_fps $fps"
		echo "devmap mp/ffa3"
		if [ -n "$extra" ]; then echo "set $extra"; fi
		if [ "${FAST:-1}" = "1" ]; then echo "set fixedtime $(( 1000 / fps ))"; echo "set timedemo 1"; fi
		for name in Reborn Reborn Reborn Reborn Reborn Reborn Jedi Jedi Jedi Jedi Alora Desann Tavion Cultist Cultist Cultist; do
			echo "addbot $name 4"
		done
		echo "wait $chunk"
		echo "gamestats"
		echo "svnetstats"
		for (( i = 0; i < MINUTES * 2; i++ )); do
			echo "wait $chunk"
		done
		echo "gamestats"
		echo "svnetstats"
		echo "quit"
	} > "$home/base/measure.cfg"
	( cd "$BUILD" && lldb -b -o run -- ./openjkded.x86_64.exe \
		+set fs_basepath "$GAME" +set fs_homepath "$home" +set net_enabled 0 +set logfile 2 +exec measure.cfg ) > "$home/lldb.txt" 2>&1 &
	local pid=$!
	# watchdog: once the second statistics are written, give the server 10 seconds to quit, then kill it
	while kill -0 "$pid" 2> /dev/null; do
		local done_count
		done_count=$(grep -a -c "game statistics" "$home/base/qconsole.log" 2> /dev/null)
		if [ "${done_count:-0}" -ge 2 ]; then
			sleep 10
			kill -0 "$pid" 2> /dev/null && kill_server "gs_${tag}"
			break
		fi
		sleep 5
	done
	wait "$pid" 2> /dev/null
	grep -a "game statistics\|server CPU" "$home/base/qconsole.log" | tail -2 | sed "s/^/[$tag] /"
}

for spec in "$@"; do
	IFS='|' read -r tag fps dll extra <<< "$spec"
	if [ -z "$tag" ] || ! [[ "$fps" =~ ^[0-9]+$ ]] || [ ! -f "$dll" ]; then
		echo "invalid configuration '$spec': expected '<tag>|<fps>|<game dll>[|<cvars>]'" >&2
		continue
	fi
	run "$tag" "$fps" "$dll" "${extra:-}" &
done
wait
