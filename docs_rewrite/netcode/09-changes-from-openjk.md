# What this branch changes in OpenJK, why, and what it implies

Last update: 9 November 2026. Branch `feature/poc-netcode`, compared with OpenJK `master` at `260c59c29`. Multiplayer only (`codemp/` and the shared code it uses); single player is not built.

This page lists every change of the branch outside the study documents, grouped by subject, with the reason and the consequences. It avoids code: file names are given so that each change can be found, and the details are in the step documents of [progress/](progress/README.md). The work follows the owner's goals stated in the [README](README.md): no loss of precision, no compatibility constraint, as few network settings as possible, a netcode other JKA forks can reuse, Windows and Linux.

## At a glance

| area | OpenJK | this branch | main consequence |
|---|---|---|---|
| Server-to-client messages | Huffman bitstream (`msg.cpp`, `huffman.cpp`) | new message codec, `netcodec` (rANS with per-field statistics and prediction) | about 0.38 to 0.45 of the bytes, values exact |
| Client-to-server messages | Huffman bitstream | **unchanged for now**; coded by netcodec in plan step 8 | |
| Netchan obfuscation (XOR) server to client | yes | removed | to revisit with real encryption (plan step 12) |
| Protocol number | 26 | **26, unchanged for now** | an OpenJK client and a branch server do not understand each other, and nothing refuses the connection |
| Reliable server commands kept | 128 | 256 | needed by the codec; a little more memory |
| Demos | copy of the network messages | server: copy; client: coded again for the demo | old demos cannot be played back |
| UDP downloads | up to 8 blocks per message, as the rate allows | 1 block per message | same speed on the internet at default rates, about 3 times slower on a LAN |
| Player velocity | rounded to integers every frame (unless `pmove_float 1`) | never rounded | exact velocity, jumps no longer depend on the frame rate |
| Missiles | positions and velocities rounded | exact | trajectories exactly as the server computes them |
| Rounding rule of `SnapVector` | differs between compilers | to nearest, ties to even, everywhere | a Windows client and a Linux server agree |
| Server defaults | `sv_fps` 40, `sv_clientRate` 50000 | `sv_fps` 20, `sv_clientRate` 25000 | the conditions of the measurements |
| Removed cvars | | `pmove_float`, `pmove_fixed`, `pmove_msec`, `sv_padPackets` | movement now always behaves as OpenJK with `pmove_float 1` and `pmove_fixed 0` (section 6) |
| Added cvar | | `sv_netcodecCompare` (debug) | byte comparison with the legacy format |
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
| End of message | an `svc_EOF` byte | none (the codec knows the length) |
| Oversized message | the server clears it and sends it empty | if a message codes larger than 1 KB, it is also coded raw and the smaller is sent; a worst case stays close to the raw size |
| Precision | what the field widths allow | exactly what the server holds |

### Results

| measurement | legacy | netcodec | ratio |
|---|---|---|---|
| 20 held-out demos of real games (maps not used for training), all messages | about 24 MB | 9.13 MB | 0.381 |
| the same, snapshots only | | 66.9 bytes per snapshot | 0.432 |
| clang server, 4 bots, 150 s, velocity rounded (before 9 November) | 82.5 bytes per message | 32.1 | 0.389 |
| the same, velocity exact (current build) | 89.1 | 38.6 | 0.433 |
| MSVC client and server, in combat (8 November, velocity rounded) | about 330 bytes per packet | about 150 | about 0.45 |

Coding and decoding are also faster than OpenJK's own code on the same messages: encoding takes about 0.38 and decoding about 0.56 of the legacy time (clang, warm measurement; step 6, part C). Every compiler (MSVC, GCC, Clang, x86 and x64) produces the same bytes, which tests check on each build.

