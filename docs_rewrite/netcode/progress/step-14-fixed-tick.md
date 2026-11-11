# Step 14: fixed simulation tick, lag compensation, and the rest of "beyond the codec"

Status: **analysis and design, nothing implemented yet** (11 November 2026), apart from part 1 (the server's CPU time in `svnetstats`, commit `aded85287`) and the fixed rate of 200,000 bytes per second. The survey, the measurements and the owner's choices that lead here are in [../11-tick-and-lag-compensation-survey.md](../11-tick-and-lag-compensation-survey.md). Paths are relative to `codemp/`.

Owner's choices (11 November 2026): a fixed simulation tick of 125 Hz shared by client and server, lag compensation by tick, rate 200,000 bytes per second; designed from recent games rather than from the JKA mods, cleanly and optimised, even if it means starting again from zero; every defect found on the way fixed; the analysis covers the engine (client and dedicated server), the shared movement (`bg_*`), the game module, cgame, qcommon and the codec.

Target of the gameplay: the game plays as the 1.01 server did at its default 20 Hz (the balance the game was tuned on: saber damage per swing, force power timings, weapon timings), at any tick rate, except where the owner decides a change (section 4). What changes is precision and latency: movement in identical 8 ms steps for everyone, the world drawn about 11 ms behind the newest snapshot instead of about 53 ms, hits judged against what the shooter saw.

## 1. How the engine works today (what the model replaces)

The model of Quake 3, kept by Jedi Academy:

- **The client makes one user command per rendered frame** (`CL_CreateNewCommands`, `client/cl_input.cpp:1386`), stamped with the time the client is drawing the world at (`cmd->serverTime = cl.serverTime`, `:1264`), which is about the round trip plus the interpolation delay behind the server's clock. The command's movement lasts from the previous command's time to its own: a frame of 8 ms at 125 fps, 3 ms at 333 fps. Movement therefore depends on the frame rate (the "125 fps" and "333 fps" jumps).
- **The server runs each command when its packet is processed**, before the next server frame (`SV_UserMove` and `SV_ClientThink`, `server/sv_client.cpp:1232-1329`, `:1210`; `ClientThink_real` and `Pmove`, `game/g_active.c`, `game/bg_pmove.c:11171`). Each client moves in its own time, behind the server's clock by its latency. A dedicated server reads the packets only once per server frame: it sleeps until the frame, then drains the socket (`Com_Frame`, `qcommon/common.cpp:1551-1562`), so commands run in batches and the ping measured at that moment includes the wait (on average half a frame: 25 ms at 20 Hz, 4 ms at 125 Hz).
- **The server frame** (`SV_Frame`, `server/sv_main.cpp`) runs the bots' AI and commands once (`SV_BotFrame`), then `GVM_RunFrame` once per `1000 / sv_fps` ms of elapsed time (several in a row after a stall), then the snapshots. `G_RunFrame` (`game/g_main.c:2919`) moves missiles, items and movers, runs every entity's think, and for every client runs the force powers and the saber once per frame (`WP_ForcePowersUpdate`, `WP_SaberPositionUpdate`, `WP_SaberStartMissileBlockCheck`, `g_main.c:3316-3318`, `:3343-3345`), then `ClientEndFrame`.
- **cgame predicts the local player** by replaying, every rendered frame, every command since the newest snapshot's player state (`CG_PredictPlayerState`, `cgame/cg_predict.c:1092-1100`), and draws the other entities interpolated between two snapshots, except the players with `cg_smoothClients 1`, extrapolated from their last command time (`game/bg_misc.c:2912-2935`, `cgame/cg_ents.c:3086`).

## 2. The target model

As Valorant, Overwatch and Rocket League (11 section 11.6):

**Time.** Tick `n` is the game time `n * 8` ms. The constant `TICK_MSEC` (8) replaces the `sv_fps` cvar; every clock stays in whole milliseconds, so `level.time`, `serverTime`, `commandTime`, `trTime` and the timers keep their meaning and their type.

**Server loop.** At each tick: read every packet that arrived (stamped with its arrival time), put each client's commands into that client's input buffer by tick; run, for every client in the game, the command of this tick (the last one received is repeated when it is missing, as Valorant and Rocket League do; a command for a tick already run is dropped); run the bots' command of the tick; run `G_RunFrame`; send a snapshot to every client; sleep until the next tick. Packets are read when they arrive, not once per tick, so their arrival time (the ping, the input margin) is exact.

**Client loop.** The client runs ahead of the server by about half the round trip plus a small margin, so that its command for tick `n` reaches the server just before tick `n` runs. It makes one command per tick it crosses in a rendered frame (one every 8 ms, whatever the frame rate), sampling the input of that frame; the mouse turns the view at once, every frame, and the command carries the angles at the tick. Each packet carries the commands of the last few ticks (the redundancy of step 11). The server reports in each snapshot how early the client's commands arrive (the input margin); the client runs its ticks slightly faster or slower (a few percent, as Overwatch's 16 to 15.2 ms and Rocket League's 2.5 %) to keep that margin small but positive.

**What the client draws.** The local player is predicted tick by tick from the newest acknowledged player state through the commands not yet acknowledged; between two ticks the view is either interpolated between the last two predicted ticks or advanced by a partial step (decision D5). The other players and entities are interpolated between snapshots at the delay of step 11 (about one interval plus the jitter: about 11 ms at 125 Hz), never extrapolated (`cg_smoothClients` removed; extrapolation stays only to bridge a lost snapshot).

**Commands.** A user command carries its tick (as `serverTime = tick * TICK_MSEC`, so `Pmove` keeps its arithmetic and always moves 8 ms) and the time the client was drawing the other entities at (the view time, for lag compensation). Both are close to the previous command's, so they cost little in the client packet format (step 8).

**Lag compensation.** The server keeps, for every tick of a bounded window, what a hit test needs from each player: position, box, angles, the animation state the skeleton is built from, and the states that change a hit (crouching, dead, saber state). A hitscan shot is traced against the players as they were at the shooter's view time (interpolated between the two stored ticks around it), after checking that time against the cap; the players are moved back only if a coarse test says the shot can reach them (Overwatch's volume). The saber and the force powers follow decision D2.

