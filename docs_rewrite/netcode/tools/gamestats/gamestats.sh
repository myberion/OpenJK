#!/bin/bash
# Combat statistics of the game module with bots, for plan step 14: the saber must hit, block and wound about as on the
# 20 Hz server at the 125 Hz tick. Runs local dedicated servers (no network, sabers only, 16 bots on mp/ffa3) in
# parallel and prints the "gamestats" and "svnetstats" lines of each after the measured time.
#
# Usage: [FAST=1] [PREMAP='<cvar> <value>;...'] gamestats.sh <openjkded binary> <fs_basepath> <game module> <minutes> '<tag>|<fps>[|<cvars>]' ...
#   <fs_basepath>  the directory holding base/assets*.pk3
#   <game module>  the jampgame module to test (jampgamex86_64.so); each run copies it into its own fs_homepath
#   <cvars>        cheat cvars set after devmap, separated by "; set "; pass each configuration as one
#                  argument (quoted): split at its spaces it would lose its cvars. For example
#                  'bot_debugClientRate 1; set g_debugSaberGrid 1'
# Example (the four configurations of the step 14 document, each twice):
#   gamestats.sh /srv/jka/openjkded.x86_64 /srv/jka /srv/jka/base/jampgamex86_64.so 10 \
#     'ref20a|20|bot_debugClientRate 1' 'ref20b|20|bot_debugClientRate 1' \
#     'grid125a|125|bot_debugClientRate 1; set g_debugSaberGrid 1' \
#     'tick125a|125|bot_debugClientRate 1; set g_debugSaberGrid 4'
#
# The commands go in a cfg run by +exec: the engine keeps only 32 "+" commands of its command line
# (MAX_CONSOLE_LINES), and a long "wait" never fires, so the measured time is chained in waits of 30 seconds.
# Each run's files are in ${GAMESTATS_DIR:-/tmp/gamestats}/<tag>.
set -u

if [ $# -lt 5 ]; then
	sed -n '2,20p' "$0"
	exit 1
fi

BINARY="$1"
BASEPATH="$2"
MODULE="$3"
MINUTES="$4"
shift 4
OUT="${GAMESTATS_DIR:-/tmp/gamestats}"

run() {
	local tag="$1" fps="$2" cvars="$3"
	local home="$OUT/$tag"
	rm -rf "$home"
	mkdir -p "$home/base"
	cp "$MODULE" "$home/base/"
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
		if [ -n "$cvars" ]; then echo "set $cvars"; fi
		# FAST=1: each frame advances the game by one interval and the server runs them as fast as it can (about 8 times
		# real time at 125 Hz with 16 bots; it takes a whole core per server, so not next to a server people play on)
		if [ "${FAST:-0}" = "1" ]; then echo "set fixedtime $(( 1000 / fps ))"; echo "set timedemo 1"; fi
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

for spec in "$@"; do
	IFS='|' read -r tag fps cvars <<< "$spec"
	run "$tag" "$fps" "${cvars:-}" &
done
wait
