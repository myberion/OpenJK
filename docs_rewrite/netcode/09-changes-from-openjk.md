# What this branch changes in OpenJK, why, and what it implies

Last update: 12 November 2026. Branch `feature/poc-netcode`, compared with OpenJK `master` at `260c59c29`. Multiplayer only (`codemp/` and the shared code it uses); single player is not built.

This page lists every change of the branch outside the study documents, grouped by subject, with the reason and the consequences. It avoids code: file names are given so that each change can be found, and the details are in the step documents of [progress/](progress/README.md). The work follows the owner's goals stated in the [README](README.md): no loss of precision, no compatibility constraint, as few network settings as possible, a netcode other JKA forks can reuse, Windows and Linux.

## At a glance

| area | OpenJK | this branch | main consequence |
|---|---|---|---|
| Server-to-client messages | Huffman bitstream (`msg.cpp`, `huffman.cpp`) | new message codec, `netcodec` (rANS with per-field statistics and prediction) | about 0.38 to 0.45 of the bytes, values exact |
| Client-to-server messages | Huffman bitstream, user commands XORed with a key | netcodec, the user commands coded against the snapshot the client acknowledges | about 0.25 of the bytes (40.7 to 10.1 per packet on recorded games), values exact |
| Netchan obfuscation (XOR) | yes, both directions | removed in both directions | to revisit with real encryption (plan step 12) |
| Ping | server frame clock (50 ms steps at `sv_fps` 20), time of the last packet acknowledging a message | real time, first packet acknowledging the message | a more precise ping |
| Protocol number | 26 | **26, unchanged for now** | an OpenJK client and a branch server do not understand each other, and nothing refuses the connection |
| Reliable server commands kept | 128 | 256 | needed by the codec; a little more memory |
| Server commands sent | repeated in every message until acknowledged (2.6 times each on real games) | sent once; resent only when their message is lost, or with a newer command so that they stay in order | up to 4 % fewer bytes on real games; a command in a lost message arrives about one ping later |
| While awaiting the gamestate | the client sends one packet a second | the client sends at its normal rate | a lost gamestate fragment costs about a round trip instead of a second or more |
| Demos | copy of the network messages | server: copy; client: coded again for the demo | old demos cannot be played back |
| UDP downloads | up to 8 blocks per message, as the rate allows | 1 block per message | same speed on the internet at default rates, about 3 times slower on a LAN |
| Player velocity | rounded to integers every frame (unless `pmove_float 1`) | never rounded | exact velocity, jumps no longer depend on the frame rate |
| Missiles | positions and velocities rounded | exact | trajectories exactly as the server computes them |
| Rounding rule of `SnapVector` | differs between compilers | to nearest, ties to even, everywhere | a Windows client and a Linux server agree |
| Server frame rate | `sv_fps` 40 | a fixed tick of 10 ms (`TICK_MSEC`), the game's logic on its 20 Hz instants | a snapshot every 10 ms; the game plays as at `sv_fps` 20 (section 7) |
| Entity states the server keeps for its snapshots | a copy of every entity of every snapshot of every client (139.5 MB at 32 players), counted on 32 bits: the server restarted the map before the counter wrapped, after about 2.3 h on one map with many players | one copy of an entity's state while it does not change, shared by every snapshot that holds it (17.4 MB), counted on 64 bits (plan step 14, part 5) | less memory, no forced restart; the encoder skips an entity that did not change since the base without comparing the two states (warm encoding 8 to 14 % faster on the three compilers), the bytes sent are the same |
| Snapshots both sides keep to code the next ones against (`PACKET_BACKUP`) | 32: a base at most 29 messages back, 290 ms at a message every 10 ms | 128: a base up to 125 messages back, 1.25 s (plan step 14, part 5) | a player with a long round trip, or in a long burst of loss, keeps small snapshots instead of whole ones in fragments; the client keeps 17.4 MB of entity states instead of 4.4; the server's entity states are zeroed by the system instead of cleared by hand and the versions of an entity are neighbours, so the memory in use is that of the entity numbers in use (a local Windows server with 16 bots: 42 to 48 MB against 57 at the window of 32) |
| Other players on the screen | with the defaults (`cg_smoothClients` 1, `g_smoothClients` 1): between two snapshots when the next one is there, carried forward by their velocity for up to 50 ms when it is not, and their drawn origin filtered (half of the remaining distance per frame: behind by a frame of their movement) | always between two snapshots, not filtered; both cvars removed, the messages unchanged (plan step 14, part 6) | the other players are drawn where the server had them, a frame sooner (8 ms at 125 frames per second); a player stops while a snapshot is missing instead of being guessed |
| Network settings | `rate`, `snaps`, `cl_maxpackets`, `cl_packetdup`, `cl_timeNudge`, the server's rate and snapshot policies, timeouts | fixed: a snapshot every server frame, a client packet per tick (100 per second; 125 at most from step 11 to step 14 part 4a), the commands of 2 packets repeated, 30 s timeout in game; the interpolation delay computed from the measured jitter and loss (plan step 11) | nothing to set; section 8 |
| Math functions | the C library's, different on every system | one entry point (`q_libm.h`) on LLVM libc's correctly rounded functions | the same bits on Windows and Linux, every compiler (section 7) |
| Random numbers | Raven's 15-bit LCG and the C library's `rand()`, another sequence on each system | one entry point (`q_rand.h`), xoshiro128++ | the same numbers on every platform (section 7) |
| Removed cvars | | `sv_fps`, `bot_debugClientRate`, `pmove_float`, `pmove_fixed`, `pmove_msec`, `sv_padPackets`, `cg_smoothCamera`, `g_randFix`, `cg_smoothClients`, `g_smoothClients` | movement now always behaves as OpenJK with `pmove_float 1` and `pmove_fixed 0` (section 7); the camera always damps as with `cg_smoothCamera 0`, the default (section 8) |
| Added cvars | | `sv_netcodecCompare`, `sv_netcodecMaxBytes` (debug) | byte comparison with the legacy format, both directions; a lower message size limit to test oversized messages |
| Client fixes outside the netcode | | menu cursor speed, font aspect, keypad and right modifier keys, server browser state (servers answering in under 1 ms were hidden) | |

## 1. The server-to-client format

### What changes

Every message the server sends to a client (snapshot, server commands, gamestate, map change, download block) is now built by the `netcodec` library (`codemp/netcodec/`) instead of being written field by field into a Huffman bitstream. The engine hands its own data to the codec in place: the client's snapshots, the shared entity ring, the command buffer, the baselines. Server side: `codemp/server/sv_netcodec.cpp`; client side: `codemp/client/cl_netcodec.cpp`.

