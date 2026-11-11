# 10. Network settings: what other games do, and the options for step 11

Written on 10 November 2026, before plan step 11 (settings computed at run time, [PLAN.md](PLAN.md)). For each setting the step is meant to remove, this page records what recent games and network libraries do, the options for this branch with their cost in CPU, bytes and memory, and the owner's choice where one was made. Nothing here was measured on a network: the numbers about other games come from the sources in section 10.12, read on 10 November 2026, and the numbers about this branch come from the step documents of [progress/](progress/README.md). Engine statements carry a `file:line` reference relative to `codemp/`.

Each source is labelled by how far it can be trusted:

- **primary**: the studio's own article, talk slides or source code, or the engine's or library's own documentation;
- **secondary**: a summary written by someone else (a press article quoting the developers, a third-party write-up of a talk);
- **community**: forum posts, player guides, cvar dumps.

## 10.1 Summary

- Competitive games run fixed rates and give the player no bandwidth setting. CS2 keeps a `rate` cvar whose default, 786432 bytes per second, means "unrestricted"; Valve's own network library sends at a fixed rate and only plans to estimate bandwidth one day.
- Adaptation is used for three things only: absorbing jitter (the interpolation delay), covering lost inputs (input redundancy), and lowering the send rate when a connection cannot keep up (Overwatch).
- No game was found that sends snapshots more often than it simulates. The reverse exists (Overwatch until 2016, Rocket League).
- When the simulation rate changes the gameplay, the usual answer is a fixed, higher tick rate and gameplay tuned for it (Quake Live went to 40 Hz, Rocket League runs at 120 Hz: "Slower rate = larger steps = larger penetrations = inconsistent hits").
- The settings players still have elsewhere are a buffering level (CS2, Valorant) and send-rate limits (Overwatch). With the choices below this branch would have none.

## 10.2 This branch's numbers and the engine facts the options depend on

Bytes (sizes from [progress/README.md](progress/README.md); headers from [README.md](README.md), "Conventions", and `qcommon/net_chan.cpp:110-114`):

- A snapshot carries about 67 bytes of payload on the held-out demos (66.9), plus the 4-byte netchan header and 28 bytes of UDP and IP: about 100 bytes. That is about 2 KB per second at 20 snapshots per second and 4 KB per second at 40. A 190-byte snapshot (the p95 target of [PLAN.md](PLAN.md) section 6) gives 4.4 and 8.9 KB per second. For comparison, Apex Legends uses about 60 KB per second at the start of a game (Respawn, primary).
- A client packet carries 6.0 bytes of payload in game and 10.07 on the recorded sessions (step 8), plus 6 bytes of netchan header (sequence and `qport`) and 28 of UDP and IP: 40 to 44 bytes, so 5.0 to 5.5 KB per second at 125 packets per second.

Engine facts:

- Player movement runs once per user command, when the command arrives: `ClientThink` calls `ClientThink_real` (`game/g_active.c:3577`), which updates the player's entity state at the end (`game/g_active.c:3310`). It does not wait for the server frame.
- The saber runs once per server frame: `WP_SaberPositionUpdate` is called from `G_RunFrame` (`game/g_main.c:3317`). This is why a higher `sv_fps` changes saber combat.
- The game clock advances only by whole server frames (`server/sv_main.cpp:1228`), and a client gets at most one snapshot per frame (`server/sv_snapshot.cpp:813`); the snapshot's `serverTime` is that clock.
- When no command arrives, the server does not move the player; after one second it sets `EF_CONNECTION` (`game/g_active.c:3740`).
- The client already adapts its clock without a margin: `CL_AdjustTimeDelta` (`client/cl_cgame.cpp:626`) moves the client's time 1 ms forward at each snapshot and 2 ms back each time it had to extrapolate, so it stays at the edge where the next snapshot arrives just in time. `cl_timeNudge` shifts that time by at most 30 ms in a release build (`client/cl_cgame.cpp:764-777`), and time never moves backwards (`client/cl_cgame.cpp:778-781`).
- `cl_packetdup` (0 to 5, default 1) repeats the commands of that many previous packets (`client/cl_input.cpp:1499-1504`); `cl_maxpackets` (20 to 1000, default 63) caps the packet rate (`client/cl_input.cpp:1453-1462`).
- Timeouts: `cl_timeout` 200 s (`client/cl_main.cpp:2658`), `sv_timeout` 200 s and `sv_zombietime` 2 s (`server/sv_init.cpp:984-985`).
- Cvars flagged `CVAR_CHEAT` are reset unless the server sets `sv_cheats 1` (`Cvar_SetCheatState`, `qcommon/cvar.cpp:909`, called from `client/cl_parse.cpp:176`).

