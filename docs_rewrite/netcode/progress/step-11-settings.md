# Step 11: network settings fixed or computed at run time

Status: **done** (11 November 2026; checked in game, locally and on the VPS with `net_dropsim`). Plan: [PLAN.md](../PLAN.md), step 11. The survey of what other games do, the options with their costs and the owner's choice for each point are in [10-network-settings-survey.md](../10-network-settings-survey.md); this page records what was built, what the checks found and how they were made.

## Scope (owner, 10 November 2026)

The plan asked for a bandwidth estimate per connection that would set the snapshot and packet rates. A survey of recent games (10, section 10.1) showed that competitive games run fixed rates and adapt only the interpolation delay and the input redundancy, and this branch's traffic (about 100 bytes per snapshot with headers, 2 KB per second at 20 Hz) does not need an estimate. The step was revised: fixed values where nothing needs measuring, and only the interpolation delay computed. Decisions by point (10, sections 10.3 to 10.11):

| point | decision |
|---|---|
| 1. bandwidth | fixed rate of 100,000 bytes per second, no cvar (1a) |
| 2. snapshot rate | a snapshot every server frame, `sv_fps` 20 kept in step 11 (2a); `sv_fps` 40 with the saber adapted in step 14 (2c) |
| 3. client packets | a packet every frame up to 125 per second, the unused interval carried over; no adaptation (3a) |
| 4. input redundancy | measured first, then fixed: each packet repeats the commands of the 2 previous packets (4a) |
| 5. interpolation delay | computed from the measured arrivals and loss (5c); `cl_timeNudge` removed; `cg_smoothClients` decided in step 14 |
| 6. time stamp | measured: not needed (6a) |
| 7. timeouts | 60 s while loading, 30 s in game (7a) |
| 8. debug | cheat-protected cvars for the computed value only, statistics in `svnetstats` and `netstats`; the server enforces the packet rate (8a) |
| 9. cleanup | the old cvars and the UI's `ui_setRate` removed |

## Part 1: measurements (commit `4a78a62dc`)

`svnetstats` (`codemp/server/sv_netcodec.cpp`, `server.h`) prints, per client: the user commands per packet, the client packets lost on the way (gaps in the netchan sequence, `netchan.dropped`) with the length of each run, and how much the time a snapshot leaves changes against the game clock it carries (mean and largest change between consecutive snapshots, measured with `std::chrono::steady_clock` in microseconds, since `Sys_Milliseconds` moves by whole milliseconds).

Measured on the VPS (GCC dedicated server, one player, `com_maxfps` 125, `cl_maxpackets` 100, about 4 to 6 minutes per run):

| `cl_packetdup` | user commands per packet | payload bytes per packet | with headers | upload | client packets lost |
|---|---|---|---|---|---|
| 0 | 1.99 | 7.7 | 41.7 | 2.6 KB/s | 0 of 21,215 |
| 1 | 4.00 | 10.7 | 44.7 | 2.8 KB/s | 0 of 14,357 |
| 2 | 6.00 | 14.4 | 48.4 | 3.0 KB/s | 0 of 15,969 |

- A repeated command costs 1.5 to 1.9 bytes; no client packet was lost on this link.
- The send time follows the game clock within 0.10 to 0.11 ms on average (largest 2.0 to 3.4 ms, 0.6 to 0.7 % of the changes of 1 ms or more): far below an Internet path's jitter and the client's arrival resolution, so no time stamp is sent (point 6).
- Found on the way: with `cl_maxpackets` 100 at 125 fps the client sent 62.7 packets per second, not 100: the cap was a minimum gap between two packets in whole milliseconds, checked once per frame, so 10 ms against frames of 8 ms let a packet leave every second frame only.

## Part 2: fixed settings (commit `e70684278`, fixes in `4aec7fa1d`)

