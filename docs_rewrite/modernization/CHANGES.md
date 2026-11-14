# What this branch changes compared with OpenJK

Last update: 14 November 2026. Branch `feature/poc-netcode`, compared with OpenJK `master` at `260c59c29`. Multiplayer only (`codemp/` and the shared code it uses); single player is not built.

This page lists every change of the branch, with its reason and what it gives. The work follows the owner's goals stated in the [README](README.md): no loss of precision, no compatibility constraint, as few network settings as possible, a netcode other JKA forks can reuse, Windows and Linux. The measurements in full, and the history of each change, are in the step documents of [progress/](progress/README.md).

## How to read this page

- The **summary** is written for players: what changes in plain words, then the delays before and now, in milliseconds.
- Then **one section per change**, always in the same order: **Before** (OpenJK as it is), **Now** (this branch), **Why**, **Gain** (with its cost when there is one). Each section ends with **Technical details**, folded: the same before and now for readers who know the engine, with the file names. There is no code on this page.
- A number is **measured** when it comes from a statistics command in game (`netstats`, `svnetstats`, `predictmiss`, section 26) or from a benchmark. It is **computed** when it is derived from the rates and not measured; the page says which. The "test server" is a Linux dedicated server with one virtual core, played over the Internet at a ping of about 20 ms; the "local server" runs on the player's machine.
- "Before" is OpenJK. Where OpenJK's default differs from the base game's (the server rate: 40 frames per second in OpenJK, 20 in the base game 1.01), both are given: 20 is the rate the game was tuned on, and the reference of this work.

## Summary

### In short

1. **The server runs and sends 100 times a second**, where the base game ran 20 times and OpenJK runs 40 by default. The client sends 100 times a second too.
2. **The rules of the game still run 20 times a second**: saber, force powers, weapons, damage, doors and lifts behave exactly as on a server at `sv_fps 20`. The game is the same; it is shown and controlled with less delay.
3. **The other players are drawn 7 to 11 ms behind the newest news from the server**, where a 20 Hz server gave about 55 ms and OpenJK's default about 30 ms.
4. **Every player is equal**: the same 10 ms movement step and the same delay from a key to the server at 100, 125 or 333 frames per second. No frame rate jumps higher or acts sooner, and no setting gives an edge.
5. **Exact positions, nothing guessed**: the positions and velocities of the players and the missiles reach the client exactly as the server holds them, and the other players are never drawn where they might be.
6. **Windows and Linux compute the same game, bit for bit**: the same math functions, random numbers and roundings on every system and compiler.
7. **Messages are about 2.5 times smaller**, which pays for most of the higher rate.
8. **No network setting is left**: `rate`, `snaps`, `cl_maxpackets`, `cl_packetdup` and `cl_timeNudge` are gone; the game computes or fixes their values.
9. **The price**: a branch client and an OpenJK server (or the reverse) do not understand each other, and demos of one do not play on the other.

### Delays before and now

#### The words

| word | meaning |
|---|---|
| server frame, tick | one step of the server: it moves the players, runs the game and sends the result. "Tick" is the name of the fixed step of this branch, 10 ms |
| snapshot | the message the server sends to a client: where everything is at that step |
| command | what the client sends to the server: the keys held, the buttons, the view direction, for one step of movement |
| ping, round trip | the time for a message to go to the server and for its answer to come back |
| prediction | the client moves your own player at once, with the same code as the server, without waiting for the server's answer |
| interpolation | the client draws the other players between two snapshots it has already received: a real position, slightly in the past |
| extrapolation | the client draws a player beyond the newest snapshot, going on by its last known speed: closer to the present, but a guess, wrong whenever the player turns |
| display delay | how far in the past the other players are drawn, counted behind the newest snapshot when it arrives |

#### What runs 100 times a second and what stays at 20

In OpenJK one cvar, `sv_fps`, sets everything at once: how often the players move, how often the game's rules run and how often snapshots leave. Raising it lowers the delay but changes the game, since the saber, the force powers and the timers count in server frames. This branch separates the two.

| 100 times a second (every tick, 10 ms) | 20 times a second (every fifth tick, 50 ms) |
|---|---|
| the movement of every player, one command per player | the saber: contacts, damage, blocks, bounces |
| the movement of the bots | the force powers and their timers |
| a snapshot to every client | the missiles, the items, the doors and lifts |
| a packet from every client | the bots' decisions, the level's rules (votes, end of a round, siege) |
| | pain, falling and drowning damage, the end of powerups |

In OpenJK's terms: the movement and the network behave as a server at `sv_fps 100` with `snaps 100`, and the rules of the game as a server at `sv_fps 20`. This is proven, not assumed: 40 scripted fights (saber, force powers, weapons) give the same counts of hits, damage, blocks and deaths at 20 and at 100 (section 1).

#### Delay by delay

The client side is OpenJK with its defaults (`cl_maxpackets 63`, `cl_timeNudge 0`, `cg_smoothClients 1`). Averages; the network trips are the same before and now and are left out.

