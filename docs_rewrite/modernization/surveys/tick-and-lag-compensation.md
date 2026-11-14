# Tick rate and lag compensation: what other games do, the measurements and the choices for step 14

Written on 11 November 2026, at the start of plan step 14 (beyond the codec, [../PLAN.md](../PLAN.md)). This page records what recent games and network libraries do about lag compensation and the simulation rate, how other games handle melee, what this engine already has, the measurements of the server's CPU time at 20, 40 and 125 Hz, and the owner's choices. It continues [network-settings.md](network-settings.md), whose points 2, 5 and 9 led here. The design that follows from it, with the inventory of the code to change, is in [../progress/step-14-fixed-tick.md](../progress/step-14-fixed-tick.md).

Each source is labelled as in the network settings survey:

- **primary**: the studio's own article, talk slides or source code, or the engine's or library's own documentation;
- **secondary**: a summary written by someone else (a press article quoting the developers, a third-party write-up of a talk);
- **community**: forum posts, player guides, cvar dumps, hosting guides.

What changed after this survey (each choice below is kept as it was taken): **the tick is 100 Hz, not 125** (owner, the evening of 11 November 2026). Every 20 Hz instant is then a tick, and the game's logic (saber, force powers, missiles, thinks) keeps its 20 Hz pace on those instants, identical to `sv_fps` 20, after tuning the saber per tick failed ([../progress/step-14-fixed-tick.md](../progress/step-14-fixed-tick.md) section 9). The saber and the force powers get no lag compensation (decision D2 of that document); the hitscan shots will (its part 8, not done). The fixed tick, the commands per tick, the two clocks and the interpolation are done; the measurements at 100 Hz are in that document and, in plain words, in [../CHANGES.md](../CHANGES.md).

**Lag compensation was dropped** (owner, 14 November 2026): it was planned for the hitscan shots only (step 14, part 8) and is not done; the shots stay judged with the positions the server holds. Sections 2 to 4 and the choices of section 8 about it are kept as they were written.

The owner asked (11 November 2026) that the design follow recent games and be done well even if it means starting again from zero, rather than follow what exists in the Jedi Academy community. The JKA mods read for this page (jaPRO, SaberMod, base_entranced, and Unlagged for Quake 3) are used only to locate the game code a change touches, never as the model.

## 1. Summary

Owner's choices (11 November 2026):

1. The client and the server share a **fixed simulation tick**, as Valorant, Overwatch and Rocket League do: movement runs once per tick instead of once per user command, the client sends one input per tick and the server buffers them, the client runs ahead of the server and adjusts its speed slightly to keep that buffer small, the other players are interpolated, and lag compensation rewinds the world by tick. This one model covers the items of step 14 that were listed apart: `sv_fps`, the fixed physics step, part of the prediction misses, `cg_smoothClients` and lag compensation.
2. The tick runs at **125 Hz** (8 ms). The code is written to work at any rate, so the value stays a constant that later measurements with real players can change. (Revised the same evening: 100 Hz, 10 ms.)
3. The fixed rate to each client goes from 100,000 to **200,000 bytes per second**: at 125 Hz no single message then delays the next snapshot.
4. Every defect found on the way is fixed, and the code is optimised where it can be; the analysis covers the engine (client and dedicated server), the shared player movement, the game module and cgame.

What the research found:

- Every game that compensates lag rewinds the targets to the time the shooter saw, which is the shooter's latency plus the interpolation delay (Valve, primary, 2001). All of them cap the rewind; the published caps range from 200 ms to 1 s, and the one game that describes what it does above its cap stops compensating (Overwatch, secondary).
- Projectiles are usually not compensated (Valve, primary); Overwatch predicts them on the client.
- No primary source describes the lag compensation of a melee game. For Honor delays offensive moves by 33 ms so that the defender sees more of the attack (Ubisoft, primary). This is the closest case to the saber, which remains a design question of this branch.
- Competitive games run 64 to 128 Hz (CS2, Overwatch, Valorant), physics games 120 Hz (Rocket League), battle royales 20 to 30 Hz (Apex Legends, Fortnite). Respawn compares going from 20 to 60 Hz to going "from 58 FPS to 60 FPS" for three times the bandwidth and CPU (primary).
- Valorant's server must finish a frame in 2.34 ms to fit three games per core at 128 Hz (primary).

