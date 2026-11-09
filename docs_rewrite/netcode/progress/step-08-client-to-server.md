# Step 8: client-to-server messages by the codec

Status: **in progress** (started 9 November 2026). Plan: [PLAN.md](../PLAN.md), step 8.

## Why recordings, and where

The demos hold only what the server sends to the client, never the client's packets (user commands, acknowledges, client commands), so the client-to-server direction has no corpus yet. The server records them instead, and an offline tool measures them, as the demo traces and `netcodec_bench_size` / `netcodec_bench_codec` did for the other direction.

The recordings come from a dedicated server reached over the Internet (owner, 9 November 2026: the VPS), not from a local game: on the loopback the client sends a packet every frame with one user command (`CL_ReadyToSendPacket`, `codemp/client/cl_input.cpp`), and on a LAN too while `cl_lanForcePackets` is 1, whereas over the Internet `cl_maxpackets` (63) caps the packets, so each carries several commands plus the previous packet's (`cl_packetdup`), with a real acknowledge delay. Bots send no packets: only the human players' packets are recorded; bots are there for combat and varied input.

Wanted in the corpus: several separate sessions (one is kept out of training), several `com_maxfps` (60, 125, 333: the number of user commands per packet changes), combat and movement, some chat and commands (`team`, votes).

## Parts

1. **Recorder and reader (done 9 November 2026; corpus of six sessions recorded, see "Results").**
   - `sv_recordClientPackets 1` (debug, server, `codemp/server/sv_netcodec.cpp`): each client's packets are written as `SV_ExecuteClientMessage` reads them, that is after the netchan header and with the obfuscation XOR already removed, to `clientpackets/client<n>_<date>.jkcp` under the home path, one file per connection (closed when the client drops, when the cvar is set back to 0 or when the server shuts down). File: a header (`JKCP`, version 1, record size), then per packet a record and the packet's bytes. The record holds `sv.time` and `Sys_Milliseconds()` at arrival, the user-command key the server derives for the packet (`SV_UserMove`), so that the packet decodes offline, and the acknowledged snapshot's server time and the player-state fields that match user-command fields (`commandTime`, `viewangles`, `delta_angles`, `weapon`, `fd.forcePowerSelected`, `stats[STAT_HOLDABLE_ITEM]`; server time -1 when the server no longer holds that snapshot): the bases a coded format can use besides the previous command.
   - `netcodec_bench_client` (`codemp/netcodec/bench/bench_client.cpp`): decodes every packet as the server does, with OpenJK's own `msg.cpp` (glue in `bench/legacy/legacy_codec.cpp`: `DecodeClientPacket`, following `SV_ExecuteClientMessage`, `SV_ClientCommand`, `SV_UserMove`), codes it again as the client does (`EncodeClientPacket`, following `CL_WritePacket` and the `clc_EOF` of `CL_Netchan_Transmit`) and checks that the bytes holding bits are identical; it prints, per file and in all, the packets per second, the bytes per packet and per second, how they split between the acknowledges, the client commands and the user commands, the user commands per packet (and how many are new rather than repeated from the previous packets), the client commands, and how often the acknowledged snapshot was known. It exits with 1 if a packet does not decode or codes again differently.
