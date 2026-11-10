# Step 10: acknowledgements, base selection, reliable data

Status: **done** (10 November 2026; checked in game). Plan: [PLAN.md](../PLAN.md), step 10.

## Transport decisions (owner, 10 November 2026)

The questions of [07-transport-study.md](../07-transport-study.md) section 7.7: no authenticated encryption for now (it stays possible later, in plan step 12, as a layer on libsodium without a backend); 32 players per server (`MAX_CLIENTS` unchanged); IPv6 later; the server browser and master-server protocols are kept. So the netchan is improved in place (07 option A), and this step works on it.

## Scope, from the measurements (owner, 10 November 2026)

The plan's items were written before steps 7 and 8 measured the real traffic. Re-read against those measurements:

| item of the plan | decision | why |
|---|---|---|
| Server commands repeated in every message until acknowledged | **done**: not repeated while in flight | on the held-out demos (real games over the Internet), commands are 7.1 % of the server's bytes and 60.2 % of the command operations are repeats: sending each once would save 4.0 % of the bytes (0.0 % on the study's LAN captures). Measured with a scratch tool coding the corpus with each command kept once (not committed) |
| Gamestate resent only after about 1 s | **done**: the client sends at the normal rate while awaiting it | the client sent one packet a second before it had the gamestate, so the server noticed a lost gamestate fragment a second or more later |
| Acknowledgement bitfield in each client packet | not done | the latest message acknowledged, already in every packet, chooses the base (one message back in nearly every in-game window of steps 7 and 8) and detects the loss of the commands (below); +1 to 4 bytes per client packet for loss statistics both sides already measure |
| Every user command not yet acknowledged resent in each packet (removes `cl_packetdup`) | not done; adaptive redundancy belongs to plan step 11 | the server runs user commands in time order and drops one older than the last it ran (`SV_UserMove`), so a command resent after a loss is detected arrives too late; only redundancy sent ahead helps, which `cl_packetdup` is. Resending all unacknowledged commands would be redundancy in proportion to the round trip, about twice the bytes of a client packet. Step 11 should set the redundancy from the measured loss (none without loss: about a third fewer bytes per client packet, an estimate, half its user commands being repeats today) |
| Snapshot history sized from the round trip | not done | 32 messages (`PACKET_BACKUP`) cover round trips up to 1.6 s at 20 snapshots per second; nothing to gain below |
| Several download blocks per message | not now | a format and interface change, useful only for UDP downloads on a LAN |

## What was done

1. **Server commands not repeated while in flight** (`codemp/server/sv_netcodec.cpp`, `SV_NetcodecBegin`, `SV_NetcodecCommandsDue`). A message sends the commands not yet acknowledged only when there is a new command, or when the last message that sent them is known lost (the client acknowledged that message or a later one without acknowledging the commands: each client packet carries both numbers), or is still unacknowledged after twice the ping; otherwise it sends none. A message that sends commands sends all of them from the client's acknowledge on, as before, so the client receives them in order (it keeps only the next command in sequence) and the codec's record of the commands the client holds stays true (a message without commands takes its base's). Bots (their demos) and clients not yet in the game (no ping measured yet) send every command until acknowledged, as before. No format or client change. Cost: a command in a lost message arrives about one ping later than before, when the next message repeated it; a burst of commands within a round trip still repeats the earlier ones, so that they stay in order. `svnetstats` counts the commands sent and how many were repeats.
2. **Gamestate resent quickly** (`codemp/client/cl_input.cpp`, `CL_ReadyToSendPacket`): the client sent one packet a second until it had the gamestate; it now sends at its normal rate, so the server, which resends the gamestate as soon as the client acknowledges a later message (`SV_ExecuteClientMessage`), resends a lost one about one round trip later. Cost: about 60 packets of about 10 bytes per second during the wait.

## Check in game (owner, 10 November 2026, MSVC x64 client, MSVC local server and GCC server on the VPS)

Results:

- Local server, no loss: 2,728 messages, 254 commands sent, 0 repeats, 0 client packets not decoded.
- VPS (15 ms network round trip, 24 ms ping), no loss: 3,093 messages, 192 commands sent, 0 repeats; 9,710 client packets, 0 not decoded.
- VPS, `net_dropsim` on the client (a window at about 4 % of the server's messages lost, then one at 17 %: 1,330 messages received, 271 lost, 0 not decoded, 0 client packets without a base, bases 1 / 2 / 3 / 4 messages back 1,107 / 189 / 22 / 10): the chat arrived; the server, over both windows: 7,355 messages, 498 commands sent, 52 of them repeats (the resends after a lost message), 22,987 client packets, 0 not decoded.
- Connection with `net_dropsim 30` on the client: immediate.
- At a 24 ms ping the client's acknowledgement comes back before the next message (50 ms later), so the legacy scheme repeated little here too; the gain shows at larger round trips (the real games of the held-out demos: 60 % of the commands sent were repeats).

The checks:

1. Over the Internet (VPS) with bots and some chat, `svnetstats` after a few minutes: "commands sent" and "repeats" (repeats should now be a small part), 0 packets not decoded, the chat and the scores as usual; `sv_netcodecCompare 1`: the message ratio against the legacy format, which still repeats every command, should drop a little (more in chat-heavy windows).
2. With `net_dropsim 20` on the client: the chat still arrives (later when its message is lost), `netstats` 0 not decoded.
3. Gamestate: connect with `net_dropsim 30` on the client; the connection should take about a round trip more per lost fragment rather than seconds.