What the measurements found (section 7): the server's work per frame does not shrink with a shorter interval (3.6 ms at 20 and 40 Hz, 2.9 ms at 125 Hz, with 31 bots), so 125 Hz uses 36 % of one core against 7.7 % at 20 Hz; one frame in 12,334 took longer than 8 ms. A defect of step 10 shows up at short intervals: server commands are sent again although nothing was lost (28 of 259 at 125 Hz).

## 2. Lag compensation in recent games

| game or library | source | rewind cap | what is rewound and how |
|---|---|---|---|
| Half-Life, Source games (Valve) | primary: Bernier, GDC 2001; Valve Developer Community wiki | `sv_maxunlag` 1.0 s, its default and its maximum | the other players only; the time is the shooter's latency plus the interpolation delay the client used, sent in each user command (`lerp_msec`); projectiles are not compensated |
| Counter-Strike 1.6 (GoldSrc) | community | `sv_maxunlag` 0.5 s | |
| CS:GO, CS2 | community | `sv_maxunlag` 0.2 s according to one cvar list, 1.0 s according to another | 64 Hz |
| Valorant | primary | a cap exists, its value is not published; Riot gives "a player with 500ms latency could kill you a half second after you'd moved behind cover" as what the cap prevents | 128 Hz; the client sends the simulation time it was looking at when it fired, which the shared fixed step makes exact |
| Apex Legends | primary | not published | 20 Hz; the full world state is saved every tick; the inconsistencies are "distributed symmetrically" between high and low pings rather than always favouring the shooter |
| Overwatch | secondary: a third-party summary of the GDC 2017 talk; a press summary of the 2016 developer video | hits are no longer predicted above about 220 ms of round trip; the target is then extrapolated | 16 ms command frames (62.5 Hz); a volume covering about the last half second of each entity's movement selects the entities worth rewinding; the shooter is favoured except against evasive abilities (Tracer's Blink, Reaper's Wraith Form), which take priority |
| Rainbow Six Siege | primary: Ubisoft dev blog, October 2017 | extra validation of hits from about 100 to 140 ms of ping; almost every hit refused at about 400 to 500 ms | thresholds tuned at any time by the studio |
| Unity Netcode for Entities | primary | a history of 16 ticks, raised to 32; an older tick is clamped to the oldest one held | the physics collision world of each tick |
| Photon Fusion | primary | `HitboxBufferLengthInMs` 200 by default | hitbox snapshots; off by default |

Valve's description of the method (Bernier 2001, primary): before running a user command, the server computes the player's latency, finds in its history the world update the player had received, moves every other player back to where they were when the command was created, accounting for "both connection latency and the interpolation amount the client was using that frame", runs the command (its shots trace against the old positions), then moves the players back. State other than the position may need rewinding too ("whether the player was alive or dead or whether the player was ducking"). The cost is the "shot around a corner" case: at about 500 ms of latency a player can be hit after reaching cover. Projectiles were left out of Half-Life's compensation on purpose ("what time space should the projectile live in?").

## 3. Melee and the saber

No primary source was found on lag compensation in a melee game.

- For Honor (Ubisoft, primary): at launch, a "Time Snap" aligned every action to the next 100 ms tick of a global clock, which added 0, 33 or 66 ms of input delay at random; it was removed. Since update 1.22.1 (2018, with the dedicated servers), offensive moves (attacks, guard break) are delayed by a fixed 33 ms and defensive moves (parry, counter guard break) are not: a defender receiving an attack 100 ms late then misses 66 ms of it instead of 100.
- Chivalry: Medieval Warfare and Mordhau: players describe hit detection on the client confirmed by the server (community only; no developer source). Mordhau's servers run 30 to 120 Hz depending on the hosting guide (community).

So the saber is a design question of this branch, to answer in the step 14 design: whether saber contacts are judged on the server at the tick the attacker saw (rewinding the defenders and their skeletons), whether defensive moves get priority as in Overwatch and For Honor, and how blocking is judged.

## 4. What this engine already has

Read in this tree (paths relative to `codemp/`):

