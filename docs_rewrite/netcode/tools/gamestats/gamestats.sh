#!/bin/bash
# Combat statistics of the game module with bots, for plan step 14: the game plays at the 100 Hz tick as it did at
# sv_fps 20. Runs local dedicated servers (no network, sabers only, 16 bots on mp/ffa3, or a scripted scenario of two
# bots) in parallel and prints the "gamestats" and "svnetstats" lines of each after the measured time.
#
# Usage: [FAST=1] [SEED=<n>] [PREMAP='<cvar> <value>;...'] [SCENARIO='saber 96 medium idle'] gamestats.sh <openjkded binary> <fs_basepath> <game module> <minutes> '<tag>[|<cvars>]' ...
#   <fs_basepath>  the directory holding base/assets*.pk3
#   <game module>  the jampgame module to test (jampgamex86_64.so); each run copies it into its own fs_homepath
#   <cvars>        cheat cvars set after devmap, separated by "; set "; pass each configuration as one
#                  argument (quoted): split at its spaces it would lose its cvars. For example
#                  'bot_debugClientRate 1'
#   SCENARIO       a scripted scenario (codemp/game/g_scenario.c) instead of the free-for-all, two bots: <minutes> must
#                  cover the repetitions and an uncounted first one (2 s each); the scenario prints its own gamestats
#                  at the end, which the watchdog takes as the end of the run
#   SEED           a first game seed: server n of the command line (from 0) gets SEED+n and, with FAST=1, the fixed frame
#                  from before the map loads, so that every run replays the same game; without it the seed comes from
#                  the clock and the free play from the machine's load
# Example (four servers; FAST=1 on a machine nobody plays on):
#   FAST=1 gamestats.sh /srv/jka/openjkded.x86_64 /srv/jka /srv/jka/base/jampgamex86_64.so 10 \
#     'ra|bot_debugClientRate 1' 'rb|bot_debugClientRate 1' 'rc|bot_debugClientRate 1' 'rd|bot_debugClientRate 1'
#   FAST=1 SCENARIO='saber 96 strong attack' gamestats.sh ... 4 's|bot_debugClientRate 1'
#
# The commands go in a cfg run by +exec: the engine keeps only 32 "+" commands of its command line
# (MAX_CONSOLE_LINES), and a long "wait" never fires, so the measured time is chained in waits of 30 seconds.
# Each run's files are in ${GAMESTATS_DIR:-/tmp/gamestats}/<tag>.
set -u

if [ $# -lt 5 ]; then
	sed -n '2,22p' "$0"
	exit 1
fi

BINARY="$1"
BASEPATH="$2"
MODULE="$3"
MINUTES="$4"
shift 4
OUT="${GAMESTATS_DIR:-/tmp/gamestats}"

run() {
	local tag="$1" cvars="$2" idx="$3"
	local home="$OUT/$tag"
	rm -rf "$home"
	mkdir -p "$home/base"
	cp "$MODULE" "$home/base/"
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
			if [ "${FAST:-0}" = "1" ]; then echo "set fixedtime 10"; echo "set timedemo 1"; fi
		fi
		echo "devmap mp/ffa3"
		if [ -n "$cvars" ]; then echo "set $cvars"; fi
		# FAST=1: each tick advances the game by 10 ms and the server runs them as fast as it can (about 8 times real
		# time at 125 Hz with 16 bots; it takes a whole core per server, so not next to a server people play on)
		if [ "${FAST:-0}" = "1" ]; then echo "set fixedtime 10"; echo "set timedemo 1"; fi
		if [ -n "${SCENARIO:-}" ]; then
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
	"$BINARY" +set fs_basepath "$BASEPATH" +set fs_homepath "$home" +set net_enabled 0 +set logfile 2 \
		+exec measure.cfg < /dev/null > "$home/stdout.txt" 2>&1 &
	local pid=$!
	# watchdog: once the second statistics are written, give the server 10 seconds to quit, then kill it
	while kill -0 "$pid" 2> /dev/null; do
		local done_count
		done_count=$(grep -a -c "game statistics" "$home/base/qconsole.log" 2> /dev/null)
		if [ "${done_count:-0}" -ge 2 ]; then
			sleep 10
			kill -0 "$pid" 2> /dev/null && kill -9 "$pid"
			break
		fi
		sleep 5
	done
	wait "$pid" 2> /dev/null
	grep -a "game statistics\|server CPU" "$home/base/qconsole.log" | tail -2 | sed "s/^/[$tag] /"
}

idx=0
for spec in "$@"; do
	IFS='|' read -r tag cvars <<< "$spec"
	run "$tag" "${cvars:-}" "$idx" &
	idx=$(( idx + 1 ))
done
wait