**Snapshots and bases.** One world state per tick shared by all clients (each client's snapshot is a list of entity numbers plus its player state), instead of each client's own copy of every entity in every frame; the same per-tick store feeds lag compensation (decision D3).

## 3. Inventory of what the change touches

Each line: what is there, why it matters at 125 Hz or with the model, and what is done (part number of section 5, or decision of section 4).

### 3.1 Engine, dedicated server

| # | where | finding | action |
|---|---|---|---|
| S1 | `server/sv_client.cpp:1232-1329`, `:1210` | commands run as their packet is processed, each moving from the previous command's time to its own | input buffer per client, one command per tick in the tick (part 4) |
| S2 | `qcommon/common.cpp:1551-1562`; `server/sv_client.cpp:1268-1276` | a dedicated server drains the socket once per frame; the ping counts the wait until then (half a frame on average) | read packets as they arrive and stamp their arrival (part 3) |
| S3 | `server/sv_main.cpp`, `SV_Frame` | `frameMsec = 1000 / sv_fps` (whole ms), several `GVM_RunFrame` in a row after a stall, `SV_BotFrame` once per `SV_Frame` and not per game frame | the tick loop: `TICK_MSEC`, bots per tick, the stall policy (part 3) |
| S4 | `server/sv_ccmds.cpp:318-323`, `server/sv_init.cpp:624-632` | `map_restart` and a map load run three game frames of 100 ms to let the world settle | run the same 300 ms as ticks of 8 ms, or keep the jump that the client follows since step 11 (part 3) |
| S5 | `server/sv_netcodec.cpp:218-231` | server commands are resent with every new command while earlier ones are in flight (all from the client's acknowledge), or after 2 × ping without news: 0, 6 and 28 repeats without loss at 20, 40 and 125 Hz (11 section 11.7). Two causes possible: a new command while one is in flight (more messages per round trip at 125 Hz), or 2 × ping too short (the ping counts the batching of S2) | count the repeats by cause in `svnetstats`, then fix the cause (part 3); decision D4 for a reliable channel with acknowledgement bits |
| S6 | `qcommon/qcommon.h:117`; `server/sv_init.cpp:274`, `:366`; `server/server.h` (`frames[PACKET_BACKUP]`) | `PACKET_BACKUP` 32 messages is 256 ms at 125 Hz: a client whose round trip plus an interval exceeds it gets snapshots without a base (several times larger). The ring of snapshot entities is `sv_maxclients * PACKET_BACKUP * 256` entity states of 532 bytes: 139.5 MB at 32 players; 128 messages (1 s) would be 558 MB | decision D3 (shared world state per tick) (part 5) |
| S7 | `server/sv_main.cpp`, `SV_CalcPings` | the ping is the mean over the frames acknowledged among the last 32 (256 ms at 125 Hz), counting the wait of S2 | follows S2 and D3 (part 3) |
| S8 | `server/server.h:261` | `SV_CLIENT_RATE` 200,000: no single message delays a snapshot at 125 Hz | done |
| S9 | `server/sv_netcodec.cpp` (send time) | the snapshot leaves after a game frame of variable length: 0.2 to 0.4 ms of change with 31 bots | measured again after part 3; the client's delay absorbs it |
| S10 | `server/sv_snapshot.cpp:713`, `:706` and every `sv_fps` read | `sv_fps` read in several places, corrected in `SV_Frame` | `TICK_MSEC` constant, `sv_fps` removed (part 3) |
| S11 | `server/sv_bot.cpp` | bots' commands made once per server frame | per tick (part 4) |

### 3.2 Engine, client

| # | where | finding | action |
|---|---|---|---|
| C1 | `client/cl_input.cpp:1386-1411`, `:1264` | one command per rendered frame, stamped with the drawing time; key states measured over the frame (`frame_msec`, `:1393-1403`) | one command per tick, input sampled per frame and attributed to the ticks it covers (part 4) |
| C2 | `client/cl_input.cpp:1424-1459` | a packet every frame up to 125 per second, the unused interval carried over | a packet per tick, the commands of the last ticks repeated (part 4) |
| C3 | `cgame/cg_public.h:28` (`CMD_BACKUP` 64) | 64 commands are 512 ms at 125 commands per second: beyond it cgame stops predicting ("exceeded PACKET_BACKUP on commands", `cgame/cg_predict.c:1024-1031`) and shows "Connection Interrupted" (`cgame/cg_draw.c:4263-4267`) | 256 commands (2 s, 7 KB) (part 4) |
| C4 | `client/cl_cgame.cpp:630-930` | the interpolation delay of step 11 and `CL_AdjustTimeDelta` steer one clock, the drawing time, which the commands also carry | two clocks: the drawing time (behind, as now) and the client tick (ahead, with time dilation) (part 4) |
| C5 | `client/cl_cgame.cpp:665` | the snapshot interval read from `sv_fps` in the server info | `TICK_MSEC` (part 3) |
| C6 | `client/client.h:90`, `:154`, `:166` | `cl.snapshots`, `cl.outPackets`, `cl.parseEntities` sized by `PACKET_BACKUP` (4.4 MB of entity states) | follows D3 (part 5) |
| C7 | `client/cl_cgame.cpp:933` | `timedemo` advances 50 ms per frame | `TICK_MSEC` (part 3) |
| C8 | `client/cl_input.cpp` (packet redundancy `CL_PACKET_DUP` 2, `client/client.h:38`) | the commands of the 2 previous packets: 3 commands per packet at 125 Hz | kept (one command per tick, 3 per packet) |

### 3.3 Shared movement (`game/bg_*`)

| # | where | finding | action |
|---|---|---|---|
| B1 | `game/bg_pmove.c:11171-11205` | `Pmove` cuts a command into steps of at most 66 ms; `pmove_framecount` counts calls | each command is one tick of 8 ms; kept as is (part 4) |
| B2 | `game/bg_misc.c:2912-2935` | `BG_PlayerStateToEntityStateExtraPolate` sets `trDuration = 50` ("1000 / sv_fps (default = 20)") for the extrapolation of `g_smoothClients` | players sent for interpolation; `g_smoothClients` and `cg_smoothClients` removed (part 6) |
| B3 | `game/bg_pmove.c`, `game/bg_saber.c`, `game/bg_panimate.c` | timers in milliseconds counted down by `pml.msec` (weapon, animations, saber moves): independent of the step | checked by the tests of part 4 (same jump heights and saber timings as at 20 Hz with 125 fps) |

### 3.4 Game module (jampgame)

| # | where | finding | action |
|---|---|---|---|
| G1 | `game/g_main.c:3316-3318`, `:3343-3345` | saber, force powers and missile blocks run once per server frame: 6.25 times more often at 125 Hz | made independent of the rate (part 2), with D1 |
| G2 | `game/w_saber.c:4054`, `:4565`, `:4572-4613`, `:4727`; `game/g_xcvar.h:140-141` | after a wound, an attacking saber may wound again 100 ms later (`saberAttackWound = level.time + 100`, `:4727`), which a frame grid rounds up: 150 ms at 20 Hz, 104 ms at 125 Hz, so about 1.4 times more wounds in a long contact; an idle saber every 350 ms (`g_saberDmgDelay_Idle`); a block and a saber lock are decided at each contact with random chances (`Q_irand` against `g_saberLockFactor`, `:4587-4613`), so more frames mean more decisions per swing: more blocks, more locks and more hits | decision D1 (part 2) |
| G3 | `game/w_saber.c:8503` | `fVSpeed *= 1.6f/sv_fps.value` (the saber box ahead of a moving player) | in time units (part 2) |
| G4 | `game/w_force.c:4133` | `FORCE_DEBOUNCE_TIME 50` ("sv_fps 20 = 50msec frametime, basejka balance/timing"): OpenJK's partial fix for the force powers | checked, generalised (part 2) |
| G5 | `game/g_combat.c:3331-3334` | dismembered limbs: physics stepped every 50 ms "to compensate in case sv_fps is not 20" | kept (already rate-independent) |
| G6 | `level.time + 50` / `- 50` in `game/w_saber.c` (163, 648, 6287, 6290, 6539, 6542, 7011, 7067, 8872-8873), `game/g_weapon.c` (395, 1383, 1413, 2553, 2607, 4917, 4943, 5031), `game/g_items.c` (1329, 1340), `game/g_misc.c` (1036, 1147, 2167, 2939: 25), `game/g_client.c` (403, 538), `game/g_mover.c` (755, 2516) | thinks scheduled one 20 Hz frame ahead; some are "every frame" in disguise, some real delays | each read and classified (part 2) |
| G7 | `FRAMETIME` (100 ms, `game/g_local.h:54`) in 24 files, mostly NPC and ICARUS thinks every 100 ms | delays in milliseconds: independent of the rate, quantised to the tick | none needed; checked (part 2) |
| G8 | thinks run every frame (`nextthink = level.time`): `game/w_saber.c` 24, `game/g_weapon.c` 13, `game/g_items.c` 8, `game/g_misc.c` 7, `game/g_combat.c` 4, `game/g_trigger.c` 2, others 1 each | any per-frame chance, decay, damage or count in them changes with the rate | each read; per-frame amounts turned into amounts per millisecond (part 2) |
| G9 | `game/ai_main.c:60` (`BOT_THINK_TIME` 0), `:7525-7600` | bots think and send a command every server frame | per tick; their AI may think less often (measured CPU: 1.3 ms per frame at 125 Hz) (part 7) |
| G10 | `game/g_active.c:2006-2011` | the game clamps a command's time to [-1000, +200] ms around `level.time` | replaced by the input buffer's rules (part 4) |
| G11 | `game/g_active.c:3607-3628` (`G_RunClient`, `g_synchronousClients`, `g_forceClientUpdateRate`) | the legacy ways to run a client in the frame when it sends nothing | replaced by the repeated command of the tick (part 4) |
| G12 | `game/g_xcvar.h:177` | the game module declares `sv_fps` with a default of 40 (the engine's is 20) | removed with `sv_fps` (part 3) |
| G13 | `game/g_weapon.c` (`WP_DisruptorMainFire`, `WP_DisruptorAltFire`, `WP_FireConcussionAlt`) | the base game's hitscan shots | lag compensation (part 8) |

### 3.5 cgame

| # | where | finding | action |
|---|---|---|---|
| CG1 | `cgame/cg_predict.c:1092-1100` | prediction replays every unacknowledged command at every rendered frame (about 15 Pmoves at 100 ms of round trip, 40 at 300 ms, at 125 Hz) | keep the predicted state per tick and replay only from a new snapshot (part 4, optimisation) |
| CG2 | `cgame/cg_ents.c:3086-3091`, `cgame/cg_players.c:8495` | `cg_smoothClients` extrapolates the other players | removed; interpolation only (part 6) |
| CG3 | `cgame/cg_draw.c:4188` (`LAG_SAMPLES` 128) | the lagometer keeps 128 snapshots: 6.4 s at 20 Hz, 1 s at 125 Hz | sized in time (part 9) |
| CG4 | `cgame/cg_players.c:1855`, `cgame/cg_scoreboard.c:611`, `cgame/cg_xcvar.h:83` (`cg_deferPlayers` 1) | the models of players who joined are loaded when the scoreboard first shows them: the freeze seen in step 11 | decision D6 (part 9) |
| CG5 | prediction misses when crouching or jumping while attacking (step 8, base game too) | cause not found by reading; likely game code changing the player state outside `Pmove` (G1) | measured with `cg_showmiss` and fixed (part 2) |

### 3.6 Codec (`codemp/netcodec`)

| # | where | finding | action |
|---|---|---|---|
| K1 | `eng::kPacketBackup` tied to `PACKET_BACKUP` (`client/cl_netcodec.cpp:36`); the base's own base used while both sides hold it | follows D3 | library change, full validations (part 5) |
| K2 | `eng::kAckWindow` 256 messages | 2.05 s at 125 Hz; the client codes against its acknowledge only while (age + round trip) < 128 intervals, 1.02 s at 125 Hz | enough; kept |
| K3 | client packet format (step 8) | the command's time is coded against the acknowledged snapshot's; a tick (+8 ms per command) and a view time are new fields | new contexts; the client tables are retrained in step 13, the uniform fallback until then (part 4) |
| K4 | server tables trained on 20 Hz demos | measured 135 bytes per message at 125 Hz (11 section 11.7), no loss of efficiency seen | retrained in step 13 |
| K5 | the input margin the server reports | a new field of the snapshot | part 4 |

## 4. Decisions for the owner

Each with options, costs and a recommendation; the owner decides.

Owner's answers (11 November 2026): **the move to 125 Hz must not make saber combat block more or deal more damage** (a general rule: hits, blocks and damage per swing as at 20 Hz; the tests of part 2 compare both); D1, D3, D4 and D7 accepted as recommended; D6: (a), the models stay loaded at the first scoreboard (`cg_deferPlayers` kept); D5: (b), the partial step for display ("never less responsive; as precise and responsive as possible, more than now where feasible"); D2: (a) as revised below, no lag compensation for the saber and the force powers, compensation for the hitscan shots, (b) behind a cheat-protected debug cvar. Every decision is taken.

- **D1. Saber damage at 125 Hz.** (a) The 20 Hz behaviour exactly: blade sweeps and damage on a 50 ms grid whatever the tick (what base_entranced does; no gain in saber precision); (b) the blade traced every tick, swept between ticks, with damage per swing and per victim defined in time (a victim hit by a swing is wounded once per swing, or once per 50 ms window of contact, to keep the 20 Hz balance); (c) a new hit model. Recommendation: (b) with "once per 50 ms window", which keeps the balance and uses the precision.
- **D2. Lag compensation of the saber and the force powers.** (a) None: the attacker hits what the server sees now (today's behaviour, the attacker aims ahead by their latency); (b) the defenders' bodies rewound to the attacker's view time for blade-against-body contacts, blocks and parries judged at the server's time with defensive moves having priority (as Overwatch's evasive abilities and For Honor's delay on attacks); (c) everything rewound, blades included. Recommendation, revised after the owner's rule that 125 Hz must not make the saber block more or deal more damage: (a) for the saber and the force powers (today's balance between attacker and defender; (b) lets more hits through, so more damage in practice), lag compensation only for the hitscan shots; (b) available behind a cheat-protected debug cvar to compare in game.
- **D3. Snapshot store.** (a) Keep the per-client copies and raise `PACKET_BACKUP` (558 MB at 32 players for 1 s); (b) one world state per tick shared by all clients plus a list of entity numbers per client snapshot (about 20 to 70 MB for 1 s at 125 Hz depending on the entities in use, plus 6.4 MB of player states), also used by lag compensation. Recommendation: (b), window of 1 s (128 ticks): a round trip up to about 900 ms keeps a base.
- **D4. Server commands.** (a) Keep step 10's rule and fix the cause found by the counters of S5; (b) a reliable channel: each command sent once, resent only when an acknowledgement bitfield shows its message lost, the client reordering what arrives. Recommendation: (a) first, (b) if the counters show the design itself is the cause.
- **D5. The local player between two ticks.** (a) Drawn interpolated between the last two predicted ticks (0 to 8 ms of added visual delay, nothing guessed); (b) advanced by a partial step of `Pmove` for display only (no delay, one more `Pmove` per frame, may differ from the next full tick). Today the client predicts up to the exact time of each frame (one command per frame), so (a) would add 0 to 8 ms (4 ms on average) to the local player's position compared with today, and (b) keeps today's responsiveness. Unity Netcode for Entities does (b): each frame it rolls back to the last full tick and predicts one partial tick with the remaining time; one-off actions (sounds, effects, spawns) run only when a full tick is predicted for the first time (primary: Unity, "Introduction to prediction"). Recommendation, revised: (b), with the events of a partial step never played. Other engines mostly draw (a): Glenn Fiedler's "Fix Your Timestep!" (primary) interpolates between the previous and current physics states, which trails by up to one step; the Netick engine's documentation interpolates the local player between its predicted snapshots (primary); Source is believed to do the same (not verified).
- **D6. Freeze when the scoreboard first shows unloaded players.** (a) Load a joining player's model at once (a short hitch when someone joins, the reason `cg_deferPlayers` exists); (b) load the waiting models while the local player is dead or spectating, when a hitch matters less; (c) load one model per frame in the background as soon as a player joins. Recommendation: (c), measured.
- **D7. Lag compensation cap.** Published values 200 ms to 1 s; Overwatch stops at about 220 ms of round trip. The rewind is the round trip plus about 11 ms. Recommendation: 250 ms of rewind (round trip up to about 240 ms), measured in game.

## 5. Parts

Each part builds, is checked in game (loopback, VPS, `net_dropsim`) and is documented in 09 before the next. The game timing comes first (11 November 2026): while `sv_fps` still exists, the game module can be checked at 20 and 125 Hz on the same build, and the game stays playable at every step (with the tick loop first, the saber would deal up to 6 times its damage until the game timing is fixed).

1. **Done**: `svnetstats` prints the server's CPU time; `SV_CLIENT_RATE` 200,000.
2. **Game timing**: every item of 3.4 G1 to G8, the saber per D1, so that the game plays the same at any server frame rate. Check, at `sv_fps` 20 and 125 on the same build with bots: the same damage per swing, the same share of blocked contacts, the same force power and weapon timings (counted by debug statistics); then the prediction misses when crouching or jumping while attacking (CG5).
3. **Tick loop**: `TICK_MSEC`, `sv_fps` removed (engine, game, client), packets read on arrival with their time, bots per tick, `map_restart` and map load in ticks, repeats of server commands counted by cause and fixed (D4). Check: 125 snapshots per second, ping exact, no repeat without loss.
4. **Commands per tick**: the input buffer on the server, one command per tick on the client, the client ahead with time dilation steered by the input margin, `CMD_BACKUP` 256, prediction per tick with the cached states, the partial step for display (D5), the client packet fields (tick, view time). Check: identical movement at 60, 125, 144 and 333 fps (jump heights, strafe speed), no prediction miss on the loopback, input margin stable on the VPS with `net_dropsim`, the local player as responsive as before.
5. **Snapshot store** (D3): shared world state per tick, `PACKET_BACKUP` from the window, the codec's frames. Library change: full validations. Check: bases kept up to the chosen round trip, memory measured.
6. **Interpolation only**: `cg_smoothClients` and `g_smoothClients` removed, players sent for interpolation. Check: no extrapolated interval without loss (`netstats`).
7. **Server CPU**: profile the 125 Hz frame (bots, game frame), optimise. Check: `svnetstats` before and after.
8. **Lag compensation** (D2, D7): history per tick, rewind for the hitscan shots, the cap; the saber's behind a cheat-protected debug cvar. Check: hits on a moving target at 100 to 200 ms of simulated latency.
9. **cgame**: lagometer in time. The models of joining players stay loaded at the first scoreboard (D6 (a)).

## 6. Measurements to take on the way

- Repeats of server commands by cause (part 3).
- The input margin and the time dilation on the VPS, with and without `net_dropsim` (part 4).
- Bytes per message at 125 Hz with the game working (part 3 onwards).
- Memory of the snapshot store (part 5).
- The 125 Hz server frame with 31 bots after part 2 (part 7).