- **Server** (`server.h`, `sv_snapshot.cpp`, `sv_main.cpp`, `sv_client.cpp`, `sv_init.cpp`, `sv_ccmds.cpp`, `sv_bot.cpp`): a snapshot every server frame (`1000 / sv_fps`); `SV_CLIENT_RATE` 100,000 bytes per second only delays the snapshot after a message larger than a frame allows (5,000 bytes at 20 Hz), such as a gamestate; LAN clients are no longer special (their rate was already 100,000). `status` loses its `rate` column. Removed: `sv_clientRate`, `sv_ratePolicy`, `sv_minRate`, `sv_maxRate`, `sv_lanForceRate`, `sv_snapsMin`, `sv_snapsMax`, `sv_snapsPolicy`, `sv_timeout`, `sv_zombietime`, and the client's `rate`, `snapshotMsec`, `wishSnaps`.
- **Client** (`client.h`, `cl_input.cpp`, `cl_main.cpp`, `cl_cgame.cpp`): `CL_ReadyToSendPacket` sends a packet every frame up to `MAX_PACKET_RATE` (125, `qcommon.h`) per second; the unused part of the interval carries over to the next frame (at most one interval), so the client sends min(frame rate, 125) packets per second (a minimum gap checked per frame would give 72 at 144 fps, 82 at 165). `CL_PACKET_DUP` 2 replaces `cl_packetdup`. A local client (loopback) still sends every frame. Removed: `rate`, `snaps`, `cl_maxpackets`, `cl_packetdup`, `cl_lanForcePackets`, `cl_timeout`, and the warning about `rate 4000`.
- **Server packet limit** (`sv_netcodec.cpp`, `SV_NetcodecPacketAllowed`): the server decodes at most `SV_PACKET_RATE` client packets per second on average, 137 (10 % above the client's 125, whose clock may run slightly faster than the server's: with no margin, a client clock 0.1 % fast would empty the burst in about 17 minutes without a map load and then lose a packet every 8 s), with a burst of `SV_PACKET_BURST` 125 packets; it drops the rest before decoding and counts them ("above the packet rate"). Loopback clients are not limited.
- **Timeouts** (`qcommon.h`): `TIMEOUT_CONNECTING` 60 s while the client is not in the game (map loading), `TIMEOUT_ACTIVE` 30 s in game, on both sides; `SV_ZOMBIE_TIME` 2 s.
- **Game module, cgame, UI** (`g_client.c`, `g_bot.c`, `g_xcvar.h`, `cg_servercmds.c`, `ui_main.c`): the advice to set `/snaps` removed; bots no longer send `rate` and `snaps`; cgame no longer sets `snaps`; `ui_setRate` removed. The userinfo validation required `rate` and `snaps`, so every client was refused at connection ("Failed userinfo validation", found in the first test): both fields are removed from `userinfoFields`. `g_userinfoValidate` is a bit mask indexed by the position of each field, so its default becomes 6291455 (the same checks as 25165823); a configuration that still stores the old value turns on the extended ASCII check until `reset g_userinfoValidate` (the cvar is archived even at its default value).
- **Fixes after a review of the code** (in `4aec7fa1d`): `1000 / sv_fps` in `SV_SendMessageToClient` could divide by zero, since a gamestate can be sent from a client packet before `SV_Frame` corrects `sv_fps 0`; the 10 % margin of the packet limit above; the send-time measure restarts at a gamestate and with a snapshot sent without a base asked (a `map_restart` showed a change of 360 ms).

Checks (owner, 11 November 2026, VPS and local, `svnetstats` with the packet rate derived from packets per snapshot at 20 per second):

| client | packets per second | user commands per packet | payload bytes per packet | dropped by the server |
|---|---|---|---|---|
| VPS, 125 fps | 124.6 | 3.00 | 10.9 | 0 |
| VPS, 60 fps (62.5 real) | 62.4 | 3.00 | 12.4 | 0 |
| VPS, 165 fps (166.7 real) | 125.0 | 4.00 | 13.9 | 0 |
| local over the LAN address, 125 fps | 125.0 | 3.00 | 10.5 | 0 |
| local over the LAN address, 60 fps | 62.5 | 3.00 | 12.8 | 0 |

The connection is accepted, no "/snaps" advice is printed, `status` reads well, `cg_showmiss` shows no new misses. A few snapshots coded against bases 2 to 8 messages back, and a few server commands resent, came from the freeze when the scoreboard (Tab) first loads player models: the client sends nothing during it.

## Part 3: interpolation delay (commit `4aec7fa1d`)

`CL_AdjustTimeDelta` (`codemp/client/cl_cgame.cpp`) computed nothing before: it moved the client's time 1 ms forward at each snapshot and 2 ms back after any frame within 5 ms of the newest snapshot, so it settled where that happened in one interval out of three (5 ms more delay than needed on a stable link, frequent extrapolation with jitter). Now:

- `CL_InterpRecord` (called by the snapshot parser, `cl_parse.cpp`) counts the snapshots read and the server messages missing before them (gaps in their numbers), for every snapshot.
- At each frame that brings snapshots, the offset between the newest one's `serverTime` and its arrival (`cls.realtime`) enters a window of the last 64 such frames (`INTERP_WINDOW`, 3.2 s at 20 Hz), with the snapshots read and lost since the previous entry. Only the newest snapshot of a frame gives an offset: the older ones read in the same frame waited for the client, not for the network.
- `CL_InterpTarget`: the target `serverTimeDelta` is the second smallest offset of the window minus one snapshot interval (`1000 / sv_fps` from the server info), so that the next snapshot has already arrived in 63 cases out of 64 and one isolated spike does not raise the delay; one more interval while more than 5 % of the messages of the window are lost (removed below 2 %).
- The client's time moves towards the target by +1 or -2 ms per snapshot as before, or by half the difference when it exceeds 100 ms (measured against the target: measured against the newest snapshot, as before, it cut every delay above 100 ms in half).
- When a snapshot arrives more than 100 ms earlier against its `serverTime` than any snapshot of the window, the server's clock jumped forward (jitter only makes arrivals later): measured on the next such snapshot, the client's time and the whole window move forward by the jump at once. `map_restart` runs three frames of 100 ms at once (`server/sv_ccmds.cpp:318-323`).
- `cl_timeNudge` removed; `cl_debugInterpForce` and `cl_debugInterpDelay` (`CVAR_CHEAT`) force a delay behind the newest snapshot when it arrives; `netstats` prints the delay, the jitter (median minus second latest arrival), the extrapolated intervals (a frame passed the newest snapshot before the next arrived) and the loss margin.

Defects found by the checks in game and fixed before the commit:

1. With `net_dropsim 10` the delay averaged 74.7 ms instead of about 100, 55 of 688 intervals extrapolated: the fast adjustment, still measured against the newest snapshot, halved every delay above 100 ms, so the delay swung between about 50 and 100 ms.
2. Found by reading the code: snapshots read within one frame counted as lost (a client at a low frame rate, or `sv_fps` 40, would have turned the loss margin on), then, recorded one by one, the snapshots that waited during a client freeze would have raised the delay by the freeze for 3 s; the split between offsets (newest of a frame) and losses (every snapshot) fixes both.
3. After a `map_restart`, the lagometer's blue bar grew large and cgame showed "Connection Interrupted": the older offsets held the client about 350 ms behind the newest snapshot for 3 s, past the 64 user commands it keeps (`CG_DrawDisconnect`). A first fix restarted the window (back to normal in 2 to 3 s); the owner asked for the clock to follow the jump at once; measured on the first snapshot after the restart, the jump was about 40 ms short (that snapshot leaves late, by the time the server takes to restart), so it is measured on the second (about 400 ms; the delay moves by 2 ms afterwards).

Checks (owner, 11 November 2026, VPS, `netstats`):

| condition | delay behind the newest snapshot when it arrives | jitter | extrapolated intervals | loss margin |
|---|---|---|---|---|
| no loss | 53.0 ms | 2 to 4 ms | 0 of 2,734 | off |
| `net_dropsim 10` (9.1 % lost) | 102.8 ms | 4 ms | 7 of 1,153 (0.6 %) | on |
| back to `net_dropsim 0` | 53.0 ms | 2 ms | 0 of 2,211 | off |
| scoreboard freezes (Tab) | 52.7 ms | 4 ms | 0 of 104 (114 snapshots: those read together not counted lost) | off |
| `map_restart` | `<JUMP 398>` to `<JUMP 400>` with `cl_showTimeDelta 1`, then the delay moves by 2 ms; no "Connection Interrupted" | | | |

The old algorithm with `cl_timeNudge 0` gave about 55 to 56 ms on a stable link (estimated from its code, not measured): the same within 2 to 3 ms. Less delay (the world closer to the newest snapshot for everyone, as the owner's former `cl_timeNudge -14`) was considered and not kept: it means extrapolated positions part of each interval; the delay will drop with `sv_fps` 40 and stop mattering for hits with lag compensation (step 14).

## Left to step 14

- `cg_smoothClients`: the other players extrapolated along the trajectory the server sends (1) or interpolated (0).
- `sv_fps` 40 with the saber made to work at that rate (the saber runs once per server frame, `game/g_main.c:3317`); the interpolation delay then halves.
- Lag compensation (the base game has none).
- The freeze when the scoreboard first shows players whose models are not loaded.
- The prediction misses when crouching or jumping while attacking (seen in step 8, the base game's too).

## How to repeat the checks

1. `svnetstats` on the server after a minute of play: packets per second (packets divided by snapshots, times 20), 3 user commands per packet at 125 fps, 0 "above the packet rate", 0 "not decoded".
2. `netstats` on the client right after connecting (it resets), then after a minute: about 53 ms behind the newest snapshot at 20 Hz, 0 extrapolated intervals, loss margin off; with `net_dropsim 10` on the client, about 100 ms and the margin on.
3. `map_restart` with `cl_showTimeDelta 1`: a `<JUMP ...>` of about 400 ms, no large blue bar, no "Connection Interrupted".
