# Step 14: fixed simulation tick, lag compensation, and the rest of "beyond the codec"

Status (12 November 2026): part 1 done (the server's CPU time in `svnetstats`, commit `aded85287`; the fixed rate of 200,000 bytes per second); part 2 (game timing) done with a new design (commits `4efd458f0` and `1376279bd`), the game's logic at its 20 Hz pace on a 100 Hz tick, proven identical to `sv_fps` 20 with scripted scenarios (section 9; the working notes, audits and traces are in [step-14-audit-100hz.md](step-14-audit-100hz.md)); parts 3 to 9 to do. The survey, the measurements and the owner's choices that lead here are in [../11-tick-and-lag-compensation-survey.md](../11-tick-and-lag-compensation-survey.md). Paths are relative to `codemp/`.

Owner's choices (11 November 2026): a fixed simulation tick of 125 Hz (revised the same evening: **100 Hz**, so that every 20 Hz instant is a tick, and the game's logic runs at its 20 Hz pace, identical to `sv_fps` 20: section 9) shared by client and server, lag compensation by tick, rate 200,000 bytes per second; designed from recent games rather than from the JKA mods, cleanly and optimised, even if it means starting again from zero; every defect found on the way fixed; the analysis covers the engine (client and dedicated server), the shared movement (`bg_*`), the game module, cgame, qcommon and the codec.

Target of the gameplay: the game plays as the 1.01 server did at its default 20 Hz (the balance the game was tuned on: saber damage per swing, force power timings, weapon timings), at any tick rate, except where the owner decides a change (section 4). What changes is precision and latency: movement in identical 8 ms steps for everyone, the world drawn about 11 ms behind the newest snapshot instead of about 53 ms, hits judged against what the shooter saw.

## 1. How the engine works today (what the model replaces)

The model of Quake 3, kept by Jedi Academy:

- **The client makes one user command per rendered frame** (`CL_CreateNewCommands`, `client/cl_input.cpp:1386`), stamped with the time the client is drawing the world at (`cmd->serverTime = cl.serverTime`, `:1264`), which is about the round trip plus the interpolation delay behind the server's clock. The command's movement lasts from the previous command's time to its own: a frame of 8 ms at 125 fps, 3 ms at 333 fps. Movement therefore depends on the frame rate (the "125 fps" and "333 fps" jumps).
- **The server runs each command when its packet is processed**, before the next server frame (`SV_UserMove` and `SV_ClientThink`, `server/sv_client.cpp:1232-1329`, `:1210`; `ClientThink_real` and `Pmove`, `game/g_active.c`, `game/bg_pmove.c:11171`). Each client moves in its own time, behind the server's clock by its latency. A dedicated server runs each packet as it arrives: it waits for the next frame in `select` on its socket (`NET_Sleep`, `qcommon/net_ip.cpp`), which returns when a packet comes, and `NET_Event` runs it at once (`Com_Frame`, `qcommon/common.cpp`). (Corrected on 12 November 2026: this line first said the socket was drained once per frame; the audit of [step-14-audit-100hz.md](step-14-audit-100hz.md) section 5, S2, showed otherwise.)
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
| S2 | `qcommon/common.cpp` (`Com_Frame`), `qcommon/net_ip.cpp` (`NET_Sleep`, `NET_Event`) | first written as "the socket is drained once per frame": wrong, the server runs each packet on arrival (12 November 2026); the last millisecond before a frame was a busy loop (up to a tenth of a core at 100 Hz, X1) | done: a dedicated server sleeps that millisecond in steps of 100 us outside Windows (12 November 2026) |
| S3 | `server/sv_main.cpp`, `SV_Frame` | `frameMsec = 1000 / sv_fps` (whole ms), several `GVM_RunFrame` in a row after a stall, `SV_BotFrame` once per `SV_Frame` and not per game frame | the tick loop: `TICK_MSEC`, bots per tick, the stall policy (part 3) |
| S4 | `server/sv_ccmds.cpp:318-323`, `server/sv_init.cpp:624-632` | `map_restart` and a map load run three game frames of 100 ms to let the world settle | run the same 300 ms as ticks of 8 ms, or keep the jump that the client follows since step 11 (part 3) |
| S5 | `server/sv_netcodec.cpp:218-231` | server commands are resent with every new command while earlier ones are in flight (all from the client's acknowledge), or after 2 × ping without news: 0, 6 and 28 repeats without loss at 20, 40 and 125 Hz (11 section 11.7). Two causes possible: a new command while one is in flight (more messages per round trip at 125 Hz), or 2 × ping too short (under 10 ms on a LAN; the audit's W5) | count the repeats by cause in `svnetstats`, then fix the cause (part 3); decision D4 for a reliable channel with acknowledgement bits |
| S6 | `qcommon/qcommon.h:117`; `server/sv_init.cpp:274`, `:366`; `server/server.h` (`frames[PACKET_BACKUP]`) | `PACKET_BACKUP` 32 messages is 256 ms at 125 Hz: a client whose round trip plus an interval exceeds it gets snapshots without a base (several times larger). The ring of snapshot entities is `sv_maxclients * PACKET_BACKUP * 256` entity states of 532 bytes: 139.5 MB at 32 players; 128 messages (1 s) would be 558 MB | decision D3 (shared world state per tick) (part 5) |
| S7 | `server/sv_main.cpp`, `SV_CalcPings` | the ping is the mean over the frames acknowledged among the last 32 (256 ms at 125 Hz, 320 ms at 100 Hz; the audit's W7) | follows D3 (part 3) |
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
| G2 | `game/w_saber.c` (`G_SPSaberDamageTraceLerped`, `CheckSaberDamage`, `WP_SaberPositionUpdate`); `game/g_xcvar.h:60`, `:140-141` | the default damage is the single-player style (`d_saberSPStyleDamage` 1): every server frame the blade is swept from its position of the previous frame in steps of 8 units and arcs of 33 degrees, each step's damage proportional to its length and rounded up, and an attacking saber may wound again on the next frame (`g_saberDmgDelay_Wound` 0; an idle one every 350 ms, `g_saberDmgDelay_Idle`); saber against saber contacts, parries and locks are decided at each contact, locks with random chances (`g_saberLockFactor`). More frames mean more decisions and more rounded-up steps per swing. Measured with bots (section 7): 6.2 times the wounds, 1.7 times the damage and 2 times the saber against saber contacts per swing at 125 Hz | decision D1 (part 2) |
| G14 | `game/w_saber.c` (`G_SaberAttackPower`, `saberCheckKnockdown_*`), `game/g_combat.c` (severed limbs) | the speed of a swing is the saber base's move over one server frame (`lastSaberBase_Always - olderSaberBase`): 50 ms at 20 Hz, 8 ms at 125 Hz, so attacks about 6 times weaker against defence (more parries, fewer knockdowns and disarms) | measured over 50 ms from a history of the blade (`WP_SaberStoreBase`, part 2) |
| G3 | `game/w_saber.c:8503` | `fVSpeed *= 1.6f/sv_fps.value` (the saber box ahead of a moving player) | in time units (part 2) |
| G4 | `game/w_force.c:4133` | `FORCE_DEBOUNCE_TIME 50` ("sv_fps 20 = 50msec frametime, basejka balance/timing"): OpenJK's partial fix for the force powers | done: the periodic timers at their 20 Hz lengths (section 8) |
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

Owner's answers (11 November 2026): **the move to 125 Hz must not make saber combat block more or deal more damage** (a general rule: hits, blocks and damage per swing as at 20 Hz; the tests of part 2 compare both); D1, D3, D4 and D7 accepted as recommended; D6: (a), the models stay loaded at the first scoreboard (`cg_deferPlayers` kept); D5: (b), the partial step for display ("never less responsive; as precise and responsive as possible, more than now where feasible"); D2: (a) as revised below, no lag compensation for the saber and the force powers, compensation for the hitscan shots, (b) behind a cheat-protected debug cvar. Every decision is taken. Later the same day, after the first measurements: a few percent fewer saber against saber blocks than at 20 Hz (about 6 % measured with bots) are acceptable, even welcome (servers are "block land" today, sabers block far more than 15 or 20 years ago); damage must not grow; precision is preferred to an exact copy of 20 Hz, with tuning where a count moves too much.

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
2. **Game timing** (done, section 9: the logic on the 20 Hz instants rather than D1's per-tick saber): every item of 3.4 G1 to G8, the saber per D1, so that the game plays the same at any server frame rate. Check, at `sv_fps` 20 and 125 on the same build with bots: the same damage per swing, the same share of blocked contacts, the same force power and weapon timings (counted by debug statistics); then the prediction misses when crouching or jumping while attacking (CG5).
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

## 7. Part 2, first result: the saber (11 November 2026)

**Superseded (section 9):** the per-tick saber described here was abandoned on the evening of 11 November and its code reverted to OpenJK's; kept as the record of what was tried.

### How it is measured

- `gamestats` (server command of the game module) counts the saber swings, wounds, damage, saber against saber contacts (blocks), parries without the sabers touching, locks, idle touches, projectiles blocked, force damage, deaths and saber hit effects since its last call (09, section 8).
- Local dedicated servers without network, 16 bots of skill 4, FFA, sabers only, `mp/ffa3`, run in parallel by a script (`docs_rewrite/netcode/tools/gamestats/gamestats.sh` for Linux; the same on Windows under lldb, since the clang dedicated server crashes without a console).
- The bots play as clients at 125 frames per second (`bot_debugClientRate 1`: they think every 50 ms and send a command every 8 ms), else they think and move once per server frame and the measure is of their AI rather than of the saber.
- **The reference runs in every batch**: OpenJK's saber at 20 Hz (a module built before the saber changes, with only the counters added) next to the configurations tested, 12 servers each, 3 batches. Results move by up to 15 % from one batch to another, so comparisons across batches are not valid; within a batch a configuration is known within about ±3 % over 180 server-minutes.
- Fast mode: `fixedtime` (the frame's length) and `timedemo 1` make a server run its frames as fast as it can, each advancing the game by one interval: 5 minutes of game in about 40 s at 125 Hz. Checked against real time (same results within the noise).
- Traps found on the way, all handled by the script: the engine keeps only 32 `+` commands of its command line (`MAX_CONSOLE_LINES`), so the commands go in a cfg run by `+exec`; a long `wait` never fires, so the time is chained in waits of 30 s; the game seeds its random numbers with the milliseconds since the process started, so a random wait before the map loads gives each server its own seed; a configuration whose cvars contain spaces must be passed as one argument (split, they once ran the bots without `bot_debugClientRate`, which invalidated a day's comparisons until found); a server that does not exit after its quit is killed once its statistics are written.

### What was found (inventory items G2 and G14)

- The unchanged saber at 125 Hz: about 4 times the wounds per minute, each smaller, 9 % more damage and 16 % more saber against saber contacts.
- The default saber damage (single-player style, `d_saberSPStyleDamage` 1) sweeps each blade point from its previous position, but each trace covered only its first 16 units (`SABER_EXTRAPOLATE_DIST`): a fraction of a 20 Hz frame's move (40 to 80 units for a fast swing) and more than a 125 Hz frame's (6 to 13 units). At 125 Hz the whole move was traced plus a lookahead, so contacts were found about 4 times more often, each with a smaller damage (proportional to the traced length).
- The blade against blade test (`WP_SabersIntersect`) extended both swept surfaces by the same 16 units ahead at every test, so blades met early and more often the more frames there were.
- The speed of a swing (attack power, knockdowns, disarms, severed limbs) was the saber base's move over one server frame.
- Each damage played a hit effect (blood or sparks, and its sound), so 3 times as many at 125 Hz for the same damage; and the pain sound and view kick need 10 points of damage within one server frame, which a blow spread over several 125 Hz frames rarely reaches.
- Variants measured and not kept: the blade traced on a 50 ms grid inside the tick (the 20 Hz balance without the precision; owner: precision preferred), the damage of a contact scaled to 50 ms of blade move (twice the damage: the precise trace already finds more contacts), the damage of a contact grouped and applied later (a delay of up to 200 ms; owner: no delay at all), the server's skeleton animations started on the 50 ms grid (explained little).

### What the final code does

The saber is traced every server frame on its whole move, with a lookahead of 16 units scaled to the frame time; blades block only head-on (the direction check of `WP_SabersIntersect`); one block or parry decision and one wound per attacker and 50 ms window; the damage coefficient of the traced length is 0.030 instead of 0.033, applied at once every frame; the hit effect plays at the first contact with a victim and then at most every 48 ms while it lasts; the pain threshold counts the last 50 ms; the speed of a swing is measured over 50 ms; the saber's wound delays keep their 20 Hz lengths (`G_GRID20_DEADLINE`). Details and reasons in 09, section 7.

| per minute, against OpenJK's saber at 20 Hz in the same batches | damage | deaths | blocks | hit effects |
|---|---|---|---|---|
| unchanged saber at 125 Hz (first batch, real time) | +9 % | | +16 % | about 3 times |
| coefficient 0.032, effects every 100 ms (180 server-minutes) | +1 % | +5 % | −8 % | −25 % |
| coefficient 0.027 (180) | −5 % | 0 % | +3 % | |
| **coefficient 0.030** (540 server-minutes, with effects every 48 to 72 ms) | about −4 % | about 0 % | about −8 % | |
| effects every 72 / 64 / 56 / **48 ms** (0.030) | | | | −16 % / −11 % / −4 % / **+1 %** |

On Linux (the owner's VPS, GCC 12, fast mode, 4 servers at a time on its single core), the reference against the final saber in the same series, 80 server-minutes each: damage +2.2 %, deaths +1.9 %, saber against saber blocks −12.5 % (the first series alone +6.2 %, +3.5 %, −8.4 %; the second, with the wider seeds, −1.4 %, +0.5 %, −16.2 %). Within the noise of the Windows results: no sign of a difference between GCC and clang. The Linux reference module is built from a detached worktree of the commit of part 2 with the saber changes undone (`E:\Git\jka\OpenJK_Netcode_saberref`, six lines of `w_saber.c`, not committed); on Windows it gives the same counts as the reference module used for the tuning (−0.4 % damage, −0.1 % deaths, −1.8 % blocks over 180 server-minutes).

The force powers were disabled in these measurements (the owner's server settings, section 8).
## 8. Part 2: the force powers (11 November 2026)

**Superseded (section 9):** the timers rewritten here (commit `00c003c91`) are back to OpenJK's code, which runs on the 20 Hz instants; kept as the record of what was tried.

### What was found (inventory item G4)

- The held powers run once per server frame (`WP_ForcePowersUpdate`, `WP_ForcePowerRun`). OpenJK had already put the lightning and drain shots on a 50 ms accumulator (`FORCE_DEBOUNCE_TIME`), and the force regeneration accumulates its interval too: both are independent of the rate.
- The other periodic timers are re-armed with `level.time + n` and tested with `<`. On the 20 Hz server the event came in the first frame after the deadline, every n + 50 ms; at 125 Hz every n + 8 ms. So at 125 Hz grip drained a force point every 104 ms instead of 150 (44 % faster), rage took 2 health points every 152, 304 or 456 ms instead of 200, 350 or 500 (by level), protect drained every 304 ms instead of 350, absorb every 608 instead of 650, the grip's damage came every 1,008 ms instead of 1,050, the lift of a gripped player every 304 instead of 350, a step of the force jump charge every 504 instead of 550, the seeker drone fired about 8 % more often and beeped every 104 ms instead of 150, and the grip sound played every 1,008 ms instead of 1,050.
- Mind trick ends when the trickster attacks in view within 4 server frames (`g_TimeSinceLastFrame * 4`): 200 ms at 20 Hz, 32 ms at 125 Hz.
- The power durations (8 to 30 s; lightning and drain 500 ms) end up to 42 ms sooner at 125 Hz: under 0.5 % for the long ones, and lightning and drain still fire the same shots per use (their last shot, at 500 ms, comes in both cases). The one-off delays (cooldowns of 1.5 to 10 s, knockdowns, the hand's extension) also end up to 42 ms sooner, once: left as written (at most 4 % of their length, and several are read by the shared movement code with the command's time, not the frame's).

### What the code does

Each periodic timer is written as its 20 Hz length and tested with `<=` (the table is in 09, section 7): the value in the code is the real interval at any rate, rounded up to the tick at 125 Hz (at most 6 ms: 150 ms becomes 152). The seeker drone's random interval is 425 to 725 ms (the 20 Hz intervals, 450 to 700 ms in steps of 50, spread over their 50 ms). The mind trick window is 200 ms, and `g_TimeSinceLastFrame` and `g_LastFrameTime`, used only there, are removed. `G_GRID20_DEADLINE`, used for the saber's wound delays, was not used here: those delays are cvars, whose values keep their 20 Hz meaning through it, whereas the force timers are constants that can state their real length (the owner prefers values set directly to hidden adjustments).

### How it is measured

- The bench of section 7 with the force powers enabled (`PREMAP="g_forcePowerDisable 0"` in the scripts). The saber measurements of section 7 ran with the owner's server settings, which the dedicated server read from the `openjk_server.cfg` of its base path: every force power disabled except jump and the saber skills (`g_forcePowerDisable 163837`), no saber locks (`g_saberLocking 0`), no private duels. The scripts now set these themselves, so that a run is the same on every machine (on the VPS it was not known which file was read).
- `gamestats` prints, for grip, rage, protect, absorb, lightning and drain, the starts, the periodic events (drains, rage health losses, shots) and the time held, counted from the frame a power starts to the frame it stops; `gamestats_agg.py` gives the events per use and per second held.
- The bots use lightning, drain, rage, protect and absorb, but never grip: lightning comes first in their choice while they have more than 50 force points, absorb prevents grip, and they use no force power at all when any power is disabled (`ai_main.c`: `bot_forcepowers` and `!g_forcePowerDisable.integer`). Grip's timers follow the same rule as the others but are not measured.

### Results

Timing, 80 server-minutes per configuration in one batch (the rates per second held are above 1 / interval because each use starts with an event):

| | rage health losses per second held | protect drains per second held | absorb drains per second held | lightning shots per use | drain shots per use |
|---|---|---|---|---|---|
| 20 Hz (the same as OpenJK for the force powers) | 2.28 | 3.17 | 1.66 | 8.56 | 4.34 |
| 125 Hz, before | 2.49 | 3.67 | 1.79 | 8.47 | 4.37 |
| **125 Hz, after** | 2.25 | 3.21 | 1.67 | 8.33 | 4.58 |

Lightning and drain shots per second held are 21.0 and 21.9 at 125 Hz against 20.0 at 20 Hz, before and after: the same shots per use over a use 42 ms shorter (the end of the 500 ms duration).

Game, 16 bots with the force powers, against OpenJK at 20 Hz in the same batches, per minute over 300 server-minutes per configuration (5 batches; with the force powers one batch moves by up to 13 %):

| per minute | saber damage | deaths | saber against saber blocks | force damage |
|---|---|---|---|---|
| this branch at 20 Hz (its saber is tuned for 125 Hz) | +8.2 % | +6.0 % | −18.4 % | −1.6 % |
| 125 Hz, force powers before | +1.1 % | +2.3 % | −11.3 % | −7.4 % |
| **125 Hz, force powers after** | +5.2 % | +4.5 % | −8.9 % | −9.3 % |

- The change itself adds about 4 % of saber damage and takes about 2 % of force damage: rage lasts longer again (its health drain is back to its 20 Hz pace), and a raged player swings 1.7 times faster and takes half the damage.
- With the force powers, the saber at 125 Hz deals about 5 % more damage than OpenJK's at 20 Hz, where without them it deals about 4 % less (section 7); this branch's saber at 20 Hz deals 8 % more with them. The saber was tuned with the owner's settings (no force powers); to decide with the owner.
- The force damage (lightning and drain only) is about 9 % lower at 125 Hz, before and after the change: the bots start lightning and drain about 8 % less often (35.2 against 38.2 lightning starts per minute), while each use fires the same shots. Nothing in the force code explains it (the cooldowns are tested with `>`, which keeps their length; the hand and weapon checks are per frame); it is left as a difference of the bots' fights at 125 Hz, to look at again with players.

Left in part 2: G3 (`fVSpeed *= 1.6f/sv_fps.value`, the saber box ahead of a moving player: the frame time is right, read from the frame instead of `sv_fps` when it is removed in part 3), G5 to G8 (the 50 ms constants, thinks run every frame), CG5 (the prediction misses when crouching or jumping while attacking).

## 9. Part 2, final design: the game's logic at its 20 Hz pace on a 100 Hz tick (11 and 12 November 2026)

### Why the per-tick tuning was abandoned

Every saber against saber reaction (bounce, deflection, knockaway, broken parry, lock) needs `dmg > SABER_NONATTACK_DAMAGE`, where `dmg` is proportional to the blade's move in one server frame (`game/w_saber.c`). At a fast tick the move is a fraction of a 20 Hz frame's, `dmg` is nearly always 1 and attacks are no longer bounced: no coefficient restores it. The pain threshold rewrite (`P_RecentDamage`) was wrong (OpenJK already accumulates damage across frames until it reaches 10) and halved lava damage with its 750 ms debounce. And every bot measurement until then mixed the movement step with the server rate: the bots ran one movement step per server frame (`ClientThink` skipped `SVF_BOT`).

Owner's decisions (11 November 2026, evening): a tick of **100 Hz** (at 125 Hz, 50 ms is 6.25 ticks: thinks every 50 ms ran every 56 ms, the NPCs' 100 ms alternated 100 and 150 ms); the game plays identically to `sv_fps` 20 (damage, blocks, attack bounces, timings, AI), as precise as possible only where outcomes do not change; deterministic scenarios rather than free-for-all games as the proof; every audit claim checked in the code.

### What the code does

- Two clocks (`game/g_local.h`, `game/g_main.c`): `level.time`, read by all game code, is the logic clock, the last multiple of 50 ms; `level.tickTime` is the tick's time (bots' command times, the clamp of a command's time); `level.logicFrame` (`G_GRID20_STEP`) is true on the 20 Hz instants and on the first frame.
- On the 20 Hz instants, OpenJK's code unchanged: `G_RunThink`; per client and NPC in `G_RunFrame`: space, hacking, jetpack and cloak fuel, siege data, `WP_ForcePowersUpdate`, `WP_SaberPositionUpdate`, `WP_SaberStartMissileBlockCheck`; `ClientEndFrame`: powerups, `P_WorldEffects`, `P_DamageFeedback`; the level's rules; the bots' AI (`ai_main.c`, every 50 ms); all missiles (`G_RunMissile`), items (`G_RunItem`) and movers (`G_RunMover`: movement, pushes, crush damage, thinks; since 12 November 2026, owner).
- Every tick: the players' commands (movement with the command's time), the snapshots.
- The server's ghoul2 clock (`server/sv_main.cpp`) is the logic instant of the frame just run (`sv.time / 50 * 50`). The engine set it to `sv.time` after each game frame, so a frame evaluated the models at the previous tick's time while the game starts their animations at `level.time`: at 100 Hz the blades ran 40 ms ahead of their 20 Hz path, with identical player states; that made 5 times less saber damage in the first scenario runs.
- Missiles move only on the 20 Hz instants: with per-tick steps the engine's model trace (`server/sv_world.cpp`) gave other results (a step starting inside a player's box hits the box without a model test; one that leaves the box reports no model surface, so the hit location comes from the box), which changed hits, hit locations and damage (bryar 768 against 1,248 for the same 96 hits, homing rocket 48 kills against 0).
- Kept from the per-tick work: `fVSpeed *= 1.6f/20.0f` (G3, the value of `sv_fps` 20) and the mind trick window of 200 ms (`g_TimeSinceLastFrame` removed); both are OpenJK's values at 20 Hz.
- At `sv_fps` 20 every instant is a 20 Hz instant: the module runs OpenJK's code paths, and the same module at `sv_fps` 20 is the reference.

Price of identity: a saber contact or a missile impact is found at the 20 Hz instant after it, up to 40 ms later than a per-tick trace; the players' movement and commands are per command as before. Known limits: during the slow-motion end of a duel (`timescale`, `g_slowmoDuelEnd`, 0 on the owner's servers) the logic runs every 50 ms of game time. The movers moved every tick until 12 November; the owner chose the instants (exact identity); a free-for-all batch after the change: damage -1.3 %, deaths -0.9 %, blocks -2.0 %, within the noise.

### How it is proven

`scenario` (`game/g_scenario.c`, 09 section 8): two bots, an attacker and a defender, reset every 2 s, play a script of commands every 10 ms with the random seeds reset at each repetition; `bot_debugClientRate 1` makes every bot command run as a player's, in the order of the commands' times across the bots; a first repetition is not counted. `scenario_trace` prints the two bots' state at every 20 Hz instant and every health loss, which found both causes above by diffing a 20 Hz and a 100 Hz run line by line.

Results, same module at `sv_fps` 20 and 100 (Windows, clang, fast mode), equal counts of wounds, damage, health taken by cause, deaths, blocks, idle touches, attacks started, bounces, shots, hits, misses, force starts, periodic events and time held:

| scenario | repetitions | 20 Hz against 100 Hz |
|---|---|---|
| saber: fast, medium, strong, dual, staff; defender idle or attacking (10 cases) | 96 each | identical |
| force: lightning, grip, drain, rage, protect, absorb | 48 each | identical |
| weapons 4 to 15, primary and alternate (24 cases) | 48 each | identical, except flechette primary: 1 shard of 195 in 1 repetition |

The differences left are not rate effects: bodies, items and doors left by the free play before the scenario (`EV_BODYFADE`, `EV_ITEM_RESPAWN`, `EV_PLAYDOORSOUND`), the death animation cycled by a static counter in `player_die`, the flechette shards processed in another entity order (the two values swapped between two runs at the same rates). A free-for-all of 16 bots, 3 batches of 12 servers per rate, 60 minutes of game each: damage -2.8 % (standard error 1.8 %), deaths -2.2 % (1.8 %), blocks -0.9 % (2.0 %), within the noise.

### Also fixed in part 2 (12 November 2026)

- `PM_irand_timesync` (`game/bg_saber.c`) drew its random numbers with `Q_random( &pm->cmd.serverTime )`, which overwrote the command's time at each draw, so the rest of the movement step read a random time (footstep and slope timers, weapon charge, saber lock and hand extension times, zoom). It now draws from `pml.randSeed`, set to the command's time at the start of each step: the same numbers, the command's time intact. The scenarios give the same counts before and after.
- X1, the dedicated server's busy wait (S2 above).

### Left in part 2

- CG5 (verified): `saberAttackChainCount` is capped at 16 (`game/bg_saber.c`) but coded on 4 bits, so 16 reaches the client as 0 and a long chain (fast style) is mispredicted once. Owner's choice (12 November 2026): a 5-bit field, no gameplay change. The field's contexts change alphabet (16 to 32 and 6 to 7 symbols), so the trained tables must be retrained: done with step 13 (owner, 12 November 2026).
- Linux: the scenarios and X1 measured on the VPS (GCC build from the Debian VM).

### Owner's decisions on the audit's engine items (12 November 2026)

B1 (snapshots without a base above 290 ms of round trip at 100 Hz) and B2 (`svs.nextSnapshotEntities` wraps after about 2.3 h with 32 clients at 100 Hz) wait for the snapshot store D3 (part 5); the interpolation redesign proposed by the audit goes with part 4; no hit confirmation sound. The status of every audit item is in [step-14-audit-100hz.md](step-14-audit-100hz.md) section 10.