2. **Format in the library (done 10 November 2026; the engine interface comes with part 3; times to measure on a quiet machine).** Designed from the measurements of part 1 (`tools/python/client_fields.py`, candidate "planned format": 8.15 ideal bytes per packet on the held-out session) and the decisions of 9 November 2026 ([README.md](README.md), "Decisions"):
   - `src/client_codec.cpp`, interface in `include/netcodec/codec.h` (`ClientPacket`, `ClientBase`, `ClientPacketEncoder`, `DecodeClientPacket`, `ClientBaseLookup`, `CountClientPacket`). A packet decodes alone. In order: the acknowledged message as its low 8 bits (the server takes the nearest number at or below its last message sent); a flag saying whether the user commands are coded against the acknowledged message, which both sides then hold (the client codes without it when it no longer holds it or its last message is too old); with it, `serverId` as "the same as that message's" (else raw) and the server command acknowledge against that message's `cmdSeq`, without it both raw; the client commands (count, first sequence number against the one after that message's `ack`, texts with the server command text contexts); the move operation; the user-command count; per user command, the time (the first against the base player state's `commandTime`, the others as a step given the previous step, escaped from 63 ms), the three view angles as 16-bit differences from the previous command (the first from the base's view angle index less `delta_angles`) given the previous difference, and nine bytes (buttons low and high, weapon, forcesel, invensel, generic_cmd, forwardmove, rightmove, upmove) given the previous command's value, with separate tables for the first command (based on the snapshot's weapon, selected force power and holdable item). As in the legacy format the angles and the buttons travel as 16 bits; the other fields are exact (the legacy reader turned a move of -128 into -127).
   - The server-to-client messages now carry `serverId` (`Message::serverId`, `HeaderRecord::serverId`, `engine::ServerMessage::serverId`, `engine::Record::serverId`, `engine::ClientMessage::serverId`; the server sets `sv.serverId`, client demos copy the decoded one): one flag symbol against the base, raw when it changes or without a base. The server tables were retrained: only the new context differs (`jka_tables.inc`, 1,296 trained contexts); round-trip hash `0xB752DA538DCD4AA6`, synthetic `0x88B36A2A82916952`.
   - Contexts appended after the server ones (`src/contexts.h`, `ClientScalar`, `ClientStep`, `ClientAngle`, `ClientField`), so that the server context numbers do not move; client tables in their own generated file, `src/generated/jka_client_tables.inc` (143 trained contexts, 19,978 frequencies), written by `netcodec_train_client` (`tools/train_client.cpp`; `tools/write_tables.h` is now shared with `netcodec_train`) from the client traces of sessions A to E. The validation scripts check that the x86 client trainer writes the committed table unchanged.
   - Client traces (`test/client_trace.h`): `netcodec_bench_client --trace` writes the decoded packets with their bases (`data/traces/client_train.bin` for A to E, `client_holdout.bin` for F; not committed). The recording holds the acknowledged snapshot's fields but not its record: its `cmdSeq`, `ack` and `serverId` are approximated (the previous packet's `reliableAck` and `serverId`, the last client command sequence before the packet); exact in the engine, they only weigh on rare symbols.
   - Tests (`test/test_client.cpp`): `netcodec.client_synthetic` (100,000 random packets on every path: with and without base, serverId changed, client commands, steps beyond the escape, any angle and byte; hash `0xCFE7E7FEB2FB9F06`) and `netcodec.client_roundtrip` (each traced packet against its base when the recording had it, then again without a base: 163,578 codings; hash `0xC371DB108DA14F6D`). Same hashes with clang x64 and x86; clang-tidy clean.
   - The engine checks the offsets the codec reads (`playerState_t` and `usercmd_t`, `codemp/server/sv_netcodec.cpp`).
3. **Integration**: `CL_WritePacket`, `SV_ExecuteClientMessage`; the user-command key and the netchan XOR dropped in this direction; `sv_netcodecCompare` covers it; the ping measured with the real time at sending and at the first acknowledge (decided 9 November 2026).
4. **Robustness first**: malformed-input test and fuzzer target on this decoder before it is used.

## How to record (owner)

Server (VPS, GCC build of the branch from the Debian VM), with the usual command line plus `+set sv_recordClientPackets 1`; the files appear in `<fs_homepath>/base/clientpackets/`. Play over the Internet with bots; set `sv_recordClientPackets 0` (or quit) to close the files, then copy them to `docs_rewrite/netcode/data/clientpackets/` (never committed: `*.jkcp` is in `.gitignore`, the packets hold player names and chat). Offline, from the repository root on this machine:

```
build-netcodec/mingw-clang-release/bench/netcodec_bench_client.exe docs_rewrite/netcode/data/clientpackets/*.jkcp
```

