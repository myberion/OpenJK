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
2. **Format in the library**: a client message in `engine.h`, contexts for the user-command fields, trained tables; each packet decodes alone.
3. **Integration**: `CL_WritePacket`, `SV_ExecuteClientMessage`; the user-command key and the netchan XOR dropped in this direction; `sv_netcodecCompare` covers it.
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
- The legacy coder sizes a message as `bit / 8 + 1` bytes (`msg.cpp`), so a packet ending on a byte boundary (12 % here) carries one more byte that nothing writes: on the client, whatever its stack held at that place (obfuscated by the XOR, but sent). The reader compares only the bytes holding bits.
