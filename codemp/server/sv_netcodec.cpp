/*
===========================================================================
Copyright (C) 2026, OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

// sv_netcodec.cpp -- every server-to-client message is coded by the message codec (codemp/netcodec, plan step 7),
// and every client packet is decoded by it (plan step 8). The codec reads the engine's own data in place: the client's
// snapshots (client_t::frames), the shared entity ring (svs.snapshotEntities), the reliable commands
// (client_t::reliableCommands) and the baselines (sv.svEntities[].baseline). Next to the engine's frames, each client
// has one netcodec Frame per message slot, which describes the snapshot a message held, so that later messages can be
// coded against it, and one PacketBase per message for its last kAckWindow messages, which the client's packets are
// coded against.

#include "server.h"
#include "netcodec/engine.h"

#include <algorithm>
#include <cstddef>

namespace eng = netcodec::engine;

static_assert( PACKET_BACKUP == eng::kPacketBackup, "message slots" );
static_assert( MAX_RELIABLE_COMMANDS >= eng::kMinCommands, "the codec needs 256 server commands on both sides" );
static_assert( MAX_SNAPSHOT_ENTITIES == eng::kMaxSnapshotEntities, "entities per snapshot" );
static_assert( MAX_GENTITIES == eng::kMaxEntities, "entity numbers" );
static_assert( sizeof( entityState_t ) == 532 && offsetof( entityState_t, number ) == 0, "entity images of the codec" );
static_assert( sizeof( playerState_t ) == 1552, "player-state images of the codec" );
// the player-state fields client packets are coded against (codemp/netcodec/src/client_codec.cpp), and usercmd_t
static_assert( offsetof( playerState_t, commandTime ) == 0 && offsetof( playerState_t, delta_angles ) == 80 &&
	offsetof( playerState_t, weapon ) == 176 && offsetof( playerState_t, viewangles ) == 184 &&
	offsetof( playerState_t, stats ) + STAT_HOLDABLE_ITEM * sizeof( int ) == 248 &&
	offsetof( playerState_t, fd.forcePowerSelected ) == 884, "player-state offsets of the client packet codec" );
static_assert( sizeof( usercmd_t ) == 28 && offsetof( usercmd_t, angles ) == 4 && offsetof( usercmd_t, buttons ) == 16 &&
	offsetof( usercmd_t, weapon ) == 20 && offsetof( usercmd_t, upmove ) == 26, "usercmd_t images of the client packet codec" );

// one encoder for every client: it keeps only scratch buffers (created on first use, once the zone is up)
static eng::ServerCodec &Codec( void ) {
	static eng::ServerCodec codec;
	return codec;
}

static eng::Frame s_frames[MAX_CLIENTS][PACKET_BACKUP];

// What the client's packets are coded against: one per message with a snapshot, for the last kAckWindow messages
// (slot seq % kAckWindow). Reset only at connection: unlike the frames, a gamestate or a demo does not reset them, since
// a packet may acknowledge a message sent before either.
static eng::PacketBase s_packetBases[MAX_CLIENTS][eng::kAckWindow];
static_assert( SV_PING_WINDOW == eng::kAckWindow, "the ping covers the messages the client can acknowledge" );

static int ClientIndex( const client_t *client ) {
	return (int)( client - svs.clients );
}

void SV_NetcodecResetClient( const client_t *client ) {
	eng::Frame *frames = s_frames[ClientIndex( client )];
	for ( int i = 0; i < PACKET_BACKUP; i++ ) {
		frames[i].seq = -1;
	}
}

// Server commands are not repeated while the message that carries them may still arrive (plan step 10). A message
// sends the commands not yet acknowledged when there is a new one, or when the last message that sent them is known
// lost (the client acknowledged it or a later message without acknowledging the commands) or still unacknowledged
// after twice the ping and a client packet interval (the client sends a packet per tick, so its acknowledge can come up
// to one interval after the round trip while the ping holds part of it: twice the ping alone repeated commands on a LAN,
// plan step 14); otherwise it sends none. A message that sends commands sends them all from the client's
// acknowledge on, as before, so the client receives them in order and the codec's record of the commands the client
// holds stays true. Bots (their demos) and clients not yet in the game send every command until acknowledged, as
// before (no ping measured yet).
static struct {
	int		carrier;	// the last message that sent commands, up to sent
	int		sent;		// the last command sent
	int		time;		// when the carrier was sent (Sys_Milliseconds)
} s_commands[MAX_CLIENTS];

// The time a snapshot leaves against the game clock it carries (sv.time), for svnetstats (plan step 11): the client
// takes any change of it for network jitter. Measured with the steady clock in microseconds, since Sys_Milliseconds
// moves by whole milliseconds.
static struct {
	long long	offset;		// real send time - game time of the last snapshot sent, in microseconds
	bool		valid;
} s_sendTime[MAX_CLIENTS];

// A snapshot sent without a base asked starts the measure again: the client enters the world (a map load, which
// runs three frames of 100 ms at once, or a map_restart, which does the same) or asked for a full snapshot.
static void SV_NetcodecMeasureSend( client_t *client, bool baseAsked ) {
	const long long offset = SV_Microseconds() - (long long)sv.time * 1000;
	auto &last = s_sendTime[ClientIndex( client )];
	if ( last.valid && baseAsked ) {
		netcodecStats_t &st = client->netcodecStats;
		const long long change = offset > last.offset ? offset - last.offset : last.offset - offset;
		st.sendChanges++;
		st.sendChangeSum += (double)change;
		st.sendChangeMax = std::max( st.sendChangeMax, (int)std::min( change, 1000000000LL ) );
		st.sendChangesOver1ms += change >= 1000 ? 1 : 0;
	}
	last.offset = offset;
	last.valid = true;
}

// At most SV_PACKET_RATE client packets per second are decoded, on average, with bursts of SV_PACKET_BURST (plan
// step 11): the client sends at most MAX_PACKET_RATE, 10 % fewer, so only a modified client goes above, and its extra
// packets are dropped before decoding. The budget is counted in thousandths of a packet: each millisecond adds
// SV_PACKET_RATE.
static struct {
	int		time;		// when the budget was last updated (Sys_Milliseconds)
	int		budget;
} s_packetBudget[MAX_CLIENTS];

static bool SV_NetcodecPacketAllowed( const client_t *client ) {
	if ( client->netchan.remoteAddress.type == NA_LOOPBACK ) {
		return true;	// a local client sends a packet every frame
	}
	const int full = SV_PACKET_BURST * 1000;
	const int now = Sys_Milliseconds();
	auto &b = s_packetBudget[ClientIndex( client )];
	const int elapsed = std::min( std::max( now - b.time, 0 ), full / SV_PACKET_RATE + 1 );
	b.time = now;
	b.budget = std::min( b.budget + elapsed * SV_PACKET_RATE, full );
	if ( b.budget < 1000 ) {
		return false;
	}
	b.budget -= 1000;
	return true;
}

// a new connection: the codec holds no message of it yet
void SV_NetcodecNewClient( const client_t *client ) {
	s_commands[ClientIndex( client )].sent = 0;
	s_sendTime[ClientIndex( client )].valid = false;
	s_packetBudget[ClientIndex( client )].time = Sys_Milliseconds();
	s_packetBudget[ClientIndex( client )].budget = SV_PACKET_BURST * 1000;
	SV_NetcodecResetClient( client );
	for ( eng::PacketBase &base : s_packetBases[ClientIndex( client )] ) {
		base.seq = -1;
	}
}

// sv_netcodecCompare: the same message or client packet in the legacy format (huffman bitstream), for its size only;
// the byte counts are printed every 10 seconds (every message or packet with 2)
struct netcodecCompare_t {
	int		count, time;
	double	codec, legacy;
};

static netcodecCompare_t s_cmpMessages, s_cmpPackets;

static void SV_NetcodecCompareAdd( netcodecCompare_t &c, const char *what, const char *unit, size_t codecBytes, int legacyBytes ) {
	c.count++;
	c.codec += (double)codecBytes;
	c.legacy += (double)legacyBytes;
	if ( sv_netcodecCompare->integer > 1 || svs.time - c.time >= 10000 || svs.time < c.time ) {
		Com_Printf( "netcodec: %d %s, %.1f bytes per %s against %.1f in the legacy format (ratio %.3f)\n",
			c.count, what, c.codec / c.count, unit, c.legacy / c.count, c.legacy > 0 ? c.codec / c.legacy : 0.0 );
		c.count = 0;
		c.codec = c.legacy = 0;
		c.time = svs.time;
	}
}

static void SV_NetcodecCompare( client_t *client, const eng::ServerMessage &sm, clientSnapshot_t *oldframe, int lastframe, size_t codecBytes ) {
	static byte buf[MAX_MSGLEN];
	msg_t msg;

	if ( sm.download ) {
		return;	// one download block per message with the codec, several in the legacy format: not comparable
	}

	MSG_Init( &msg, buf, sizeof( buf ) );
	msg.allowoverflow = qtrue;
	if ( sm.gameState ) {
		SV_CreateClientGameStateMessage( client, &msg );	// acknowledge, commands, gamestate
	} else {
		MSG_WriteLong( &msg, client->lastClientCommand );
		SV_UpdateServerCommandsToClient( client, &msg );
		if ( sm.setGame ) {
			MSG_WriteByte( &msg, svc_setgame );
			for ( const char *c = sm.setGame; *c; c++ ) {
				MSG_WriteByte( &msg, *c );
			}
			MSG_WriteByte( &msg, 0 );
		}
		if ( sm.snapshot ) {
			SV_WriteSnapshotToClient( client, &msg, oldframe, lastframe );
		}
		if ( sm.mapChange ) {
			MSG_WriteByte( &msg, svc_mapchange );
		}
	}
	MSG_WriteByte( &msg, svc_EOF );
	if ( msg.overflowed ) {
		return;
	}
	SV_NetcodecCompareAdd( s_cmpMessages, "messages", "message", codecBytes, msg.cursize );
}

// the last server command the client acknowledged (for a bot recording a demo, the last one it was sent)
static int SV_NetcodecAcknowledged( const client_t *client ) {
	if ( client->demo.isBot && client->demo.demorecording ) {
		return client->demo.botReliableAcknowledge;
	}
	return client->reliableAcknowledge;
}

// Why a message sends the commands not yet acknowledged (see s_commands), for svnetstats' count of the repeats by cause
// (plan step 14: commands were repeated without loss at short intervals); the first that applies, in this order.
enum class CommandsDue { kNo, kNotInGame, kLost, kNew, kTimeout, kNewAndTimeout };

// whether this message sends the commands not yet acknowledged, and why; it records them as sent if so
static CommandsDue SV_NetcodecCommandsDue( const client_t *client, int seq ) {
	auto &c = s_commands[ClientIndex( client )];
	const int now = Sys_Milliseconds();
	const bool notInGame = client->state != CS_ACTIVE || client->netchan.remoteAddress.type == NA_BOT;
	const bool lost = client->messageAcknowledge >= c.carrier;	// the carrier arrived without them
	const bool fresh = client->reliableSequence > c.sent;			// a new command
	const bool timeout = now - c.time > 2 * client->ping + 1000 / MAX_PACKET_RATE;	// no news of the carrier
	const CommandsDue due = notInGame ? CommandsDue::kNotInGame : lost ? CommandsDue::kLost
		: fresh && timeout ? CommandsDue::kNewAndTimeout : fresh ? CommandsDue::kNew
		: timeout ? CommandsDue::kTimeout : CommandsDue::kNo;
	if ( due != CommandsDue::kNo ) {
		c.carrier = seq;
		c.sent = client->reliableSequence;
		c.time = now;
	}
	return due;
}

// every message starts with the last client command received and the server commands not yet acknowledged (all of
// them, or none while they are in flight: SV_NetcodecCommandsDue)
static void SV_NetcodecBegin( client_t *client, eng::ServerMessage &sm ) {
	const int acknowledged = SV_NetcodecAcknowledged( client );
	sm.seq = client->netchan.outgoingSequence;
	sm.ack = client->lastClientCommand;
	sm.serverId = sv.serverId;
	sm.firstCommand = acknowledged + 1;	// the commands firstCommand to reliableSequence (none if above it)
	if ( client->reliableSequence > acknowledged ) {
		netcodecStats_t &st = client->netcodecStats;
		const int previous = s_commands[ClientIndex( client )].sent;
		const CommandsDue due = SV_NetcodecCommandsDue( client, sm.seq );
		if ( due != CommandsDue::kNo ) {
			const int alreadySent = previous < client->reliableSequence ? previous : client->reliableSequence;
			const int repeats = alreadySent > acknowledged ? alreadySent - acknowledged : 0;
			st.commands += client->reliableSequence - acknowledged;
			st.repeats += repeats;
			st.repeatsBy[static_cast<int>( due ) - 1] += repeats;
		} else {
			sm.firstCommand = client->reliableSequence + 1;
		}
	}
	client->reliableSent = client->reliableSequence;
}

// codes sm into out against the client's frames; returns its size (0: not coded), the base used (delta, 0: none) and
// the Record of its snapshot
static size_t SV_NetcodecEncode( client_t *client, const eng::ServerMessage &sm, byte *out, size_t outSize, uint32_t &delta, eng::Record &record ) {
	eng::EntityRing ring;
	eng::CommandRing commands;
	eng::Baselines baselines;

	ring.states = svs.snapshotEntities;
	ring.count = (uint32_t)svs.numSnapshotEntities;
	commands.texts = client->reliableCommands[0];
	commands.stride = MAX_STRING_CHARS;
	commands.count = MAX_RELIABLE_COMMANDS;
	commands.last = client->reliableSequence;
	baselines.bytes = &sv.svEntities[0].baseline;
	baselines.stride = sizeof( svEntity_t );

	return Codec().Encode( sm, s_frames[ClientIndex( client )], ring, svs.nextSnapshotEntities, commands, baselines, out, outSize, delta, record );
}

// A snapshot message that does not fit in outSize bytes is coded again with only the entities nearest to the player,
// as many as fit (a binary search over their count), instead of being sent empty; the others are absent from this
// snapshot only (the next ones code them against their baseline again). The frame's entities, which only this message
// uses, are rewritten in place in the ring. Returns the size (0: it does not fit even without entities) and how many
// entities were left out.
static size_t SV_NetcodecFitSnapshot( client_t *client, eng::ServerMessage &sm, byte *out, size_t outSize, uint32_t &delta, eng::Record &record, int &left ) {
	static entityState_t all[MAX_SNAPSHOT_ENTITIES];
	clientSnapshot_t *frame = &client->frames[sm.seq & PACKET_MASK];
	const int n = frame->num_entities;
	int order[MAX_SNAPSHOT_ENTITIES];
	float dist[MAX_SNAPSHOT_ENTITIES];

	for ( int i = 0; i < n; i++ ) {
		all[i] = svs.snapshotEntities[( frame->first_entity + i ) % svs.numSnapshotEntities];
		dist[i] = DistanceSquared( all[i].pos.trBase, frame->ps.origin );
		order[i] = i;
	}
	std::sort( order, order + n, [&]( int a, int b ) { return dist[a] != dist[b] ? dist[a] < dist[b] : a < b; } );

	// codes the message with the k entities nearest to the player, in the order of their numbers
	const auto code = [&]( int k ) -> size_t {
		bool keep[MAX_SNAPSHOT_ENTITIES] = {};
		for ( int j = 0; j < k; j++ ) {
			keep[order[j]] = true;
		}
		int m = 0;
		for ( int i = 0; i < n; i++ ) {
			if ( keep[i] ) {
				svs.snapshotEntities[( frame->first_entity + m++ ) % svs.numSnapshotEntities] = all[i];
			}
		}
		frame->num_entities = k;
		sm.frame.numEntities = (uint32_t)k;
		return SV_NetcodecEncode( client, sm, out, outSize, delta, record );
	};

	left = 0;
	if ( !code( 0 ) ) {
		code( n );	// restores the frame; the message goes out empty
		return 0;
	}
	int fits = 0, tooLarge = n;
	while ( tooLarge - fits > 1 ) {
		const int mid = ( fits + tooLarge ) / 2;
		if ( code( mid ) ) {
			fits = mid;
		} else {
			tooLarge = mid;
		}
	}
	left = n - fits;
	return code( fits );	// the ring, delta and record of the last call must be those of the message sent
}

// codes the message, keeps its snapshot's Frame and sends it; false if it could not be coded (sent empty)
static bool SV_NetcodecSend( client_t *client, eng::ServerMessage &sm, clientSnapshot_t *oldframe, int lastframe ) {
	static byte buf[MAX_MSGLEN];
	eng::Frame *frames = s_frames[ClientIndex( client )];
	eng::Record record = {};
	uint32_t delta = 0;
	msg_t msg;
	// sv_netcodecMaxBytes (debug): a smaller limit than MAX_MSGLEN, to test what happens to larger messages
	const int maxBytes = sv_netcodecMaxBytes->integer;
	const size_t outSize = maxBytes > 0 && maxBytes < MAX_MSGLEN ? (size_t)maxBytes : sizeof( buf );
	int left = 0;

	size_t size = SV_NetcodecEncode( client, sm, buf, outSize, delta, record );
	if ( !size && sm.snapshot ) {
		size = SV_NetcodecFitSnapshot( client, sm, buf, outSize, delta, record, left );
	}

	eng::Frame &slot = frames[sm.seq % PACKET_BACKUP];
	eng::PacketBase &packetBase = s_packetBases[ClientIndex( client )][sm.seq % eng::kAckWindow];
	slot.seq = -1;
	packetBase.seq = -1;
	if ( !size ) {
		// the legacy code cleared an overflowed message too, and sent it empty
		Com_Printf( "WARNING: message for %s not coded (larger than %d bytes, or malformed)\n", client->name, (int)outSize );
	} else {
		netcodecStats_t &st = client->netcodecStats;
		st.messages++;
		st.bytes += (double)size;
		st.escaped += Codec().Escaped() ? 1 : 0;
		if ( left ) {
			st.cut++;
			if ( outSize == sizeof( buf ) ) {
				Com_Printf( "WARNING: snapshot for %s larger than %d bytes: sent with the %d entities nearest to the player, %d left out\n",
					client->name, (int)outSize, (int)sm.frame.numEntities, left );
			}
		}
		if ( sm.snapshot ) {
			slot = sm.frame;
			slot.seq = sm.seq;
			slot.record = record;
			packetBase = eng::PacketBaseOf( slot );
			st.snapshots++;
			if ( !sm.delta ) {
				st.noBase++;
			} else if ( !delta ) {
				st.baseRefused++;
			} else {
				st.baseAge[delta <= 4 ? delta - 1 : delta <= 8 ? 4 : 5]++;
			}
		}
	}

	if ( sv_netcodecCompare->integer && size ) {
		SV_NetcodecCompare( client, sm, oldframe, lastframe, size );
	}

	Com_Memset( &msg, 0, sizeof( msg ) );
	msg.data = buf;
	msg.maxsize = sizeof( buf );
	msg.cursize = (int)size;
	if ( sm.snapshot && size && client->netchan.remoteAddress.type != NA_BOT ) {
		SV_NetcodecMeasureSend( client, sm.delta != 0 );
	}
	SV_SendMessageToClient( &msg, client );
	return size != 0;
}

void SV_NetcodecSendGameState( client_t *client ) {
	eng::ServerMessage sm = {};

	SV_NetcodecResetClient( client );	// the client clears its state with a gamestate: no earlier snapshot is a base
	s_sendTime[ClientIndex( client )].valid = false;	// the send-time measure too: a map load moves the game clock
	SV_NetcodecBegin( client, sm );
	sm.gameState = true;
	sm.relSeq = client->reliableSequence;
	sm.configStrings = sv.configstrings;
	sm.numConfigStrings = MAX_CONFIGSTRINGS;
	sm.clientNum = ClientIndex( client );
	sm.checksum = sv.checksumFeed;
	if ( !SV_NetcodecSend( client, sm, NULL, 0 ) ) {
		// the client cannot play without it: dropped, with the reason (CL_ParseCommandString shows it before cgame runs)
		Com_Printf( S_COLOR_YELLOW "WARNING: the gamestate does not fit in a message: %s dropped\n", client->name );
		SV_DropClient( client, "gamestate too large for a message" );
	}
}

// The gamestate at the start of a server demo (svrecord, sv_autoDemo); the demo then holds every message sent. The
// client's frames are reset, so the next message is coded without a base and the later ones only against messages of
// the demo. The commands a later message carries start after the client's acknowledge, and each may be coded against
// one up to kCommandWindow earlier: the gamestate holds those, so the demo holds every command a message refers to.
size_t SV_NetcodecDemoGameState( client_t *client, byte *out, size_t outSize ) {
	eng::ServerMessage sm = {};
	eng::Record record = {};
	uint32_t delta = 0;
	int first = SV_NetcodecAcknowledged( client ) + 1 - (int)eng::kCommandWindow;

	if ( first <= client->reliableSequence - MAX_RELIABLE_COMMANDS ) {
		first = client->reliableSequence - MAX_RELIABLE_COMMANDS + 1;	// older ones are no longer held
	}
	if ( first < 1 ) {
		first = 1;
	}

	SV_NetcodecResetClient( client );
	sm.seq = client->netchan.outgoingSequence - 1;
	sm.ack = client->lastClientCommand;
	sm.serverId = sv.serverId;
	sm.firstCommand = first;
	sm.gameState = true;
	sm.relSeq = client->reliableSequence;
	sm.configStrings = sv.configstrings;
	sm.numConfigStrings = MAX_CONFIGSTRINGS;
	sm.clientNum = ClientIndex( client );
	sm.checksum = sv.checksumFeed;
	return SV_NetcodecEncode( client, sm, out, outSize, delta, record );
}

void SV_NetcodecSendMapChange( client_t *client ) {
	eng::ServerMessage sm = {};

	SV_NetcodecBegin( client, sm );
	sm.mapChange = true;
	SV_NetcodecSend( client, sm, NULL, 0 );
}

// after SV_BuildClientSnapshot: the commands, the game dir (setGame, or NULL), the snapshot and a download block
void SV_NetcodecSendSnapshot( client_t *client, const char *setGame ) {
	eng::ServerMessage sm = {};
	clientSnapshot_t *frame = &client->frames[client->netchan.outgoingSequence & PACKET_MASK];
	svDownloadBlock_t download;
	int lastframe = 0;

	SV_NetcodecBegin( client, sm );
	sm.setGame = setGame;

	clientSnapshot_t *oldframe = SV_SnapshotBase( client, &lastframe );
	sm.snapshot = true;
	sm.delta = (uint32_t)lastframe;
	sm.time = SV_SnapshotTime( client );
	sm.flags = (uint32_t)SV_SnapshotFlags( client );
	// a margin is sent once, in the snapshot after its measure: the client takes each one as a new sample
	sm.inputMargin = client->inputMarginNew ? Net_MarginToWire( client->inputMargin ) : INPUT_MARGIN_NONE;
	client->inputMarginNew = qfalse;
	sm.areas = frame->areabits;
	sm.numAreas = (uint32_t)frame->areabytes;
	sm.frame.seq = sm.seq;
	sm.frame.ps = &frame->ps;
	sm.frame.vps = frame->ps.m_iVehicleNum ? &frame->vps : NULL;
	sm.frame.firstEntity = frame->first_entity;
	sm.frame.numEntities = (uint32_t)frame->num_entities;

	SV_NextDownloadBlock( client, &download );
	if ( download.present ) {
		sm.download = true;
		sm.block = (uint32_t)download.block;
		sm.fileSize = download.fileSize;
		sm.data = download.data;
		sm.dataSize = (uint32_t)download.size;
	}

	SV_NetcodecSend( client, sm, oldframe, lastframe );
}

// svnetstats: the server's CPU time, then the message codec's statistics of each client, since its connection or the
// last svnetstats, then reset
void SV_NetcodecStats_f( void ) {
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}
	SV_CostStats();
	for ( int i = 0; i < sv_maxclients->integer; i++ ) {
		client_t *cl = &svs.clients[i];
		netcodecStats_t &st = cl->netcodecStats;
		if ( cl->state < CS_CONNECTED || ( !st.messages && !st.packets ) ) {
			continue;
		}
		const int decoded = st.packets - st.packetsNotDecoded - st.packetsLimited;
		Com_Printf( "%2d %s^7: %d messages, %.1f bytes per message, %d escaped; %d snapshots: %d without a base asked, "
			"%d with the asked base refused, %d cut to fit, base 1: %d, 2: %d, 3: %d, 4: %d, 5-8: %d, 9+: %d messages back; "
			"ping %d ms; %d commands sent, %d of them repeats (not in the game %d, carrier lost %d, new command %d, "
			"twice the ping passed %d, both %d); %d client packets, %.1f bytes per packet, %d not decoded, "
			"%d above the packet rate, %.2f user commands per packet, %d lost (runs of 1: %d, 2: %d, 3: %d, 4+: %d); "
			"ticks run with their command %d, without one %d, %d commands after their tick; input margin: mean %.1f ms, "
			"lowest %d ms; "
			"send time against the game clock: mean change %.3f ms, max %.3f ms, %d of %d changes of 1 ms or more\n",
			i, cl->name, st.messages, st.messages ? st.bytes / st.messages : 0.0, st.escaped, st.snapshots, st.noBase,
			st.baseRefused, st.cut, st.baseAge[0], st.baseAge[1], st.baseAge[2], st.baseAge[3], st.baseAge[4], st.baseAge[5],
			cl->ping, st.commands, st.repeats, st.repeatsBy[0], st.repeatsBy[1], st.repeatsBy[2], st.repeatsBy[3],
			st.repeatsBy[4], st.packets, st.packets ? st.packetBytes / st.packets : 0.0, st.packetsNotDecoded,
			st.packetsLimited, decoded ? (double)st.userCmds / decoded : 0.0,
			st.packetsLost, st.lossRuns[0], st.lossRuns[1], st.lossRuns[2], st.lossRuns[3],
			st.cmdsOnTime, st.cmdsMissing, st.cmdsLate, st.margins ? st.marginSum / st.margins : 0.0, st.marginMin,
			st.sendChanges ? st.sendChangeSum / st.sendChanges / 1000.0 : 0.0, st.sendChangeMax / 1000.0,
			st.sendChangesOver1ms, st.sendChanges );
		Com_Memset( &st, 0, sizeof( st ) );
	}
}

// sv_netcodecCompare on client packets: the same packet in the legacy format, with the user-command key
static void SV_NetcodecComparePacket( client_t *client, const eng::ClientPacket &p, size_t codecBytes ) {
	static byte buf[MAX_MSGLEN];
	usercmd_t cmds[MAX_PACKET_USERCMDS + 1] = {};	// cmds[0]: the null command the first one is coded against
	msg_t msg;

	MSG_Init( &msg, buf, sizeof( buf ) );
	msg.allowoverflow = qtrue;
	MSG_Bitstream( &msg );
	MSG_WriteLong( &msg, p.serverId );
	MSG_WriteLong( &msg, p.ack );
	MSG_WriteLong( &msg, p.reliableAck );
	for ( uint32_t i = 0; i < p.numCommands; i++ ) {
		MSG_WriteByte( &msg, clc_clientCommand );
		MSG_WriteLong( &msg, p.commands[i].seq );
		MSG_WriteString( &msg, p.commands[i].text );
	}
	if ( p.move != eng::Move::kNone ) {
		const int key = sv.checksumFeed ^ p.ack ^ Com_HashKey( client->reliableCommands[p.reliableAck & ( MAX_RELIABLE_COMMANDS - 1 )], 32 );
		Com_Memcpy( &cmds[1], p.userCmds, p.numUserCmds * sizeof( usercmd_t ) );
		MSG_WriteByte( &msg, p.move == eng::Move::kDelta ? clc_move : clc_moveNoDelta );
		MSG_WriteByte( &msg, (int)p.numUserCmds );
		for ( uint32_t i = 1; i <= p.numUserCmds; i++ ) {
			MSG_WriteDeltaUsercmdKey( &msg, key, &cmds[i - 1], &cmds[i] );
		}
	}
	MSG_WriteByte( &msg, clc_EOF );
	if ( msg.overflowed ) {
		return;
	}
	SV_NetcodecCompareAdd( s_cmpPackets, "client packets", "packet", codecBytes, msg.cursize );
}

// Decodes a client packet (the payload after the netchan header). NULL if it is malformed or coded against a message
// whose PacketBase the server no longer holds: the packet is then dropped. The packet points into the codec until the
// next one is decoded.
const eng::ClientPacket *SV_NetcodecDecodeClientPacket( client_t *client, const msg_t *msg ) {
	static eng::ClientPacket packet;
	const int size = msg->cursize - msg->readcount;
	netcodecStats_t &st = client->netcodecStats;

	st.packets++;
	if ( client->netchan.dropped > 0 ) {
		st.packetsLost += client->netchan.dropped;
		st.lossRuns[std::min( client->netchan.dropped, 4 ) - 1]++;
	}
	if ( !SV_NetcodecPacketAllowed( client ) ) {
		st.packetsLimited++;
		return NULL;
	}
	if ( size <= 0 ) {
		st.packetsNotDecoded++;
		return NULL;
	}
	st.packetBytes += size;
	if ( !Codec().DecodePacket( msg->data + msg->readcount, (size_t)size, client->netchan.outgoingSequence - 1,
		s_packetBases[ClientIndex( client )], packet ) ) {
		st.packetsNotDecoded++;
		Com_DPrintf( "netcodec: packet of %s not decoded\n", client->name );
		return NULL;
	}
	if ( packet.move != eng::Move::kNone ) {
		st.userCmds += (int)packet.numUserCmds;
	}
	if ( sv_netcodecCompare->integer ) {
		SV_NetcodecComparePacket( client, packet, (size_t)size );
	}
	return &packet;
}