## Results

- Validation recording, owner, 9 November 2026: GCC dedicated server on the VPS, MSVC client over the Internet, `com_maxfps` 125, `cl_maxpackets` 100, bots, 3 minutes. `netcodec_bench_client`: 10,960 packets in 179 s (61.2 per second), 0 not decoded, 0 coded again differently. 37.64 bytes per packet in the legacy format (2,303 bytes per second, before the 28 bytes of UDP and IP and the netchan header of each packet): the three acknowledges 5.79, the client commands 1.00 (7 commands in 18 packets, `userinfo` among them), the user commands and the final `clc_EOF` 30.28, unused bits of the last byte 0.57. User commands: 4.00 per packet, of which 2.00 new: half are the previous packet's, sent again (`cl_packetdup` 1). The acknowledged snapshot was known for every packet.
- Corpus, owner, 9 November 2026, same setup (VPS, Internet, bots, team FFA); from session B on, `devmap` with `give all` and `g_weapondisable 0` (force powers were not available in any session). Files in `data/clientpackets/`, all decoded and coded again identically:

  | session | file | settings | content | packets | seconds | bytes per packet | user commands per packet (new) | set |
  |---|---|---|---|---|---|---|---|---|
  | A | `client4_20261109-205452` | 125 fps, `cl_maxpackets` 100 | validation, ordinary play | 10,960 | 179 | 37.6 | 4 (2) | training |
  | B | `client2_20261109-211032` | 125 fps, 100 | combat with saber and weapons | 11,824 | 192 | 38.6 | 4 (2) | training |
  | C | `client4_20261109-211741` | 60 fps, 100 | the same | 17,336 | 279 | 32.3 | 2 (1) | training |
  | D | `client4_20261109-212333` | 333 fps, 63 | the same, a round end | 18,821 | 284 | 55.0 | 10 (5) | training |
  | E | `client4_20261109-213019` | 125 fps, 100 | chat, spectator and follow, team change, vote, weapon and item selection | 11,665 | 188 | 37.0 | 4 (2) | training |
  | F | `client2_20261109-213533` | 125 fps, 100 | ordinary play on another map (`mp/ffa3`) | 11,183 | 181 | 38.6 | 4 (2) | held out |

  All: 81,789 packets in 1,303 s, 40.7 bytes per packet (acknowledges 7.8, client commands 1.0, user commands and end 31.4, unused bits 0.6), 2,555 bytes per second. `cl_maxpackets` 63 gives 66 packets per second, not 63: the interval is 1000 / 63 = 15 ms in integer division.
- Legacy coding time on the six sessions (clang, this machine, `netcodec_bench_client`; cold: the fastest of 3 passes over every packet, warm: the fastest of 5 codings of each packet, as `netcodec_bench_codec`): encoding 1,181 ns per packet (warm 557), decoding 435 ns (warm 304). A server with 64 players at 63 packets per second each spends about 0.2 % of a core decoding them: the time matters little; the comparison with netcodec comes with part 2.
- Ideal cost by field (9 November 2026; `tools/python/client_fields.py` on the dump of `netcodec_bench_client --dump`, in `data/traces/clientpackets.txt`): static models per context trained on sessions A to E, evaluated on them and on the held-out session F; cost `-log2 p`, with an escape and raw bits for symbols the training never saw; wide integers (times, angle deltas) as a bucket (zero, or sign and bit length) plus the bits below the leading one. Bytes per packet:

  | part | candidate | training | held out |
  |---|---|---|---|
  | acknowledges | legacy (Huffman of three 32-bit values) | 7.78 | 7.68 |
  | | `serverId` "unchanged" flag, `messageAck` modulo 32 raw (5 bits; the server knows the window of 32 messages it can still use), `reliableAck` against the previous packet (an approximation of a base both sides hold: the commands the acknowledged message carried) | 0.64 | 0.64 |
  | | for reference, not robust to losses: both acknowledges against the previous packet | 0.13 | 0.13 |
  | user commands | legacy (count, deltas against the previous command with the key XOR, Huffman) | 31.71 | 29.29 |
  | | first command absolute as in the legacy format, then deltas, a model per field | 13.75 | 13.45 |
  | | first command against the acknowledged snapshot (time against its server time, angles against its view angles less `delta_angles`), then deltas | 8.55 | 8.21 |
  | | the same, first time against the snapshot's `commandTime` | 8.49 | 8.13 |
  | | against the snapshot, time step in the context of the previous step, angle delta in the context of the previous delta's bucket | 7.31 | **7.07** |
  | | against the snapshot, angles predicted to second order | 7.86 | 7.49 |
  | client commands | legacy (Huffman) | 0.97 | 1.05 |
  | | count, then the text with an order-1 byte model (sequence numbers implied) | 0.03 | 0.11 |

  Best held-out total: about 7.8 ideal bytes per packet against 38.0 for the legacy format (0.21), before the end cost of a real rANS stream on such short messages (to measure with the real coder). The training and held-out figures are close, but all sessions come from one player. Half the user commands are copies from the previous packet (`cl_packetdup`); they are counted in both formats (plan step 10 decides their resending).