| | OpenJK (legacy format) | this branch (netcodec) |
|---|---|---|
| Entropy coder | one static Huffman table for every byte | rANS with a separate probability table for each kind of field (trained on real games) |
| Unchanged fields | one bit per field up to the last changed one, plus a count byte | only the changed fields are listed |
| Changed values | written in full, or as a 13-bit integer when integral | coded against the previous value or a prediction (position from the velocity, time from the snapshot time, angle from its last two values) |
| Floats | full 32 bits or 13-bit integers | by kind: zero, integral, exact angle step, or the exact 32 bits; `-0.0` kept |
| Booleans | a "changed" bit, then the value | the "changed" flag only (the new value is the opposite) |
| `user*` fields of mods | 1 bit wide unless a mod widens them with `netf_overrides.txt` / `psf_overrides.txt` | coded by their C type, so the full value arrives; the overrides files are no longer read |
| Server commands | text through Huffman, repeated in every message until acknowledged | still repeated until acknowledged, but each one is coded against an earlier command of the same kind (token by token) |
| Game directory (`svc_setgame`) | a separate message | carried by the next message |
| Map instance (`serverId`) | only in the system-info config string | also in every message, coded as "unchanged" (a fraction of a bit) except after a map change or a `map_restart`, so that the client's packets can code theirs as "the same as in the message I acknowledge" (plan step 8) |
| End of message | an `svc_EOF` byte | none (the codec knows the length) |
| Oversized message | the server clears it and sends it empty | if a message codes larger than 1 KB, it is also coded raw and the smaller is sent; a worst case stays close to the raw size. A snapshot still larger than the 49,152-byte limit is sent with the entities nearest to the player (the others come back in the next message); a gamestate that large drops the client, which returns to the menu with the reason |
| Precision | what the field widths allow | exactly what the server holds |

### Results

| measurement | legacy | netcodec | ratio |
|---|---|---|---|
| 20 held-out demos of real games (maps not used for training), all messages | about 24 MB | 9.13 MB | 0.381 |
| the same with the snapshot's input margin (plan step 14, part 4c: one byte per snapshot until the tables are retrained on recordings that hold real margins) | about 24 MB | 9.24 MB | 0.385 |
| the same, snapshots only | | 66.9 bytes per snapshot | 0.432 |
| clang server, 4 bots, 150 s, velocity rounded (before 9 November) | 82.5 bytes per message | 32.1 | 0.389 |
| the same, velocity exact (current build) | 89.1 | 38.6 | 0.433 |
| MSVC client and server, in combat (8 November, velocity rounded) | about 330 bytes per packet | about 150 | about 0.45 |

Coding and decoding are also faster than OpenJK's own code on the same messages: encoding takes about 0.38 and decoding about 0.56 of the legacy time (clang, warm measurement; step 6, part C). Every compiler (MSVC, GCC, Clang, x86 and x64) produces the same bytes, which tests check on each build.

Tested in game over UDP with simulated loss (5 % and 20 % of the server's messages lost: no message the client could not decode) and with a GCC dedicated server on a Linux VPS played from a Windows client over the Internet (3,019 messages in about 2.5 minutes, none lost or undecodable, prediction misses of the same order as with a Windows server).

### What does not change

- Connectionless messages (`getinfo`, `getstatus`, `connect`, the master server): the server browser works as before (one display defect fixed, section 9).
- The choice of the base snapshot (the newest one the client acknowledged), the rate and snapshot-rate rules, the netchan fragmentation; `PACKET_BACKUP` was 32 until plan step 14 part 5 (128 since, "At a glance").
- The legacy writer is kept in the server, only to compare sizes (`sv_netcodecCompare`).

### What it implies

- Branch builds talk only to branch builds. The protocol number is still 26, so an OpenJK or 1.01 client can connect to a branch server (and the reverse) but nothing it receives makes sense. The protocol number, the handshake and a possible support of both protocols are decided at the end of the plan.
- Mods that relied on the overrides files to widen `user*` fields get the full values without them.
- `cl_shownet 1` still prints the packet sizes and `cl_shownet 3` the snapshot line; `cl_shownet 2` prints one line per decoded operation (commands, gamestate, game directory, snapshot with its base and entity count, download block, map change). The per-entity and per-field output (levels 2 and 4, -1, -2) is gone, since the codec has no per-field trace.
- A message the client cannot decode (only possible if it no longer holds the base the server used) is dropped whole; the client then asks for a full snapshot, as before, and the commands are sent again until acknowledged.

## 2. The client-to-server format and the ping

### What changes

Every packet the client sends (about 60 per second) is coded by `netcodec` too: on the client, `CL_WritePacket` hands the values to `CL_NetcodecWritePacket` (`cl_netcodec.cpp`); on the server, `SV_ExecuteClientMessage` takes them from `SV_NetcodecDecodeClientPacket` (`sv_netcodec.cpp`). What the server does with the decoded values is unchanged: the acknowledge checks, the `serverId` checks (`map_restart`, gamestate resent), the client commands with the flood protection, then the user commands with the `sv_legacyFixes` clamps, except when the user commands run (plan step 14, part 4): OpenJK runs each one when its packet arrives; this branch keeps them in a buffer per client (256 commands) and runs, at each tick, those whose time is at or below the tick's, after the bots' commands and before the game frame, so every player moves at the ticks. Within a tick the players run in rising order of their client numbers at one tick and in falling order at the next, so of any two players each runs first every other tick (the first to move takes a contested place, the first to shoot kills first); OpenJK's order was the packets' order of arrival. The bots still run before the players, in the order of their numbers. Since part 4c every player runs exactly one command per tick, the one dated with that tick: the client makes its commands on a second clock, the command clock, ahead of the server's by the trip of a packet plus a margin, so that the command of a tick arrives just before the tick runs. The server measures that margin (how early the command arrived, in milliseconds) and sends it in every snapshot (one byte until the tables are retrained); the client keeps the margins of the last 2 s and moves its clock, by at most about 6 % faster or 7 % slower, to where the second smallest is 3 ms, and sets it at once at a connection or after a hitch. So every player's state at a tick is the result of its command of that tick, whatever its ping: in OpenJK each player lived in its own past on the server, behind by its round trip and its interpolation delay, and a player could hold its commands back and send up to a second of them at once. A command that comes after its tick gets no movement time, and what it newly pressed (buttons, a jump, a force power or a style change) goes into the next command run, as Counter-Strike 2 does: a short press acts a tick late instead of being lost. A tick without a command runs the last one again for 250 ms, then an empty one (no movement, no button): a lost packet no longer stops the player on the server, a lost connection does. Times the movement code writes into the player state (the weapon charge, the rocket lock) are now on the same clock as the game's: in OpenJK they were on the client's past clock, so a charged shot of a player with a high ping was computed with a charge longer by its latency. In cgame the local player's state is ahead of the drawn world by the round trip and the interpolation delay; what compares its times (charge bar, zoom, rocket lock, saber lock) uses the command clock (`cg.cmdTime`). The client makes one command per tick (part 4b): every command moves 10 ms whatever the frame rate, and the view still turns every frame. A command is made as the client's clock enters a tick, with the input gathered over the tick before, and carries the time that tick ends, so cgame already predicts the tick in progress and draws the local player between the last two ticks at the frame's time (position interpolated, the rest the newest tick's, the view angles of the frame, except on an axis the movement holds in the newest tick, as during a wall run, a lunge or a jump attack): smooth at any frame rate, nothing guessed. A key acts at the next tick, 0 to 10 ms later (about 5 ms on average, against about 1.5 ms at 333 frames per second and 4 ms at 125 with a command per frame). `cg_showmiss` also prints "shown flicker" when an animation or the crouch shown goes back and forth over three frames, and "anim miss" when a snapshot's animations differ from those the client predicted for the same command time. With these commands, which carry the time the client draws the world at, behind the server's clock, a command runs at the next tick, up to 10 ms after its arrival, but just before the game frame and the snapshot that took it into account before as well: nothing is later for the other players or for the game's decisions; only the order of the commands within a tick changes (the bots', then the players' by client number, instead of the order of arrival).