## 10.3 Point 1: server-to-client bandwidth

Removes `rate`, `sv_maxRate`, `sv_minRate`, `sv_ratePolicy`, `sv_clientRate`, `sv_lanForceRate`.

What others do:

- CS2: `rate` still exists, default and menu maximum 786432 bytes per second, shown as "Unrestricted" (community).
- GameNetworkingSockets (Valve's library): the send rate is clamped by `SendRateMin` and `SendRateMax`, default 256K, and the header says both "should always be set to the same value, to manually configure a specific send rate"; "Eventually we hope to have the library estimate the bandwidth of the channel" (primary).
- Overwatch: no bandwidth setting. Since August 2016 the game "will automatically and adaptively scale down your update rate if we find your connection can't keep up" (community manager quoted by PC Gamer, secondary), and the options "Limit Client Send Rate" and "Limit Server Send Rate" lower the rates (to about 21 Hz according to third-party guides, community).
- Unreal Engine, Tribes, Halo: Reach: a bandwidth cap per client and a priority per object; when a connection is saturated, low-priority objects wait (Unreal: Steve Streeting's article; Tribes: SnapNet's summary of the paper; Halo: Reach built on the Tribes model; secondary). This suits games where bandwidth is a real limit (many objects, large worlds).
- Apex Legends: 20 Hz, about 60 KB per second per player, no adaptive logic described (primary).

Options:

| option | for | against | CPU | bytes | memory |
|---|---|---|---|---|---|
| 1a. fixed high cap, no cvar (as CS2 and GameNetworkingSockets) | simple, predictable; the cap only paces the gamestate and downloads | a link below about 10 KB/s loses packets instead of slowing down | 0 | 0 | 0 |
| 1b. 1a plus a fallback: halve the snapshot rate while loss lasts (as Overwatch) | protects weak links | effort S; a rule to tune | negligible | 0 | a few bytes per client |
| 1c. bandwidth estimation with priorities (Unreal, Tribes) | adapts to a saturated link | effort M, risk of oscillation; useless at 2 to 9 KB/s | negligible | 0 | a few hundred bytes per client |

**Owner's choice (10 November 2026): 1a.** 1b can be added later if measurements show weak links.

## 10.4 Point 2: snapshot rate and server frame rate

Removes `snaps`, `sv_snapsMin`, `sv_snapsMax`, `sv_snapsPolicy`.

What others do:

- Valorant: 128 Hz simulation and sending, client simulation at the same fixed rate (Riot, primary).
- CS2: 64 Hz with sub-tick timing; the tick rate cannot be changed (community).
- Overwatch: the server simulates at about 62.5 Hz; it sent 20.8 updates per second by default until August 2016 ("the server does tick at 60Hz, it's the client update rate that is lower", the game director quoted by GameSpot), then 63 (secondary).
- Rocket League: physics at 120 Hz on server and client; the server sends about 60 times per second (slides, primary; send rate from a developer's forum post, community).
- Apex Legends: 20 Hz. Moving to 60 Hz would triple the bandwidth for about two frames less delay; Respawn compares it to "58 FPS and 60 FPS" (primary).
- Quake Live: `sv_fps` 40, and `snaps` removed because the client follows the server (community).

Options:

| option | for | against | CPU | bytes | memory |
|---|---|---|---|---|---|
| 2a. one snapshot per server frame, `sv_fps` stays 20 (as Apex) | no gameplay change; the snapshot rate follows `sv_fps` if it changes later | 50 ms between snapshots | 0 | 0 | 0 |
| 2b. snapshots at 40 Hz with the game at 20 Hz | fresher player positions (movement runs per command), about 25 ms of interpolation, saber unchanged | no precedent found; needs a snapshot time of its own (point 6, 6c); missiles, movers and saber stay at 20 Hz | snapshot building doubled (to measure) | about +640 bytes/s of headers per client | `PACKET_BACKUP` 32 covers 0.8 s instead of 1.6 s; 64 doubles the server's frame memory |
| 2c. `sv_fps` 40 with the saber adapted to it (as Quake Live; the usual answer) | everything at 40 Hz | a gameplay change, validated by playing; belongs to step 14 | game CPU doubled | about doubled (about 4 KB/s) | unchanged |

**Owner's choice (10 November 2026): 2a in step 11 (nothing changes in game), then 2c in step 14** (`sv_fps` 40 with the saber adapted to it, once the saber code is read): 40 Hz is smoother and more precise (half the interpolation delay, the saber checked twice as often).

## 10.5 Point 3: client packet rate

Removes `cl_maxpackets`, `cl_lanForcePackets`.

What others do:

- Quake Live: `cl_maxpackets` locked at 125 (community).
- Rocket League: "Client sends input for every physics frame", 120 Hz (slides, primary).
- Valorant: fixed 128 Hz client simulation (primary; the send rate itself is not stated).
- Overwatch: about 63 Hz, with the "Limit Client Send Rate" option (community).
- Apex Legends: "We process variable rate of inputs fine" (primary); about 58 packets per second measured by a third party (secondary).
- CS2: `cl_cmdrate` removed, commands sent per tick (community).
- SnapNet: input rates "often need to be clamped to both a minimum and maximum value", since 300 Hz can hurt client and server performance (primary).

Options (the client sends one packet per frame, up to the cap; above it, a packet carries several commands):

| option | for | against | CPU | bytes | memory |
|---|---|---|---|---|---|
| 3a. cap at 125 Hz (as Quake Live) | about 4 ms of added input delay on average (8 ms at 63) | none found | server: 32 × 125 = 4,000 packets/s, to measure, expected negligible | 5.0 to 5.5 KB/s up | 0 |
| 3b. cap at 250 Hz | about 2 ms | twice the packets for the server | twice 3a | 10 to 11 KB/s up | 0 |
| 3c. send at the server's rate (as CS2, Overwatch, Valorant) | | does not fit this engine: movement runs per command, so 20 Hz would add about 25 ms | lower | lower | 0 |

**Owner's choice (10 November 2026): 3a**, a fixed cap of 125 with no adaptation: the frame rate does the rest (60 fps sends 60 packets per second; 333 fps sends 125 packets of 2 or 3 commands). Commands, and so movement precision, still follow the frame rate.

Found while measuring (10 November 2026, VPS, `com_maxfps` 125, `cl_maxpackets` 100): the client sent 62.7 packets per second, not 100. The cap is a minimum gap between two packets, `1000 / cl_maxpackets` ms in integer milliseconds, checked once per frame (`client/cl_input.cpp:1460-1464`): 10 ms against frames of 8 ms lets a packet leave every second frame only. The fixed cap of 3a has the same trap (8 ms against frames of 6.9 ms at 144 fps gives 72 packets per second, at 165 fps 82), so it must be a budget instead: the unused part of the interval carries over to the next frame, within a bound, so that the client sends min(frame rate, 125) packets per second on average.

## 10.6 Point 4: user-command redundancy

Removes `cl_packetdup`.

What others do:

- QuakeWorld: three commands in every packet, the current one and the two before it, "so if the last packet was dropped, it can be recovered" (`QW/client/cl_input.c`, primary).
- Quake 3: `cl_packetdup`, 0 to 5, default 1.
- Overwatch: every input since the last movement state the server acknowledged (a sliding window); when the server runs out of inputs, it tells the client to simulate slightly faster to rebuild its buffer (summary of the GDC 2017 talk, secondary).
- Rocket League: the server buffers inputs; an empty buffer repeats the last input; the server tells the client to run extra or fewer physics frames (slides, primary).
- Valorant: a missing move is predicted by the server, "usually assuming the keys held in the last received update are still held" (primary).
- This engine has no input buffer: when a packet is lost, the next command runs over the missing time with its own input, so a prediction miss appears only when the lost input changed something (a jump, an attack).

Options:

| option | for | against | CPU | bytes | memory |
|---|---|---|---|---|---|
| 4a. three commands, fixed (as QuakeWorld, `cl_packetdup` 2) | covers two losses in a row | bytes paid without loss | negligible | to measure (a repeated command is cheap in netcodec) | 0 |
| 4b. adaptive, from the loss the server measures | no extra byte without loss | the server must report its measured loss to the client (a format change) | negligible | a few bits per server message | a few bytes per client |
| 4c. sliding window with a cap (as Overwatch) | no parameter | clients at 333 fps make about 33 commands per 100 ms of round trip: the cap is mandatory | negligible | grows with the round trip | 0 |
| 4d. input buffer and client time scaling (Overwatch, Rocket League, Unity) | the modern model | changes the movement model to a fixed tick: step 14 | | | |

**Owner's choice (10 November 2026): measure first** (the real loss of client packets on Internet links, and the byte cost of a repeated command in netcodec), then choose between 4a and 4b.

Measured on 10 November 2026 (VPS over the Internet, `com_maxfps` 125, `cl_maxpackets` 100, so 62.5 packets per second and 2 new commands per packet, about 4 to 6 minutes per run, `svnetstats`):

| `cl_packetdup` | user commands per packet | payload bytes per packet | with headers (+6 netchan, +28 UDP and IP) | upload at 62.5 packets/s | client packets lost |
|---|---|---|---|---|---|
| 0 | 1.99 | 7.7 | 41.7 | 2.6 KB/s | 0 of 21,215 |
| 1 | 4.00 | 10.7 | 44.7 (+7 %) | 2.8 KB/s | 0 of 14,357 |
| 2 | 6.00 | 14.4 | 48.4 (+16 %) | 3.0 KB/s | 0 of 15,969 |

A repeated command costs 1.5 to 1.9 bytes. No client packet was lost in 51,541 on this link, so the loss that 4b would measure is zero here, and the saving 4b offers over a fixed redundancy is at most about 0.4 KB/s of upload.

**Owner's choice (10 November 2026): 4a, each packet repeats the commands of the 2 previous packets** (as QuakeWorld; `cl_packetdup` removed). Two client packets lost in a row then cost no command: without the repeat, the server runs the next command over the missing time with that command's input, so a jump, an attack or a change of direction in a lost command never happens on the server, and the client, which predicted it, is corrected. With 1, as today, two losses in a row lose commands. Two packets cover 32 ms of commands at 62.5 packets per second and 16 ms at 125; a longer burst still loses commands.

## 10.7 Point 5: interpolation delay

Removes `cl_timeNudge` and the user choice of `cg_smoothClients` (`cl_timenudgeDuration`, named in the plan, does not exist in this tree).

What others do:

- Source: `cl_interp` 0.1 s, the interpolation period being `max(cl_interp, cl_interp_ratio / cl_updaterate)` with a ratio of 2: two snapshot intervals, so that one lost snapshot still leaves two to interpolate between (Valve Developer Wiki, primary, read through search excerpts).
- CS2: `cl_interp` is read-only; the menu offers "Buffering to smooth over packet loss": none, 1 or 2 packets (15.6 ms each at 64 Hz), default none (community).
- Valorant: targets one buffered frame; players choose "Network Buffering" among Minimum, Moderate and Maximum (primary for the setting's existence; the delays per level are community figures).
- Unity Netcode for Entities: the interpolation delay is computed from the measured jitter of snapshot arrivals (`InterpolationDelayJitterScale`, default 1.25), corrected a little each frame and never moving backwards (primary).
- Gaffer On Games: about three send intervals at 2 to 5 % loss, plus one or two frames for jitter; FPS games often hide the delay by extrapolation (primary).
- This engine: about one interval, at the edge, then extrapolation when the next snapshot is late (section 10.2).

Options:

| option | for | against | CPU | bytes | memory |
|---|---|---|---|---|---|
| 5a. fixed N intervals (Source: 2) | simple, survives one loss | +50 ms at 20 Hz on every link | 0 | 0 | 0 |
| 5b. a few levels chosen by the player (CS2, Valorant) | | contrary to the goal of no network settings | 0 | 0 | 0 |
| 5c. adaptive (as Unity): the current edge plus a margin from the measured jitter and loss | the smallest delay each link allows; equal to today's behaviour on a good link | an estimator to tune | negligible | 0 | about 256 bytes |
| 5d. remove `cl_timeNudge` only, rethink the clock in step 14 | | jittery links keep extrapolating | 0 | 0 | 0 |

How 5c works: for each snapshot the client records the difference between its arrival time and its `serverTime`; the spread of that difference over the last 64 snapshots or so is the jitter, and lost snapshots are counted as well. The target is the edge used today plus a margin proportional to the jitter. The client's time drifts slowly towards the target, as `CL_AdjustTimeDelta` already drifts, and never moves backwards. On a good link the margin is about zero.

**Owner's choice (10 November 2026): 5c, with the margin.** The game should be as precise and responsive as possible in as many cases as possible: the edge gives the least delay without guessing positions, and the margin keeps the positions shown real (interpolated, not extrapolated) on unstable links. Going below the edge, an automatic negative `cl_timeNudge`, would extrapolate, so positions would be wrong whenever a player turns; it is not proposed. The largest gain in responsiveness comes from point 2 (40 Hz halves the interval).

Interpolating shows the other players between two snapshots already received: slightly in the past, at real positions. Extrapolating continues their last known movement beyond the newest snapshot: closer to the present, but a guess that is wrong whenever they change direction, and they jump to their real position when the next snapshot arrives. In saber combat directions change all the time, so a real position a few milliseconds late is more reliable than a guess. With lag compensation (step 14) the server rewinds the world to the time the shooter saw, so an interpolated position is exactly right and an extrapolated one is wrong even for the server: interpolation is the model to keep.

What the engine did before (`CL_AdjustTimeDelta`, `client/cl_cgame.cpp:626`): it moved the client's time 1 ms forward at each snapshot and 2 ms back after any frame that came within 5 ms of the newest snapshot, so it settled where that happened in one interval out of three. On a stable link that is 5 ms more delay than needed; with jitter the late snapshots push the client past the newest one, so it extrapolates (estimated, assuming normally distributed jitter: about 8 % of the intervals with 5 ms of jitter, 18 % with 10 ms).

Design agreed on 10 November 2026:

- At each frame that brings snapshots, the client records the offset between the newest one's `serverTime` and its arrival time (`cls.realtime` of the frame that processes it: the moment it becomes usable for display), over the last 64 such frames (3.2 s at 20 Hz). Only the newest snapshot of a frame gives an offset: the older ones read in the same frame waited for the client (a long frame, the scoreboard loading models), not for the network, and would raise the delay for 3 s after every client freeze. The server messages missing are counted for every snapshot read, from the gaps in their numbers, so that snapshots read together do not count as lost.
- The target is the client time at which the next snapshot has already arrived in 63 cases out of 64: the second latest arrival of the window minus one snapshot interval (the second, so that one isolated spike does not raise the delay). On a stable link it shows the world about 5 ms more recent than before, at real positions; on an unstable link the delay grows by about the jitter, and about 1.5 % of the intervals are extrapolated instead of 8 to 18 % (estimates).
- When the newest snapshot arrives more than 100 ms earlier against its `serverTime` than any snapshot of the window, the server's clock jumped forward against the client's (jitter only makes arrivals later): the client's time and every offset of the window move forward at once by the jump, estimated against the window's median, so the delay stays what it was (owner, 11 November 2026). The jump is measured on the second snapshot that shows it: the first one after a `map_restart` leaves late, by the time the server takes to restart the map, which made the jump about 40 ms short (seen in game: the delay then crept up by 37 to 42 ms over the next 5 s); measured on the second, the jump is about 400 ms and the delay moves by 2 ms afterwards. `map_restart` does that: it runs three frames of 100 ms at once to let the game settle (`server/sv_ccmds.cpp:318-323`, id Software's code; a map load does the same, but the client starts again from its gamestate). Without it, the older offsets held the client 350 ms behind the newest snapshot for 3 s, past the 64 user commands it keeps, so cgame showed "Connection Interrupted" (found in game); a first fix restarted the window, which brought the delay back in 2 to 3 s; the old engine's fast adjustment took about 3 snapshots.
- One interval of delay is added (50 ms at 20 Hz) while more than 5 % of the server messages of the window are lost, and removed below 2 % (owner: such a link is bad, and 50 ms are better than an extrapolation every second); below that, a lost snapshot is bridged by extrapolation as before.
- The client's time moves towards the target by the same steps as before, +1 or -2 ms per snapshot: no jump, and it never moves backwards. The engine's fast adjustment (half the difference when it exceeds 100 ms) is measured against the target, not against the newest snapshot as before: measured against the newest snapshot, it cut in half every delay above 100 ms, so with the loss margin the delay swung between about 50 and 100 ms (found in game with `net_dropsim 10`: 74.7 ms on average, 55 of 688 intervals extrapolated).
- `cl_timeNudge` is removed. Two `CVAR_CHEAT` cvars force the delay for debugging (an explicit flag, not a magic value): `cl_debugInterpForce` (0 computed, 1 forced) and `cl_debugInterpDelay` (the forced delay behind the newest snapshot when it arrives, in ms). `netstats` prints the measured delay, the jitter, the extrapolated intervals and whether the loss margin is on.
- Measured in game (11 November 2026, VPS): 53.0 ms behind the newest snapshot when it arrives, 0 extrapolated intervals without loss; 102.8 ms and 0.6 % extrapolated with 9 % of the messages lost (loss margin on); unchanged after a client freeze (Tab). The old algorithm with `cl_timeNudge 0` gave about 55 to 56 ms (estimated from its code, not measured): the same within 2 to 3 ms.
- Less delay was considered (owner, 11 November 2026, who played with `cl_timeNudge -14` to see half blue and half yellow on the lagometer: about 14 ms more recent, guessed half of the time): showing the world closer to the newest snapshot for everyone means extrapolated positions part of each interval. Kept as designed (no extrapolation): the delay will drop with `sv_fps` 40 (half the interval: about 25 ms when a snapshot arrives) and stop mattering for hits with lag compensation, both in step 14.
- `cg_smoothClients` (1: the other players follow the trajectory the server sends, carried forward to the displayed time, which extrapolates; 0: they are interpolated between snapshots) is decided in step 14 with the audit of prediction and interpolation (owner, 10 November 2026).

## 10.8 Point 6: a snapshot time stamp for the client's clock

What others do:

- Valorant: the server keeps a mapping between each client's move numbers and its own ticks and adjusts it to keep a healthy queue (primary).
- Overwatch, Rocket League, Unity: the client runs ahead by half the round trip plus a buffer and speeds up or slows down to keep the server's input buffer small but not empty (Rocket League: speed changes up to 2.5 %; secondary and primary).
- CS2: each input carries a sub-tick time (community).
- All of these have a fixed tick and an input buffer; none compares directly with this engine, where the snapshot leaves right after the server frame.

Why a time stamp could matter here: the client derives the server clock from the arrival times of the snapshots. If the time between the server frame and the actual send varies, the client takes it for network jitter, and 5c would add a margin for nothing. A time stamp lets the client remove it. On the server that time varies for several reasons: frames start with a millisecond granularity (the dedicated server sleeps in steps of 1 ms, `qcommon/common.cpp:1551-1558`), a frame can start late under load, two frames can run back to back to catch up (`server/sv_main.cpp:1226`) with the snapshots sent after both, and the snapshots of 32 clients are built and coded one after the other. The coding order alone is probably below 1 ms (32 clients at a few tens of microseconds each, an estimate); the frame scheduling was not measured.

`serverTime` itself stays the game clock: every time in the entity states (trajectories, events) is on that clock, and a real time in its place would no longer match them. What a "real server time" means here is the offset between the real send time and the game clock, sent next to it (6b).

Options:

| option | for | against | CPU | bytes | memory |
|---|---|---|---|---|---|
| 6a. nothing | | | 0 | 0 | 0 |
| 6b. the frame-to-send delay in each snapshot | removes server-side jitter from the clock | small gain if the delay is small | negligible | about 0.1 to 0.5 byte per message | 0 |
| 6c. a time of its own in each snapshot | required by 2b | only useful with 2b | negligible | about 1 byte per message | 0 |
| 6d. a tick-based clock as in Overwatch | | goes with 4d: step 14 | | | |

**Owner's choice (10 November 2026), after asking whether a real server time would be better: measure first, then 6b if it helps.** The offset between the real send time and the game clock is added to `svnetstats`; if it varies by 1 ms or more, it is sent in each snapshot (6b) together with 5c, so that the jitter 5c measures is the network's only and its margin stays as small as possible. If it stays constant, nothing is sent (6a).

Measured on 10 November 2026 (VPS, GCC dedicated server, one player, 6,770 snapshots, about 5.6 minutes): mean change 0.105 ms, 45 changes of 1 ms or more out of 6,769 (0.7 %), largest 2.56 ms. This is far below the jitter of an Internet path and below the client's arrival resolution (one client frame, 8 ms at 125 fps), so nothing is sent (6a). With 32 players the coding order shifts each client by about the same amount at every frame, so the change between consecutive snapshots should stay small; not measured.

## 10.9 Point 7: timeouts

What others do:

- GameNetworkingSockets: `TimeoutInitial` and `TimeoutConnected`, 10 s each by default (secondary).
- Source and CS2: `cl_timeout` 30 s (community).
- Unreal Engine: `InitialConnectTimeout` 60 s and `ConnectionTimeout` 60 s in `BaseEngine.ini` (community, quoting the file); shipped games often use 15 to 30 s.
- All of them separate the connection phase from the game.
- This engine: 200 s on both sides (section 10.2). The fixed 3 s of the connection stay (owner, 10 November 2026).

Options:

| option | for | against | CPU | bytes | memory |
|---|---|---|---|---|---|
| 7a. two constants: about 60 s until the client is active (map loading), about 30 s in game | as the libraries above | the values are a choice | 0 | 0 | 0 |
| 7b. a multiple of the keep-alive interval | | the client sends every frame anyway; only loading matters | 0 | 0 | 0 |
| 7c. keep 200 s, as a constant | | a dead client holds its slot for over 3 minutes | 0 | 0 | 0 |

**Owner's choice (10 November 2026): 7a, with 60 s until the client is active and 30 s in game.**

## 10.10 Point 8: debug cvars and statistics

What others do:

- Overwatch: a network performance graph (Ctrl+Shift+N: interpolation delay, packets per second) (community).
- CS2: `net_graph` removed, replaced by a "Telemetry" section (ping, loss, frame time) and `cl_ticktiming` (community).
- Source: `net_fakelag`, `net_fakeloss`. Unreal: `net.DisableBandwithThrottling`, packet lag and loss emulation (secondary).

Options:

| option | for | against |
|---|---|---|
| 8a. a cvar forcing each computed value, the measured values in `svnetstats` and `netstats`, `net_dropsim` (exists) for loss | can reproduce the old behaviour and isolate a problem | |
| 8b. display only | | forcing a value needs a rebuild |
| 8c. 8a plus an on-screen display for players (ping, loss, delay), as Overwatch and CS2 | | more work in cgame |

**Owner's choice (10 November 2026): 8a, and it must not be a way around the computed values**, with the following protection (agreed the same day):

- Server-side forcing cvars (bandwidth, timeouts) can only be set by the server's administrator.
- Client-side forcing cvars (interpolation delay, packet rate) are flagged `CVAR_CHEAT`, so the engine resets them unless the server sets `sv_cheats 1`.
- The limit: the code is GPL, so a modified client gets around anything decided on the client. What matters is enforced on the server: it accepts at most 137 packets per second per client on average (10 % above the client's 125, for the difference between the two clocks), with a burst of 125 packets; if step 14 adds lag compensation, the server bounds the view time a client claims. Without lag compensation (the base game), the interpolation delay only changes what the player sees.

## 10.11 Point 9: removing the old settings

The old cvars are removed, with the menu that sets them (`ui/ui_main.c:4686-4693`), and the change is described in [09-changes-from-openjk.md](09-changes-from-openjk.md). Owner's choice (10 November 2026): agreed.

The owner notes that JKA players care a great deal about `cl_timenudge` and impose 0, so that no player gains an advantage by changing it. With 5c:

- the minimum is exactly today's behaviour with `cl_timenudge 0` (the edge);
- no player can go below it: the setting no longer exists, and the debug cvar is `CVAR_CHEAT`;
- a player on an unstable link gets more delay, so sees the others slightly later: a disadvantage for that player rather than an advantage, replacing the extrapolation jumps such a link gives today.

This is the equivalent of `cl_timenudge 0` imposed on everyone, with an automatic margin for bad links (the owner chose the margin, point 5).

The owner asked whether `cl_timeNudge` stays useful on connections with a high ping. It does not reduce the ping: a negative value shows the other players up to 30 ms earlier by extrapolating their positions, a positive one adds margin, whatever the ping. What hurts at a high ping is that a player must aim ahead of the target by their ping, since the base game has no lag compensation (nothing in `game/`). Recent games rewind the world on the server to the time the shooter saw (Valorant and Apex Legends, primary; Overwatch, secondary): that is an item of step 14.

## 10.12 What was not read or verified

- The video of the Overwatch GDC 2017 talk was not watched; its points come from a third-party summary.
- The Tribes paper and Unity's source code were not read; Unity's documentation was.
- The Valve Developer Wiki page refused the download (HTTP 403); its figures come from search excerpts.
- The figures for Quake Live, the CS2 cvars, the Valorant buffering levels and the GameNetworkingSockets timeouts come from community or secondary sources.
- None of these games was measured here.

## 10.13 Sources

Primary:

- Riot Games, "Peeking into VALORANT's Netcode": https://www.riotgames.com/en/news/peeking-valorants-netcode
- Respawn, "What Makes Apex Tick: A Developer Deep Dive into Servers and Netcode": https://www.ea.com/en/games/apex-legends/apex-legends/news/servers-netcode-developer-deep-dive
- Jared Cone, "It IS Rocket Science! The physics and networking of Rocket League", GDC 2018 slides: https://media.gdcvault.com/gdc2018/presentations/Cone_Jared_It_Is_Rocket.pdf
- Valve, GameNetworkingSockets, `steamnetworkingtypes.h`: https://github.com/ValveSoftware/GameNetworkingSockets/blob/master/include/steam/steamnetworkingtypes.h
- id Software, QuakeWorld `cl_input.c`: https://github.com/id-Software/Quake/blob/master/QW/client/cl_input.c
- Valve Developer Wiki, "Source Multiplayer Networking": https://developer.valvesoftware.com/wiki/Source_Multiplayer_Networking
- Unity, Netcode for Entities, `ClientTickRate`: https://docs.unity3d.com/Packages/com.unity.netcode@1.1/api/Unity.NetCode.ClientTickRate.html, and "Time synchronization": https://docs.unity3d.com/Packages/com.unity.netcode@1.6/manual/time-synchronization.html
- Glenn Fiedler, "Snapshot Interpolation": https://gafferongames.com/post/snapshot_interpolation/
- SnapNet, "Netcode Architectures Part 3: Snapshot Interpolation": http://www.snapnet.dev/blog/netcode-architectures-part-3-snapshot-interpolation/

Secondary:

- Edgegap, Overwatch netcode (summary of Tim Ford's GDC 2017 talk "Overwatch Gameplay Architecture and Netcode"): https://edgegap.com/blog/game-backend-deep-dive-overwatch-2016-netcode-architecture-rollback
- PC Gamer, Overwatch high-bandwidth rollout: https://www.pcgamer.com/high-bandwidth-support-is-being-rolled-out-to-overwatch-at-this-very-moment/
- Game Informer, Overwatch 63 Hz: https://gameinformer.com/b/news/archive/2016/08/12/overwatch-is-getting-high-bandwidth-63hz-tick-rate-for-all-modes.aspx
- GameSpot, Overwatch tick rate: https://www.gamespot.com/articles/overwatch-dev-responds-to-tick-rate-confusion-talk/1100-6440543/
- SnapNet, "Netcode Architectures Part 4: Tribes": http://www.snapnet.dev/blog/netcode-architectures-part-4-tribes/
- Steve Streeting, "Problems With Unreal Network Saturation": https://www.stevestreeting.com/2025/05/12/problems-with-unreal-network-saturation/
- DeepWiki, GameNetworkingSockets connection management: https://deepwiki.com/ValveSoftware/GameNetworkingSockets/2.2-connection-management
- PCGamesN, Apex Legends measured rates: https://www.pcgamesn.com/apex-legends/apex-legends-netcode-problems

Community:

- CS2 cvars: https://steamcommunity.com/sharedfiles/filedetails/?id=3046217806, https://boostpirate.com/guides/cs2-subtick-explained, https://tradeit.gg/blog/cs2-interp-settings/, https://vredux.com/articles/packet-loss-cs2, https://csdb.gg/cs2-commands/
- Quake Live: https://www.esreality.com/index.php?a=post&id=1801334, https://steamcommunity.com/app/282440/discussions/0/1744479698803871347/
- Overwatch send-rate options: https://us.forums.blizzard.com/en/overwatch/t/guide-maximizing-system-performance-for-overwatch-pc/30
- Valorant network buffering: https://www.sportskeeda.com/valorant/network-buffering-valorant-how-affect-gameplay
- Unreal timeouts: https://forums.unrealengine.com/t/where-to-set-connectiontimeout-value/378351
