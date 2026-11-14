#!/bin/bash
# Windows version of gamestats.sh for the owner's machine (llvm-mingw clang build, run under lldb: the clang dedicated
# server crashes without a console). Runs local dedicated servers (no network) with 16 saber bots and prints
# gamestats after a measured time. The runs go in $GAMESTATS_DIR/gs_<tag>; GAME is set below, BUILD too unless the
# environment gives another folder (to time two builds of the server against each other).
# Usage: [FAST=0|1] [SEED=<n>] [PREMAP='<cvar> <value>;...'] gamestats_windows.sh <minutes> '<tag>|<game dll>[|<cvar value>]' ...   (all run in parallel)
# The commands go in a cfg run by +exec: the engine keeps only 32 "+" commands of its command line
# (MAX_CONSOLE_LINES); the measured time is chained in waits of 30 seconds. FAST=1 (default) sets fixedtime and
# timedemo: the server runs its ticks as fast as it can, each advancing the game by 10 ms. SEED=<n>: server n of
# the command line (from 0) gets the game seed SEED+n and, in fast mode, the fixed frame from before the map loads, so
# that every run replays the same game (see gamestats.sh).
# A server that does not exit after its quit (seen in fast mode under lldb) is killed once its statistics are written.
set -u
MINUTES="$1"
shift
SCRATCH="${GAMESTATS_DIR:-C:/Users/Yberion/AppData/Local/Temp/gamestats}"
mkdir -p "$SCRATCH"
BUILD="${BUILD:-E:/Git/jka/OpenJK_Netcode/build-netcodec/openjk-clang}"	# the folder of openjkded.x86_64.exe
GAME="E:/Jeux/Jedi Academy default - netcodev_openjk"
export PATH="/c/Logiciels/llvm-mingw-20261104-ucrt-x86_64/x86_64-w64-mingw32/bin:/c/Logiciels/llvm-mingw-20261104-ucrt-x86_64/bin:$PATH"

# kills the dedicated server whose command line holds this text (its own fs_homepath)
kill_server() {
	powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name='openjkded.x86_64.exe'\" | Where-Object { \$_.CommandLine -like '*$1*' } | ForEach-Object { Stop-Process -Id \$_.ProcessId -Force }" > /dev/null 2>&1
}

run() {
	local tag="$1" dll="$2" extra="$3" idx="$4"
	local home="$SCRATCH/gs_${tag}"
	rm -rf "$home"
	mkdir -p "$home/base"
	cp "$dll" "$home/base/jampgamex86_64.dll"
	local chunk=3000	# ticks in 30 s (TICK_MSEC, 10 ms)
	{
		echo "set sv_maxclients 20"
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
		# the map loads, in ticks of 10 ms (400 values, at most 4 s), gives each server its own seed (40 values of a
		# 50 ms frame gave the same seed, so the same game, twice)
		echo "wait $(( RANDOM % 400 + 1 ))"
		# SEED: server n of the command line (from 0) gets the game seed SEED+n (g_debugSeed) and, in fast mode, the fixed
		# frame from before the map loads (the frames of the map's first seconds otherwise follow the real time, so the free
		# play depended on the machine's load): every run then replays the same game, its free play included
		if [ -n "${SEED:-}" ]; then
			echo "set g_debugSeed $(( SEED + idx ))"
			if [ "${FAST:-1}" = "1" ]; then echo "set fixedtime 10"; echo "set timedemo 1"; fi
		fi
		echo "devmap mp/ffa3"
		if [ -n "$extra" ]; then echo "set $extra"; fi
		if [ "${FAST:-1}" = "1" ]; then echo "set fixedtime 10"; echo "set timedemo 1"; fi
		if [ -n "${SCENARIO:-}" ]; then
			# a scripted scenario (codemp/game/g_scenario.c): two bots, SCENARIO="saber 200 medium idle" and the like;
			# MINUTES must cover the repetitions and the uncounted first one (2 s each); the scenario prints its own
			# gamestats at the end, which the watchdog takes as the end of the run
			echo "addbot Jedi 4"
			echo "addbot Reborn 4"
			echo "wait $chunk"
			echo "gamestats"
			echo "scenario $SCENARIO"
		else
			for name in Reborn Reborn Reborn Reborn Reborn Reborn Jedi Jedi Jedi Jedi Alora Desann Tavion Cultist Cultist Cultist; do
				echo "addbot $name 4"
			done
			echo "wait $chunk"
			echo "gamestats"
			echo "svnetstats"
		fi
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

idx=0
for spec in "$@"; do
	IFS='|' read -r tag dll extra <<< "$spec"
	if [ -z "$tag" ] || [ ! -f "$dll" ]; then
		echo "invalid configuration '$spec': expected '<tag>|<game dll>[|<cvars>]'" >&2
		continue
	fi
	run "$tag" "$dll" "${extra:-}" "$idx" &
	idx=$(( idx + 1 ))
done
wait