| | OpenJK (legacy format) | this branch (netcodec) |
|---|---|---|
| `serverId`, last server message received, last server command received | three 32-bit values through Huffman | the message as its low 8 bits (the server takes the nearest number at or below its last message sent); `serverId` as "the same as in that message"; the command acknowledge against the commands that message carried |
| Client commands | text through Huffman, each with its sequence number | their count, the first sequence number against that message, the texts coded as the server's command texts |
| User commands | the first against an empty command, the others against the previous one, XORed with a key, through Huffman | the first against the snapshot of the acknowledged message (time against its `commandTime`, view angles against its angles less `delta_angles`, weapon, force power and item against its own), the others against the previous one, each field with its own statistics |
| User-command key (XOR with the checksum feed, the acknowledge and a hash of the last command) | yes | removed |
| End of packet | a `clc_EOF` byte | none |
| A move of -128 (the keys give -127 to 127; the mouse or joystick sums, or a modified client, can give -128) | read back as -127 by the server | never made: the client's bound of a move (`ClampChar`) is -127 to 127 instead of -128 to 127, so its prediction and the server use the same value, and the server turns a -128 it receives into -127 (plan step 14). From step 8 until then it passed through exactly: 0.8 % faster backward, left or down |
| A client command longer than 1,023 characters (no client can send one: it holds them in buffers of 1,024) | the first 1,024 characters read, the rest read as the next fields of the packet | the packet is refused, as malformed |

Coding the first user command against the acknowledged snapshot needs both sides to hold that snapshot. The server keeps, for each client, the few values a packet is coded against for each of its last 256 messages (32 bytes each: 8 KB per client, 256 KB for 32 clients), so it can decode any packet whose acknowledge it reads without ambiguity. The client codes against the acknowledged message only while it is sure the server has sent fewer than 256 messages since: the time since that message arrived plus the round-trip time it measures, over the time between the server's messages, must stay under 128 messages (6.4 s at `sv_fps` 20). Otherwise, or when one of these is unknown (right after a gamestate), the packet is coded without it: about twice larger (estimate), never wrong. A packet the server cannot decode (malformed, or coded against a message it no longer holds) is dropped and counted by `svnetstats`. Since the server decodes packets from anyone, the decoder bounds everything it builds (counts, text lengths) and is tested with 400,000 malformed packets on every build and with a fuzzer (plan step 8, part 4).

### The ping