- Real sizes of the format (part 2, 10 November 2026, `netcodec_bench_client`, every packet coded and decoded back, 0 failures):

  | session | legacy bytes per packet | netcodec | ratio |
  |---|---|---|---|
  | A to E (training) | 41.02 | 10.10 | 0.246 |
  | F (held out) | 38.58 | **9.89** | **0.256** |
  | D alone (333 fps, 10 user commands per packet) | 55.04 | 12.43 | 0.226 |
  | C alone (60 fps, 2 per packet) | 32.27 | 8.21 | 0.254 |

  The difference with the ideal 8.15 bytes is mostly the rANS final state, 4 bytes per message of which about 1.8 are overhead (step 6), which weighs on packets this short.
- Sizes and coding times, legacy (`msg.cpp`) then netcodec (10 November 2026, commit `d737012ce`, `netcodec_bench_client` on the six sessions, 81,789 packets, two runs per compiler within 3 %; the host quiet; clang on one fixed core at high priority). The sizes are the same with every compiler: 40.68 bytes per packet, then 10.07 (0.248; the held-out session alone 38.58 then 9.89). Nanoseconds per packet, the ratio in brackets:

  | compiler | bytes per packet | encoding (client), warm | decoding (server), warm | encoding, cold | decoding, cold |
  |---|---|---|---|---|---|
  | clang x64 (llvm-mingw, this machine) | 40.68 → 10.07 | 563 → 197 (0.35) | 295 → 253 (0.86) | 1,170 → 270 (0.23) | 423 → 320 (0.76) |
  | MSVC x64 (Windows VM) | 40.68 → 10.07 | 529 → 213 (0.40) | 344 → 282 (0.82) | 1,148 → 311 (0.27) | 462 → 362 (0.78) |
  | GCC 12 x64 (Debian VM; the server target) | 40.68 → 10.07 | 401 → 161 (0.40) | 276 → 225 (0.81) | 903 → 207 (0.23) | 641 → 277 (0.43) |
  | Clang 14 x64 (Debian VM) | 40.68 → 10.07 | 588 → 197 (0.34) | 335 → 249 (0.74) | 1,171 → 243 (0.21) | 474 → 314 (0.66) |

  Four times fewer bytes and faster in both directions. A server with 64 players at 63 packets per second each decodes about 4,000 packets per second: about 1.3 ms per second, 0.13 % of a core (legacy 0.17 %).
- The legacy coder sizes a message as `bit / 8 + 1` bytes (`msg.cpp`), so a packet ending on a byte boundary (12 % here) carries one more byte that nothing writes: on the client, whatever its stack held at that place (obfuscated by the XOR, but sent). The reader compares only the bytes holding bits.