| delay | OpenJK, server at 20 Hz (the base game's rate) | OpenJK, server at 40 Hz (its default) | this branch | section |
|---|---|---|---|---|
| Between two snapshots | 50 ms | 25 ms | 10 ms | [1](#1-server-tick-of-100-hz-with-the-rules-of-the-game-at-20-hz) |
| Other players drawn behind the newest snapshot (display delay) | about 55 ms (computed from the code; 53.0 ms measured with this branch's first rule, the same within 3 ms) | about 30 ms (computed: one interval plus 5 ms) | 7.0 to 7.8 ms at 125 frames per second, 10.5 to 11.4 ms at 333 (measured) | [4](#4-display-delay-of-the-other-players-computed) |
| Smoothing of the other players, added to it | a frame of their movement: 8 ms at 125 frames per second, 17 ms at 60 | the same | none | [5](#5-other-players-never-guessed) |
| When a snapshot is late or lost | the other players go on by their speed for up to 50 ms (a guess) | the same | they wait at their last real position; frames drawn past the newest snapshot: none without loss, 0.2 to 0.5 % with 2 to 9 % of loss (measured) | [5](#5-other-players-never-guessed) |
| From a key to its command | the next drawn frame: 4 ms at 125 frames per second, 1.5 ms at 333, 8 ms at 60 | the same | the next tick: 5 ms at every frame rate | [2](#2-one-command-per-tick) |
| From the command to its packet | at most 63 packets per second, so every second frame at 125 frames per second: 4 ms more (computed) | the same | none: a packet leaves with each command, 100 per second | [8](#8-network-settings-removed) |
| The command at the server | run when it arrives, but dated in the player's own past | the same | run at its tick, 3.6 to 3.8 ms after it arrives, the same at 100, 125 and 333 frames per second (measured) | [3](#3-commands-made-at-the-start-of-the-tick) |
| From your movement on the server to the snapshot that shows it | the next server frame: 25 ms | 12.5 ms | none: the snapshot of the same tick | [1](#1-server-tick-of-100-hz-with-the-rules-of-the-game-at-20-hz) |
| Length of a movement step | a drawn frame: 8 ms at 125 frames per second, 3 ms at 333 | the same | 10 ms for everyone | [2](#2-one-command-per-tick) |
| A decision of the game (a saber hit, damage, a block) | at the next server frame: 25 ms | 12.5 ms, at a rate the game was not tuned on | at the next 20 Hz instant: 25 ms, as the base game | [1](#1-server-tick-of-100-hz-with-the-rules-of-the-game-at-20-hz) |
| The ping shown | counted in server frames: steps of 50 ms | steps of 25 ms | the real round trip, to the millisecond | [7](#7-ping-measured-in-real-time) |

The whole chain, from your key to another player's screen, both clients at 125 frames per second, in milliseconds (sums of the averages above, computed, not measured end to end):

```
OpenJK, server at 20 Hz
  key -4-> command -4-> packet ==trip==> server -25-> snapshot ==trip==> client -55+8-> screen      96 ms + the trips

OpenJK, server at 40 Hz
  key -4-> command -4-> packet ==trip==> server -12-> snapshot ==trip==> client -30+8-> screen      58 ms + the trips

This branch
  key -5-> command and packet  ==trip==> server  -4-> snapshot ==trip==> client   -7-> screen       16 ms + the trips
```

What it means in a fight:

- **You see the others sooner.** On your screen your own player is at the present and the others are in the past. That gap was the network round trip plus about 55 ms on a 20 Hz server; it is now the round trip plus about 11 ms: 20 ms measured at 125 frames per second on the test server (its shown ping of 20 ms includes the wait for the client's next packet). The server judges saber hits with the positions it holds now, so the smaller the gap, the closer what you aim at is to what the server judges.
- **You learn sooner what happened.** A hit, a block or a death decided by the server is shown about 45 ms sooner than on a 20 Hz server.
- **The others see you sooner**, by about 80 ms against a 20 Hz server and 40 ms against OpenJK's default (the chain above).

What did not get faster, and what it costs:

- The game decides 20 times a second, as the base game does. A saber contact is judged at the next 20 Hz instant, 25 ms later on average: the same as a 20 Hz server, 12 ms more than OpenJK's default of 40, which changes the game's balance.
- A key acts at the next tick, not at the next drawn frame: 5 ms on average instead of 4 ms at 125 frames per second and 1.5 ms at 333. A player at 333 frames per second loses 3.5 ms on its own screen; in exchange every player has the same delay and the same movement.
- There is no lag compensation: a hitscan shot (the disruptor) is judged with the positions the server holds, as before. It was planned for the hitscan shots only (plan step 14, part 8) and dropped on 14 November 2026.
- Bandwidth, measured with 17 or 18 bots: 90 to 115 bytes per snapshot, 100 times a second, about 12 to 15 KB per second down for each client with the packet headers, and about 4.5 KB per second up. OpenJK's format on the same messages would be 2.4 times larger.
- Server CPU, measured on the test server (1 virtual core) with 16 bots and no player: 4.1 % of the core at 100 Hz (5.3 % before the optimisation of plan step 14, part 7, which takes about a third off the time of a tick; section [23](#23-server-fixes)).

#### Prediction, interpolation, extrapolation

| | before | now |
|---|---|---|
| Your own player (prediction) | predicted at every drawn frame, one movement step per frame | predicted tick by tick and drawn between the last two ticks at the frame's exact time: as smooth, the same steps as the server's. Measured against the server: every field right in more than 97 % of the ticks; the position differs in 0.4 % of the ticks, all of them in contact with another player (section [6](#6-prediction-of-your-own-player)) |
| The other players (interpolation) | between two snapshots when the next one is there, then smoothed | always between two snapshots, 100 real positions a second, no smoothing |
| Extrapolation | when the next snapshot is late; and by choice, with a negative `cl_timeNudge` | never. The delay grows by itself on a link with jitter or loss, so that the next snapshot is nearly always there |

### List of changes

Each line links to its section.

**Responsiveness and equality**

| change | before | now |
|---|---|---|
| [1. Server tick](#1-server-tick-of-100-hz-with-the-rules-of-the-game-at-20-hz) | one rate for everything, a setting: 20 or 40 frames per second | 100 ticks per second for the movement and the network, the game's rules at 20, no setting |
| [2. One command per tick](#2-one-command-per-tick) | a command per drawn frame: the movement depends on the frame rate | a command per tick: the same movement for everyone |
| [3. Commands at the start of the tick](#3-commands-made-at-the-start-of-the-tick) | a key waits for the next drawn frame | the same delay at every frame rate |
| [4. Display delay computed](#4-display-delay-of-the-other-players-computed) | about one snapshot interval; `cl_timeNudge` to shift it | as short as the link allows, computed; no setting |
| [5. Other players never guessed](#5-other-players-never-guessed) | smoothed, and carried on by their speed when a snapshot is late | drawn between two real positions only |
| [6. Prediction of your own player](#6-prediction-of-your-own-player) | wrong on lifts at a high server rate, and on a few animations | measured field by field and corrected |
| [7. Ping](#7-ping-measured-in-real-time) | counted in server frames | the real round trip |
| [8. Network settings](#8-network-settings-removed) | `rate`, `snaps`, `cl_maxpackets`, `cl_packetdup`, timeouts | fixed values, nothing to set |

**Precision: the same game on every machine**

| change | before | now |
|---|---|---|
| [9. Player velocity](#9-player-velocity-never-rounded) | rounded at every step: jumps depend on the frame rate | exact |
| [10. Missiles and roundings](#10-missiles-exact-and-one-rounding-rule) | missiles rounded; the rounding differs between compilers | missiles exact; one rule everywhere |
| [11. Math functions](#11-same-math-functions-on-every-platform) | the system's: other results on Windows and Linux | one library: the same bits everywhere |
| [12. Random numbers](#12-same-random-numbers-on-every-platform) | another sequence on each system | one generator: the same numbers everywhere |

**Network format and memory**

| change | before | now |
|---|---|---|
| [13. Messages from the server](#13-messages-from-the-server-coded-by-a-new-codec) | Huffman coding | a new codec: about 0.4 of the bytes, values exact |
| [14. Packets from the client](#14-packets-from-the-client-coded-by-the-new-codec) | Huffman coding | the same codec: about 0.25 to 0.4 of the bytes |
| [15. Server commands](#15-server-commands-sent-once) | repeated in every message until acknowledged | sent once, sent again only when lost |
| [16. Lost gamestate](#16-lost-gamestate-sent-again-quickly) | sent again after a second | sent again after about a round trip |
| [17. Snapshot history](#17-snapshot-history-of-128-messages) | 32 messages | 128 messages: a long loss or a long ping keeps small snapshots |
| [18. Entity store of the server](#18-entity-store-of-the-server-shared) | a copy per client and per snapshot; a forced map restart after hours | one copy while a state does not change; less memory, no restart |
| [19. Messages too large](#19-messages-too-large) | sent empty | sent with the nearest entities |
| [20. Obfuscation](#20-obfuscation-removed) | messages scrambled with a guessable key | not scrambled |
| [21. Demos](#21-demos) | a copy of the network messages | coded for the demo; old demos do not play |
| [22. Downloads](#22-downloads-over-udp) | several blocks per message, by the rate | one block per message |

**Fixes found on the way**

| change | before | now |
|---|---|---|
| [23. Server](#23-server-fixes) | an idle Linux server used a tenth of a core; a clock change could freeze it; work repeated for nothing at every tick | it sleeps; a clock that only moves forward; about a third less time per tick |
| [24. Game](#24-game-fixes) | three random choices differed between compilers; a zoomed animation restarted at every step | fixed |
| [25. Client](#25-client-fixes-outside-the-netcode) | menu cursor too fast, keypad binds, servers hidden from the browser; images decoded by another library on Windows and on Linux; the pk3 archives read through libraries of 2010 and 2013, the system's on Linux | fixed; one image decoder everywhere, more than twice as fast; one reader of the archives everywhere, 1.8 times as fast |

**Reference**: [26. Settings and commands](#26-settings-and-commands), [27. Build, tests and repository](#27-build-tests-and-repository), [28. Compatibility](#28-compatibility), [29. Not changed yet](#29-not-changed-yet).

## A. Responsiveness and equality

### 1. Server tick of 100 Hz with the rules of the game at 20 Hz

**Before.** The cvar `sv_fps` sets how many frames per second the server runs: 20 in the base game, 40 by default in OpenJK. A frame does everything: it runs the game's rules once and sends a snapshot. A higher value shows the game sooner but changes it, because the game was tuned on frames of 50 ms: the saber is traced once per frame, and the force powers, the timers and the bots count in frames.

**Now.** The server runs 100 ticks per second, a constant with no setting. At every tick it moves each player with that player's command and sends a snapshot to every client. Every fifth tick is a "20 Hz instant": the rules of the game run there, with OpenJK's code.

**Why.** To cut the delay without changing the game. Running OpenJK's game code unchanged at 100 Hz breaks it (saber contacts 5 times shorter, damage nearly always 1 point, timers up to 44 % more frequent), and tuning that code tick by tick failed: the saber's reactions need a 20 Hz frame's worth of blade movement.

**Gain.**

- A snapshot every 10 ms instead of every 50 ms (20 Hz) or 25 ms (40 Hz).
- The same game as at `sv_fps 20`. Proof: 40 scripted fights between two bots (saber: 5 styles, defender idle or attacking; 6 force powers; 12 weapons with both fire modes) give the same wounds, damage, deaths, blocks, bounces, shots and hits at 20 and at 100, on Windows (clang, MSVC) and on Linux (GCC). A free-for-all of 16 bots shows no difference beyond the noise (damage -2.8 % with a margin of 1.8 %).
- Cost: server CPU (4.1 % of the test server's core with 16 bots and no player, measured; an empty server 1.1 %), bandwidth (summary), and demos, which hold 100 messages per second instead of 20 or 40 (their size is not measured yet: plan step 14, part 11).

<details>
<summary>Technical details</summary>

| | OpenJK | this branch |
|---|---|---|
| Server rate | `sv_fps`, default 40 | `TICK_MSEC` 10 (`qcommon/q_shared.h`); the cvar is removed |
| Game clock | `level.time`, one step per frame | two clocks (`game/g_local.h`, `g_main.c`): `level.time`, read by all game code, is the logic clock, the last multiple of 50 ms; `level.tickTime` is the tick's time, used only for the bots' command times and the sanity clamp of a command's time |
| On the 20 Hz instants | everything, every frame | thinks, the saber, the force powers and missile blocks, space, hacking, jetpack and cloak fuel, the end of powerups, world damage (lava, drowning) and the pain feedback, the level's rules (votes, exit rules, tournament, siege waves and timers), the bots' AI, all missiles, items and movers (doors, lifts, trains: movement, pushes, crush damage) |
| Every tick | | the players' commands, the bots' commands, the snapshots |
| Clock of the server's models (ghoul2: bolts and model traces) | `sv.time`, set after the frame | the logic instant of the frame just run (`server/sv_main.cpp`), so an instant evaluates the models 50 ms after the previous one, as every frame did at 20 Hz. With `sv.time` at 100 Hz the blades ran 40 ms ahead of their 20 Hz path (5 times less saber damage) |
| Missiles | moved every frame | moved on the 20 Hz instants only: the engine's model trace depends on the step, so per-tick steps changed which shots hit, where, and the damage (the bryar 38 % less, the homing rocket lethal every time) |
| Bots | their AI and one movement per server frame, inside the frame | one command per tick, run as a player's; their AI every 50 ms on the instants. The debug cvar `bot_debugClientRate` is removed |
| After a stall | catches up to 5 s of frames at once | at most 25 ticks (250 ms); the rest is dropped with a "Hitch" message and the clients follow the server's clock as after a `map_restart` |
| `map_restart`, map load | three frames of 100 ms | the same |
| Client | takes the snapshot interval from the server info | from the constant; `timedemo` advances one tick per frame (50 ms before) |

What OpenJK's code does when run unchanged at 100 Hz, and what this branch does:

| what | OpenJK's code at 100 Hz | this branch at 100 Hz |
|---|---|---|
| Saber traces, damage, bounces | every frame; contacts 5 times shorter, damage nearly always 1 point per contact, attacks rarely bounced | on the 20 Hz instants: the 20 Hz damage, bounces and blocks; a contact is found at the instant after it, up to 40 ms later than a per-tick trace would |
| Force power timers (grip, rage, protect, absorb drains, grip damage, force jump charge, seeker drone) | re-armed with "now + n", so every n + 10 ms instead of n + 50 | the 20 Hz lengths |
| Mind trick broken by an attack in view | within 4 frames (40 ms) | within 200 ms (written as a time: the frame-length variable is removed) |
| Pain sound and view kick | 10 points within one frame, which a blow spread over 5 frames rarely reaches | 10 points within a 50 ms window |
| Movers (doors, lifts) | move and crush every frame | on the 20 Hz instants; the client places them accordingly (section 6) |

The scenarios: the server command `scenario` and the script `tools/gamestats/scenarios40.sh`, which compares the 40 cases with recorded counts (section 26). Details: [progress/step-14-fixed-tick.md](progress/step-14-fixed-tick.md) sections 9 and 10, [progress/step-14-audit-100hz.md](progress/step-14-audit-100hz.md) sections 9 to 11.

</details>

### 2. One command per tick

**Before.** The client makes one command per drawn frame, and a command moves the player for the length of that frame: 8 ms at 125 frames per second, 3 ms at 333. The movement code does not give the same result for different step lengths, so the frame rate changes the jumps and the air control. The server runs each command when its packet arrives, in the order of arrival. Each command is dated with the time the client draws the world at, which is in the past: on the server every player lives in its own past, behind by its ping and its display delay. A player can hold its commands back and send up to a second of them at once. When a packet is lost, the player stops on the server until the next one.

**Now.** The client makes one command per tick, 100 per second at any frame rate; the view still turns at every drawn frame. Every command moves the player for 10 ms. The server runs exactly one command per player at each tick, the one made for that tick. To make that possible the client runs a second clock, the command clock, slightly ahead of the server: far enough for a command to arrive just before its tick. The server tells the client in every snapshot how early its command arrived (the input margin), and the client speeds its clock up or slows it down by a few percent to keep that margin small.

- Order within a tick: the players run from the lowest client number to the highest at one tick and in the reverse order at the next, so of any two players each is first every other tick.
- A command that arrives after its tick gets no movement, and what it newly pressed (a button, a jump, a force power) goes into the next one: a short press acts a tick late instead of being lost.
- A tick without a command runs the previous one again, for 250 ms at most, then an empty one: a lost packet no longer stops the player, a lost connection does.

**Why.** Equality: the same movement for everyone, every player at the same time on the server, no way to bank commands. It is also what lag compensation by tick would need (planned as step 14 part 8, dropped).

**Gain.**

- The same movement step for every player, by construction (not re-measured as jump heights in game).
- Measured on the test server (ping 20 ms): every tick ran with the player's own command, except 1 or 2 in 100 s. With 5 % of the client's packets lost: 0.34 % of the ticks without it.
- A charged shot is fair: the charge's start and the server's clock are now the same clock. In OpenJK a charge was longer by the player's latency.
- A third of the commands to predict and to run at 333 frames per second.
- Cost: a key acts at the next tick, 5 ms on average, against 4 ms at 125 frames per second and 1.5 ms at 333 with a command per frame.

<details>
<summary>Technical details</summary>

| | OpenJK | this branch |
|---|---|---|
| Commands made | one per frame (`CL_CreateNewCommands`), dated `cl.serverTime`, the drawn time | one per tick of the command clock, made as the clock enters the tick with the input gathered over the tick before, dated with the time that tick ends; below 100 frames per second a frame makes several, and its generic command (a style change, a force power) goes in the first only |
| Command clock | none | `cl.cmdTime = cls.realtime + cl.cmdTimeDelta` (`client/cl_cgame.cpp`). The client keeps the margins of the last 2 s (64 slots of 32 ms) and moves the clock to where the second smallest is 3 ms (`CMD_MARGIN_MSEC`), by at most about 6 % faster or 7 % slower (Counter-Strike 2's limits), or at once at a connection or when the target is more than 100 ms away |
| Input margin | none | measured by the server in real time between a command's arrival and its tick, sent once in the next snapshot (one byte until the tables are retrained; to the millisecond up to 64 ms either way, steps of 8 ms beyond; `INPUT_MARGIN_NONE` when nothing was measured) |
| Commands run | on arrival, in `SV_UserMove` | kept in a buffer per client (`SV_INPUT_BUFFER` 256 commands), run by `SV_RunClientCommands` in the tick loop after the bots' commands and before the game frame (`server/sv_client.cpp`) |
| Order of the players | the packets' order of arrival | rising client numbers at one tick, falling at the next; the bots before the players, in the order of their numbers; the saber and the force powers from client 0 to 31 on the 20 Hz instants, as OpenJK |
| A late command | cannot exist | no movement time; its new presses (buttons, jump, generic command) go into the next command run, as Counter-Strike 2 does |
| No command | the player does not move; with `g_forceClientUpdateRate` 250 an empty command after 250 ms | the last command again (without its generic command) for 250 ms, then an empty one every tick |
| Commands held back | up to a second of them can be sent at once, and all run on arrival | one command runs per tick; a command dated more than 1 s ahead is not kept |
| `g_synchronousClients`, `g_forceClientUpdateRate` | cvars | removed (`G_RunClient` too): every player runs one command per tick and the client always predicts |
| Commands the client keeps | 64 (`CMD_BACKUP`); "Connection Interrupted" past them | 256, that is 2.56 s |
| Times the movement code writes (weapon charge, rocket lock, zoom) | on the client's past clock | on the game's clock; cgame compares them with the command clock (`cg.cmdTime`) instead of the drawn time |
| Random numbers of the movement code | seeded with the command's time | seeded with the command's tick (`PmoveSingle`, `game/bg_pmove.c`) |
| A move of -128 (the keys give -127 to 127; mouse or joystick sums, or a modified client, can give -128) | read back as -127 by the server | never made: the client's bound (`ClampChar`) is -127 to 127, and the server turns a -128 it receives into -127 |

Measured in game (13 November 2026, `svnetstats` and `netstats`):

| | ticks run with the player's command | commands after their tick | client packets lost |
|---|---|---|---|
| local server, 125 frames per second | 10,383 of 10,383 | 0 | 0 |
| local server, 333 frames per second | 10,418 of 10,418 | 0 | 0 |
| test server, 125 frames per second | 9,384 of 9,385 | 1 | 0 |
| test server, 333 frames per second | 10,104 of 10,106 | 2 | 0 |
| test server, 125 frames per second, 5 % of the client's packets dropped | 13,218 of 13,263 | 43 | 659 |

`cg_showmiss` also prints "shown flicker" when an animation or the crouch shown goes back and forth over three frames, and "anim miss" when a snapshot's animations differ from those the client predicted for the same command time. Details: [progress/step-14-fixed-tick.md](progress/step-14-fixed-tick.md) section 11.

</details>

### 3. Commands made at the start of the tick

**Before.** A command is made when a frame is drawn. A key pressed just after a frame waits for the next one, so the wait depends on the frame rate: on average 5 ms at 100 frames per second, 4 ms at 125, 1.5 ms at 333.

**Now.** The command of a tick is made and sent at the moment the tick begins, between two drawn frames if need be: the game then reads the keys and the mouse and sends the packet, without drawing.

**Why.** The client is for every player. A delay that depends on the frame rate favours the players with the fastest machines.

**Gain.** Measured on the test server (ping 20 ms):

| frames per second | input margin on the server | lowest | commands after their tick |
|---|---|---|---|
| 100 | 3.6 ms | 1 ms | 0 |
| 125 | 3.7 ms | 2 ms | 0 |
| 333 | 3.8 ms | -1 ms | 2 of 9,017 |

- The same delay from a key to the server at every frame rate.
- Against one command per tick made at the drawn frames (this branch before the change, 125 frames per second): the margin was 7.3 ms, so a key reaches the server 3.6 ms sooner on average, and your own player is drawn 8 ms less ahead of the others (20 ms instead of 28).
- The packets leave at the ticks, so the ping is steady and the server codes its snapshots against a base one message newer.
- Cost: the wait before a tick ends with a busy loop of up to a millisecond, up to a tenth of a core more on the player's machine. The mouse filter (`m_filter`) and the mouse acceleration read the mouse at the ticks too, so on shorter spans at a low frame rate.

<details>
<summary>Technical details</summary>

- `Com_Frame` (`qcommon/common.cpp`): the wait for the next frame also ends when the command clock enters a tick (`CL_NextCommandMsec`). The input alone then runs, `CL_InputTick` (`client/cl_input.cpp`): the system's events, the key binds (skipped while a `wait` command counts frames), the view's turn, the tick's command and its packet.
- The frames still make the commands that are due when they run (a tick that begins while a frame is drawn, a frame rate with no wait).
- Not with a scaled or a fixed time (`timescale`, `fixedtime`), in a demo or in a pause.
- The input code measures on a clock of its own (`s_inputTime`) instead of the frame's time: the keys' held time, the time since the input was last read, the time since the last command.
- The dedicated server has the two functions as stubs (`null/null_client.cpp`).
- Details: [progress/step-14-fixed-tick.md](progress/step-14-fixed-tick.md) section 14.

</details>

### 4. Display delay of the other players computed

**Before.** The client draws the world slightly in the past, so that it nearly always holds a snapshot before and after the drawn time. OpenJK's rule moves the client's time forward a little at each snapshot and back when a frame comes too close to the newest one: it settles about 5 ms further back than needed, and with jitter it often passes the newest snapshot and guesses. The player can shift the result by up to 30 ms with `cl_timeNudge`; a negative value means guessed positions, and an edge over a player who does not use it.

**Now.** The client measures, for the last 2 s, how close to the newest snapshot it could have drawn without passing it, and stays 2 ms behind that. On a link with jitter or loss the delay grows by itself, by about the jitter. `cl_timeNudge` is removed.

**Why.** The least delay that still shows real positions, the same rule for every player, no setting.

**Gain.** Measured with `netstats` (delay behind the newest snapshot when it arrives; a frame "past the newest snapshot" is a guessed one):

| | delay | jitter | frames past the newest snapshot |
|---|---|---|---|
| local server, 125 frames per second | 7.8 ms | 2 ms | 0 of 7,320 |
| local server, 333 frames per second | 10.5 ms | 1 ms | 0 of 7,948 |
| test server, 125 frames per second | 7.3 ms (7.0 ms in a later session) | 0 ms | 0 of 5,457 |
| test server, 333 frames per second | 11.4 ms (10.6 ms in a later session) | 1 ms | 0 of 8,110 |
| test server, 125 frames per second, 2 % of the snapshots dropped | 14.5 ms | 8 ms | 28 of 6,943 (0.4 %) |
| test server, 125 frames per second, 9 % dropped | 19.5 ms | 16 ms | 38 of 7,321 (0.5 %) |
| the same server at 20 Hz, this branch's first rule, no loss | 53.0 ms | 2 to 4 ms | 0 of 2,734 |
| the same at 20 Hz, 9 % dropped | 102.8 ms | 4 ms | 7 of 1,153 (0.6 %) |

- The delay depends on the frame rate: no frame is drawn past the newest snapshot, so the last frame before the next snapshot arrives sets it. At 100 frames per second a frame lasts exactly one snapshot interval, and the delay moves between about 2 and 10 ms over a game (2.1 ms measured in one session).
- OpenJK's own rule was not measured: about 55 to 56 ms at 20 Hz, computed from its code.

<details>
<summary>Technical details</summary>

| | OpenJK | this branch |
|---|---|---|
| Rule (`CL_AdjustTimeDelta`, `client/cl_cgame.cpp`) | 1 ms forward at each snapshot, 2 ms back after a frame within 5 ms of the newest snapshot: it settles where that happens in one interval out of three | at every frame the client's time must not pass the newest snapshot's. Each interval between two snapshot arrivals gives a sample: how far ahead the last frame before the next snapshot could have been. The client keeps the smallest sample of each of the last 64 slots of 32 ms and aims 2 ms under the second smallest |
| Speed of the correction | 1 ms forward or 2 back per frame that brought snapshots | at most about 6 % faster or 7 % slower than real time |
| A late snapshot | pushes the client past the newest snapshot | one in 2 s does not raise the delay, two do |
| Loss | no rule | a lost snapshot gives a lower sample, so loss that comes more than once in 2 s raises the delay by itself |
| An interruption of more than 100 ms | | gives no sample |
| A jump of the server's clock (`map_restart` runs three frames of 100 ms) | the client drifts back in a few snapshots | followed at once, measured on the second snapshot that shows it |
| The player's choice | `cl_timeNudge`, -30 to 30 ms | removed. Debug cvars `cl_debugInterpForce` and `cl_debugInterpDelay` force a delay; they are cheat-protected (reset unless the server sets `sv_cheats 1`) |
| The client's own ping (`snap.ping`, the lagometer) | over its last 32 packets, with the last frame's time: 999 above 320 ms of round trip at 100 Hz, up to a frame short | over its last 256 packets, in real time |

The first rule of this branch (plan step 11) took the second latest arrival of the last 64 snapshots minus a full interval, plus one interval above 5 % of loss. The present one (plan step 14, part 4e) gives 5 to 8 ms less delay at 60 and 125 frames per second and about 1 ms less at 333, simulated on the same arrivals (`tools/clock/sim_clock.py`). Survey of what other games do: [surveys/network-settings.md](surveys/network-settings.md), point 5. Details: [progress/step-14-fixed-tick.md](progress/step-14-fixed-tick.md) section 11, "4e".

</details>

### 5. Other players never guessed

**Before.** With the defaults (`cg_smoothClients 1`, `g_smoothClients 1`) another player is drawn between two snapshots when the next one has arrived, and carried on by its speed for up to 50 ms when it has not: a guess, wrong if the player turned. Its drawn position is also smoothed: at each frame it moves half of the remaining distance, so a moving player is drawn behind by a frame of its movement, and a sudden change of direction is spread over 3 to 4 frames.

**Now.** The other players and the NPCs are always drawn between two snapshots, without smoothing. While a snapshot is missing a player stays at its last real position. Both cvars are removed.

**Why.** The least delay comes first, and nothing is guessed. With 100 snapshots per second the smoothing has nothing left to hide.

**Gain.**

- The other players are drawn a frame sooner: 8 ms at 125 frames per second, 17 ms at 60.
- A change of direction shows at once.
- Cost: with loss, a player stops for the length of the missing snapshot (10 ms each) instead of sliding on. Tested in game with 30 % of the snapshots dropped: playable, nothing wrong reported.

<details>
<summary>Technical details</summary>

- cgame: `CG_CalcEntityLerpPositions` (`cgame/cg_ents.c`) always interpolates, which is what `cg_smoothClients 0` did. The filter on the drawn origin (`cgame/cg_players.c`) is gone; it stays for a player held or thrown by another.
- Game module: `g_smoothClients` is removed and the player's entity state keeps the form of its default (the velocity and the command's time, `BG_PlayerStateToEntityStateExtraPolate`). So the messages do not change: the same bytes, and the form the codec's position prediction and tables were made for.
- This removal takes the other value than the default of `cg_smoothClients`, by the owner's decision, against the usual rule (a removed cvar keeps its default's behaviour).
- Details: [progress/step-14-fixed-tick.md](progress/step-14-fixed-tick.md) section 13.

</details>

### 6. Prediction of your own player

**Before.** The client predicts your own player with the same movement code as the server, and corrects it when the server's answer differs. Nothing measures how often it differs beyond the position. Several things make it differ:

- on a lift at a server rate above 20 Hz, the client and the server do not place the lift at the same height;
- an animation left and taken again within one step is restarted by the server only;
- some values the movement code keeps from one step to the next are not sent, and start from zero at every snapshot;
- under loss, the client predicts from an older snapshot than the newest it holds.

**Now.**

- A measuring tool, the console command `predictmiss`, compares the whole predicted state of the player with the server's, field by field, at every tick.
- The lifts and doors are placed where the server has them, for your own player, for the other players and for the items standing on them.
- The server's animation rule is applied in the prediction, and the disruptor's zoomed animation is no longer restarted at every step.
- The unsent values are carried over from the client's own prediction.
- Under loss, the prediction starts from the newest snapshot received.
- Your own player is drawn between its last two predicted ticks, at the frame's exact time.
- Your own sounds and effects (a jump, a saber ignited, a hit taken) are played even in a frame where the server has already run every command the client made, which is common on a local server or at a very low ping below 333 frames per second.

**Why.** Every prediction error that can be removed is removed; what is left is what only the server can know.

**Gain.** Measured with `predictmiss` on the test server (ping 20 ms):

| | before the fixes (22,938 ticks) | after (9,696 ticks) |
|---|---|---|
| Ticks where the position differs | 0.6 %, two thirds of them standing on a lift (up to 25 units) | 0.4 %, all in contact with another player (1.0 unit on average, 2.5 at most) |
| On a lift | a fall predicted at every tick | none |
| Zoomed with the disruptor | an animation difference at every tick | none |

- Every field is predicted right in more than 97 % of the ticks. What differs most is what the game decides on the server: the force's regeneration, the jetpack's fuel, the idle animation, hits, blocks, deaths.
- With 20 % of the snapshots dropped: 17,634 frames predicted from a newer snapshot, and one position difference in 13,484 ticks (a respawn).
- Not removed: the contact with another player. The client collides with the others where it draws them (the past), the server where they are.

<details>
<summary>Technical details</summary>

| | OpenJK | this branch |
|---|---|---|
| Movers for the client's collisions | placed at the snapshot's time, the same as the server's while a snapshot comes per server frame | placed at the server's last 20 Hz instant (`CG_MoverTime`, `cgame/cg_predict.c`): the server moves its movers on the instants only (section 1). Also for the ride of the local player, for the other players and entities standing on a mover (`CG_CarryWithMover`, `cgame/cg_ents.c`), for a spectator, a followed player and a demo |
| An animation left and taken again within one step | restarted by the server only (`BG_StartLegsAnim`, `BG_StartTorsoAnim`, `game/bg_panimate.c`) | the prediction applies the same rule, with the animations the command starts from |
| Zoomed with the disruptor | the torso animation is left and taken again at every movement step, on both sides, so its restart bit changes at every tick and is sent at every tick | the animation is kept (`PM_TorsoAnimation`, `game/bg_pmove.c`; shared code, so the server too) |
| Fields of the player state that are not sent (187 of its 388 words) | 0 at every snapshot | those the movement code keeps between steps (force jump drain delay, zoom lock time, use delay, rocket lock time, jump pad frame) are taken from the client's own prediction of the snapshot's tick (`CG_CarryUnsentFields`), unless a respawn or a teleport lies between |
| Snapshot the prediction starts from | cgame's next snapshot, the first one after the drawn time | the newest one received, when it is ahead (`CG_SnapshotAhead`); not across a teleport, a respawn, a change of followed player, a vehicle boarded or left, or a map restart |
| Between two movement steps | the player is predicted up to each frame's time | the predicted states of the last 64 command times are kept; the player is drawn between the newest tick and the one before at the frame's time (`CG_InterpolateTicks`): position and velocity interpolated, the rest the newest tick's, the view angles of the frame, except on an axis the movement holds (wall run, lunge, jump attack). A pilot is drawn at the newest tick |
| Prediction error check | before each replayed command | one more turn after the newest command: with a command per tick, a frame that brings a snapshot and no new command had no check |
| Held view compared | | on 16 bits, the width the field is sent with |
| Measuring tools | `cg_showmiss`: position and animations | also `predictmiss`: writes `predictmiss.txt` with every word that differs, the position differences sorted by cause, and the first cases in full; `tools/predict/ps_fields.sh` puts the field names |

Decided, not done: the force power levels the movement code reads (saber defence, throw, offence) are not sent, so the client predicts with level 0 (the saber's delay after a block: 700 ms predicted for 250); a block came 5 times in 224 s of fights. They are added to the format with plan step 13. A cache of predicted states was measured and not kept (the replay costs 0.27 % of a core at 125 frames per second and 0.51 % at 333 at a ping of 21 ms). Details: [progress/step-14-fixed-tick.md](progress/step-14-fixed-tick.md) sections 11 and 14.

</details>

### 7. Ping measured in real time

**Before.** The server counts a message's round trip on its frame clock, which moves once per frame: steps of 50 ms at 20 Hz. Every packet that acknowledges the message overwrites the time, so the last one counts, not the first. The history holds 32 messages: above a round trip of 1.6 s at 20 Hz (800 ms at 40 Hz) no message is acknowledged within it and the ping shows 999.

**Now.** The real time when the message leaves and when the first packet acknowledging it arrives, over the last 256 messages.

**Why.** The ping is the measure the players read and the server uses (to send a lost command again); it should be the real round trip.

**Gain.** The ping is exact and steady, and measured up to a round trip of 2.56 s. On the test server it shows 19 to 21 ms.

<details>
<summary>Technical details</summary>

| | OpenJK | this branch |
|---|---|---|
| When a message was sent | `svs.time`, the server's frame clock | the real time (`Sys_Milliseconds`) |
| When it was acknowledged | `svs.time` again, written by every packet acknowledging it | the real time of the first packet acknowledging it |
| Messages measured | the last 32 (`PACKET_BACKUP`), in the frame history | the last 256 (`SV_PING_WINDOW`, the window of the client's 8-bit acknowledge), in a history of their own |
| An acknowledge older than the history | could write into the frame of a newer message | ignored |
| "Not acknowledged yet" | a time of -1 (any time at or below 0 counted as none) | an explicit flag |

The ping is the average over the acknowledged messages among the last 256 (`SV_CalcPings`). It holds the network round trip, the wait for the client's next packet (up to a tick) and the processing.

</details>

### 8. Network settings removed

**Before.** The player sets `rate` (bytes per second), `snaps` (snapshots per second), `cl_maxpackets` (packets per second) and `cl_packetdup` (commands repeated against loss), and the server has policies to bound them. A wrong value costs delay or lost commands, and the right one depends on the server.

**Now.** None of them exists. Every client gets a snapshot per tick and sends a packet per tick; each packet repeats the commands of the 2 packets before it; the timeouts are fixed.

**Why.** One of the owner's goals: a value that needs no measuring is a constant, a value that depends on the link is computed (section 4), and what must not be bypassed is enforced by the server. Recent competitive games do the same ([surveys/network-settings.md](surveys/network-settings.md)).

**Gain.**

- Nothing to set, on any link, and no setting that gives an edge.
- Two client packets lost in a row cost no command (one in OpenJK).
- A dead client frees its slot after 30 s instead of 200.

<details>
<summary>Technical details</summary>

| cvars | OpenJK | this branch |
|---|---|---|
| `rate`, `snaps` (client); `sv_clientRate`, `sv_ratePolicy`, `sv_minRate`, `sv_maxRate`, `sv_lanForceRate`, `sv_snapsMin`, `sv_snapsMax`, `sv_snapsPolicy` (server) | the client asks, the server's policies decide; LAN clients unlimited | removed. A snapshot per tick for every client. A fixed rate of 200,000 bytes per second (`SV_CLIENT_RATE`) only paces what is sent in several fragments (a gamestate, a download, a very large snapshot): a single message counts at most 1,500 bytes, which the rate clears within a tick. The server's `status` loses its `rate` column |
| `cl_maxpackets`, `cl_lanForcePackets` | at most 63 packets per second, as a minimum gap checked once per frame; every frame on a LAN | removed. One packet per tick, 100 per second (`MAX_PACKET_RATE`), and one per frame until the first snapshot. The server decodes at most 110 per second on average (`SV_PACKET_RATE`, 10 % above the client's rate, whose clock may run slightly faster), with a burst of 100 (`SV_PACKET_BURST`), and drops the rest unread (`svnetstats` counts them) |
| `cl_packetdup` | 1: each packet repeats the commands of the previous packet | removed: always 2 (`CL_PACKET_DUP`). A repeated command costs 1.5 to 1.9 bytes (measured) |
| `cl_timeout`, `sv_timeout` (200 s), `sv_zombietime` (2 s) | cvars | removed: 60 s while connecting or loading, 30 s in the game (`TIMEOUT_CONNECTING`, `TIMEOUT_ACTIVE`, both sides), 2 s of zombie state (`SV_ZOMBIE_TIME`) |
| `cl_timeNudge` | -30 to 30 ms | removed (section 4) |
| `sv_fps` | 40 | removed (section 1) |

- Configuration files that still set a removed cvar are harmless: the value is stored and never read.
- The UI's `ui_setRate`, which set `cl_maxpackets` and `cl_packetdup` from `rate`, is removed; the game's menus have no network setting.
- The game module no longer advises `/snaps`; its userinfo validation no longer knows `rate` and `snaps`; bots no longer send them; cgame no longer sets `snaps`.
- `g_userinfoValidate` is a bit mask indexed by the position of each checked field, so removing the two fields moves the bits after them. Its default becomes 6291455 (the same checks as OpenJK's 25165823). A server configuration that still stores the old value turns on the extended ASCII check (names with accents refused) until the cvar is reset (`reset g_userinfoValidate`).
- Details: [progress/step-11-settings.md](progress/step-11-settings.md).

</details>

## B. Precision: the same game on every machine

These changes are in the game module and cgame (`codemp/game/`, `codemp/cgame/`) and in the shared math code. The interface between the engine and the modules is unchanged, but the server's game module and the client's cgame must come from the same source, since cgame predicts the movement with the same code.

### 9. Player velocity never rounded

**Before.** After every movement step the player's velocity is rounded to whole units per second (`pmove_float 0`, the default). The rounding adds up differently with the length of the step, which is the length of a drawn frame: hence the higher jumps at 125 and 333 frames per second. A Windows client and a Linux server did not round a half the same way, which gave prediction errors of up to 2.7 units. Two more cvars, `pmove_fixed` and `pmove_msec`, could force a fixed step for everyone.

**Now.** The velocity is never rounded and the three cvars are removed. With section 2, every player moves in steps of 10 ms.

**Why.** No loss of precision: the client gets the velocity the server holds. And equality: the movement must not depend on the frame rate or on the system.

**Gain.**

- The same jump for everyone.
- A Windows client and a Linux server agree: at most 0.0006 to 0.0009 unit of difference in the movement test, 100 times under the threshold of a prediction error (measured before the math functions were made identical, section 11).
- Cost: 6.5 bytes more per message with bots (6.1 estimated on recorded games), because the tables were trained on rounded velocities; retraining on this build's games (plan step 13) should lower it.
- What it implies: the jumps of 125 and 333 frames per second are gone, and a jump designed for them on a map may no longer pass.

<details>
<summary>Technical details</summary>

| cvar | what it did | OpenJK default | this branch (no cvar) |
|---|---|---|---|
| `pmove_float` | 0: the velocity is rounded to whole units per second at the end of every step. 1: it is kept exact | 0 | always exact |
| `pmove_fixed` | 0: one step per command, as long as the time since the previous command (cut into pieces of at most 66 ms). 1: steps of exactly `pmove_msec` for everyone | 0 | one step per command; every command lasts one tick, 10 ms (section 2) |
| `pmove_msec` | the length of the fixed step of `pmove_fixed 1`, 8 to 33 ms | 8 | not used |

| | velocity rounded (OpenJK default) | velocity exact (this branch) |
|---|---|---|
| Jump height | depends on the frame time | the same for everyone |
| Windows client (MSVC) against Linux server (GCC) | up to 2.7 units of prediction error; with the rounding rule fixed (section 10), 0.0001 to 0.0002 unit in the test, but a rounding can still flip by 1 unit per second | at most 0.0006 to 0.0009 unit, no flip possible |
| Legs of a player pushing against a wall or another player | the velocity the push leaves (under 0.5 unit per second) rounds to 0: the legs stay straight | the legs turn to the velocity's direction only from 1 unit per second on the ground (`BG_G2PlayerAngles`, `game/bg_pmove.c`: the speed under which the movement code takes a player as stopped). Without that threshold the few hundredths of a unit per second the push leaves sideways turned the legs up to 60 degrees, on the client and in the server's model for saber hits |

A mod that takes this game code loses the three `pmove_*` cvars; a mod with its own game code keeps its own physics, as long as its game and cgame match each other. The trap and `Sys_SnapVector` stay in the module interface.

</details>

### 10. Missiles exact and one rounding rule

**Before.** The game rounds a missile's launch point, its velocity and its impact and explosion points to whole units. The rounding function itself differs between compilers: it truncates on most, rounds to the nearest on 32-bit Windows, and the engine's own version rounds a half away from zero with MSVC and to the even number elsewhere.

**Now.** Missiles are not rounded. What is still rounded (events, dropped items, the spawn point) is rounded to the nearest, a half to the even number, on every compiler.

**Why.** The client sees the trajectory the server computes, and a Linux server and a Windows server place things the same way.

**Gain.** Exact trajectories for 0.5 byte more per message on recorded games.

<details>
<summary>Technical details</summary>

| what | OpenJK | this branch |
|---|---|---|
| Missile launch point, velocities, impact and explosion points | rounded to integers (`SnapVector`, `SnapVectorTowards`) | exact |
| `SnapVector` (events, dropped items, spawn point) | truncation, except MSVC 32-bit: nearest | to nearest, ties to even, on every compiler, the rounding mode forced as in `Sys_SnapVector` (the base game's Linux build truncated: its values move by at most 1 unit) |
| `Sys_SnapVector` (the engine's) | ties away from zero with MSVC, to even elsewhere | to even everywhere; no movement call uses it now, kept consistent for any module that does |

Files: `shared/qcommon/q_math.c`, `shared/sys/snapvector.cpp`. The findings: [progress/snapvector-rounding-report.md](progress/snapvector-rounding-report.md). The base game's code, OpenJK's and the fix side by side, for a merge request upstream: [progress/step-06-cpp-codec.md](progress/step-06-cpp-codec.md), part F.

</details>

### 11. Same math functions on every platform

**Before.** OpenJK calls the system's sine, cosine, arc tangent, power and the like. No standard fixes their last bit: it differs between Linux versions, between Windows versions, between 32 and 64-bit programs and between processors. A Windows client and a Linux server therefore do not compute the same movement, and two Linux servers running the same program do not compute the same game.

**Now.** All the multiplayer code calls one set of functions, built on a library whose results are the correctly rounded ones (LLVM libc). Reading a number from text (a cvar, a map entity) goes through it too.

**Why.** Identical results on Linux and Windows, which the prediction, the scripted tests and the comparison of two servers all need.

**Gain.** The same bits on every compiler, system and processor measured (10 builds and the test server). With section 12, the 40 scripted fights give the same counts with clang, MSVC and GCC, on Windows and on Linux.

<details>
<summary>Technical details</summary>

| what | OpenJK | this branch |
|---|---|---|
| `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `log`, `log2`, `pow` and their float versions, everywhere in the multiplayer code and `shared` (game, cgame, UI, client, server, botlib, renderers, sound; not the two third-party libraries inside rend2) | the C library's | one entry point, `shared/qcommon/q_libm.h` (`Q_sinf`, `Q_sin`, `Q_atan2f`, ...), implemented in `q_libm.cpp` on LLVM libc's correctly rounded functions (`lib/llvm-libc`); each call keeps its precision |
| `Q_rsqrt` (inverse square root) | Quake's approximation, about 0.2 % off | `Q_rsqrtf`, correctly rounded |
| `Q_acos`, `Q_asin` | the C library's with a clamp that never applied | the entry point's |
| `Q_powf(float, int)` | an integer power by repeated multiplication, used by the third-person camera | removed; the name is the entry point's real power, called with the same whole exponent |
| `Q_log2(int)` | integer logarithm | renamed `Q_ilog2` |
| `sqrt`, `floor`, `ceil`, `fabs`, `fmod`, `round`, `lrintf`, `isnan`, `isfinite` | the C library's, called directly | through the same entry point, as inline calls to the C library (IEEE 754 fixes their results) |
| Trigonometry of angles in degrees (`AngleVectors`, `RotatePointAroundVector`, the sine trajectories, field of view, effects) | degrees converted to radians in float, then `sin`, `cos`, `tan` | `Q_sinpif`, `Q_cospif`, `Q_tanpif` of the angle in half turns: exact at multiples of 90 degrees (the radians path differed from the exact result on 1.5 million of 2.1 million angles) |
| Angles from vectors (`vectoangles`, `vectoyaw`, the movers' arc) | `atan2`, `atan` or `acos` in float, times 180 over a float pi | `Q_atan2degf`, `Q_atandegf`, `Q_acosdegf`: computed in double, rounded to float once |
| Vector lengths and normalisation | in float, each operation rounded | in double, each result rounded to float once |
| Text to numbers: `atof` (318 calls, the cvars included), `strtod`, the `sscanf` of floats in map entities, vehicle, effect, ICARUS and BSP strings (28) | the C library's: Windows rounds a decimal exactly halfway between two floats away from even, Linux to even | `Q_atof`, `Q_strtod`, `Q_strtof`, `Q_ScanFloats` on LLVM libc's correctly rounded parser |
| `a * b + c` fused into one instruction | allowed (GCC and Clang do it when the target has the instruction) | `-ffp-contract=off`; no effect on today's builds |
| `cg_smoothCamera` | 0 (default): the third-person camera's damping uses an integer power; 1: the real power | removed: always the damping of 0, computed by the entry point's power function |

Survey, measurements and choice: [surveys/deterministic-math.md](surveys/deterministic-math.md).

</details>

### 12. Same random numbers on every platform

**Before.** Three generators: Raven's (15 bits, biased ranges), the system's `rand()`, which gives another sequence on Linux and on Windows, and a special one for the saber's choices in the movement code. A Linux server and a Windows server therefore play differently. The connection challenges come from `rand()` and the clock.

**Now.** One generator (xoshiro128++), the same numbers on every platform, without bias. The connection challenges come from the system's secure random bytes.

**Why.** Identical results everywhere; and a challenge an attacker cannot guess.

**Gain.** The same game on Windows and Linux for the same seed: a run can be replayed exactly (`g_debugSeed`, section 26).

<details>
<summary>Technical details</summary>

| what | OpenJK | this branch |
|---|---|---|
| Generators | `holdrand` (MSVC's LCG, behind `Q_irand`, `Q_flrand`, `irand`, `flrand`), the C library's `rand()`, `PM_irand_timesync` | one entry point, `shared/qcommon/q_rand.h`: xoshiro128++ seeded through SplitMix64, unbiased integer ranges (Lemire), 24-bit floats |
| The saber's choices in the movement code (`PM_irand_timesync`) | drawn by overwriting the command's time (section 24) | from a state seeded with the command's tick; the base game's effective range kept |
| `g_randFix` | how `RandFloat` scaled `rand()`: 0 as the base game, 1 (default) the correct range; its Linux branch never compiled, so Linux servers got values far outside the range | removed: the default's correct range, from the entry point |
| Connection challenges, pure checksum feed | `rand()` and the clock | the system's random bytes (`Com_RandomBytes`) |
| Two draws in one expression | three places (section 24) | none; `tools/rng/unsequenced.py` lists such statements |

Survey and measurements: [surveys/random-numbers.md](surveys/random-numbers.md).

</details>

## C. Network format and memory

### 13. Messages from the server coded by a new codec

**Before.** The server writes each message (snapshot, server commands, gamestate) field by field into a bit stream compressed with one fixed Huffman table. A changed value is written in full.

**Now.** A new library, `netcodec`, builds every message. It knows each kind of field: it codes a value against its previous value or against a prediction (a position from the velocity, a time from the snapshot's time, an angle from its last two values), with statistics learnt on real games.

**Why.** Fewer bytes for the same values, so that 100 snapshots per second stay cheap; and exact values, where the old format cut some to their field width.

**Gain.**

| measurement | OpenJK's format | netcodec | ratio |
|---|---|---|---|
| 20 recorded games kept out of the training (maps not used for it), all messages | about 24 MB | 9.24 MB | 0.385 |
| the same, snapshots only | 155.1 bytes per snapshot | 66.9 | 0.432 |
| in game at 100 Hz, test server with 18 bots, the same messages in both formats | 264.6 to 419.2 bytes per message | 112.7 to 171.0 | 0.417 |

- Coding takes about 0.38 and decoding about 0.56 of OpenJK's time on the same messages (clang; the encoder is another 8 to 14 % faster since section 18).
- Every compiler (MSVC, GCC, Clang, 32 and 64 bits) produces the same bytes, checked by tests on each build.
- Tested in game with 5 % and 20 % of the server's messages dropped, and with a Linux server played from a Windows client over the Internet: no message the client could not decode.

<details>
<summary>Technical details</summary>

| | OpenJK (`msg.cpp`, `huffman.cpp`) | this branch (`codemp/netcodec/`; `server/sv_netcodec.cpp`, `client/cl_netcodec.cpp`) |
|---|---|---|
| Entropy coder | one static Huffman table for every byte | rANS with a separate probability table for each kind of field, trained on real games |
| Unchanged fields | one bit per field up to the last changed one, plus a count byte | only the changed fields are listed |
| Changed values | written in full, or as a 13-bit integer when integral | coded against the previous value or a prediction |
| Floats | full 32 bits or 13-bit integers | by kind: zero, integral, exact angle step, or the exact 32 bits; `-0.0` kept |
| Booleans | a "changed" bit, then the value | the "changed" flag only |
| `user*` fields of mods | 1 bit wide unless a mod widens them with `netf_overrides.txt` or `psf_overrides.txt` | coded by their C type, so the full value arrives; the overrides files are no longer read |
| Server commands | text through Huffman | each one coded against an earlier command of the same kind, token by token (and sent once: section 15) |
| Game directory (`svc_setgame`) | a separate message | carried by the next message |
| Map instance (`serverId`) | only in the system-info config string | also in every message, coded as "unchanged" (a fraction of a bit), so that the client's packets can code theirs against it |
| Input margin | none | one byte per snapshot until the tables are retrained (section 2) |
| End of message | an `svc_EOF` byte | none |
| Precision | what the field widths allow | exactly what the server holds |

- The engine hands its own data to the codec in place: the snapshots, the entity store, the command buffer, the baselines.
- Not changed: connectionless messages (`getinfo`, `getstatus`, `connect`, the master server); the choice of the base snapshot (the newest one the client acknowledged); the netchan and its fragmentation.
- OpenJK's writer is kept in the server, only to compare sizes (`sv_netcodecCompare`, section 26).
- `cl_shownet 1` still prints the packet sizes and `cl_shownet 3` the snapshot line; `cl_shownet 2` prints one line per decoded operation. The per-entity and per-field output (levels 4, -1, -2) is gone.
- A message the client cannot decode (only possible if it no longer holds the base the server used) is dropped whole; the client then asks for a full snapshot, as before.
- Details: [progress/step-06-cpp-codec.md](progress/step-06-cpp-codec.md), [progress/step-07-engine-integration.md](progress/step-07-engine-integration.md).

</details>

### 14. Packets from the client coded by the new codec

**Before.** Each client packet carries the acknowledges and the commands through the same Huffman coding, the commands scrambled with a key.

**Now.** The same codec as the server's messages. The first command of a packet is coded against the snapshot the client acknowledges (its time, its view angles, its weapon), the others against the command before.

**Why.** Fewer bytes, so that 100 packets per second cost less than OpenJK's 63.

**Gain.**

| measurement | OpenJK's format | netcodec | ratio |
|---|---|---|---|
| six recorded games over the Internet, 81,789 packets | 40.68 bytes per packet | 10.07 | 0.25 |
| in game at 100 Hz, three commands per packet | | 12.6 to 13.6 | 0.38 to 0.41 |

- 100 packets of 13 bytes per second are 1.3 KB per second of payload; OpenJK's 63 packets of 27 to 28 bytes were 1.75 KB (measured in plan step 8).
- Coding takes 0.34 to 0.40 and decoding 0.74 to 0.86 of OpenJK's time.
- The server decodes packets from anyone: the decoder bounds everything it builds, and is tested with 400,000 malformed packets on every build and with a fuzzer.
- In game: no packet the server could not decode, also with 10 % of the server's messages or 20 % of the client's packets dropped, after `map_restart` and after a server demo started in the middle of a game.

<details>
<summary>Technical details</summary>

| | OpenJK | this branch |
|---|---|---|
| `serverId`, last server message received, last server command received | three 32-bit values through Huffman | the message as its low 8 bits (the server takes the nearest number at or below its last message sent); `serverId` as "the same as in that message"; the command acknowledge against the commands that message carried |
| Client commands | text through Huffman, each with its sequence number | their count, the first sequence number against that message, the texts coded as the server's command texts |
| User commands | the first against an empty command, the others against the previous one, XORed with a key, through Huffman | the first against the snapshot of the acknowledged message, the others against the previous one, each field with its own statistics |
| User-command key | XOR with the checksum feed, the acknowledge and a hash of the last command | removed |
| End of packet | a `clc_EOF` byte | none |
| A client command longer than 1,023 characters (no client can send one) | the first 1,024 characters read, the rest read as the next fields of the packet | the packet is refused, as malformed |

- What the server does with the decoded values is unchanged: the acknowledge checks, the `serverId` checks, the client commands with the flood protection, the `sv_legacyFixes` clamps. When the commands run changes (section 2).
- The server keeps, for each client, the few values a packet is coded against for each of its last 256 messages (32 bytes each: 8 KB per client).
- The client codes against the acknowledged message only while it is sure the server has sent fewer than 256 messages since: the time since that message arrived plus the round-trip time it measures must stay under 128 message intervals (1.28 s). Otherwise, or right after a gamestate, the packet is coded without it: about twice larger (estimate), never wrong.
- A packet the server cannot decode is dropped and counted by `svnetstats`.
- `cl_nodelta` is unchanged: the client still asks for a full snapshot when it misses the last one.
- Details: [progress/step-08-client-to-server.md](progress/step-08-client-to-server.md).

</details>

### 15. Server commands sent once

**Before.** A server command (a chat line, a score update, a config string) is repeated in every message until the client acknowledges it: 2.6 times each on real games, about 60 % of the bytes of the commands.

**Now.** A command is sent once. It is sent again only when the message that carried it is lost, or together with a newer command so that they arrive in order.

**Why.** At 100 messages per second, repeating until the acknowledge would send each command many times.

**Gain.**

- Up to 4 % fewer bytes on real games.
- Measured in game: no command repeated without loss, on a LAN (ping 4 ms) and on the test server (19 ms). With 4 to 17 % of the messages dropped, 52 of 498 commands were sent again.
- Cost: a command in a lost message arrives about one ping later than with the repeats. The command buffers are twice larger (256 KB per client on the server, 512 KB on the client).

<details>
<summary>Technical details</summary>

| | OpenJK | this branch |
|---|---|---|
| A command not yet acknowledged | in every message | in one message; again when nothing is heard of that message after twice the ping plus a client packet interval (10 ms), or when a newer command is sent |
| `MAX_RELIABLE_COMMANDS` (`qcommon/qcommon.h`) | 128 | 256: a command is coded against an earlier one up to 127 commands back, so both sides must hold at least 256 to share the reference. The client-to-server buffer uses the same constant |
| Client dropped when this many server commands are unacknowledged | 129 | 257 |

`svnetstats` counts the commands sent and the repeats, by cause. Details: [progress/step-10-acks-reliable.md](progress/step-10-acks-reliable.md).

</details>

### 16. Lost gamestate sent again quickly

**Before.** While a client waits for the gamestate (the first large message of a connection), it sends one packet a second. The server learns that a fragment was lost only from those packets, so a loss costs a second or more.

**Now.** The client sends at its normal rate while it waits.

**Why.** A connection should not hang on one lost fragment.

**Gain.** A lost fragment costs about a round trip. Tested: the connection is immediate with 30 % of the messages dropped.

<details>
<summary>Technical details</summary>

`client/cl_input.cpp`: the packet rate while awaiting the gamestate. The connection's own fixed delays stay (`RETRANSMIT_TIMEOUT`, `sv_reconnectlimit`). Details: [progress/step-10-acks-reliable.md](progress/step-10-acks-reliable.md).

</details>

### 17. Snapshot history of 128 messages

**Before.** A snapshot is coded as the differences from an older one the client has confirmed (its base). Both sides keep the last 32 snapshots, so the base is at most 29 messages back. When the client's confirmation is older than that, the server sends a whole snapshot, in several fragments.

**Now.** Both sides keep 128 snapshots: a base up to 125 messages back.

**Why.** At 100 messages per second, 29 messages are 290 ms. A player with a long ping, or in a long burst of loss, would get whole snapshots at every tick.

**Gain.**

- A base is kept up to 1.25 s of round trip or of loss.
- Tested with 95 % of the server's messages dropped on the client: no whole snapshot needed, 2,171 snapshots coded against a base 9 messages back or more.
- Cost: the client keeps 17.4 MB of entity states instead of 4.4 MB. The server's memory is in section 18.

<details>
<summary>Technical details</summary>

- `PACKET_BACKUP` (`qcommon/qcommon.h`) and `eng::kPacketBackup` (`netcodec/engine.h`): 32 to 128. The rest follows: the server's snapshots per client, the codec's frames, the versions kept of each entity (`SV_SNAPSHOT_TICKS`), the client's snapshots, its entity ring (`MAX_PARSE_ENTITIES`, 8,192 to 32,768 states) and its demo frames.
- The format does not change: the base's distance is one symbol that goes up to 255. Its tables were trained on bases at most 29 messages back, so a base further back costs more bits until the retraining (plan step 13); not measured.
- A new client is built in static storage (`SV_DirectConnect`, `server/sv_client.cpp`): with 128 snapshots the structure is 896 KB, too large for the 1 MB stack of a Windows program.
- Details: [progress/step-14-fixed-tick.md](progress/step-14-fixed-tick.md) section 12.

</details>

### 18. Entity store of the server shared

**Before.** For every client the server keeps its own copy of every entity of each of its last 32 snapshots: 139.5 MB for 32 players. The copies are counted on 32 bits, and the server restarts the map before the counter wraps: after about 11.6 hours on one map with 32 players at 20 Hz, which would be 2.3 hours at 100 Hz (computed).

**Now.** The server keeps one copy of an entity's state for as long as it does not change, shared by every snapshot of every client that holds it. The counter is on 64 bits.

**Why.** A window of 128 snapshots (section 17) with a copy per client would take 558 MB. It was also to be the store lag compensation reads (plan step 14, part 8, dropped).

**Gain.**

- Memory: 69.7 MB reserved for 128 snapshots, in use only where a state was written. Measured, whole server process with 16 bots: 42 to 48 MB on Windows (57 MB at the window of 32, before the array was zeroed by the system), 82.6 MB on the Linux test server.
- No forced map restart.
- The encoder skips an entity that did not change since the base without comparing the two states (86.7 % of the entities on recorded games): encoding is 14.3 % faster with clang, 12.1 % with GCC, 8.4 % with MSVC. The bytes sent are the same.

<details>
<summary>Technical details</summary>

- `svs.snapshotEntities` holds `SV_SNAPSHOT_TICKS` (128) versions of each of the 1,024 entities; version v of entity n is at place `n * SV_SNAPSHOT_TICKS + v % SV_SNAPSHOT_TICKS` (`SV_SnapshotPlace`, `server/sv_snapshot.cpp`). A tick compares an entity's state with its last copy once, for the first client that sees it, and writes a new version only if it differs.
- A client's snapshot is a list of places. An unchanged entity is at the same place in a snapshot and in its base, so the encoder skips it on the places alone (library: `EntityRing::index`, `Frame::index`).
- The array is allocated zeroed by the system instead of cleared by hand at every map, and the versions of an entity are neighbours: the memory in use is that of the entity numbers in use. On Linux with transparent huge pages a write makes 2 MB resident, which is why the test server uses more than Windows.
- A snapshot too large is cut by rewriting its list, not the shared states. The bots and `sv_netcodecCompare` read their snapshot's entities through the list.
- Lag compensation would have needed the version each entity had at each tick (a byte per entity and tick); not written, the part is dropped.
- Details: [progress/step-14-fixed-tick.md](progress/step-14-fixed-tick.md) section 12.

</details>

### 19. Messages too large

**Before.** When a snapshot does not fit in a message, the server clears it and sends it empty, with a warning in its console.

**Now.**

- A message that codes larger than 1 KB is also coded in a raw form, and the smaller of the two is sent: a worst case stays close to the raw size.
- A snapshot still larger than the limit (49,152 bytes) is sent with the entities nearest to the player; the others come back in the next message.
- A gamestate that large drops the client, which returns to the menu with the reason.

**Why.** Never drop a snapshot silently.

**Gain.** Tested in game with a lowered limit: 296 snapshots cut, no message the client could not decode.

<details>
<summary>Technical details</summary>

`server/sv_netcodec.cpp` (`SV_NetcodecFitSnapshot`: a binary search over the number of entities). The debug cvar `sv_netcodecMaxBytes` lowers the limit to test both cases; `svnetstats` counts the cut snapshots and the messages sent raw. Details: [progress/step-09-safety-limits.md](progress/step-09-safety-limits.md).

</details>

### 20. Obfuscation removed

**Before.** OpenJK scrambles each message, in both directions, with a key made of the connection's challenge, the message numbers and the last command texts. It is not security: anyone who sees the connection can undo it.

**Now.** Removed in both directions, with the key of the client's commands.

**Why.** The receiver found the key from numbers read in clear at the start of the message; the codec now codes those numbers, so the scheme could not stay as it was.

**Gain.** None for the player. Whether to replace it with real encryption is decided in plan step 12.

<details>
<summary>Technical details</summary>

`server/sv_net_chan.cpp`, `client/cl_net_chan.cpp`: `SV_Netchan_Encode`, `CL_Netchan_Decode` and their counterparts no longer XOR the payload.

</details>

### 21. Demos

**Before.** A demo is a copy of the network messages. A client demo asks the server for a full snapshot to start.

**Now.** A server demo is still a copy of the messages sent. A client demo codes each message again for the demo, so that the file refers only to what it contains.

**Why.** A demo is played back from an empty state, so each message may only refer to snapshots and commands that are in the demo. The client cannot know which earlier messages the server's next ones will refer to, and lost messages would leave holes.

**Gain.**

- A client demo is complete by construction, also after losses, and starts without asking the server for anything.
- Cost: one more coding per message on the player's machine, about 1.5 microseconds. Demos hold 100 messages per second (their size is not measured yet).
- What it implies: demos recorded with OpenJK or the base game do not play on this build, and the reverse.

<details>
<summary>Technical details</summary>

| | OpenJK | this branch |
|---|---|---|
| Server demo (`svrecord`, `sv_autoDemo`) content | copy of the messages sent | the same |
| Server demo start | waits for a full snapshot (`demowaiting`), then forbids older bases (`minDeltaFrame`) | resets the codec's view of the client: the next message has no base and the later ones refer only to messages of the demo |
| Client demo (`record`) content | copy of the messages received | each decoded message coded again against the demo's previous snapshot, with the commands it brought and without its download block |
| Client demo start | asks the server for a full snapshot and waits for it | nothing asked of the server |
| Gamestate at the start | OpenJK's format | coded, with the last 127 server commands |
| Cost | none | server: none with `sv_autoDemo`; about one round trip of full snapshots after a `svrecord` in the middle of a game |

The files keep the `.dm_26` extension for now. As in OpenJK, server demos still open when the server quits have no end marker; the player stops cleanly at the end of such a file. A reader or converter for old demos is decided with the protocol number, at the end of the plan. Details: [progress/step-07-engine-integration.md](progress/step-07-engine-integration.md), part 3.

</details>

### 22. Downloads over UDP

**Before.** The server sends as many 2,048-byte blocks of a file as the client's rate allows in one snapshot interval, up to 8.

**Now.** One block per message.

**Why.** The codec carries one download operation per message. A better path (several blocks per message, or a channel of its own) is decided with the transport, in plan step 12.

**Gain.** None intended. Computed from the code, not measured: a block is sent in two fragments, one per tick, so about 100 KB per second on every link. OpenJK is bounded by the rate: 50 KB per second with its default `sv_clientRate`, 100 KB per second on a LAN. HTTP downloads are not affected.

<details>
<summary>Technical details</summary>

`SV_WriteDownloadToClient` (`server/sv_client.cpp`) gives one block to each coded message; a message above 1,300 bytes leaves in fragments, paced at `SV_CLIENT_RATE`. The window of 8 blocks and the one-second resend of a lost block are OpenJK's.

</details>

## D. Fixes found on the way

### 23. Server fixes

**Before.**

- A dedicated server spends the last millisecond before each frame in a busy loop: up to a tenth of a core at 100 Hz, even when empty.
- Small vector operations (a dot product, a copy, a subtraction) are functions called from every loop of the collision and model code: the dot product alone is 11.5 % of a server tick with 16 bots.
- At every server frame, for every client, the game searches the nearest navigation node, which only NPCs use.
- At every command of every client, the game computes a distance and two arc tangents to every other client, used only in the Jedi Master game type or against a client with force sight on.
- Every call about a player's model (a bone, a bolt, a collision) looks the model up again by its file name, and reads a setting by its name.
- The server builds a snapshot for every bot at every tick, and sends it to no one.
- On Linux the engine reads the wall clock. When the system's time is set back (a time synchronisation, a date set by hand), the server runs no frame and sends nothing until the clock catches up; beyond 30 s every client times out. When it is set forward, up to 5 s of frames run at once.

**Now.**

- Outside Windows the server sleeps that millisecond, and a packet that arrives still wakes it at once.
- On Linux the engine reads a clock that only moves forward.
- The small vector operations are inline, for the server and the client.
- The navigation node is searched 20 times a second, as on a 20 Hz server.
- The distance and the angles are computed only when a test needs them.
- The model lookup is cheaper, on the server and on the client; the setting is found once.
- A bot's snapshot is built only when the bot is recorded in a demo.

**Why.** Found while measuring and profiling the server at 100 Hz.

**Gain.** An empty server at 100 Hz on the test server: 9.7 % of a core before, 1.1 % after (measured over 120 s). A change of the system's time no longer freezes the server. The speed changes, with 16 bots and the same game replayed tick for tick (same statistics, the 40 scripted fights unchanged): 35 % less time per tick on the test server with GCC (311 to 201 microseconds), 30 % with MSVC on Windows (360 to 253), about 36 % with clang on Windows (four steps, each timed against the one before: 368 to 309 microseconds, 324 to 274, 279 to 267, 264 to 250). In real time: 5.3 to 4.1 % of the test server's core, 4.8 to 2.6 % of one core of the Windows machine. Nothing changes in the simulation and no delay is added.

<details>
<summary>Technical details</summary>

- `Com_Frame` (`qcommon/common.cpp`): outside Windows the last millisecond is slept in steps of 100 microseconds (`NET_Sleep` takes microseconds, `qcommon/net_ip.cpp`). Windows keeps the busy loop (`select` waits whole timer periods there); OpenJK's cvar `com_busyWait 1` keeps it everywhere.
- `Sys_Milliseconds` (`shared/sys/sys_unix.cpp`): `clock_gettime( CLOCK_MONOTONIC )` instead of `gettimeofday`, at the same cost; time spent suspended is no longer counted. The unused `baseTime` parameter is gone.
- `shared/qcommon/q_math.h`: `VectorAdd`, `VectorSubtract`, `VectorScale`, `VectorMA`, `VectorSet`, `VectorClear`, `VectorCopy`, `VectorLength`, `VectorLengthSquared`, `Distance`, `DistanceSquared`, `VectorInc`, `VectorDec`, `VectorInverse`, `CrossProduct`, `DotProduct`, `VectorCompare` are `static` inline functions with the bodies they had in `q_math.c` (macros in Quake 3, functions in OpenJK); same semantics, an aliased output included.
- `NAV_FindPlayerWaypoint` on the 20 Hz instants (`game/g_main.c`); `G_UpdateClientBroadcasts` skips a pair of clients when neither of its tests applies (`game/g_active.c`).
- `G2_SetupModelPointers` (`rd-dedicated/G2_API.cpp`, `rd-vanilla/G2_API.cpp`): the cvar `dedicated` is found once (`G2_Dedicated`); the model hash (`generateHashValue`, `tr_model.cpp`) folds the case without `tolower`. The model is still looked up at every call, as in OpenJK.
- `SV_SendClientSnapshot` (`server/sv_snapshot.cpp`): no snapshot for a bot unless it is recorded (`sv_autoDemoBots`, a demo started on it); `BotGetSnapshotEntity`, which the base game's bots never call, then finds no entity.
- The profile and the timings: [progress/step-14-fixed-tick.md](progress/step-14-fixed-tick.md) section 15.
- Windows keeps `timeGetTime`, already monotonic. Its value changes every 0.5 ms with the timer period the engine asks for, so a frame starts 0 to 0.5 ms after its millisecond (measured), without accumulating; `QueryPerformanceCounter` would cost 14.7 ns per call instead of 2.8 for no visible gain.

</details>

### 24. Game fixes

**Before.**

- The saber's random choices in the movement code (chain ends, lock breaks, flip and jump attacks) draw their numbers by overwriting the command's time, so the rest of that movement step reads a random time: footstep and slope timers, weapon charge, saber lock and hand extension times, zoom.
- Three places draw two random numbers in one expression, in an order the language leaves to the compiler: the bouncing flechettes of the alternate fire (their speed and their lifetime), an NPC's victory voice and the Galak mech's aim. A server built with MSVC gives other flechettes than one built with clang or GCC.
- Zoomed with the disruptor, the torso animation is left and taken again at every movement step.

**Now.**

- The saber's choices draw from a seed of their own; the command's time stays intact. The numbers are the same, and the client and the server still agree.
- The draws are made one after the other, in the left-to-right order.
- The zoomed animation is kept.

**Why.** Each one made two machines, or the client and the server, compute something different.

**Gain.** The flechette's alternate fire was the one scripted fight of 40 that differed between a clang and an MSVC server; all 40 now agree. No animation difference while zoomed (section 6).

<details>
<summary>Technical details</summary>

- `PM_irand_timesync` (`game/bg_saber.c`).
- `WP_CreateFlechetteBouncyThing` (`game/g_weapon.c`), `NPC.c`, `NPC_AI_GalakMech.c`.
- `PM_TorsoAnimation` (`game/bg_pmove.c`): the test that sets the weapon's ready animation is skipped while zoomed with the disruptor.
- The legs of a player pushing against a wall: section 9. `g_randFix`: section 12.

</details>

### 25. Client fixes outside the netcode

Made on the way while testing, mostly as TaystJK does.

| fix | before | now | files |
|---|---|---|---|
| Menu cursor speed | moves by raw screen pixels: 3 times faster than the desktop at 1920 x 1080 | moves by mouse deltas scaled to the 640 x 480 menu space | `ui_main.c` |
| `r_aspectCorrectFonts` | narrows the spacing but not the glyphs: letters overlap | narrows both | `tr_font.cpp` |
| Keypad with num lock on | keypad keys become digits everywhere, so `KP_*` binds never fire in game | digits only while typing (and not while binding a key) | `sdl_input.cpp`, `ui_shared.c` |
| Right shift, ctrl, alt | the same keys as the left ones | keys of their own (`RSHIFT`, `RCTRL`, `RALT`), bindable apart; shortcuts accept either side | `cl_keys.cpp`, `sdl_input.cpp`, `ui_shared.c` |
| Dropped while connecting | a client dropped before its game screen runs (kicked while loading, for example) keeps waiting | back to the menu with the reason | `cl_parse.cpp` |
| Image files | JPEG decoded by libjpeg 9a on Windows and by the system's library on Linux, which give other pixels (up to 71 levels of 255 on a channel of a player's skin); PNG refused unless 8-bit RGB or RGBA with sides that are powers of two; a malformed TGA stops the game | one decoder for JPEG, PNG and TGA on every platform, the same pixels everywhere (those the Linux build drew), every PNG accepted, a malformed image refused with a warning. The game's JPEG files decode 2.4 times as fast and its PNG files 2.0 times: a map loads in 0.89 s instead of 1.16 s and a player's model in 19 ms instead of 27 (measured with the textures' compression off) | `shared/rd-common/tr_image_codec.cpp`, `tr_image_load.cpp` |
| Archive files (pk3) | read through minizip (2010) on zlib 1.2.8 (2013) on Windows and on the system's zlib on Linux; an archive's directory read entry by entry, twice; the CRC-32 of a file computed and never looked at; a file that could not be inflated handed to the game as it was | one reader on every platform, on zlib-ng 2.3.3 built with the project: all of the game's files read in 2.08 s instead of 3.70 (1.8 times as fast), the directories of its four archives in 0.5 ms instead of 81; a file whose CRC-32 is wrong, or that cannot be inflated, is refused with a warning, as a file that is not there; a file read in pieces (a video, music) is still inflated as it is read | `shared/qcommon/q_pk3.cpp`, `q_pk3.h`, `shared/qcommon/files.cpp` |
| Server browser | a ping of 0 means "request sent", "no answer" and "answered in 0 ms" at once, so a server answering in under 1 ms (same machine or LAN) is hidden | each server has a state of its own (not asked, waiting, answered, no answer); the ping is only a measure | `client.h`, `cl_main.cpp`, `cl_lan.cpp`, `cl_uiapi.cpp`, `ui_public.h`, `ui_main.c`, `ui_syscalls.c` |

The server browser's change adds an import for the UI module, `trap->ext.LAN_GetServerState`, and makes `serverInfo_t` larger, so an existing `servercache.dat` is discarded once, favourites included.

## E. Reference

### 26. Settings and commands

Removed cvars. A removed cvar keeps the behaviour of its default, except `cg_smoothClients`.

| cvar | what stays | section |
|---|---|---|
| `sv_fps`, `bot_debugClientRate` | a tick of 10 ms; the bots send a command per tick | 1 |
| `g_synchronousClients`, `g_forceClientUpdateRate` | one command per player and tick; the last command repeated 250 ms | 2 |
| `cl_timeNudge` | the display delay is computed | 4 |
| `cg_smoothClients`, `g_smoothClients` | the other players between two snapshots, not smoothed; the messages as with `g_smoothClients 1` | 5 |
| `rate`, `snaps`, `sv_clientRate`, `sv_ratePolicy`, `sv_minRate`, `sv_maxRate`, `sv_lanForceRate`, `sv_snapsMin`, `sv_snapsMax`, `sv_snapsPolicy` | a snapshot per tick | 8 |
| `cl_maxpackets`, `cl_lanForcePackets`, `cl_packetdup` | a packet per tick, the commands of 2 packets repeated | 8 |
| `cl_timeout`, `sv_timeout`, `sv_zombietime` | 60 s while connecting, 30 s in the game, 2 s of zombie state | 8 |
| `pmove_float`, `pmove_fixed`, `pmove_msec` | exact velocity, steps of one tick | 9 |
| `cg_smoothCamera` | the default's camera damping | 11 |
| `g_randFix` | the default's correct range | 12 |
| `sv_padPackets` | nothing: it padded only OpenJK's format | 13 |

Changed: `cl_shownet` has levels 1, 2 and 3 (section 13). `g_userinfoValidate` has a new default (section 8).

Changed: the lagometer (`cg_lagometer 1`) counts in time and shows one more measure. A column was one drawn frame (upper graph) or one snapshot (lower graph): the graph held 0.48 s at 100 frames per second and 0.14 s at 333, so a loss left it before it could be read, sooner for a player at a high frame rate. A column is now 50 ms of real time and shows the worst of what happened in it: 2.4 s for everyone. Every bar is a delay, so a thin graph is a good connection: the usual values are about one pixel high.

| where, colour | what it measures | what it means for you | usual | when it grows |
|---|---|---|---|---|
| top, cyan (new) | how long your command waited on the server before the tick that ran it (the input margin; the highest of the 50 ms) | a delay added to your keys and your mouse; the client keeps it as short as it safely can | about 4 ms: 1 pixel (12 pixels for 50 ms) | your packets reach the server irregularly: the client then sends them earlier so that none is late |
| top, magenta (new), the whole band | a command reached the server after its tick | the server ran that tick with your previous command; what you had just pressed took effect a tick later (10 ms) | none | packets of yours late or lost |
| middle, blue, below the line | how far behind the newest snapshot the world is drawn (the farthest of the 50 ms) | the other players are shown that much in the past, on top of the network's own delay | 7 to 11 ms: 1 pixel (16 pixels for 150 ms) | the snapshots arrive irregularly or get lost: the client waits longer so that the next one is there in time |
| middle, yellow, above the line | a frame was drawn after the newest snapshot ran out: the next one was late | the other players stand still for that time, then catch up at once | none | a snapshot later than the blue delay covers |
| bottom, green | your ping (the highest of the 50 ms) | the round trip to the server: your own movement is predicted, but hits, damage and the other players reach you after it | 20 ms is half a pixel, 100 ms about 3 pixels (24 pixels for 900 ms, as before) | the route to the server |
| bottom, yellow in place of green | the server held a snapshot back to respect the rate | a snapshot sent late | none: the rate is 200,000 bytes per second | a message far larger than usual |
| bottom, red, the whole band | a snapshot of the server never arrived | the other players' positions of that tick are missing: if the next snapshot is not there in time, they stand still until it comes (the yellow above the line) | none | loss on the way from the server |
| an empty column | nothing was read in those 50 ms | the connection was interrupted, or the client itself was stopped (a load, for example) | none | |

The client gives cgame the input margin with each snapshot (`snapshot_t`, `cgame/cg_public.h`): the client and cgame must be of the same build. Details: [progress/step-14-fixed-tick.md](progress/step-14-fixed-tick.md) section 16.

Added cvars, all for debugging or measuring:

| cvar | side | what it does |
|---|---|---|
| `sv_netcodecCompare` | server | also codes each message and each client packet in OpenJK's format and prints the byte ratios, every 10 s (1) or every message (2) |
| `sv_netcodecMaxBytes` | server | the largest message the server codes (0: 49,152 bytes), to test what happens to larger ones (section 19) |
| `cl_debugInterpForce`, `cl_debugInterpDelay` | client, cheat-protected | force a display delay (section 4) |
| `g_debugSeed` | server (game module) | a non-zero value replaces the game's start seed: with `fixedtime` set before the map loads, a run replays the same game |

Added commands. The statistics commands print their counters since the previous call.

| command | side | what it prints |
|---|---|---|
| `svnetstats` | server | the server's CPU time: the server frames and, within them, the game frames, the bots, the players' commands and the snapshots sent, each with its count, mean and largest time, the frames longer than a tick and the share of one core. Then for each client: messages, bytes per message, messages sent raw, snapshots without a base, snapshots cut to fit, the base's age; the ping; server commands sent and repeated, by cause; client packets, bytes per packet, packets not decoded or dropped above the packet rate, commands per packet, packets lost on the way and the length of each run of losses; how much the time a snapshot leaves changes against the game clock; the ticks run with and without the player's command, the late commands, the mean and lowest input margin |
| `netstats` | client | messages lost on the way and not decoded, bytes per message, snapshots without a base, the base's age; the display delay, the jitter of the arrivals, the frames drawn past the newest snapshot; the command clock (how far ahead of the drawn time, the mean and lowest input margin the server reported); client packets sent, bytes per packet, packets coded without a base |
| `predictmiss` | client (cgame) | writes `predictmiss.txt`: every difference between the predicted player state and the server's (section 6) |
| `gamestats` | server (game module) | combat statistics: saber swings, wounds, damage, blocks, parries, locks; projectiles blocked, force damage, deaths; for each force power the starts, the periodic events and the time held; the health taken and the deaths by cause, the shots by weapon and fire mode, the saber attacks started, bounced or deflected by style |
| `scenario` | server (game module), cheat | `scenario saber <repetitions> <fast\|medium\|strong\|dual\|staff> <idle\|attack> [distance]`, `scenario force <repetitions> <power> [distance]`, `scenario weapon <repetitions> <weapon number> <0\|1> [distance]`, `scenario off`: two bots, placed face to face and reset every 2 s, play a fixed script of commands timed in milliseconds; `gamestats` is printed at the end. With `scenario_trace <n>` the two bots' state at every 20 Hz instant and every health loss are printed for the first n repetitions. Every reset puts the bots back in a state that does not depend on what came before |

`cg_showmiss` prints two more kinds of lines (section 2).

### 27. Build, tests and repository

| | OpenJK | this branch |
|---|---|---|
| New library | | `codemp/netcodec/`: C++20 inside, a C++11 interface for the engine (`engine.h`), trained tables compiled in; linked by the client and the dedicated server |
| Math functions | the C library's | `lib/llvm-libc/`: the headers of LLVM libc's math functions (Apache 2.0 with LLVM exceptions, a fixed commit, `update.py` to refresh them); `shared/qcommon/q_libm.cpp` (C++17) in the static library `qlibm`, linked by every binary that uses `q_math.c`, the single-player targets included; `-ffp-contract=off` for GCC and Clang |
| Image libraries | libjpeg 9a and libpng 1.6, built with the project on Windows only; the system's on Linux | `lib/wuffs/` (Wuffs, MIT, decodes JPEG, PNG and TGA) and `lib/stb/` (stb_image_write, writes the screenshots and the video capture), vendored and compiled inside `shared/rd-common/tr_image_codec.cpp`, the only file that knows them, shared by the multiplayer and the single-player renderers with the loader (`tr_image_load.cpp`); the multiplayer renderers no longer link libjpeg, libpng or zlib; `lib/jpeg-9a` and `lib/libpng` are removed from the repository, with the options `UseInternalJPEG` and `UseInternalPNG`; the single-player renderers build the same two files; the measurements: `docs_rewrite/modernization/tools/imagebench/`, [surveys/image-libraries.md](surveys/image-libraries.md) |
| Compression libraries | zlib 1.2.8, built with the project on Windows only, the system's on Linux; minizip, always the repository's | `lib/zlib-ng/` (zlib-ng 2.3.3, zlib licence, its source tree built by its own CMake file on every platform), known to `shared/qcommon/q_pk3.cpp` only, one file for the multiplayer and the single-player engines; `lib/zlib` and `lib/minizip` are removed from the repository, with the option `UseInternalZlib`; nothing links the system's zlib any more; the measurements, the checks of the reader and its fuzz target: `docs_rewrite/modernization/tools/zipbench/`, [surveys/compression-libraries.md](surveys/compression-libraries.md) |
| File system | `qcommon/files.cpp` twice: 3,071 lines in the single-player engine, 4,384 in the multiplayer one, 56 functions identical and 21 nearly | one: `shared/qcommon/files.cpp` and its interface `shared/qcommon/files.h`, built by both engines; what differs between them (the configuration files' names, the modules loaded from pk3 files, the sound buffer cleared before a read) is in each engine's `qcommon/files_engine.h`. The multiplayer engines' machine code is unchanged; the single-player engine gets the multiplayer file system, whose pure-server code stays idle in it, and opens the same files in the same order on a scripted session |
| Random numbers | `holdrand` in `q_math.c`, the C library's `rand()` | `shared/qcommon/q_rand.h` and `q_rand.c`; tools `docs_rewrite/modernization/tools/rng/` |
| Scripted fights | | `docs_rewrite/modernization/tools/gamestats/scenarios40.sh` runs the 40 cases on Windows or Linux and compares them with `scenarios40_golden.txt`, the same on every platform |
| Tests of the library | | round trip on about 36,000 recorded messages, 100,100 synthetic messages, malformed-input test and fuzzer, determinism hashes per compiler, movement determinism (game against cgame, compiler against compiler); client packets: 100,000 synthetic packets, the recorded packets through the library and through the engine interface, 400,000 malformed packets, and a fuzzer |
| Compilers checked | | MSVC (x64, x86), GCC 12 (x64, x86) and 16 (x64), Clang (llvm-mingw on Windows, Clang 14 on Linux) |
| `.gitattributes`, `.gitignore` | | line endings of scripts, demo files and local traces ignored |
| Study, plan, progress | | `docs_rewrite/modernization/` (this folder) |

### 28. Compatibility

| combination | works |
|---|---|
| Branch client with branch server | yes |
| Branch client with an OpenJK or base game server, or the reverse | no, and the connection is not refused: the protocol number is still 26, so the two sides connect and nothing they receive makes sense (decided at the end of the plan) |
| Branch build playing an OpenJK or base game demo | no |
| Demo of the branch played by OpenJK | no |
| Game and cgame modules built from OpenJK with the branch engine | not checked since the server ticks at 100 Hz (it worked before plan step 14). OpenJK's game code run at 100 Hz does not play as at 20 Hz (section 1). The server's game and the client's cgame must come from the same source, or the prediction misses |
| Mods that widened `user*` fields with the overrides files | they get the full values without the files (section 13) |
| Server browser, master server, `getstatus`, `getinfo` | yes, unchanged |
| Configuration files from OpenJK | yes; removed cvars are stored and never read |

### 29. Not changed yet

| subject | where |
|---|---|
| Lag compensation: none, as in OpenJK (planned for the hitscan shots, dropped on 14 November 2026) | plan step 14, part 8 |
| The freeze when the scoreboard first shows players whose models are not loaded | plan step 14, part 9 |
| The size of demos at 100 messages per second, a long client hitch, the sound louder at 333 frames per second than at 125 (as in OpenJK), priority and culling per client | plan step 14, part 11 |
| The delay the other players are drawn with depends on your frame rate: about 2 ms at 100 frames per second, 7 at 125, 11 at 333 | plan step 14, part 11 |
| Encryption, or a replacement for the removed obfuscation; several download blocks per message | plan step 12 |
| Tables retrained on games of this build; the force power levels the prediction lacks; a wider `saberAttackChainCount` | plan step 13 |
| The client's window, input and sound device rewritten on SDL 3 | plan step 15 |
| Protocol number, handshake, support of the old protocol, old demos | end of the plan |
| An acknowledgement bitfield, a snapshot history sized from the round trip | not planned (reasons in [progress/step-10-acks-reliable.md](progress/step-10-acks-reliable.md)) |