| | OpenJK | this branch |
|---|---|---|
| When a message was sent | `svs.time`, the server's frame clock (it moves once per frame: 50 ms steps at `sv_fps` 20) | the real time (`Sys_Milliseconds`) |
| When it was acknowledged | `svs.time` again, written by every packet acknowledging it (a client sends about three packets per message, so the last one counted) | the real time of the first packet acknowledging it |
| Messages measured | the last 32 (`PACKET_BACKUP`), in the frame history | the last 256 (`SV_PING_WINDOW`, the window of the client's 8-bit acknowledge), in a history of their own (plan step 14) |
| An acknowledge older than the history | could write into the frame of a newer message | ignored |
| "Not acknowledged yet" | a time of -1 (any time at or below 0 counted as none) | an explicit flag |

The ping is the average over the acknowledged messages among the last 256 (`SV_CalcPings`): 2.56 s at the 100 Hz tick, so it is measured up to a round trip of 2.56 s, where the last 32 messages held 320 ms at 100 Hz (1.6 s at OpenJK's 20 Hz; above that round trip no message was acknowledged within the history and the ping showed 999). It is now the real round trip of a message, without the frame-clock steps and without the extra packet intervals. Measured on a server 15 ms away (network round trip): 24 ms, that is the round trip plus the wait for the client's next packet (8 ms on average at `cl_maxpackets` 63) plus processing.

### Results

On the six games recorded in plan step 8 (81,789 packets): 40.68 bytes per packet in the legacy format, 10.07 with netcodec (the game kept out of training: 38.58 to 9.89). Coding takes 0.34 to 0.40 and decoding 0.74 to 0.86 of the legacy time (warm measurement, clang, MSVC, GCC 12, Clang 14). Every compiler produces the same bytes.

In game (10 November 2026, a GCC server on a Linux VPS played from a Windows client over the Internet, and a local game): 6.0 to 6.1 bytes per packet against 27.4 to 28.0 for the same packets in the legacy format (ratio about 0.22), 6.7 to 10.7 bytes per packet depending on the activity; no packet the server could not decode, also with 10 % of the server's messages or 20 % of the client's packets lost on purpose, after `map_restart` and after a server demo started in the middle of a game.

### What does not change

- Connectionless messages, including `connect`, whose user info is still compressed with Huffman (decided with the protocol number at the end of the plan).
- `cl_nodelta`: the client still asks for a full snapshot (`clc_moveNoDelta`) when it misses the last one. (`cl_packetdup` and `cl_maxpackets` are replaced by fixed values in plan step 11, section 8.)

## 3. Netchan obfuscation removed

OpenJK XORs each message, in both directions, with a key made of the challenge, the sequence number or the `serverId` and acknowledge, and the last command texts. The receiver found the key from the numbers read in clear at the start of the message; the codec now codes those numbers, so the scheme could not stay as it was. It was never security: anyone who sees the connection can undo it. It is removed in both directions (`sv_net_chan.cpp`, `cl_net_chan.cpp`), with the user-command key of the client packets. Whether to restore an equivalent or replace it with real encryption is decided in plan step 12.

## 4. Reliable command buffer: 128 to 256 commands

| | OpenJK | this branch |
|---|---|---|
| `MAX_RELIABLE_COMMANDS` (`qcommon.h`) | 128 | 256 |
| Server memory per client | 128 KB | 256 KB (+4 MB for 32 clients) |
| Client memory (two buffers) | 256 KB | 512 KB |
| Client dropped when this many server commands are unacknowledged | 129 | 257 |

Why: a server command is coded against an earlier command up to 127 commands back, so both sides must keep at least 256 to always hold the same reference. The client-to-server buffer uses the same constant and grows too.

## 5. Demos

A demo is played back by the normal decoder starting from an empty state, so every message in it may only refer to what the demo itself contains: its base snapshot, that base's own base, and the commands it is coded against.

| | OpenJK | this branch |
|---|---|---|
| Server demo (`svrecord`, `sv_autoDemo`) content | copy of the messages sent | copy of the messages sent (unchanged) |
| Server demo start | waits for a full snapshot (`demowaiting`), then forbids older bases (`minDeltaFrame`) | resets the codec's view of the client: the next message has no base and the later ones refer only to messages of the demo |
| Client demo (`record`) content | copy of the messages received | each decoded message is coded again for the demo, against the demo's previous snapshot, with the commands it brought and without its download block |
| Client demo start | asks the server for a full snapshot and waits for it | nothing asked of the server |
| Gamestate at the start | legacy format | coded, with the last 127 server commands |
| Cost | none | server: none with `sv_autoDemo`; about one round trip of full snapshots after a `svrecord` in the middle of a game. Client: one more coding per message, about 1.5 µs |

Why the two sides differ: the server records every message it sends and receives the client's packets in order, so a copy is complete once its start is right. The client cannot know which earlier messages the server's next ones will refer to, and losses or messages it could not decode would leave holes; coding the demo again makes it complete by construction.

What it implies: demos recorded with OpenJK or 1.01 cannot be played back by this build, and the reverse (a reader or converter for the old format is decided with the protocol number). The files keep the `.dm_26` extension for now. Client demos no longer contain download blocks (useless at playback). As in OpenJK, server demos still open when the server quits have no end marker; the player stops cleanly at the end of such a file.

## 6. Downloads over UDP

| | OpenJK | this branch |
|---|---|---|
| Blocks per message | as many as the rate allows in one snapshot interval, up to the 8-block window | one 2,048-byte block |
| Internet, branch defaults (rate 25000, 20 snapshots per second) | 1 block per message | 1 block per message: same speed |
| LAN (`sv_lanForceRate`, rate 100000) | 3 blocks per message | 1 block: about 3 times slower |

Why: the codec carries one download operation per message. A better download path (several blocks, or a channel of its own next to the reliable data) is part of plan step 10. HTTP downloads are not affected.

## 7. Game code: precision and physics

These changes are in the game module and cgame (`codemp/game/`, `codemp/cgame/`) and in the shared math code. The interface between the engine and the modules is unchanged, but the server's game module and the client's cgame must come from the same source, since cgame predicts the movement with the same code.

How the movement code works, in two words: the server runs the player movement once per user command the client sends, over the time elapsed since the previous command (this is the "step"); cgame runs the same code to predict the player's own movement before the server's answer arrives. The three removed cvars chose how that step behaves:

| cvar | what it did | OpenJK default | this branch (no cvar any more) |
|---|---|---|---|
| `pmove_float` | 0: at the end of every step, the velocity is rounded to whole units per second. 1: it is kept exact | 0 (rounded) | **always exact**, as `pmove_float 1` |
| `pmove_fixed` | 0: one step per command, as long as the time since the previous command (cut into pieces of at most 66 ms); the step therefore depends on the client's frame rate. 1: steps of exactly `pmove_msec` milliseconds for everyone, command times rounded up to a multiple of it | 0 (variable step) | **always variable**, as `pmove_fixed 0` |
| `pmove_msec` | the length of the fixed step of `pmove_fixed 1`, 8 to 33 ms | 8 | not used (there is no fixed step) |

In one sentence: the movement behaves exactly as an OpenJK server set to `pmove_float 1` and `pmove_fixed 0`, and nobody can change it. Compared with OpenJK's defaults, only the velocity rounding changes.

| what | OpenJK | this branch | why |
|---|---|---|---|
| Player velocity after each movement step | rounded to whole units per second (default `pmove_float 0`) | never rounded | the client gets the exact velocity; see below |
| Length of a movement step | the time since the previous command (default `pmove_fixed 0`) | the same | a fixed step is a gameplay question (plan step 14) |
| Missile launch point, velocities, impact and explosion points | rounded to integers (`SnapVector`, `SnapVectorTowards`) | exact | the client sees the trajectory the server computes; +0.5 byte per message on the demos |
| Rounding rule of `SnapVector` (events, dropped items, spawn point) | truncation, except MSVC 32-bit: nearest | to nearest, ties to even, on every compiler, the rounding mode forced as in `Sys_SnapVector` | a Linux server and a Windows server place them the same way (1.01's Linux build truncated: its values move by at most 1 unit) |
| Rounding rule of `Sys_SnapVector` (the engine's) | ties away from zero with MSVC, to even elsewhere | to even everywhere | no pmove call uses it now; kept consistent for any module that does |

The velocity in more detail:

| | velocity rounded (OpenJK default) | velocity exact (this branch) |
|---|---|---|
| Jump height | depends on the frame time (the 125 and 333 fps advantages) | the same at any frame rate (small step effects of acceleration and friction remain) |
| Windows client (MSVC) against Linux server (GCC) | before the tie fix: up to 2.7 units of prediction error; after it, 0.0001 to 0.0002 unit in the test, but a rounding can still flip by 1 unit/s | at most 0.0006 to 0.0009 unit, no flip possible (100 times under the prediction miss threshold) |
| Bytes | reference | +6.5 bytes per message with bots, +6.1 estimated on the demos (the tables were trained on rounded velocities; retraining on this build's games, plan step 13, should lower it) |
| Legs of a player pushing against a wall or another player | the velocity the push leaves (under 0.5 unit per second) rounds to 0: the legs stay straight | the legs turn to the velocity's direction only from 1 unit per second on the ground (`BG_G2PlayerAngles`; the speed under which the movement code takes a player as stopped). Without that (9 to 13 November 2026) the few hundredths of a unit per second left sideways by the push turned the legs up to 60 degrees left or right, on the client and in the server's model for saber hits |

What it implies: players used to the 125 or 333 fps jumps lose them, and a jump designed for them on a map may no longer pass. A physics fully independent of the frame rate needs a fixed step, a gameplay question left to plan step 14. A mod that takes this game code loses the three `pmove_*` cvars; a mod with its own game code keeps its own physics, as long as its game and cgame match each other.

### The game's timing at a higher server frame rate (plan step 14)

The game module was tuned on the 50 ms frames of a 20 Hz server: the saber is traced once per frame and its reactions (bounces, deflections, broken parries) need a frame's worth of blade movement, the force powers, thinks, pain and the bots' AI count in frames. Run unchanged at the 100 Hz tick of plan step 14, the saber's move per frame is 5 times shorter, its damage per contact is nearly always 1, attacks are no longer bounced, periodic timers come up to 44 % more often. Tuning that code per tick failed (no coefficient restores the reactions), so the game logic keeps its 20 Hz pace on a faster tick (owner, 11 November 2026): every 20 Hz instant is a tick at 100 Hz, and the logic runs on those instants with OpenJK's code.

- Two clocks (`game/g_local.h`, `g_main.c`): `level.time`, read by all game code, is the logic clock, the last multiple of 50 ms; `level.tickTime` is the tick's time, used only for the bots' command times and the sanity clamp of a command's time.
- On the 20 Hz instants: thinks, the saber, the force powers and missile blocks, space, hacking, jetpack and cloak fuel, the expiry of powerups, world damage (lava, drowning) and the pain feedback, the level's rules (votes, exit rules, tournament, siege waves and timers), the bots' AI (every 50 ms), all missiles, items and movers (doors, lifts, trains: movement, pushes, crush damage).
- Every tick: the players' commands (movement with each command's own time, as before), the snapshots.
- The server's ghoul2 clock (`server/sv_main.cpp`) is the logic instant of the frame just run: the game starts animations at `level.time`, and bolts and model traces read this clock, so a 20 Hz instant evaluates the models 50 ms after the previous one, as every frame did at `sv_fps` 20. With `sv.time`, at 100 Hz the blades ran 40 ms ahead of their 20 Hz path (5 times less saber damage).
- Missiles move only on the 20 Hz instants: the engine's model trace depends on the step (a step that starts inside a player's box hits the box without a model test; one that leaves it reports no model surface), so per-tick steps changed which shots hit, where, and the damage (the bryar 38 % less, the homing rocket lethal every time).

At `sv_fps` 20 every instant is a 20 Hz instant and the game runs OpenJK's code paths (the ghoul2 clock is unchanged there).

| what | OpenJK at 100 Hz (unchanged code) | this branch at 100 Hz |
|---|---|---|
| Saber traces, damage, bounces | every frame; contacts 5 times shorter, damage nearly always 1 point per contact, attacks rarely bounced | on the 20 Hz instants: the 20 Hz damage, bounces and blocks; a contact is found at the instant after it, up to 40 ms later than a per-tick trace would |
| Force power timers (grip, rage, protect, absorb drains, grip damage, force jump charge, seeker drone) | re-armed with "now + n", so every n + 10 ms instead of n + 50 | the 20 Hz lengths |
| Mind trick broken by an attack in view | within 4 frames (40 ms) | within 200 ms (written as such: the frame-length variable is removed) |
| Pain sound and view kick | 10 points within one frame, which a blow spread over 5 frames rarely reaches | 10 points within a 50 ms window |
| Missiles | traced every 10 ms | traced on the 20 Hz instants, as at 20 Hz |
| Movers (doors, lifts) | move and crush every frame | on the 20 Hz instants |
| Bots | think and act every frame | think every 50 ms on the 20 Hz instants |

Checked with scripted bot scenarios (`scenario`, below), the same module at `sv_fps` 20 and 100: the same wounds, damage, health taken by cause, deaths, blocks, bounces, attacks, shots, hits and force power events for the saber (5 styles, the defender idle or attacking, 96 swings each), the force powers (6 powers, 48 uses each) and the weapons (12 weapons, both fire modes, 48 bursts each); the same counts on Linux (GCC); after the bench was made deterministic (every reset clears what the bots' free play before the scenario left, `g_debugSeed` below), no difference at all, neither between the rates nor between two runs ([progress/step-14-audit-100hz.md](progress/step-14-audit-100hz.md), section 11). A free-for-all of 16 bots shows no difference beyond the noise (damage -2.8 % ± 1.8 %). Details: [progress/step-14-audit-100hz.md](progress/step-14-audit-100hz.md), section 9.

Defects fixed on the way (plan step 14):

- The saber's random choices in the movement code (`PM_irand_timesync`, `game/bg_saber.c`: chain ends, lock breaks, flip and jump attacks) drew their numbers by overwriting the command's time, so the rest of that movement step read a random time (footstep and slope timers, weapon charge, saber lock and hand extension times, zoom). They now draw from a seed of their own, set to the command's time at each step: the same numbers, the command's time intact. Client and server agreed before and still do.
- A dedicated server spent the last millisecond before each frame in a busy loop (`Com_Frame`, `qcommon/common.cpp`), up to a tenth of a core at 100 Hz. Outside Windows it now sleeps that millisecond in steps of 100 us (`NET_Sleep` takes microseconds, `qcommon/net_ip.cpp`); a packet that arrives still wakes it at once. Windows keeps the busy loop (`select` waits whole timer periods there); `com_busyWait 1` keeps it everywhere. Measured on the VPS (GCC, an empty server at `sv_fps` 100 for 120 s): 11.6 s of processor time before (9.7 % of a core), 1.3 s after (1.1 %).
- Outside Windows the engine's clock (`Sys_Milliseconds`, `shared/sys/sys_unix.cpp`) read the wall clock (`gettimeofday`), which moves with every step of the system time (NTP, a date set by hand): when it went back, the server ran no frame and sent no snapshot until it caught up (beyond 30 s every client timed out); when it went forward, up to 5 s of frames ran at once. It now reads the monotonic clock (`clock_gettime( CLOCK_MONOTONIC )`), at the same cost (both are read without a system call); time spent suspended is no longer counted. Windows keeps `timeGetTime`, already monotonic: its value changes at the system timer's interrupts (every 0.5 ms with the 1 ms period the engine asks for), so a frame starts 0 to 0.5 ms after its millisecond (measured), without accumulating, which the client's interpolation delay absorbs; `QueryPerformanceCounter` would cost 14.7 ns per call instead of 2.8 for no visible gain. The unused `baseTime` parameter is gone.
- Random numbers drawn in an order the compiler chooses: the bouncing flechettes of the alternate fire (`WP_CreateFlechetteBouncyThing`, `game/g_weapon.c`) drew their speed and their lifetime as two arguments of one call, which C evaluates in an unspecified order (clang left to right, MSVC right to left), so an MSVC server gave other flechettes than a clang or GCC one; the same in an NPC's victory voice event (`NPC.c`) and the Galak mech's aim (`NPC_AI_GalakMech.c`). The draws are now made one after the other, in the left-to-right order.

### Math functions identical on every platform (plan step 14)

OpenJK calls the C library's sine, cosine, arc tangent, power and the like. Their last bit is not fixed by any standard, and it differs between glibc versions (2.36 and 2.41 measured), between Windows versions (the UCRT ships with Windows), between 32 and 64-bit programs and between processors; MSVC's static and DLL runtimes differ too. A Windows client and a Linux server therefore did not compute the same movement, and two Linux servers running the same binary did not compute the same game. Survey, measurements and choice: [12-deterministic-math-survey.md](12-deterministic-math-survey.md).

| what | OpenJK | this branch | why |
|---|---|---|---|
| `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `log`, `log2`, `pow` and their float versions, everywhere in the multiplayer code and `shared` (game, cgame, UI, client, server, botlib, renderers, sound; not the two third-party libraries inside rend2, `stb_image.h` and MikkTSpace) | the C library's | one entry point, `shared/qcommon/q_libm.h` (`Q_sinf`, `Q_sin`, `Q_atan2f`, ...), implemented in `q_libm.cpp` on LLVM libc's correctly rounded functions (`lib/llvm-libc`); each call keeps its precision (a double call stays double) | the same bits on every compiler, system and processor (measured on 10 builds and on the VPS); changing the library later changes one file |
| `Q_rsqrt` (inverse square root) | Quake's approximation (`0x5f3759df` and one Newton step, about 0.2 % off) | `Q_rsqrtf`, correctly rounded | precision; used by `SQRTFAST` and renderer code |
| `Q_acos`, `Q_asin` | the C library's `acosf` and `asinf` with a clamp to ±π that never applied | `Q_acosf`, `Q_asinf` of the entry point | one version of each function |
| `Q_powf(float, int)` | an integer power by repeated multiplication, used by the third-person camera with `cg_smoothCamera 0` | removed; the name is the entry point's real power, which the camera calls with the same whole exponent | one version (section 8, `cg_smoothCamera`) |
| `Q_log2(int)` | integer logarithm | renamed `Q_ilog2` | the name `Q_log2` is the entry point's `log2` |
| `sqrt`, `floor`, `ceil`, `fabs`, `fmod`, `round`, `lrintf`, `isnan`, `isfinite` and their float versions | the C library's, called directly; `Q_fabs(float)` and `Q_isnan(float)` as own copies | through the same entry point (`Q_sqrtf`, `Q_floor`, ...), as inline calls to the C library (IEEE 754 fixes their results: same bits, same instruction); `Q_fabs` and `Q_isnan` are the entry point's | one place for every math function, one version of each |
| Trigonometry of angles in degrees (`AngleVectors`, `RotatePointAroundVector`, the TR_SINE trajectories, the map entities' rotation, field of view, effects) | degrees converted to radians in float (`DEG2RAD`, `M_PI` itself a float), then `sin` / `cos` / `tan` | `Q_sinpif`, `Q_cospif`, `Q_tanpif` of the angle in half turns (degrees / 180) | correctly rounded results of the angle the code holds, exact at multiples of 90 degrees (the radians path gave about 1e-7 instead of 0 for 180 degrees); measured: the radians path differed from the exact result on 1.5 million of 2.1 million angles |
| Angles from vectors (`vectoangles`, `vectoyaw`, the normal encoding, the movers' arc, field of view) | `atan2`, `atan` or `acos` in float, times 180 / `M_PI` (a float pi) | `Q_atan2degf`, `Q_atandegf`, `Q_acosdegf`: computed in double, rounded to float once | one rounding instead of three |
| Vector lengths and normalisation (`VectorLength`, `VectorLengthSquared`, `Distance`, `DistanceSquared`, `VectorNormalize`, `VectorNormalize2`, `VectorNormalizeFast`) | in float: the squares, their sum, the square root, the inverse and the products each rounded | in double (the squares of floats are exact there), each result rounded to float once | the most precise float result |
| Text to numbers: `atof` (318 calls, the cvars included), `strtod`, and the `sscanf( s, "%f %f %f", ... )` of map entities, vehicle, effect, ICARUS and BSP entity strings (28) | the C library's: the UCRT's `strtof` and `sscanf %f` round a decimal exactly halfway between two floats away from even (`83034.89453125`, `16777217`), glibc to even | `Q_atof`, `Q_strtod`, `Q_strtof` and `Q_ScanFloats` of the entry point, on LLVM libc's correctly rounded parser | a map or a file reads the same values on Windows and Linux; `Q_isanumber` uses `Q_strtod` |
| Random numbers | three generators: Raven's `holdrand` (MSVC's LCG, 15 bits, biased ranges, behind `Q_irand`, `Q_flrand`, `irand`, `flrand`), the C library's `rand()` (another sequence on glibc and on the UCRT, so a Linux and a Windows server played differently) and `PM_irand_timesync` | one entry point, `shared/qcommon/q_rand.h`: xoshiro128++ seeded through SplitMix64, unbiased integer ranges (Lemire), 24-bit floats; `PM_irand_timesync` from a state seeded with the command's time, 1.01's effective range kept; `rand()`, `irand`, `flrand`, `RandFloat` and `g_randFix` gone | the same random numbers on every platform; survey [13-random-numbers-survey.md](13-random-numbers-survey.md) |
| Connection challenges, pure checksum feed | `rand()` and the clock | the system's random bytes (`Com_RandomBytes`) | an attacker cannot guess them |
| Fused multiply-add contracted from `a * b + c` | allowed (GCC and Clang contract when the target has the instruction) | `-ffp-contract=off` for GCC and Clang; MSVC's `/fp:precise` does not contract | a build for a CPU with the instruction would otherwise round differently; no effect on today's builds |


## 8. Settings

| cvar | OpenJK | this branch | why |
|---|---|---|---|
| `sv_fps` | 40 | removed: a fixed tick of 10 ms (`TICK_MSEC`, `qcommon/q_shared.h`), a game frame and a snapshot every tick (plan step 14; 20 from step 7 to step 14 part 2, the snapshot rate of the captures and demos the codec was measured and trained on) | the client, the server and the game module share one simulation tick; the game's logic keeps its 20 Hz pace on the tick's 20 Hz instants (section 7). The client takes the snapshot interval from the constant, no longer from the server info; `timedemo` advances one tick per frame (50 ms before). After a stall the server runs at most 25 ticks at once (250 ms; OpenJK caught up to 5 s): the rest of the stall is dropped with a "Hitch" message, and the clients follow the server's clock as after a `map_restart`. The bots run before every tick (OpenJK: once per server frame, after the packets): each sends one command per tick, run as a player's command is when it arrives (OpenJK: a bot moved once per game frame, at the frame's time, inside the frame); their AI still thinks every 50 ms on the 20 Hz instants. `bot_debugClientRate`, the debug cvar that did this for the step 14 measurements, is removed |
| `rate` and `snaps` (client), `sv_clientRate`, `sv_ratePolicy`, `sv_minRate`, `sv_maxRate`, `sv_lanForceRate`, `sv_snapsMin`, `sv_snapsMax`, `sv_snapsPolicy` (server) | the client asks for a rate and a snapshot rate, the server's policies decide; LAN clients unlimited | removed (plan step 11) | a snapshot every server frame for every client; a fixed rate of 200,000 bytes per second (`SV_CLIENT_RATE`; 100,000, the former LAN rate, in step 11, raised in step 14 for the 125 Hz tick) only delays the snapshot after a message larger than a frame allows; a single message counts at most 1,500 bytes, so at 125 Hz only a fragmented message, such as a gamestate, delays one; snapshots are about 100 bytes with headers, 2 KB per second at 20 Hz. The server's `status` loses its `rate` column. The tick (`TICK_MSEC`, 10 ms) alone sets the snapshot rate |
| `cl_maxpackets`, `cl_lanForcePackets` (client) | at most `cl_maxpackets` (63) packets per second, as a minimum gap between two packets checked once per frame; every frame on a LAN | removed (plan step 11) | since plan step 14 (part 4b), a packet per tick: the client makes one user command per tick and sends a packet when a frame made one, 100 per second (`MAX_PACKET_RATE`; the server decodes at most 110 on average, `SV_PACKET_RATE`, with a burst of 100, `SV_PACKET_BURST`), and a command and a packet every frame until its first snapshot. Before (step 11): a packet every frame up to 125 per second (`MAX_PACKET_RATE`), the unused part of the interval carried over, so the client sends min(frame rate, 125) packets per second; the minimum gap let a packet leave only every second frame when frames were slightly shorter than it (62.7 packets per second measured at 125 fps with `cl_maxpackets` 100). The server decodes at most 137 packets per second per client on average (`SV_PACKET_RATE`, 10 % above the client's 125, whose clock may run slightly faster than the server's: with no margin, a client clock 0.1 % fast would empty the burst in about 17 minutes without a map load and then lose a packet every 8 s), with a burst of 125 packets (`SV_PACKET_BURST`), and drops the rest unread (`svnetstats` counts them); a local client (loopback) still sends every frame |
| `cl_packetdup` (client) | 1: each packet repeats the user commands of the previous packet | removed: always 2 (`CL_PACKET_DUP`) | two client packets lost in a row cost no command; a repeated command costs 1.5 to 1.9 bytes (measured: 41.7 bytes per packet with headers without repeat, 44.7 with 1, 48.4 with 2, at 62.5 packets per second); at 125 packets per second, 2 packets cover 16 ms of commands, as 1 did at 63 |
| `cl_timeNudge` (client) | the player shifts the displayed time by up to 30 ms (negative: the other players are extrapolated, closer to the present but guessed) | removed; debug cvars `cl_debugInterpForce` and `cl_debugInterpDelay` (`CVAR_CHEAT`: reset unless the server sets `sv_cheats 1`) force a delay | the client computes the delay (`CL_AdjustTimeDelta`). Since plan step 14 (part 4e): at every frame the client's time must not pass the newest snapshot's; each interval between two snapshot arrivals gives a sample, how far the last frame before the next snapshot could have been ahead; the client keeps the smallest sample of each of the last 64 slots of 32 ms (about 2 s) and aims 2 ms under the second smallest, moving its time by at most about 6 % faster or 7 % slower. One late snapshot in 2 s does not raise the delay, two do, lost snapshots too when they come more than once in 2 s (no separate loss rule), and an interruption of more than 100 ms gives no sample. Against the rule of step 11 (the second latest of the last 64 arrivals minus a full interval, plus one interval above 5 % of loss), simulated on the same arrivals: 5 to 8 ms less delay at 60 and 125 frames per second, about 1 ms less at 333, and with 2 % of loss at 333 frames per second 6 ms more delay for 8 times fewer extrapolated frames. The client's own ping (`snap.ping`, the lagometer) is measured over its last 256 packets in real time (it showed 999 above 320 ms of round trip and was up to a frame short). In step 11: it showed the world at the time at which the next snapshot has already arrived in 63 cases out of 64 over the last 64 snapshots, plus one interval while more than 5 % of the server messages are lost; before, it drifted until it came within 5 ms of the newest snapshot in one interval out of three. On a stable link the world is shown about 5 ms more recent than before, at real positions; on an unstable link the delay grows by about the jitter instead of extrapolating. Nobody can choose a delay below another player's, which is what the players who imposed `cl_timenudge 0` wanted. `cg_smoothClients` (whether the other players are extrapolated along the trajectory the server sends or interpolated) is decided in plan step 14. Details: 10-network-settings-survey.md, point 5 |
| `cl_timeout`, `sv_timeout` (200 s), `sv_zombietime` (2 s) | cvars | removed: 60 s while connecting or loading, 30 s in the game (`TIMEOUT_CONNECTING`, `TIMEOUT_ACTIVE`, both sides), 2 s of zombie state (`SV_ZOMBIE_TIME`) | a dead client frees its slot after 30 s instead of 200 |
| `sv_padPackets` | adds padding bytes | removed | it padded only the legacy format |
| `sv_netcodecCompare` | | added (debug, 0 by default) | also codes each message and each client packet in the legacy format and prints the byte ratios, every 10 s (1) or every message (2) |
| `sv_netcodecMaxBytes` | | added (debug, 0 by default: 49,152 bytes) | the largest message the server codes, to test what happens to larger ones: snapshots are cut to the nearest entities, a gamestate drops the client |
| `pmove_float`, `pmove_fixed`, `pmove_msec` | game and cgame cvars | removed | section 7 |
| `g_synchronousClients` (game and cgame), `g_forceClientUpdateRate` (game) | 0: a player moves when its commands arrive; 1: every player moves in the server frame with its last command, and the client does not predict (used to record demos). `g_forceClientUpdateRate` 250: a player that sends nothing for 250 ms gets an empty command | removed (plan step 14, part 4c) | every player runs one command per tick in the server's tick, and the client predicts; a player that sends nothing has its last command run again for 250 ms, then empty ones every tick (the default's 250 ms kept) |
| `cg_smoothCamera` (cgame) | 0 (default): the third-person camera's damping used an integer power of the time in whole 50 ms intervals, and the damping factor itself below two intervals, so the camera followed faster at a higher frame rate; 1: the real power, the same at any frame rate | removed: always the damping of 0, the default (owner, 12 November 2026), its integer power computed by the entry point's `Q_powf` (one rounding instead of one per multiplication) | one behaviour, the default's; the math entry point has one power function (section 7) |
| `g_randFix` (game) | how `RandFloat` (bounces of missiles, a saber choice) scaled `rand()`: 0 as basejka, 1 (default) the correct range, 2 a deliberately wrong one; its Linux branch never compiled (`__GCC__`), so Linux servers always got values far outside the range | removed: the default's correct range, from the entry point's `Q_flrand` | `rand()` is gone (section 7, random numbers) |
| `cl_shownet` | levels 1 to 4, -1, -2 | levels 1, 2 and 3 | section 1 |

Console commands and two cvars are added for statistics and measurements; the statistics commands print their counters since the previous call:

| command | side | what it prints |
|---|---|---|
| `svnetstats` | server | first the server's CPU time (plan step 14, to size the tick rate): the server frames, and within them the game frames, the bot frames and the snapshots sent, then the client packets decoded and run as they arrive, each with its count, mean and largest time in microseconds, the server frames longer than the interval between frames, and the share of one core; then, for each client: messages, bytes per message, messages sent raw (escapes), snapshots without a base, snapshots whose base was refused, snapshots cut to fit, base age; the ping; server commands sent and how many were repeats, by why the message sent them again (not in the game or a bot, the message that carried them lost, a new command, twice the ping passed without news, both of the last two; plan step 14); client packets, bytes per packet, packets not decoded, packets dropped above the packet rate, user commands per packet, client packets lost on the way and the length of each run of losses; how much the time a snapshot leaves changes against the game clock it carries (mean and largest change between consecutive snapshots, measured in microseconds), which the client would otherwise take for network jitter (plan step 11); the ticks run with the player's own command and without one, the commands that came after their tick, the mean and the lowest input margin (plan step 14, part 4c) |
| `gamestats` | server (game module) | combat statistics since the last call, then reset: saber swings, wounds, damage, saber against saber contacts (blocks), body hits parried without the sabers touching, saber locks and idle touches, in total and per swing; projectiles blocked by sabers, force damage, deaths; for grip, rage, protect, absorb, lightning and drain, the starts, the periodic events (drains, rage health losses, shots) and the time held; then the events added by the game by type, the health taken from clients and the deaths by cause, the shots by weapon and fire mode, and the saber attacks started and moves bounced or deflected by style (plan step 14: the game must play the same at any server frame rate; `docs_rewrite/netcode/tools/gamestats/` runs bot servers and tables the results) |
| `scenario` | server (game module), cheat | `scenario saber <repetitions> <fast\|medium\|strong\|dual\|staff> <idle\|attack> [distance]`, `scenario force <repetitions> <power> [distance]`, `scenario weapon <repetitions> <weapon number> <0\|1> [distance]`, `scenario off`: two bots, placed face to face and reset every 2 s, play a fixed script of commands timed in milliseconds; a first repetition is not counted, `gamestats` is printed at the end. With `scenario_trace <n>`, the two bots' state at every 20 Hz instant and every health loss are printed for the first n repetitions, to compare two server rates line by line (plan step 14). Every reset puts the bots back in a state that does not depend on what came before: health, timers, powerups, damage sums, walking direction, the weapon's ready animations, a thrown or knocked-away saber back in the hand, the blades' last positions forgotten, the random numbers reseeded before a bot killed in the previous repetition respawns; when the counted repetitions start, the corpses, missiles, events and dropped items are removed and every free entity slot can be taken at once |
| `g_debugSeed` | server (game module) | a non-zero value replaces the game's start seed (the clock's milliseconds): with `fixedtime` set before the map loads, a run replays the same game; the bench scripts set it with `SEED=<n>` (plan step 14) |
| `netstats` | client | messages lost on the way, messages not decoded, bytes per message, snapshots without a base, base age; the interpolation delay (how far behind each snapshot the client's time was when it arrived), the jitter of the arrivals, the intervals in which the client passed the newest snapshot (extrapolated); the command clock (how far ahead of the drawn time, the mean and lowest input margin the server reported, how many were negative); client packets sent, bytes per packet, packets coded without a base |

Configuration files that still set a removed cvar are harmless: the value is stored and never read. The UI module's `ui_setRate`, which set `cl_maxpackets` and `cl_packetdup` from `rate`, is removed (the game's menus have no network setting). The game module no longer advises `/snaps`, its userinfo validation no longer knows `rate` and `snaps` (it required them, so every client was refused at connection), and bots no longer send them. `g_userinfoValidate` is a bit mask indexed by the position of each checked field, so removing the two fields moves the bits after them: its default becomes 6291455 (the same checks as OpenJK's 25165823), and a server configuration that still stores the old value turns on the extended ASCII check (names with accents refused) until the cvar is reset (`reset g_userinfoValidate`); cgame no longer sets `snaps`. Why each setting is fixed or computed, and what other games do: [10-network-settings-survey.md](10-network-settings-survey.md).
## 9. Client fixes outside the netcode

Made on the way while testing, each in its own commit, mostly as TaystJK does.

| fix | before | after | files |
|---|---|---|---|
| Menu cursor speed | moved by raw screen pixels: 3 times faster than the desktop at 1920 x 1080 | moves by mouse deltas scaled to the 640 x 480 menu space | `ui_main.c` |
| `r_aspectCorrectFonts` | narrowed the spacing but not the glyphs: letters overlapped | narrows both | `tr_font.cpp` |
| Keypad with num lock on | keypad keys became digits everywhere, so `KP_*` binds never fired in game | digits only while typing (and not while binding a key) | `sdl_input.cpp`, `ui_shared.c` |
| Right shift, ctrl, alt | the same keys as the left ones | keys of their own (`RSHIFT`, `RCTRL`, `RALT`), bindable apart; shortcuts accept either side | `cl_keys.cpp`, `sdl_input.cpp`, `ui_shared.c` |
| Dropped while connecting | a client dropped before its cgame runs (kicked while loading, for example) kept waiting for the gamestate: only cgame executed the `disconnect` server command | the `disconnect` command is handled as soon as it arrives when cgame does not run yet: back to the menu with the reason | `cl_parse.cpp` |
| Server browser: whether a server answered | a ping of 0 meant "request sent", "no answer" and "answered in 0 ms" at once, so a server answering in under 1 ms (same machine or LAN) was hidden from the list | each server has a state of its own (not asked, waiting, answered, no answer); the ping is only a measure and 0 ms is valid; the UI reads the state through a new import, `trap->ext.LAN_GetServerState` | `client.h`, `cl_main.cpp`, `cl_lan.cpp`, `cl_uiapi.cpp`, `ui_public.h`, `ui_main.c`, `ui_syscalls.c` |

## 10. Build and repository

| | OpenJK | this branch |
|---|---|---|
| New library | | `codemp/netcodec/`: C++20 inside, a C++11 interface for the engine (`engine.h`), trained tables compiled in; linked by the client and the dedicated server |
| Math functions | the C library's | `lib/llvm-libc/`: the headers of LLVM libc's header-only math functions (Apache 2.0 with LLVM exceptions, a fixed commit, `update.py` to refresh them); `shared/qcommon/q_libm.cpp` (C++17) in the static library `qlibm`, linked by every binary that uses `q_math.c` (engine, dedicated server, game, cgame, UI, renderers, and the single-player targets); `-ffp-contract=off` for GCC and Clang |
| Random numbers | `holdrand` in `q_math.c`, the C library's `rand()` | `shared/qcommon/q_rand.h` and `q_rand.c` (xoshiro128++, in `SharedCommonFiles`); tools `docs_rewrite/netcode/tools/rng/` (`rngbench.c`, `unsequenced.py`) |
| Scenario counts | | `docs_rewrite/netcode/tools/gamestats/scenarios40.sh` runs the 40 scripted cases at 20 and 100 Hz on Windows or Linux and compares them with `scenarios40_golden.txt`, the same on every platform |
| Tests | | round trip on about 36,000 recorded messages, 100,100 synthetic messages, malformed-input test and fuzzer, determinism hashes per compiler, pmove determinism (game against cgame, compiler against compiler); client packets: 100,000 synthetic packets, the recorded packets through the library and through the engine interface, 400,000 malformed packets, and a fuzzer |
| Compilers checked | | MSVC (x64, x86), GCC 12 (x64, x86) and 16 (x64), Clang (llvm-mingw on Windows, Clang 14 on Linux) |
| `.gitattributes`, `.gitignore` | | line endings of scripts, demo files and local traces ignored |
| Study and plan | | `docs_rewrite/netcode/` (this folder) |

## 11. Compatibility summary

| combination | works |
|---|---|
| Branch client with branch server | yes |
| Branch client with an OpenJK or 1.01 server, or the reverse | no, and the connection is not refused (protocol number unchanged, end of the plan) |
| Branch build playing an OpenJK or 1.01 demo | no |
| Demo of the branch played by OpenJK | no |
| Game and cgame modules built from OpenJK with the branch engine | yes (the module interface is unchanged), with OpenJK's physics; the server's game and the client's cgame must come from the same source, or the prediction misses |
| Server browser, master server, `getstatus` / `getinfo` | yes, unchanged |
| Configuration files from OpenJK | yes; removed cvars are ignored |

## 12. Not changed yet

| subject | where |
|---|---|
| Protocol number, handshake, support of the old protocol, old demos | end of the plan |
| An acknowledgement bitfield, several UDP download blocks per message, a snapshot history sized from the round trip (not done in step 10, reasons in its document) | later, if wanted |
| Lag compensation | plan step 14 |
| Encryption, or a replacement for the removed XOR | plan step 12 |
| Tables retrained on games of this build | plan step 13 |
| Fixed physics step, prediction and interpolation audit, lag compensation | plan step 14 (right after step 11) |
