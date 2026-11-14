# Implementation plan

This plan turns [04](study/04-recommendations.md) into steps to do one after the other. Each step has a deliverable, a check that says when it is done, and the steps it depends on. Progress, results of finished steps and the next action are in [progress/README.md](progress/README.md). The plan follows the owner's goals listed in the study's [README](README.md): no loss of precision, no compatibility constraint, as few network settings as possible, netcode that other JKA forks can reuse, and Windows and Linux (x86 and x64) as targets.

The work happens in this repository (a fork of OpenJK, branch `feature/poc-netcode`). Paths are relative to `codemp/`; where a function is cited, its line in this tree is in [study/08-openjk.md](study/08-openjk.md) section 8.6. Effort labels: S = days, M = weeks, L = months, for one engineer who knows the code.

## 1. Principles

1. **Values are never less precise than today.** For any message, the values the client decodes are equal to what the original code delivers (truncation to the field widths, `-0.0` as `+0.0`), or more precise. Every test compares field by field.
2. **One independent coded stream per message.** No coder state survives from one packet to the next; only acknowledged snapshots are used as bases.
3. **The codec is a library.** It knows the field descriptors, not the game: no dependency on the server, the client, `game` or `cgame` beyond `entityState_t`, `playerState_t` and their field tables. Another fork links it, supplies its own field tables, trains or loads its own probability tables, and works with the uniform fallback until it does.
4. **The codec is isolated from the transport.** The transport can change later without touching it.
5. **Data-driven.** Field lists, widths, modes and tables are data generated from one source of truth, versioned together.
6. **No legacy path is kept for compatibility.** The protocol number changes and old clients and servers are refused at connection. A switch between the old and the new coder is kept only while it helps comparison and is removed afterwards.
7. **Settings are measured, not configured.** A new network cvar is added only for debugging or statistics (04 section 4.10).
8. **Game and cgame may change.** The codec keeps giving them the same structures, so changes there are optional and separate (for example a later audit of prediction and interpolation).

## 2. Steps

### Step 1: confirm the prediction modes with the real codec (Python, S)

Depends on: nothing.

**Done on 5 November 2026** (03 section 3.13): 107.3 real bytes per snapshot with integer arithmetic, 0 failures on real, synthetic and simulated-loss messages. The integer arithmetic is kept.

- Add to `codec.py` and `codec2.py` the modes measured in 03 section 3.12, offered to the per-field mode chooser rather than forced:
  - T: time fields (`pos.trTime`, `apos.trTime`, `commandTime`) against the message's `serverTime`;
  - P2: positions against `old + mean velocity * elapsed`, residual on the 32-bit patterns; two versions of the prediction arithmetic: float (computed in double from float32, rounded once) and integer only;
  - A2: angle index against `2 * old - older`, where `older` is the base's own base (the client must find it by the base's delta number, not by `seq - 2 * age`).
- Add the rANS initial-state trick (05 section 5.9) and measure the end-of-stream cost.
- Check: 0 round-trip failures on the 8,842 real messages, on `fuzz.py` seeds 1 to 5, and in `sim_loss.py`; mean real bytes on 5min-test at most 110 (study: 139.6 without prediction, 106.1 ideal with it); the integer version of P2 within 2 bytes of the float version, or the float version kept with its determinism test of step 6.

### Step 2: one source of truth for the field descriptors (S)

Depends on: nothing (can run alongside step 1).

**Done on 5 November 2026** ([progress/step-02-field-descriptors.md](progress/step-02-field-descriptors.md)): `data/fields.json`, offsets and C types computed by the compiler (`tools/fieldinfo`); schema version 2 adds the C type of each field (bool, int, float, enum).

- Generate, from the arrays of `qcommon/msg.cpp` (`entityStateFields`, `playerStateFields`, `pilotPlayerStateFields`, `vehPlayerStateFields`, the four player-state arrays), a descriptor list: name, offset, width, float flag, array membership, plus a schema version and hash.
- Decide the fate of the runtime width overrides (`netf_overrides.txt`, `psf_overrides.txt`, 02 section 2.3). Decided: removed from the new protocol; `user*` fields are coded by C type.
- Check: the descriptors reproduce the tables extracted by `jkadec.py` (132, 137, 140, 69 entries); the Python tools read the same generated file.

### Step 3: state recorder in the server (S)

Depends on: step 2. **No longer needed before step 5**: client demos give the data (step 4). The recorder is still wanted later, to record states on this branch's server without a Huffman decoder and to feed the training of step 13.

- A server option that writes, per client and per message, the decoded content the codec needs: header, player state(s), entity list, baselines, server commands, gamestate, downloads, the chosen base and the send time. Simple binary format with the schema hash.
- It replaces the proxy capture of the study (which recorded bitstreams and needed a Huffman decoder). The proxy stays usable on the original 1.01 server.
- Check: replaying a recording through the legacy writer gives back the same bytes as the live server sent (or the same values, if byte identity is not possible because of server-side `-0.0`, 05 section 5.4).