Tested in game over UDP with simulated loss (5 % and 20 % of the server's messages lost: no message the client could not decode) and with a GCC dedicated server on a Linux VPS played from a Windows client over the Internet (3,019 messages in about 2.5 minutes, none lost or undecodable, prediction misses of the same order as with a Windows server).

### What does not change

- The client-to-server direction: user commands, client commands, their XOR and Huffman coding (plan step 8 changes it).
- Connectionless messages (`getinfo`, `getstatus`, `connect`, the master server): the server browser works as before (one display defect fixed, section 8).
- The choice of the base snapshot (the newest one the client acknowledged), `PACKET_BACKUP` (32), the rate and snapshot-rate rules, the netchan fragmentation.
- The legacy writer is kept in the server, only to compare sizes (`sv_netcodecCompare`).

### What it implies

- Branch builds talk only to branch builds. The protocol number is still 26, so an OpenJK or 1.01 client can connect to a branch server (and the reverse) but nothing it receives makes sense. The protocol number, the handshake and a possible support of both protocols are decided at the end of the plan.
- Mods that relied on the overrides files to widen `user*` fields get the full values without them.
- `cl_shownet 1` still prints the packet sizes and `cl_shownet 3` the snapshot line; `cl_shownet 2` prints one line per decoded operation (commands, gamestate, game directory, snapshot with its base and entity count, download block, map change). The per-entity and per-field output (levels 2 and 4, -1, -2) is gone, since the codec has no per-field trace.
- A message the client cannot decode (only possible if it no longer holds the base the server used) is dropped whole; the client then asks for a full snapshot, as before, and the commands are sent again until acknowledged.

## 2. Netchan obfuscation removed from server to client

OpenJK XORs each server message with a key made of the challenge, the sequence number and the last command texts. The client found the key from the acknowledge number read in clear at the start of the message; the codec now codes that number, so the scheme could not stay as it was. It was never security: anyone who sees the connection can undo it. It is removed in the server-to-client direction (`sv_net_chan.cpp`, `cl_net_chan.cpp`); the client-to-server direction keeps it. Whether to restore an equivalent or replace it with real encryption is decided in plan step 12.

## 3. Reliable command buffer: 128 to 256 commands

| | OpenJK | this branch |
|---|---|---|
| `MAX_RELIABLE_COMMANDS` (`qcommon.h`) | 128 | 256 |
| Server memory per client | 128 KB | 256 KB (+4 MB for 32 clients) |
| Client memory (two buffers) | 256 KB | 512 KB |
| Client dropped when this many server commands are unacknowledged | 129 | 257 |

Why: a server command is coded against an earlier command up to 127 commands back, so both sides must keep at least 256 to always hold the same reference. The client-to-server buffer uses the same constant and grows too.

## 4. Demos

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

## 5. Downloads over UDP

| | OpenJK | this branch |
|---|---|---|
| Blocks per message | as many as the rate allows in one snapshot interval, up to the 8-block window | one 2,048-byte block |
| Internet, branch defaults (rate 25000, 20 snapshots per second) | 1 block per message | 1 block per message: same speed |
| LAN (`sv_lanForceRate`, rate 100000) | 3 blocks per message | 1 block: about 3 times slower |

Why: the codec carries one download operation per message. A better download path (several blocks, or a channel of its own next to the reliable data) is part of plan step 10. HTTP downloads are not affected.

## 6. Game code: precision and physics

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
| Rounding rule of `SnapVector` (events, dropped items, spawn point) | truncation, except MSVC 32-bit: nearest | to nearest, ties to even, on every compiler | a Linux server and a Windows server place them the same way |
| Rounding rule of `Sys_SnapVector` (the engine's) | ties away from zero with MSVC, to even elsewhere | to even everywhere | no pmove call uses it now; kept consistent for any module that does |

The velocity in more detail:

| | velocity rounded (OpenJK default) | velocity exact (this branch) |
|---|---|---|
| Jump height | depends on the frame time (the 125 and 333 fps advantages) | the same at any frame rate (small step effects of acceleration and friction remain) |
| Windows client (MSVC) against Linux server (GCC) | before the tie fix: up to 2.7 units of prediction error; after it, 0.0001 to 0.0002 unit in the test, but a rounding can still flip by 1 unit/s | at most 0.0006 to 0.0009 unit, no flip possible (100 times under the prediction miss threshold) |
| Bytes | reference | +6.5 bytes per message with bots, +6.1 estimated on the demos (the tables were trained on rounded velocities; retraining on this build's games, plan step 13, should lower it) |

What it implies: players used to the 125 or 333 fps jumps lose them, and a jump designed for them on a map may no longer pass. A physics fully independent of the frame rate needs a fixed step, a gameplay question left to plan step 14. A mod that takes this game code loses the three `pmove_*` cvars; a mod with its own game code keeps its own physics, as long as its game and cgame match each other.

## 7. Settings

| cvar | OpenJK | this branch | why |
|---|---|---|---|
| `sv_fps` | 40 | 20 | the snapshot rate of the captures and demos the codec was measured and trained on |
| `sv_clientRate` | 50000 | 25000 | the client's default rate; same delay threshold as OpenJK's pair (messages from 1,202 bytes) |
| `sv_padPackets` | adds padding bytes | removed | it padded only the legacy format |
| `sv_netcodecCompare` | | added (debug, 0 by default) | also codes each message in the legacy format and prints the byte ratio, every 10 s (1) or every message (2) |
| `pmove_float`, `pmove_fixed`, `pmove_msec` | game and cgame cvars | removed | section 6 |
| `cl_shownet` | levels 1 to 4, -1, -2 | levels 1, 2 and 3 | section 1 |

Two console commands are added for statistics, each printing its counters since the previous call:

| command | side | what it prints |
|---|---|---|
| `svnetstats` | server | for each client: messages, bytes per message, messages sent raw (escapes), snapshots without a base, snapshots whose base was refused, base age |
| `netstats` | client | messages lost on the way, messages not decoded, bytes per message, snapshots without a base, base age |

Configuration files that still set a removed cvar are harmless: the value is stored and never read. The other network settings (`rate`, `snaps`, `cl_maxpackets`, ...) are unchanged for now; plan step 11 replaces them with values measured at run time.

## 8. Client fixes outside the netcode

Made on the way while testing, each in its own commit, mostly as TaystJK does.

| fix | before | after | files |
|---|---|---|---|
| Menu cursor speed | moved by raw screen pixels: 3 times faster than the desktop at 1920 x 1080 | moves by mouse deltas scaled to the 640 x 480 menu space | `ui_main.c` |
| `r_aspectCorrectFonts` | narrowed the spacing but not the glyphs: letters overlapped | narrows both | `tr_font.cpp` |
| Keypad with num lock on | keypad keys became digits everywhere, so `KP_*` binds never fired in game | digits only while typing (and not while binding a key) | `sdl_input.cpp`, `ui_shared.c` |
| Right shift, ctrl, alt | the same keys as the left ones | keys of their own (`RSHIFT`, `RCTRL`, `RALT`), bindable apart; shortcuts accept either side | `cl_keys.cpp`, `sdl_input.cpp`, `ui_shared.c` |
| Server browser: whether a server answered | a ping of 0 meant "request sent", "no answer" and "answered in 0 ms" at once, so a server answering in under 1 ms (same machine or LAN) was hidden from the list | each server has a state of its own (not asked, waiting, answered, no answer); the ping is only a measure and 0 ms is valid; the UI reads the state through a new import, `trap->ext.LAN_GetServerState` | `client.h`, `cl_main.cpp`, `cl_lan.cpp`, `cl_uiapi.cpp`, `ui_public.h`, `ui_main.c`, `ui_syscalls.c` |

## 9. Build and repository

| | OpenJK | this branch |
|---|---|---|
| New library | | `codemp/netcodec/`: C++20 inside, a C++11 interface for the engine (`engine.h`), trained tables compiled in; linked by the client and the dedicated server |
| Tests | | round trip on about 36,000 recorded messages, 100,100 synthetic messages, malformed-input test and fuzzer, determinism hashes per compiler, pmove determinism (game against cgame, compiler against compiler) |
| Compilers checked | | MSVC (x64, x86), GCC 12 (x64, x86) and 16 (x64), Clang (llvm-mingw on Windows, Clang 14 on Linux) |
| `.gitattributes`, `.gitignore` | | line endings of scripts, demo files and local traces ignored |
| Study and plan | | `docs_rewrite/netcode/` (this folder) |

## 10. Compatibility summary

| combination | works |
|---|---|
| Branch client with branch server | yes |
| Branch client with an OpenJK or 1.01 server, or the reverse | no, and the connection is not refused (protocol number unchanged, end of the plan) |
| Branch build playing an OpenJK or 1.01 demo | no |
| Demo of the branch played by OpenJK | no |
| Game and cgame modules built from OpenJK with the branch engine | yes (the module interface is unchanged), with OpenJK's physics; the server's game and the client's cgame must come from the same source, or the prediction misses |
| Server browser, master server, `getstatus` / `getinfo` | yes, unchanged |
| Configuration files from OpenJK | yes; removed cvars are ignored |

## 11. Not changed yet

| subject | where |
|---|---|
| Protocol number, handshake, support of the old protocol, old demos | end of the plan |
| Statistics counters, tests under loss, Linux server with Windows client | step 7, part 5 |
| Client-to-server messages coded by netcodec (user commands, client commands), their XOR and user-command key dropped | plan step 8 |
| Acknowledgements, reliable path, downloads | plan step 10 |
| Network settings measured at run time (rate, snaps, packets, interpolation delay) | plan step 11 |
| Encryption, or a replacement for the removed XOR | plan step 12 |
| Tables retrained on games of this build | plan step 13 |
| Fixed physics step, prediction and interpolation audit | plan step 14 |