- The server already knows, for every user command, the time the client was displaying: `CL_CreateCmd` sets `cmd->serverTime = cl.serverTime` (`client/cl_input.cpp:1264`), the time cgame draws the world at. The game clamps it to between 1,000 ms before and 200 ms after `level.time` (`game/g_active.c:2006-2011`). Valorant has to send that time explicitly; here the movement and the view share it, which the fixed tick will separate (a tick number for the movement, a view time for lag compensation).
- The rewind is therefore the round trip plus the interpolation delay (about 53 ms at 20 Hz, step 11), so a cap is a cap on the rewind, not on the ping: 200 ms of rewind covers a round trip of about 147 ms at 20 Hz and about 190 ms at 125 Hz (interpolation delay about 11 ms, estimated).
- The base game has no lag compensation (nothing in `game/`). Its hitscan weapons are the disruptor (primary and alternate fire, `WP_DisruptorMainFire`, `WP_DisruptorAltFire`) and the concussion rifle's alternate fire (`WP_FireConcussionAlt`), all in `game/g_weapon.c`; every other weapon fires projectiles. The saber is traced on the server once per server frame from the attacker's blade, against the defenders' current positions (`WP_SaberPositionUpdate`, called from `G_RunFrame`, `game/g_main.c:3317`); the force powers that trace (lightning, grip, push, pull) do so when they are used.
- `cg_smoothClients 1` (the default) draws the other players extrapolated from their last command time along their velocity (`BG_PlayerStateToEntityStateExtraPolate`, `game/bg_misc.c:2912`, with `trDuration = 50`, "1000 / sv_fps (default = 20)", `:2935`). A position extrapolated by the client exists nowhere in the server's history, so lag compensation requires the other players to be interpolated (`cg_smoothClients 0`'s behaviour).
- The game code was tuned on a 50 ms server frame: constants such as `level.time - 50` and `nextthink = level.time + 50` in the saber code (`game/w_saber.c:163`, `:648`), `FORCE_DEBOUNCE_TIME 50` ("sv_fps 20 = 50msec frametime, basejka balance/timing", `game/w_force.c:4133`), `FRAMETIME` (100 ms, Quake 3's) in 24 files, per-frame chances and decays. Any rate above 20 Hz changes their behaviour unless they are rewritten in time units; the step 14 document lists them.

The JKA mods, read only to locate the code they touch:

- jaPRO keeps 10 server frames of each client's box, origin and animation (`NUM_CLIENT_TRAILS`, 250 ms at 40 Hz), rewinds to the command's `serverTime`, and rewinds the animations as well for the skeleton traces; it compensates the disruptor and the concussion rifle's alternate fire among the base weapons, push and pull, and nudges projectiles forward.
- SaberMod (Jedi Outcast) keeps 32 server frames and caps the rewind with `g_unlaggedMaxPing` (default 200 ms, which bounds the rewind, not the ping), for the disruptor.
- Unlagged (Quake 3) keeps 17 frames of boxes (850 ms at 20 Hz).
- base_entranced (Siege) lists the areas of the game code whose timing depends on `sv_fps` (input, force powers, sabers and their damage, animation, box, sampling and order, weapons, stations, items, clients, movers, NPCs, Siege) and keeps the saber on a 50 ms grid at any rate. This confirms that raising the rate touches most of the game module, which the analysis of step 14 covers directly.

## 5. Simulation rates of recent games

| game | rate | source |
|---|---|---|
| Apex Legends | 20 Hz; 60 Hz would cost three times the bandwidth (about 60 KB/s per player at the start of a game, 180 KB/s at 60 Hz) and CPU for "two frames worth of latency" | primary |
| Fortnite | 30 Hz (raised from 20) | secondary |
| Jedi Academy servers | 20 by default; most servers 30 or 40 (Movie Battles II 30, JA+ 40) | community |
| Quake Live | 40 Hz | community |
| Overwatch | 62.5 Hz (16 ms); 20.8 updates per second sent until August 2016, then 63 | secondary |
| CS2 | 64 Hz with sub-tick timing of the inputs | community |
| Rocket League | physics 120 Hz on both sides, about 60 updates per second sent | primary (slides); send rate community |
| Valorant | 128 Hz for movement, physics and sending; frame budget 2.34 ms (three games per core); with 22 clients the frame time grows 2.2 times, so 15 clients is the limit for a stable 128 Hz; more than 99.3 % of frames on time since patch 5.07 | primary |

## 6. The fixed tick model

What the games that use it describe:

- One fixed step for movement and physics on client and server (Valorant 128 Hz, primary; Overwatch 16 ms, secondary; Rocket League 120 Hz, primary).
- One input per tick; each packet repeats the recent inputs (Overwatch: every input since the last movement state the server acknowledged, secondary). A missing input is replaced by the last one received (Rocket League, Valorant, primary).
- The client runs ahead of the server by half the round trip plus a buffer, so its inputs arrive just in time (Overwatch: one buffered command frame, secondary); Valorant targets about half a frame of buffered input on the server and one frame on the client (primary).
- The client speeds up or slows down slightly to keep the server's input buffer small but not empty: Overwatch shortens its 16 ms step to about 15.2 ms (secondary), Rocket League changes speed by up to 2.5 % (the network settings survey, point 4).
- The other entities are interpolated; the interpolation delay adapts to the measured jitter (Unity, primary; this branch since step 11).
- Lag compensation rewinds by tick, and the client tells the server the tick and the interpolation fraction it was looking at (Valorant, primary).

## 7. Measurements on the VPS

Measured on 11 November 2026 by the owner on the VPS (GCC 12 build of the dedicated server), with `svnetstats`, which prints the server's CPU time since step 14 part 1 (CHANGES.md, `svnetstats`): one human over the Internet and 31 bots on `mp/ffa3`, FFA with sabers only, `sv_maxclients 32`, about 100 to 120 s per rate. The game behaves wrongly above 20 Hz (tuned on 50 ms), which does not change the CPU time measured.

| | 20 Hz | 40 Hz | 125 Hz |
|---|---|---|---|
| duration | 122.0 s | 94.8 s | 98.7 s |
| server frames | 2,440 | 3,792 | 12,334 |
| server frame, mean / max | 3,647 / 7,749 µs | 3,741 / 7,553 µs | 2,882 / 11,209 µs |
| of which the game frame | 1,913 / 5,075 µs | 1,792 / 6,018 µs | 1,411 / 9,077 µs |
| of which the bots (their AI and movement) | 1,535 / 3,919 µs | 1,765 / 5,571 µs | 1,311 / 5,531 µs |
| of which the snapshots (32 built, 1 sent) | 196 / 2,671 µs | 182 / 3,284 µs | 159 / 2,782 µs |
| client packets (decoded, then the human's movement) | 15,250, 35.2 µs each | 11,850, 28.7 µs | 12,334, 24.4 µs |
| mean frame / interval | 7 % | 15 % | 36 % |
| frames longer than the interval | 0 | 0 | 1 |
| share of one core | 7.73 % | 15.32 % | 36.33 % |
| bytes per message (the human's) | 148.9 | 167.1 | 135.1 |
| down per client with headers (32 bytes per packet), estimated | 3.6 KB/s | 8.0 KB/s | 20.9 KB/s |
| base of the snapshots | 1 message back (all) | 1 (72 %), 2 (28 %) | 3 (99.5 %), i.e. 24 ms |
| server commands sent, of them repeats | 209, 0 | 163, 6 | 259, 28 |
| client packets lost | 0 | 0 | 0 |
| send time against the game clock: mean change, max | 0.417 ms, 4.162 ms | 0.355 ms, 4.529 ms | 0.206 ms, 7.272 ms |

What it shows:

- The work per frame is about the same at 20 and 40 Hz and lower at 125 Hz: part of it depends on the time elapsed rather than on the number of frames. 125 Hz costs 4.7 times the CPU of 20 Hz, not 6.25 times, before any optimisation.
- At 125 Hz the mean frame uses 36 % of the 8 ms budget; one frame of 12,334 (11.2 ms) went over it (99.99 % on time; Valorant reports 99.3 %).
- At 125 Hz the server sends about 21 KB/s to each client, about 670 KB/s (5.4 Mbit/s) for 32 players, far below the fixed rate.
- The bytes per message do not follow the rate in a simple way (148.9, 167.1, 135.1); the base age and the bots' behaviour, wrong above 20 Hz, both change. To measure again with the game working at 125 Hz.
- **Defect** (step 10): server commands are sent again although no packet was lost, more often as the interval shortens (0, 6, 28). Two causes are possible: a message that sends a new command also sends the earlier ones still in flight (by design of step 10, and there are more messages per round trip at 125 Hz), or the wait of twice the ping before resending is too short (the ping counts the time the server waits before reading packets). To count by cause and fix in step 14.
- With 31 bots the snapshot leaves 0.2 to 0.4 ms later or earlier from one frame to the next (0.1 ms without bots in step 11): it is sent after a game frame whose length varies.

## 8. Options and the owner's choices

### Tick rate

Estimated interpolation delay: one interval plus about 3 ms (measured only at 20 Hz: 53 ms).

| tick | interpolation delay | CPU, 32 players | down per client | exact interval in ms |
|---|---|---|---|---|
| 40 Hz | about 28 ms | 15 % (measured) | about 8 KB/s | yes (25 ms) |
| 62.5 Hz | about 19 ms | about 21 % (between the measurements) | about 11 KB/s | yes (16 ms) |
| 125 Hz | about 11 ms | 36 % (measured) | about 21 KB/s | yes (8 ms) |
| 128 Hz (Valorant) | about 11 ms | about 37 % | about 21 KB/s | no: 7.8125 ms |

**Owner's choice (11 November 2026): 125 Hz** (revised the same evening to 100 Hz, so that every 20 Hz instant is a tick: the note at the top of this page). It is the level of recent competitive games; the CPU and the bandwidth fit without optimisation; one input per tick matches the 125 client packets per second of step 11 (`MAX_PACKET_RATE`); 8 ms is a whole number of milliseconds; and every player gets the physics of "125 fps", the most common client frame rate in the JKA community, in identical 8 ms steps.

The owner asked whether to align on Valorant's 128 Hz, with the client packet rate and `com_maxfps`. Not done: every clock of the engine and the game counts whole milliseconds (`sv.time`, `level.time`, `serverTime`, `commandTime`, `trTime`, the timers), so 7.8125 ms would mean ticks of 7 and 8 ms in turn (thirteen of 8 and three of 7 every 125 ms) and uneven physics, or every clock moved to microseconds or tick numbers, for 0.19 ms less interval. The client packet rate follows the tick (125). `com_maxfps` stays the player's display setting: with the fixed tick the movement no longer depends on the frame rate, and the display interpolates between the predicted ticks, so a 144, 240 or 360 Hz screen stays smooth.

### Fixed rate to each client

The server delays the next snapshot when a message takes longer than an interval at the rate: `(size + 48) * 1000 / rate` in whole milliseconds, a single message counted at most 1,500 bytes (`SV_RateMsec`, `server/sv_snapshot.cpp:652`); larger messages are fragmented and paced fragment by fragment.

| rate | at 125 Hz, a message delays the next snapshot from | longest delay of a snapshot | fragments (gamestate, downloads) |
|---|---|---|---|
| 100,000 bytes/s | 752 bytes | 15 ms instead of 8 | reference |
| 150,000 bytes/s | 1,152 bytes | 10 ms | 1.5 times faster |
| 200,000 bytes/s | never (1,548 bytes give 7 ms) | none | 2 times faster |

Snapshots of 135 to 167 bytes are never delayed at any of these rates; only large messages are (a full snapshot at connection or after a long loss, a gamestate, a download). A higher rate costs nothing in normal play (it is a cap, not a use); a link slower than the rate loses packets during such a burst instead of being paced (below 200 KB/s, that is 1.6 Mbit/s).

**Owner's choice (11 November 2026): 200,000 bytes per second** (`SV_CLIENT_RATE`).

### Lag compensation

Decided: lag compensation by tick, with interpolated players. To decide in the step 14 design, with options and costs: the rewind cap (published values 200 ms to 1 s, Overwatch stops at about 220 ms of round trip), what is rewound (positions and boxes, the skeletons and animations for the disruptor and the saber, the states such as ducking or dead), the saber and the force powers (no published model; For Honor's delay on offensive moves and Overwatch's priority of evasive abilities are the two references), and the projectiles (not compensated in Source; spawned forward or predicted elsewhere).

## 9. What was not read or verified

- Overwatch's figures (the 220 ms limit, the half-second volume, the 15.2 ms step) come from a third-party summary of the GDC 2017 talk, whose video was not watched.
- The CS2 and GoldSrc values of `sv_maxunlag` come from community cvar lists, which disagree for CS2. The Valve Developer Community page on lag compensation refused automated reading (HTTP 403); its content was read through search excerpts.
- The melee games' hit detection (Chivalry, Mordhau) is described only by players.
- Fortnite's 30 Hz comes from a press article quoting a third-party analysis.
- The measurements of section 7 are one session per rate with bots and one human; nothing was measured with 32 human players.

## 10. Sources

Primary:

- Riot Games, "Peeking into VALORANT's Netcode": https://www.riotgames.com/en/news/peeking-valorants-netcode
- Riot Games, "VALORANT's 128-Tick Servers": https://technology.riotgames.com/news/valorants-128-tick-servers
- Riot Games, "How We Got to the Best-Performing VALORANT Servers Since Launch": https://playvalorant.com/en-us/news/dev/how-we-got-to-the-best-performing-valorant-servers-since-launch/
- Respawn, "What Makes Apex Tick": https://www.ea.com/en/games/apex-legends/apex-legends/news/servers-netcode-developer-deep-dive
- Yahn W. Bernier (Valve), "Latency Compensating Methods in Client/Server In-game Protocol Design and Optimization", GDC 2001: https://www.gamedevs.org/uploads/latency-compensation-in-client-server-protocols.pdf
- Valve Developer Community, "Lag compensation": https://developer.valvesoftware.com/wiki/Lag_compensation
- Ubisoft, "Dev Blog: Ping Abuse, Peeker's Advantage, and Next Steps" (Rainbow Six Siege): https://www.ubisoft.com/en-us/game/rainbow-six/siege/news-updates/1su64agGqZZQWb0mBeJUem/dev-blog-ping-abuse-peekers-advantage-and-next-steps
- Ubisoft, "Input Delay in For Honor": https://www.ubisoft.com/en-us/game/for-honor/news-updates/4qgcoZf3m61lpWD8GlN4JO/input-delay-in-for-honor
- Unity, Netcode for Entities, `NetCodePhysicsConfig`: https://docs.unity3d.com/Packages/com.unity.netcode@1.1/api/Unity.NetCode.NetCodePhysicsConfig.html, and its changelog: https://docs.unity3d.com/Packages/com.unity.netcode@1.9/changelog/CHANGELOG.html
- Photon Fusion, `LagCompensationSettings`: https://doc-api.photonengine.com/en/fusion/current/class_fusion_1_1_lag_compensation_settings.html
- Jared Cone, "It IS Rocket Science!", GDC 2018 slides (in the network settings survey): https://media.gdcvault.com/gdc2018/presentations/Cone_Jared_It_Is_Rocket.pdf
- Source code read to locate the affected game code: jaPRO (https://github.com/videoP/jaPRO), SaberMod (https://github.com/aufau/SaberMod), base_entranced (https://github.com/deathsythe47/base_entranced), Unlagged 2.01 in OpenArena (https://github.com/OpenArena/legacy/blob/master/game_code/oa-0.8.8/code/game/g_unlagged.c)

Secondary:

- Edgegap, Overwatch netcode (summary of Tim Ford's GDC 2017 talk): https://edgegap.com/blog/game-backend-deep-dive-overwatch-2016-netcode-architecture-rollback
- PCGamesN, Overwatch netcode video: https://www.pcgamesn.com/overwatch/cute-stickers-and-little-cups-help-explain-the-overwatch-netcode
- GamingBolt, Fortnite netcode: https://gamingbolt.com/epics-stunning-improvements-to-fortnite-netcode-continue-to-impress

Community:

- `sv_maxunlag` lists: https://totalcsgo.com/commands/svmaxunlag, https://hlds101.com/cvars/all.htm
- Chivalry: Medieval Warfare hit detection: https://steamcommunity.com/app/219640/discussions/0/535150948592117885
- Mordhau server tick rate: https://mordhau.fandom.com/wiki/Server_Configuration
- Jedi Academy server list (server frame rates): https://servers.moviebattles.org/?game=jka

## 11. Input margin, late and missing commands, clock steering (survey for step 14 part 4c, 13 November 2026)

What each game or library does about the four questions of part 4c: how early a command must reach the server, what the server does with a command that comes after its tick, what it does when no command comes, and how the client's clock is steered. Read on 13 November 2026; labels as in section 10.

| game or library | margin the client keeps | command after its tick | no command at the tick | clock steering | label |
|---|---|---|---|---|---|
| Counter-Strike 2 | a margin in milliseconds: `cl_clock_recvmargin_desired` (5 in the dumps), measured over a window of seconds (`cl_clock_recvmargin_window`), plus whole ticks against loss chosen by the player (`cl_clock_buffer_ticks`, 0); the server's receive margin is shown on the client's HUD (`cl_hud_telemetry_serverrecvmargin_graph_show`), so the server reports it | not thrown away: "Allow N late commands to run at 0 timescale prior to running an on-time command" (`sv_late_commands_allowed`), their button presses condensed into the on-time command (`sv_condense_late_buttons`); a queue too long is trimmed (`sv_cq_trim_bloat_*`) | not found | time sped up by at most 106 % and slowed to at least 93 % (`cl_clock_recvmargin_adjust_limit_speedup`, `_slowdown`), 63.2 % of the error removed in a time constant, one for speeding up and one for slowing down | names and descriptions: convar dump of the game (primary data, s2v.app); default values: community dumps of 2023 |
| Valorant | half a frame of buffered input on the server (about 4 ms at 128 Hz), "as short as possible to minimize latency"; the server "adjusts as needed"; a player option adds buffering | not described | "the server will predict what the client would have done", usually the keys still held | not described | primary (Riot, "Peeking into Valorant's netcode") |
| Overwatch | half the round trip plus one buffered command frame; the buffer grows under loss and shrinks afterwards | not described | the last input again, and the client is told | the client shortens its 16 ms frame to 15.2 ms (5 %) while it refills the buffer | secondary (machine-translated transcript of the GDC 2017 talk, Edgegap's write-up) |
| Rocket League | a buffer of inputs on the server: "Try to avoid empty buffer (runs physics using previous player input)", "Try to avoid large buffer (adds latency)" | the server "consumes 0, 1, or 2 inputs per frame" (buffer full: two inputs for one frame; "effective but with minor desyncs") | the previous input | "Server can tell client to run faster or slower": the client runs extra or fewer physics frames | primary (slides of the GDC 2018 talk) |
| Photon Fusion 2.1 | measured: "The client will send its inputs early enough to cover all but this percent of packet jitter" (the example: 3 %, the 97th percentile), more for a chosen number of consecutive losses (Redundant Inputs), plus an optional fixed amount | the input is missing for that tick (`GetInput` false) | left to the game | not described on the pages read | primary (Photon documentation) |
| Unity Netcode for Entities | fixed: 2 ticks (`TargetCommandSlack`) | not described | not described | time scale 0.9 to 1.1, 10 % of the error corrected per frame | primary (Unity documentation) |
| Unreal, Chaos networked physics | the client kept within the server's rewind history | not read | not read | time dilation driven by the server (`np2.TimeDilation`), the tick offset recomputed after 5 s out of bounds | community (Epic forum tutorial) |
| Source (Counter-Strike: Source server fork) | none: commands run on arrival, at most `sv_maxusrcmdprocessticks` per tick | run on arrival | a null command after `sv_player_usercommand_timeout` (3 s) | client clock correction against the snapshots | community |

What they agree on: the margin is a few milliseconds above the measured jitter, not whole ticks, where latency matters (Counter-Strike 2, Valorant, Fusion); the server tells the client its margin and the client changes its speed by 5 to 10 %, faster to catch up than to fall back (Counter-Strike 2); a missing command is replaced by the previous one; the most recent design (Counter-Strike 2) keeps the button presses of a late command and gives it no movement time. Loss buffering is a player option in Counter-Strike 2 and Valorant and automatic in Overwatch and Fusion; this branch has no network settings (plan step 11), so it is automatic here.

Not found: how long any of them repeats the previous input before giving up (Source: 3 s before a null command), Counter-Strike 2's current default values (the dumps are from 2023), the exact wording of the Overwatch talk.

Sources: Riot, https://www.riotgames.com/en/news/peeking-valorants-netcode ; Counter-Strike 2 convars, https://s2v.app/SchemaExplorer/cs2/convars , https://cs2.poggu.me/dumped-data/convar-list/ , https://github.com/ArmynC/ArminC-CS2-Cvars/blob/main/cvars/cvarlist.md ; Rocket League slides, https://media.gdcvault.com/gdc2018/presentations/Cone_Jared_It_Is_Rocket.pdf ; Overwatch, https://www.mo4tech.com/overwatchs-architecture-is-designed-to-synchronize-with-the-web.html , https://edgegap.com/blog/game-backend-deep-dive-overwatch-2016-netcode-architecture-rollback ; Photon Fusion, https://doc.photonengine.com/fusion/v2/manual/network-project-config ; Unity, https://docs.unity3d.com/Packages/com.unity.netcode@1.4/api/Unity.NetCode.ClientTickRate.html ; Unreal, https://forums.unrealengine.com/t/tutorial-networked-physics-pawn-tutorial/2657596?page=3 ; Source, https://github.com/hy1ex/improved-srcds-v34 .