### Step 4: demo corpus (S; the owner supplies the demos)

Depends on: nothing (decided 5 November 2026: client demos replace new recordings).

**Done on 5 November 2026** ([progress/step-04-demo-corpus.md](progress/step-04-demo-corpus.md)): 1,116 demos (148 hours, CTF, team FFA, siege, duel; 1.01 protocol; no JA+ or JAPRO) indexed; 60 training and 20 held-out demos (held out by map), first 5,400 messages each; round trip 0 failures after removing the 1.01 pilot rule from `codec2.py`. All demos stay in `data/demos/train/` as supplied; the sets are a column of `data/demos/selection.tsv`, not folders.

Why demos work: the client writes into a demo exactly the server message it received, after removing the netchan header (fragments reassembled) and the XOR, and before nothing else is changed (`CL_PacketEvent` and `CL_WriteDemoMessage`, `codemp/client/cl_main.cpp:2052-2104`, `:206`). The proxy captures of the study hold the same message taken on the server just before `SV_Netchan_Transmit`. Differences, all handled by the reader:

- a demo message ends with the `svc_EOF` the server appends after the proxy's capture point (remove it before measuring sizes);
- the first message of a demo is a gamestate rebuilt by the client (`CL_Record`) from its configstrings and baselines, not the one the server sent: use it for decoding, never for gamestate sizes;
- messages lost on the way are absent (the demo is the client's view, with the real base ages, losses and rate-delay flags of that player);
- recording starts at the first snapshot without a base (`demowaiting`), so a demo decodes from its start.

Work:

- A `.dm_26` reader in `tools/python/jkadec.py` (per message: `int32` sequence, `int32` length, the bytes; end marker `-1 -1`), and its use in `bdata.py` and `fullload.py` in place of the capture files.
- Validation as for the captures: every message must end within 1 to 8 bits of its length after `svc_EOF`; a demo written by another client that differs is rejected and listed.
- The owner supplies demos in `data/demos/train/` and `data/demos/holdout/` (the held-out set is chosen before any measurement and never used for training). `data/demos/` is not committed (`.gitignore`); a list file records each demo: map, mode, mod, number of players, `sv_fps` if known, client or server demo, protocol.
- Wanted: protocol 1.01 (`.dm_26`; 1.00 `.dm_25` was not checked); several humans per game; many maps; FFA, CTF, team FFA, duel, siege, race; base, JA+, JAPRO and other mods; both game-module behaviours (players as `TR_LINEAR_STOP` and as `TR_INTERPOLATE`, 03 section 3.12); server-side demos (`sv_autoDemo`) as well, which use the same format.
- Check: every demo of the list decodes with the validation above; the list file exists; train and held-out sets are separate.

### Step 5: re-measure on the new data, finish the model (S)

Depends on: steps 1 and 4. **Done** 5 November 2026: [progress/step-05-demo-measurements.md](progress/step-05-demo-measurements.md). One global table set, no age context; held-out ratio to the engine 0.445 on snapshots; `bench2.py` / `bench5.py` and the `TR_INTERPOLATE` check dropped.

- Re-run `bench2.py`, `bench5.py`, `run_pred.py` and the loss simulation on the demos: sizes per map, mode, mod and `sv_fps`; training on the train set, test on the held-out set. Compare with the study's figures (one map, bots).
- The demos carry real base ages: report sizes by base age as well, and compare with `sim_loss.py`.
- Check the elapsed-time rule for `TR_INTERPOLATE` players (P2 with the two snapshots' `serverTime`) on demos of servers whose game module sends players that way.
- Decide: one global table set or per-mode sets; age as a context or not (04 section 4.5).
- Check: documented mean / p95 / max on the held-out set per map and mode; if held-out sizes exceed training sizes by more than 15 %, widen the training data first.

### Step 6: the codec library in C++ (M)

Depends on: steps 2 and 5. **Done on 8 November 2026** ([progress/step-06-cpp-codec.md](progress/step-06-cpp-codec.md)): rANS and whole-message codec in `codemp/netcodec/`, the format in C++ only (held-out ratio 0.381 of the engine's bytes), faster than OpenJK's coder; robustness and completeness checks (determinism hashes on every compiler, synthetic and malformed-input tests, libFuzzer, per-message escape); the codec reads the engine's arrays in place; the engine header; the precision inventory and its decisions (part F).

- New directory `codemp/netcodec/` (decided 5 November 2026; not `shared/`, which holds code common to the single-player, JK2 and multiplayer trees), C++20 inside the library (`std::span` for buffers, C++ Core Guidelines I.13) with a plain C++ interface (a few functions and plain structures, no STL or exceptions in the public header; no C interface, since the engine and the forks that would reuse it are C++), no dependency on server or client; CMake builds it with MSVC, GCC and Clang; the trained tables are compiled in:

| unit | content | reference |
|---|---|---|
| `rans` | 32-bit byte-renormalising rANS, 12 to 15-bit probabilities, initial-state trick | ryg_rans (CC0); `codec.py` |
| `schema` | generated descriptors of step 2, pilot and vehicle subsets, arrays | step 2 output |
| `tables` | loader of trained tables and per-field modes, version and schema hash, uniform fallback | `codec.py` `Tables` |
| `fieldcodec` | absolute / delta classes, float kinds (zero, integral, angle index, other), prediction modes T, P2, A2; booleans by C type (changed = inverse, no value); `user*` fields by C type, not by their 1-bit legacy width (04 section 4.2) | `put_int`, `put_float`, `get_int`, `get_float` in `codec.py` |
| `msgcodec` | whole message: header, operations, snapshot, gamestate, commands, setgame, download, mapchange | `codec2.py` |
| `escape` | raw coding of an entity or a message and the rule that picks it (worst case, 04 section 4.8) | not in the study |
| `tests` | golden traces, synthetic fuzz, garbage-input fuzz, determinism | section 4 |

- Interfaces (shape only): `bytes = Encode(base*, header, ops, baselines, tables)`; `ops = Decode(bytes, lookupBase(delta) -> base*, baselines, tables)`, where `lookupBase` is called after the delta has been decoded (as `msg_get` in `codec2.py`). All bounds are checked while decoding (entity numbers below `MAX_GENTITIES`, field indices below the table length, string and download lengths, end of input); a failure drops the message.
- Check: with the same tables, the C++ encoder produces byte-identical output to the Python codec on all recordings; 0 mismatches on at least 100,000 synthetic messages; random and mutated bytes never crash under ASAN / UBSAN; angle decode and P2 prediction bit-identical on MSVC, GCC and Clang, x86 and x64, optimised and not, for all 65,536 indices and for the recorded positions; encode about 2 microseconds per snapshot on an optimised build (study: 1 to 2 µs for 300 symbols).
- Then (owner, 7 November 2026): an inventory of the places where the game code rounds networked values before the codec (`SnapVector`, `ANGLE2SHORT`, ...), each with what the client loses and what its removal would cost in bytes; the owner decides case by case ([progress/step-06-cpp-codec.md](progress/step-06-cpp-codec.md), part F).

### Step 7: integration in the engine (M)

Depends on: step 6.

**Done on 9 November 2026** ([progress/step-07-engine-integration.md](progress/step-07-engine-integration.md)): every server-to-client message coded by netcodec, demos, the precision decisions of step 6 part F, statistics commands; tested on the loopback, with simulated loss and with a GCC server on a Linux VPS played from Windows: no message the client could not decode, prediction misses of the same order as the reference build.

- Server: build every message with the codec in `SV_SendClientSnapshot`, `SV_SendClientGameState`, `SV_WriteDownloadToClient` and `SV_SendClientMapChange`; keep the base selection of `SV_WriteSnapshotToClient` for now (step 10 changes it).
- Client: decode in `CL_ParseServerMessage` into `clSnapshot_t`, `cl.parseEntities` and `cl.entityBaselines`, with the validity rules of `CL_ParseSnapshot` (base valid, matching message number, entities not overwritten) and the `%` sanitation of strings.
- Cover: bots (built but not sent), spectators, vehicles and pilots, downloads (UDP; HTTP downloads are separate), `svc_setgame`, `svc_mapchange`, and both demo systems: client demos and the server-side demos (`sv_autoDemo`, present in OpenJK) record the message stream, so the demo format changes with the protocol (old demos need the old reader or a converter; decide). Done 8 November 2026: server demos copy the messages sent (the codec's frames are reset when recording starts), client demos code each decoded message again against the demo's previous snapshot; each starts with a coded gamestate holding the last 127 server commands ([progress/step-07-engine-integration.md](progress/step-07-engine-integration.md), part 3).
- New protocol number; refuse a mismatch at the `connect` handshake. Out-of-band messages keep their own format. (Owner, 7 and 8 November 2026: the protocol number, the handshake and any coexistence of protocols 26 and 27 are decided at the end of the plan; until then the codec simply replaces the server-to-client format in the branch's build, with no switch, and the reference build is the comparison.)
- The netchan obfuscation XOR is dropped on server-to-client messages (decided 8 November 2026; to revisit in step 12).
- A debug cvar on the server also codes each message with the legacy `msg.cpp` and reports the byte ratio (works with bots, where step 5's demos are not comparable).
- Server commands are coded against earlier commands (step 6, part 2 B): server and client give the codec their command history (`CommandHistory`), and both keep the last 256 commands (the engine keeps 128 today, `MAX_RELIABLE_COMMANDS`).
- Statistics cvars or commands: bytes per snapshot, symbols, escapes, base age.
- Check: a normal game plays with no visible difference; `cg_showmiss` shows no new prediction misses; the statistics match step 5 within 5 %; client and server demos record and play back.

### Step 8: client-to-server messages by the codec (M)

Depends on: step 7. Decided 9 November 2026 (owner): the client-to-server direction is coded by netcodec too; the steps after it were renumbered (the former steps 8 to 13 are 9 to 14). **Done on 10 November 2026** ([progress/step-08-client-to-server.md](progress/step-08-client-to-server.md)): the recorder (`sv_recordClientPackets`) and the offline reader (`netcodec_bench_client`) are written; the recordings are made on a server reached over the Internet, since on the loopback and a LAN the client sends one user command per packet; six sessions recorded. The format is in the library (part 2, 10 November 2026): 9.89 bytes per packet against 38.58 for the legacy format on the held-out session (0.256), with `serverId` added to the server messages so that the client codes its own against the acknowledged message; encoding 0.34 to 0.40 and decoding 0.74 to 0.86 of the legacy time (warm) on clang, MSVC, GCC 12 and Clang 14. The integration in the engine is done (part 3, 10 November 2026: the server keeps the bases of its last 256 messages per client, the ping is measured in real time, the XOR and the user-command key are removed, the recorder too; checked in game on the loopback and on a VPS with simulated loss, about 0.22 of the legacy bytes, no packet the server could not decode); the robustness tests are done too (part 4: 400,000 malformed packets on every build, a libFuzzer target, client command texts limited to 1,023 characters); validated on every compiler.

Today each client packet (about 60 per second, `CL_WritePacket`) carries the server id, the acknowledge of the last server message and of the last server command, the client commands not yet acknowledged (text), and the user commands (time, view angles, buttons, movement, weapon) coded against the previous one with a "key" XOR, all through Huffman, then the netchan XOR (`CL_Netchan_Encode`). None of it was studied or measured: the demos do not hold it.

1. Measure and record: a server debug cvar writes the decoded client packets (legacy size, acknowledges, client commands, user commands, arrival time) to a file; the owner plays a few games with it (bots send no packets, their commands are made inside the server). This gives the legacy cost and the training data.
2. The format in the library: a client message in `engine.h`, contexts for the user-command fields, trained tables. Each packet decodes alone (the server does not acknowledge client packets): the bases are the previous command of the same packet and the snapshot the client acknowledges (its time, its angles). Client commands are coded against earlier client commands, as server commands are.
3. Integration: `CL_WritePacket` and `SV_ExecuteClientMessage` (`SV_UserMove`, `SV_ClientCommand`); the user-command key and the netchan XOR are dropped in this direction as well (to revisit in step 12); `sv_netcodecCompare` covers it.
4. Robustness first: the server decodes data from anyone, so the malformed-input test and a fuzzer target cover this decoder before it is used.
- Out of scope here: connectionless messages (`connect`, `getinfo`, `getstatus`, the master server), decided with the protocol number at the end of the plan.
- Check: a normal game plays with no visible difference (movement, prediction, chat, votes); round trip and determinism tests on the recordings; the byte ratio against the legacy format is documented.

### Step 9: safety and limits (S)

Depends on: steps 7 and 8.

**Done on 10 November 2026** ([progress/step-09-safety-limits.md](progress/step-09-safety-limits.md)): the escape path and the worst case were done in step 6; a snapshot too large is sent with the entities nearest to the player, a gamestate too large drops the client with the reason (the client now handles a `disconnect` before cgame runs), `sv_netcodecMaxBytes` (debug) tests both; checked in game; no CI (owner: the validations stay on the VMs).

- Escape path: done in step 6 per message (owner, 7 November 2026), not per entity: a message coded larger than 1 KB is also coded with its fields raw and the smaller is sent, which bounds the worst case to about the raw layout ([progress/step-06-cpp-codec.md](progress/step-06-cpp-codec.md), part D).
- Size cap: on overflow, retry with fewer entities or a full-state message; never drop a snapshot silently (today the engine clears the message and prints a warning, `server/sv_snapshot.cpp:852`).
- Fuzz corpus and sanitizer builds in CI.
- Check: the synthetic worst case (every field random, 1,000 entities) stays within the raw layout plus a small constant and decodes.

### Step 10: acknowledgements, base selection, reliable data (M)

Depends on: steps 7 and 8. The options and the decision procedure are in [07](study/07-transport-study.md).

**Done on 10 November 2026** ([progress/step-10-acks-reliable.md](progress/step-10-acks-reliable.md)), scoped from the measurements of steps 7 and 8 (owner): the netchan improved in place (07 section 7.7 answered); server commands not repeated while their message is in flight (up to 4 % fewer bytes on real games); the gamestate resent about a round trip after a loss. The items below that are not done, and why (the user-command resend in particular, too late for the server, becomes adaptive redundancy in step 11), are in the step document. Checked in game, locally and on the VPS with simulated loss.

- Acknowledgement record in every client packet (bitfield of the last 32 or more messages); the server uses the newest acknowledged snapshot as base.
- Gamestate, server commands and client commands on a reliable path with its own fast resend, instead of the 1,000 ms gamestate resend wait; snapshots never wait behind reliable data. Measured on the held-out demos (step 6): every server command is sent 2.59 times on average (repeated in every message until acknowledged), about 60 % of the command bytes.
- Client input: every `usercmd` not yet acknowledged is resent in each packet, up to a cap; removes `cl_packetdup`.
- Snapshot history sized from the supported RTT; replaces the fixed `PACKET_BACKUP`.
- UDP downloads: since step 7 the codec carries one 2,048-byte block per message. The legacy format sent as many as the rate allowed in one snapshot interval (up to the 8-block window): also one block at the branch's defaults (`sv_clientRate` 25000, 50 ms), but 3 on a LAN (rate 100000), so downloads are slower only from a rate of about 41,000 bytes per second. To fix (owner, 8 November 2026), possibly with a system of their own: several blocks per message, or a separate download channel next to the reliable path above.
- Check: simulated loss, latency and jitter (as `sim_loss.py`, extended to burst loss and lost acknowledgements) and two-machine tests; 0 decode failures; base age equal to RTT / frame time plus at most one frame.

### Step 11: settings computed at run time (M)

Depends on: step 10.

**Done on 11 November 2026** ([progress/step-11-settings.md](progress/step-11-settings.md)): fixed rates, a packet budget enforced by the server, the input redundancy fixed after measuring, the interpolation delay computed from the measured arrivals; checked in game locally and on the VPS with simulated loss. Step 14 changed three of its values: the fixed rate is 200,000 bytes per second, the client sends one packet per tick (100 per second), and the interpolation delay has a new rule (part 4e).

**Revised on 10 November 2026** after a survey of what recent games do ([surveys/network-settings.md](surveys/network-settings.md), which holds the options, their costs, the measurements and the owner's choice for each point): fixed values instead of a bandwidth estimate, which recent competitive games do not use and which this branch's 2 to 9 KB per second do not need.

- Part 1 (done): `svnetstats` measures the client packets lost and their runs, the user commands per packet, and the change of the snapshot send time against the game clock; measured on the VPS: no client packet lost, a repeated command costs 1.5 to 1.9 bytes, the send time follows `serverTime` within about 0.1 ms on average, so no time stamp is added (point 6).
- Part 2 (done): a snapshot every server frame at a fixed rate of 100,000 bytes per second; up to 125 client packets per second with the unused interval carried over, enforced by the server; the user commands of 2 packets repeated; timeouts of 60 s while loading and 30 s in game. Removes `rate`, `snaps`, `cl_maxpackets`, `cl_lanForcePackets`, `cl_packetdup`, `cl_timeout`, `sv_maxRate`, `sv_minRate`, `sv_ratePolicy`, `sv_clientRate`, `sv_lanForceRate`, `sv_snapsMin`, `sv_snapsMax`, `sv_snapsPolicy`, `sv_timeout`, `sv_zombietime`.
- Part 3 (done): the interpolation delay computed from the measured jitter of the snapshot arrivals and from the loss, the client's clock following a jump of the server's (`map_restart`) at once (the network settings survey, point 5). Removes `cl_timeNudge` (`cl_timenudgeDuration` does not exist in this tree). The user choice of `cg_smoothClients` moves to step 14.
- Debug cvars force only the computed value (the interpolation delay, `CVAR_CHEAT`); fixed values have no cvar.
- Check: the measured packet rates and losses in `svnetstats`, the interpolation statistics in `netstats`, in game locally and on the VPS with `net_dropsim`; no setting is needed on any link.

### Step 12: transport security and library decision (M to L, optional)

Depends on: steps 10 and 11, and the owner's answers in 07 section 7.7.

- Decide on authenticated encryption and the connection model; evaluate an in-place AEAD layer, netcode + reliable, GameNetworkingSockets against the requirements of 07 section 7.3, on a prototype with the real codec payloads.
- The legacy netchan obfuscation (`SV_Netchan_Encode` / `CL_Netchan_Decode`: the payload XORed with a key made of the challenge, the sequence and the last command texts) is removed from server-to-client messages in step 7 (owner, 8 November 2026: its key was chosen from the reliable acknowledge read in clear at the start of the message, which the codec now codes). The client-to-server direction loses it, and the user-command key, in step 8. Decide here whether to restore an equivalent, replace it with the encryption above, or leave it out.
- UDP downloads (placed here on 14 November 2026): one block per message since step 7, about 3 times slower on a LAN than the legacy format at 20 messages per second (at the 100 Hz tick of step 14, computed from the code on 14 November 2026 and not measured: a block every two ticks, about 100 KB per second, the legacy format's LAN speed; to measure before deciding); several blocks per message, or a download channel of their own, decided with the transport.
- Check: the decision is recorded with measured overhead, CPU at 64 clients, behaviour under loss, and licence review.

### Step 13: live measurement and tuning (M)

Depends on: steps 7 to 12.

- Playtest servers with the recorder: bytes per client per second, snapshots per second, prediction misses, CPU per client, escape rate.
- Retrain the tables from the new recordings; size regression check.
- With the retraining (owner's choice of 12 November 2026, done with this step): `saberAttackChainCount` on 5 bits instead of 4 (capped at 16, so 16 reached the client as 0: step 14 audit, CG5); its contexts change alphabet, so the schema and the tables are regenerated.
- Also with the retraining, since the format and the tables change anyway (placed here on 14 November 2026): the shorter rANS final state (an estimated 15 % fewer bytes per client packet and 2 % per server message, both directions); the optional format items of step 6 part 2 B (B.3, B.5 to B.7); the symbol of the base's distance, trained on bases at most 29 messages back while the window is 128 since step 14 part 5.
- Final performance pass, after the last format change (placed here on 14 November 2026): GCC 16.2 encoding about 9 % slower than GCC 12; MSVC decoding 2 % slower warm since step 14 part 4c, its origin not separated; the library built with clang-cl on Windows (MSVC cannot align code); 32-bit (MSVC Win32 encodes 2.1 times slower than x64): kept or dropped.
- The analysis tools in C++ rather than Python (owner, 9 November 2026), with the recorder of this step.

### Step 14: beyond the codec (L)

**Started on 11 November 2026.** After a survey of recent games and measurements of the server's CPU time at 20, 40 and 125 Hz ([surveys/tick-and-lag-compensation.md](surveys/tick-and-lag-compensation.md)), the owner chose the modern model: the client and the server share a **fixed simulation tick of 125 Hz** (8 ms), movement runs once per tick instead of once per user command, the client sends one input per tick and runs ahead of the server with a slight time dilation to keep the server's input buffer small, the other players are interpolated, and **lag compensation rewinds by tick**; the fixed rate to each client is **200,000 bytes per second**. The design follows recent games, not the JKA mods, done cleanly and optimised even if it means starting again from zero; every defect found on the way is fixed (among them the server commands repeated without loss at short intervals, the tick survey, section 7). The design and the inventory of the code to change (engine client and server, shared movement, game module, cgame) are in [progress/step-14-fixed-tick.md](progress/step-14-fixed-tick.md). The model covers several items below at once: `sv_fps`, the fixed physics step, `cg_smoothClients`, lag compensation and part of the prediction misses.

**Revised on 11 November 2026, evening (owner): the tick is 100 Hz**, so that every 20 Hz instant is a tick, and the game's logic (saber, force powers, thinks, pain, missiles, items, bots' AI, level rules) keeps its 20 Hz pace on those instants with OpenJK's code, so the game plays exactly as at `sv_fps` 20; per-tick tuning of the saber had failed (its reactions need a 20 Hz frame's worth of blade movement). Part 2 (game timing) is done and proven with scripted bot scenarios: the same module at `sv_fps` 20 and 100 gives the same counts for the saber, the force powers and the weapons, on Windows (clang) and on Linux (GCC, VPS), with a bench made deterministic (12 November 2026; [progress/step-14-fixed-tick.md](progress/step-14-fixed-tick.md) section 9, working notes in [progress/step-14-audit-100hz.md](progress/step-14-audit-100hz.md)). Linux and Windows aligned before part 3 (12 November 2026, owner): math functions through one entry point on LLVM libc's correctly rounded functions ([surveys/deterministic-math.md](surveys/deterministic-math.md)) and random numbers through one entry point on xoshiro128++ ([surveys/random-numbers.md](surveys/random-numbers.md)); the 40 scenarios give the same statistics with clang, MSVC and GCC on Windows and Linux, recorded as golden counts. Owner's decisions on the audit items (12 November 2026, that document's section 10): movers on the 20 Hz instants and `PM_irand_timesync` fixed (done), the dedicated server's busy wait removed outside Windows (done), packet rate constants and the interpolation redesign with part 4, B1 and B2 with the snapshot store (part 5).

**State on 14 November 2026**: parts 1 to 7 and 10 are done (the game's timing, the tick loop, one command per player and tick with the two clocks, the snapshot store with a window of 128 messages, the other players interpolated only, the server's time per tick cut by about a third, the review of the prediction and the commands made at the tick's start). Part 8 (lag compensation for the hitscan shots) was dropped by the owner on 14 November 2026: the shots stay judged with the positions the server holds, as in the base game. Left: part 9 (cgame: the lagometer, the scoreboard's freeze) and part 11 (what the audits and the sessions left). What the branch does now, with the delays before and after: [CHANGES.md](CHANGES.md).

The items as listed before the survey (05 section 5.7):

- per-client priority and bandwidth budget, distance-based update rates, interest culling;
- an audit of the client's prediction and interpolation code (`cg_predict.c`, `cg_ents.c`, time synchronisation in `CL_AdjustTimeDelta`), now that compatibility does not constrain it; with it, the choice of `cg_smoothClients`, moved here from step 11 (owner, 10 November 2026): whether the other players follow the trajectory the server sends, carried forward to the displayed time (1, extrapolation), or are interpolated between snapshots (0);
- lag compensation (the server rewinds the world to the time the shooter saw, as Valorant, Overwatch and Apex Legends do; the base game has none), which `cl_timeNudge` never replaced (network-settings.md, point 9): a negative `cl_timeNudge` showed guessed positions while the server judged with its own, whereas lag compensation keeps real positions on the screen and makes the server judge with them. Its cost: a target can be hit after reaching cover on its own screen, so games cap the rewind (Overwatch stops predicting hits above about 220 ms of round trip, from a summary of its 2017 talk). In JKA most weapons fire projectiles (a separate question: when the projectile appears); hitscan: the disruptor, and the concussion rifle's alternate fire (to check); the saber and the force powers that trace (lightning, grip) to examine; dropped on 14 November 2026 (owner);
- `sv_fps` 40 with the saber made to work at that rate (step 11, point 2: the saber runs once per server frame, `game/g_main.c:3317`);
- the prediction misses when crouching or jumping while attacking (seen during step 8, the base game's too);
- the freeze when the scoreboard (Tab) first shows players whose models are not loaded yet (cgame loads them then; seen during step 11): load them earlier;
- a fixed physics step (`pmove_*`, 04 section 4.10), a gameplay decision.

Placed on 14 November 2026 (owner: every open point gets a place; the parts are in [progress/step-14-fixed-tick.md](progress/step-14-fixed-tick.md) section 5): the cost of the statistics commands with part 7 (server CPU); the prediction of the local player, the saber first, with the animation misses and the misses when crouching or jumping while attacking, as part 10; what the audits and the sessions left (the demos' size at 100 messages per second, the dead code reported, a long client hitch, the sound at 333 frames per second, priority and culling) as part 11, at the end of the step.

### Step 15: a new platform layer on SDL 3 (L)

Depends on: step 14 (it touches the input that step 14 settles). Added on 14 November 2026 (owner). The survey, the design and the decisions to take are in [surveys/platform-layer.md](surveys/platform-layer.md); nothing is implemented.

The client's platform code (window and OpenGL context, input, sound device, joystick: `shared/sdl/`, SDL 2.0.12 of 2020, inherited from ioquake3 and adapted from SDL 1.2) is replaced by a layer written from zero on SDL 3: the same features for the player, no workaround, no fallback for hardware that no longer exists, the present practices, as few settings as possible. Not a port: the present files are kept only for the knowledge they hold (the key translation, the character set). The dedicated server is not concerned (it does not use SDL).

- A module of its own, `codemp/platform/`, in C++20 by the C++ Core Guidelines, with a public header the engine can include; for the multiplayer client only. `shared/sdl/` stays for the single-player and Jedi Outcast trees, and stays selectable by a CMake option for the multiplayer client until the new layer is accepted.
- SDL 3 at a pinned release, built from source with the project (it is in neither Debian 12 nor Ubuntu 24.04) and shipped with the client.
- Parts, in this order, each built and checked on Windows and Linux before the next:
  1. the build and the window with its OpenGL context: three states (window, borderless fullscreen, exclusive fullscreen with one of the screen's modes), the switch without a video restart, several screens, the pixel density, one request for the context and one retry;
  2. the keyboard and the mouse: events with their time, the keys by their place (decision P1), text only while a text field is open, relative mode without warp; read at the frames and at the ticks (step 14 part 10);
  3. the sound device: the mixer asked for samples by the device, the system's default device followed; with it the loudness that depends on the frame rate (step 14 part 11);
  4. the gamma in the renderer, since SDL 3 has no gamma ramp: one pass at the end of the frame (decision P2);
  5. the gamepad through SDL's gamepad API;
  6. optional: the main loop's wait on a high-resolution timer instead of its busy loop.
- Check: on Windows 11 and Linux (X11 and Wayland), the feel of the mouse and the keys unchanged (the same turn for the same mouse movement, the binds, the keypad, the console key on QWERTY and AZERTY, text with accents); `netstats`' input margin no higher at 100, 125 and 333 frames per second; the display's three states, two screens, alt-tab; the same brightness on screenshots; the sound following the default device; no setting needed.

## 3. Order at a glance

Order of work (owner, 10 November 2026): step 14 comes right after step 11, before steps 12 (encryption, not wanted for now) and 13 (retraining on recordings of this build).

| step | what | depends on | effort |
|---|---|---|---|
| 1 | prediction modes and rANS trick in the Python codec | none | S |
| 2 | field descriptors from one source | none | S |
| 3 | state recorder (later; not needed before step 5) | 2 | S |
| 4 | demo corpus: `.dm_26` reader, demos from the owner | none | S |
| 5 | re-measure, finish the model | 1, 4 | S |
| 6 | C++ codec library | 2, 5 | M |
| 7 | engine integration, demos | 6 | M |
| 8 | client-to-server messages by the codec | 7 | M |
| 9 | safety and limits | 7, 8 | S |
| 10 | acknowledgements, reliable data | 7, 8 | M |
| 11 | settings computed at run time | 10 | M |
| 12 | transport security decision | 10, 11 | M to L |
| 13 | live measurement | 7 to 12 | M |
| 14 | beyond the codec (after step 11) | 11 | L |
| 15 | a new platform layer on SDL 3, from zero | 14 | L |

## 4. Test matrix

| test | method | when |
|---|---|---|
| Golden round trip | decode recordings, re-encode, decode again, compare field by field; compare with the values of the original decoder | every build |
| Python and C++ identity | same tables, same input, same bytes | every codec change |
| Synthetic fuzz | `fuzz.py` generator ported to C++ (any base age, vehicle and pilot, download, extreme values) | every build |
| Garbage fuzz | random and mutated byte streams into the decoder under ASAN / UBSAN | nightly |
| Determinism | MSVC, GCC, Clang, x86 and x64, optimised and not: all angle indices, P2 predictions on recorded positions | every build |
| Platforms | build and run the tests above on Linux (GCC and Clang); a Windows client against a Linux server and the reverse | every build from step 6 (decided 5 November 2026: Linux is built and tested alongside Windows) |
| Size regression | evaluator on held-out recordings; fail if the mean grows by more than 3 % | table or schema change |
| Loss, latency, jitter | simulator over recordings: loss 0 to 20 % with bursts, RTT 0 to 400 ms, jitter, lost acknowledgements | transport change |
| Worst case | adversarial snapshots stay within the cap and decode | every build |
| CPU | C benchmark over recordings on the target machine | codec change |
| Live | two or more machines, several humans, loss emulation | step 13 |

## 5. Risks

| risk | consequence | mitigation |
|---|---|---|
| Training data too narrow | real packets larger than measured | step 4 demo corpus, held-out set, escape path |
| Table drift when content or mods change | larger packets, never wrong ones | versioned tables, uniform fallback, retrain in the release procedure |
| P2 prediction computed differently on client and server | wrong positions on the client | integer version, or fixed float arithmetic with the determinism test |
| A2 base's base not available on the client | decode failure | the server uses the base's real delta source; fall back to plain delta when the client cannot have it |
| Server-side `-0.0` and similar hidden bit patterns | the study treats `-0.0` and `+0.0` as equal because the engine does | keep the equality rule explicit in tests |
| Message growth under unusual states | overflow, fragmentation | escape path, cap, fallback |
| Acknowledgement redesign breaks base selection | corrupted deltas | round-trip tests under simulated loss |
| Bandwidth estimation oscillates or is too slow | stutter, delayed snapshots | simulator with changing links before live tests; debug cvars to force values |
| Demo format change | old demos unreadable | keep a legacy reader or write a converter |
| Scope creep into priority, culling, prediction audit | schedule | keep step 14 separate |

## 6. Targets taken from the study

Targets for the study's content (one map, bots, 20 snapshots per second). Re-measured in step 5 on the demos: these targets hold for the study's content only; on the held-out demos the codec gives 0.445 of the engine size for snapshots (69.0 bytes against 155.1) and 0.483 for all messages ([progress/step-05-demo-measurements.md](progress/step-05-demo-measurements.md)):

- snapshot payload mean at most 110 bytes against 337.6 today, p95 about 190, maximum below 450 for ordinary snapshots;
- gamestate about 4 KB instead of 7 KB on the study map;
- encoding and decoding each below 0.3 % of a core for 64 clients at 20 snapshots per second;
- zero decode mismatches in every test of section 4.
