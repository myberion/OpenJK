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

// cl_netcodec.cpp -- the server's messages are decoded by the message codec (codemp/netcodec, plan step 7), and the
// client's packets coded by it (plan step 8). The codec writes the snapshot's entities into cl.parseEntities in place,
// and its player states and the gamestate's baselines where it is told; the CL_Parse* functions of cl_parse.cpp then
// apply what it decoded, as they applied what the legacy reader parsed. Next to cl.snapshots, one netcodec Frame per
// message slot describes the snapshot a message held, so that later messages can be decoded against it, and the
// client's packets coded against the last one.

#include "client.h"
#include "cl_cgameapi.h"
#include "netcodec/engine.h"

#include <cstddef>

namespace eng = netcodec::engine;

static_assert( PACKET_BACKUP == eng::kPacketBackup, "message slots" );
static_assert( MAX_RELIABLE_COMMANDS >= eng::kMinCommands, "the codec needs 256 server commands on both sides" );
static_assert( MAX_SNAPSHOT_ENTITIES == eng::kMaxSnapshotEntities, "entities per snapshot" );
static_assert( MAX_GENTITIES == eng::kMaxEntities, "entity numbers" );
static_assert( sizeof( entityState_t ) == 532 && offsetof( entityState_t, number ) == 0, "entity images of the codec" );
static_assert( sizeof( playerState_t ) == 1552, "player-state images of the codec" );
static_assert( MAX_STRING_CHARS - 1 == eng::kMaxCommandLength, "client commands: the server decodes at most MAX_STRING_CHARS - 1 characters" );

static eng::ClientCodec &Codec( void ) {
	static eng::ClientCodec codec;
	return codec;
}

static eng::Frame s_frames[PACKET_BACKUP];
static int s_arrival[PACKET_BACKUP];	// the real time the message of each frame arrived (client packets)

// the baselines the codec decodes a gamestate into and decodes snapshots against: a copy of its own, since a
// gamestate clears the client state (cl.entityBaselines included) after it is decoded; CL_ParseGamestateEnd copies it
static entityState_t s_baselines[MAX_GENTITIES];

// Client demos (record): the demo is a coded stream of its own. Each message the client decodes is coded again by a
// ServerCodec against the previous snapshot of the demo, with the server commands it brought (each command is written
// once) and without its download block. The demo then holds every base and every command its messages refer to, and
// decodes from its start whatever the network lost or the client could not decode.
static eng::ServerCodec &DemoCodec( void ) {
	static eng::ServerCodec codec;
	return codec;
}

static eng::Frame s_demoFrames[PACKET_BACKUP];

static void CL_NetcodecDemoReset( void ) {
	for ( int i = 0; i < PACKET_BACKUP; i++ ) {
		s_demoFrames[i].seq = -1;
	}
}

void CL_NetcodecReset( void ) {
	for ( int i = 0; i < PACKET_BACKUP; i++ ) {
		s_frames[i].seq = -1;
	}
	CL_NetcodecDemoReset();	// a demo's player resets its state with a gamestate too
}

// codes sm for the demo against its frames, keeps its snapshot's Frame and writes it; qfalse if it could not be coded
static qboolean CL_NetcodecDemoWrite( const eng::ServerMessage &sm ) {
	static byte buf[MAX_MSGLEN];
	eng::EntityRing ring;
	eng::CommandRing commands;
	eng::Baselines baselines;
	eng::Record record = {};
	uint32_t delta = 0;
	int len;

	ring.states = cl.parseEntities;
	ring.count = MAX_PARSE_ENTITIES;
	commands.texts = clc.serverCommands[0];
	commands.stride = MAX_STRING_CHARS;
	commands.count = MAX_RELIABLE_COMMANDS;
	commands.last = clc.serverCommandSequence;
	baselines.bytes = cl.entityBaselines;
	baselines.stride = sizeof( entityState_t );

	const size_t size = DemoCodec().Encode( sm, s_demoFrames, ring, cl.parseEntitiesNum, commands, baselines, buf, sizeof( buf ), delta, record );

	eng::Frame &slot = s_demoFrames[sm.seq % PACKET_BACKUP];
	slot.seq = -1;
	if ( !size ) {
		Com_Printf( "WARNING: message %d not recorded (too large or malformed)\n", sm.seq );
		return qfalse;
	}
	if ( sm.snapshot ) {
		slot = sm.frame;
		slot.seq = sm.seq;
		slot.record = record;
	}

	len = LittleLong( sm.seq );
	FS_Write( &len, 4, clc.demofile );
	len = LittleLong( (int)size );
	FS_Write( &len, 4, clc.demofile );
	FS_Write( buf, (int)size, clc.demofile );
	return qtrue;
}

// the gamestate of the client's current state: config strings, baselines, client number, checksum feed
static void CL_NetcodecDemoGameState( eng::ServerMessage &sm ) {
	static const char *configStrings[MAX_CONFIGSTRINGS];

	for ( int i = 0; i < MAX_CONFIGSTRINGS; i++ ) {
		const int offset = cl.gameState.stringOffsets[i];
		configStrings[i] = offset ? cl.gameState.stringData + offset : NULL;
	}
	sm.gameState = true;
	sm.relSeq = clc.serverCommandSequence;
	sm.configStrings = configStrings;
	sm.numConfigStrings = MAX_CONFIGSTRINGS;
	sm.clientNum = clc.clientNum;
	sm.checksum = clc.checksumFeed;
}

// record: the gamestate at the start of the demo. It holds the last kCommandWindow server commands, which the commands
// of the later messages may be coded against.
qboolean CL_NetcodecRecordGameState( void ) {
	eng::ServerMessage sm = {};
	const int first = clc.serverCommandSequence + 1 - (int)eng::kCommandWindow;

	CL_NetcodecDemoReset();
	sm.seq = clc.serverMessageSequence - 1;
	sm.ack = clc.reliableSequence;
	sm.serverId = cl.serverId;
	sm.firstCommand = first < 1 ? 1 : first;
	CL_NetcodecDemoGameState( sm );
	return CL_NetcodecDemoWrite( sm );
}

// a decoded message, recorded: the commands after firstCommand - 1 (the ones it brought), the snapshot against the
// demo's snapshot baseSeq (the previous one)
static void CL_NetcodecDemoRecord( const eng::ClientMessage &cm, int seq, int firstCommand, int baseSeq ) {
	eng::ServerMessage sm = {};

	sm.seq = seq;
	sm.ack = cm.ack;
	sm.serverId = cm.serverId;
	if ( firstCommand <= clc.serverCommandSequence - MAX_RELIABLE_COMMANDS ) {
		firstCommand = clc.serverCommandSequence - MAX_RELIABLE_COMMANDS + 1;	// older ones are no longer held
	}
	sm.firstCommand = firstCommand;
	if ( cm.gameState ) {
		CL_NetcodecDemoGameState( sm );	// the demo's frames were reset with the client state
	}
	sm.setGame = cm.setGame;
	if ( cm.snapshot ) {
		sm.snapshot = true;
		sm.delta = seq - baseSeq > 0 && seq - baseSeq < PACKET_BACKUP ? (uint32_t)( seq - baseSeq ) : 0;
		sm.time = cm.time;
		sm.flags = cm.flags;
		sm.inputMargin = cm.inputMargin;
		sm.areas = cm.areas;
		sm.numAreas = cm.numAreas;
		sm.frame = s_frames[seq % PACKET_BACKUP];	// the player states and entities the message was decoded into
	}
	sm.mapChange = cm.mapChange;
	CL_NetcodecDemoWrite( sm );
}

// the decoded text NUL-terminated within size bytes, format specifiers made harmless as the legacy string readers
// did ('%' became '.')
static const char *Sanitized( const char *text, uint32_t length, char *buf, size_t size ) {
	const size_t n = length < size - 1 ? length : size - 1;
	for ( size_t i = 0; i < n; i++ ) {
		buf[i] = text[i] == '%' ? '.' : text[i];
	}
	buf[n] = 0;
	return buf;
}

// netstats: the message codec's statistics since the last netstats
static struct {
	int		packets;		// client packets sent
	double	packetBytes;
	int		packetsNoBase;	// coded without the message they acknowledge (see CL_NetcodecPacketBase)
	int		messages;
	int		lost;			// gaps in the message sequence: lost on the way (or dropped by the netchan)
	int		notDecoded;		// empty, malformed, or coded against a snapshot this client no longer holds
	double	bytes;
	int		snapshots;
	int		noBase;
	int		baseAge[6];		// snapshots coded against a base 1, 2, 3, 4, 5 to 8, 9 or more messages back
	int		lastSeq;
} s_stats;

void CL_NetcodecStats_f( void ) {
	if ( !s_stats.messages && !s_stats.packets ) {
		Com_Printf( "No server message or client packet since the last netstats.\n" );
		return;
	}
	if ( s_stats.messages ) {
		Com_Printf( "%d messages (%d lost on the way, %d not decoded), %.1f bytes per message; %d snapshots: %d without a base, "
			"base 1: %d, 2: %d, 3: %d, 4: %d, 5-8: %d, 9+: %d messages back\n",
			s_stats.messages, s_stats.lost, s_stats.notDecoded, s_stats.bytes / s_stats.messages, s_stats.snapshots,
			s_stats.noBase, s_stats.baseAge[0], s_stats.baseAge[1], s_stats.baseAge[2], s_stats.baseAge[3], s_stats.baseAge[4],
			s_stats.baseAge[5] );
		CL_InterpStats();
		CL_CmdClockStats();
	}
	if ( s_stats.packets ) {
		Com_Printf( "%d client packets, %.1f bytes per packet, %d without a base\n", s_stats.packets,
			s_stats.packetBytes / s_stats.packets, s_stats.packetsNoBase );
	}
	const int lastSeq = s_stats.lastSeq;
	Com_Memset( &s_stats, 0, sizeof( s_stats ) );
	s_stats.lastSeq = lastSeq;
}

void CL_NetcodecParseServerMessage( msg_t *msg ) {
	static clSnapshot_t newSnap;	// two player states: not on the stack
	static char text[BIG_INFO_STRING];
	const int start = msg->readcount;
	const int size = msg->cursize - start;
	const int seq = clc.serverMessageSequence;
	const int slot = seq & PACKET_MASK;
	eng::EntityRing ring;
	eng::CommandRing commands;
	eng::ClientTarget target;
	eng::ClientMessage cm = {};
	const bool show = cl_shownet->integer >= 2;	// one line per decoded operation
	const int oldCommand = clc.serverCommandSequence;	// for a demo: the commands this message brings come after it
	const int oldSnapshot = cl.snap.messageNum;			// for a demo: the snapshot this one is coded against

	if ( cl_shownet->integer == 1 ) {
		Com_Printf( "%i ", msg->cursize );	// the whole packet, netchan header included, as before the codec
	} else if ( show ) {
		Com_Printf( "------------------ %i\n", msg->cursize );
	}
	s_stats.messages++;
	if ( s_stats.lastSeq && seq > s_stats.lastSeq + 1 ) {
		s_stats.lost += seq - s_stats.lastSeq - 1;
	}
	s_stats.lastSeq = seq;
	if ( size <= 0 ) {
		s_stats.notDecoded++;
		return;	// the server could not code this message and sent it empty
	}
	s_stats.bytes += size;

	// a slot whose entities this message may overwrite in cl.parseEntities is no base any more
	for ( int i = 0; i < PACKET_BACKUP; i++ ) {
		if ( s_frames[i].seq >= 0 && cl.parseEntitiesNum - s_frames[i].firstEntity > MAX_PARSE_ENTITIES - MAX_SNAPSHOT_ENTITIES ) {
			s_frames[i].seq = -1;
		}
	}

	Com_Memset( &newSnap, 0, sizeof( newSnap ) );
	ring.states = cl.parseEntities;
	ring.count = MAX_PARSE_ENTITIES;
	commands.texts = clc.serverCommands[0];
	commands.stride = MAX_STRING_CHARS;
	commands.count = MAX_RELIABLE_COMMANDS;
	commands.last = clc.serverCommandSequence;
	target.ps = &newSnap.ps;
	target.vps = &newSnap.vps;
	target.ring = cl.parseEntities;
	target.ringCount = MAX_PARSE_ENTITIES;
	target.firstEntity = cl.parseEntitiesNum;
	target.baselines = s_baselines;
	target.baselineStride = sizeof( entityState_t );

	if ( !Codec().Decode( msg->data + start, (size_t)size, seq, s_frames, ring, commands, target, cm ) ) {
		// malformed, or coded against a snapshot this client does not hold: no snapshot, so the next packet asks for
		// a full one (clc_moveNoDelta); the reliable commands are sent again until acknowledged
		s_stats.notDecoded++;
		if ( show ) {
			Com_Printf( "not decoded\n" );
		} else {
			Com_DPrintf( "netcodec: message %d not decoded\n", seq );
		}
		return;
	}
	msg->readcount = msg->cursize;

	// the reliable sequence acknowledge number
	clc.reliableAcknowledge = cm.ack;
	if ( clc.reliableAcknowledge < clc.reliableSequence - MAX_RELIABLE_COMMANDS ) {
		clc.reliableAcknowledge = clc.reliableSequence;
	}

	for ( uint32_t i = 0; i < cm.numCommands; i++ ) {
		const eng::Command &c = cm.commands[i];
		const char *s = Sanitized( c.text, c.length, text, MAX_STRING_CHARS );
		if ( show ) {
			Com_Printf( "serverCommand %d: %s\n", c.seq, s );
		}
		CL_ParseCommandString( c.seq, s );
	}

	if ( cm.gameState ) {
		if ( show ) {
			Com_Printf( "gamestate: %u config strings\n", cm.numConfigStrings );
		}
		CL_ParseGamestate( cm.relSeq );	// clears the client state, and the codec's frames with it
		for ( uint32_t i = 0; i < cm.numConfigStrings; i++ ) {
			const eng::ConfigString &cs = cm.configStrings[i];
			CL_ParseGamestateString( (int)cs.index, Sanitized( cs.text, cs.length, text, sizeof( text ) ) );
		}
		CL_ParseGamestateEnd( s_baselines, cm.clientNum, cm.checksum );
	}

	if ( cm.setGame ) {
		if ( show ) {
			Com_Printf( "setgame: %s\n", cm.setGame );
		}
		CL_ParseSetGame( cm.setGame );
	}

	s_frames[slot].seq = -1;
	if ( cm.snapshot ) {
		if ( cm.numAreas > sizeof( newSnap.areamask ) ) {
			Com_Error( ERR_DROP, "CL_NetcodecParseServerMessage: Invalid size %u for areamask", cm.numAreas );
			return;
		}
		s_stats.snapshots++;
		if ( !cm.delta ) {
			s_stats.noBase++;
		} else {
			s_stats.baseAge[cm.delta <= 4 ? cm.delta - 1 : cm.delta <= 8 ? 4 : 5]++;
		}
		// we will have read any new server commands in this message before the snapshot
		newSnap.serverCommandNum = clc.serverCommandSequence;
		newSnap.serverTime = cm.time;
		newSnap.messageNum = seq;
		newSnap.deltaNum = cm.delta ? seq - (int)cm.delta : -1;
		newSnap.snapFlags = (int)cm.flags;
		newSnap.inputMarginValid = cm.inputMargin != INPUT_MARGIN_NONE ? qtrue : qfalse;
		newSnap.inputMargin = newSnap.inputMarginValid ? Net_MarginFromWire( cm.inputMargin ) : 0;
		Com_Memcpy( newSnap.areamask, cm.areas, cm.numAreas );
		newSnap.parseEntitiesNum = cl.parseEntitiesNum;
		newSnap.numEntities = (int)cm.numEntities;
		cl.parseEntitiesNum += (int)cm.numEntities;

		if ( show ) {
			Com_Printf( "snapshot: delta %u, %u entities%s\n", cm.delta, cm.numEntities, cm.vehicle ? ", vehicle" : "" );
		}
		CL_ParseSnapshot( &newSnap );	// copies it to cl.snapshots[slot]

		eng::Frame &f = s_frames[slot];
		s_arrival[slot] = cls.realtime;
		f.seq = seq;
		f.ps = &cl.snapshots[slot].ps;
		f.vps = cm.vehicle ? &cl.snapshots[slot].vps : NULL;
		f.firstEntity = newSnap.parseEntitiesNum;
		f.numEntities = cm.numEntities;
		f.record = cm.record;
	}

	if ( cm.download ) {
		if ( show ) {
			Com_Printf( "download: block %u, %u bytes\n", cm.block, cm.dataSize );
		}
		CL_ParseDownload( (int)cm.block, cm.fileSize, cm.data, (int)cm.dataSize );
	}

	if ( cm.mapChange ) {
		if ( show ) {
			Com_Printf( "mapchange\n" );
		}
		if ( cls.cgameStarted ) {
			CGVM_MapChange();
		}
	}

	if ( clc.demorecording ) {
		CL_NetcodecDemoRecord( cm, seq, oldCommand + 1, oldSnapshot );
	}
}

// Client packets (plan step 8). A packet's user commands are coded against the message it acknowledges (the last one
// received, clc.serverMessageSequence) when the client holds that message's snapshot and the server surely still holds
// its PacketBase and reads the acknowledge right: the server keeps the PacketBases of its last kAckWindow messages, and
// the acknowledge travels as its low 8 bits. So the messages the server has sent since must be fewer than kAckWindow
// when the packet arrives. Their number is estimated from the time since the message arrived plus the round-trip time,
// over the time between the server's messages (the snapshot times of the message and of its own base), with a margin
// of 2; when one of them is unknown, the packet is coded without a base.

// per packet sent, for the round-trip time: its netchan sequence, its real time, the server time of its last user
// command (moved: it had user commands)
static struct {
	int		seq;
	int		sent;
	int		serverTime;
	bool	moved;
} s_sent[eng::kAckWindow];

// The round-trip time seen with the message of frame f: from the sending of the newest packet whose last user command
// the server had executed in it (at or before its snapshot's commandTime) to its arrival, as the legacy client's ping
// (CL_ParseSnapshot) but over the last kAckWindow packets. qfalse if none of them is found.
static qboolean CL_NetcodecRoundTrip( const eng::Frame &f, int &rtt ) {
	const int commandTime = static_cast<const playerState_t *>( f.ps )->commandTime;
	const int last = clc.netchan.outgoingSequence - 1;

	for ( int seq = last; seq >= 1 && last - seq < (int)eng::kAckWindow; seq-- ) {
		const auto &s = s_sent[seq % eng::kAckWindow];
		if ( s.seq != seq ) {
			break;	// not sent by this connection
		}
		if ( s.moved && s.serverTime <= commandTime ) {
			rtt = s_arrival[f.seq & PACKET_MASK] - s.sent;
			return rtt >= 0 ? qtrue : qfalse;
		}
	}
	return qfalse;
}

// the frame of the message the next packet acknowledges, when the packet may be coded against it (see above)
static const eng::Frame *CL_NetcodecPacketBase( void ) {
	const int seq = clc.serverMessageSequence;
	const eng::Frame &f = s_frames[seq & PACKET_MASK];
	if ( f.seq != seq || f.record.olderSeq < 0 ) {
		return NULL;	// no snapshot held, or no earlier one for the time between messages
	}
	const eng::Frame &older = s_frames[f.record.olderSeq & PACKET_MASK];
	const int64_t between = f.record.time - older.record.time;	// over seq - olderSeq messages
	int rtt = 0;
	if ( older.seq != f.record.olderSeq || between <= 0 || !CL_NetcodecRoundTrip( f, rtt ) ) {
		return NULL;
	}
	const int64_t elapsed = (int64_t)( cls.realtime - s_arrival[seq & PACKET_MASK] ) + rtt;
	if ( elapsed * ( seq - f.record.olderSeq ) >= (int64_t)( eng::kAckWindow / 2 ) * between ) {
		return NULL;
	}
	return &f;
}

// Codes the client packet into buf: the serverId, the last server message and server command received, the client
// commands not yet acknowledged, and the last count user commands (none if count < 1), as a delta or asking for a full
// snapshot. qfalse if it could not be coded (too large): nothing is sent.
qboolean CL_NetcodecWritePacket( msg_t *buf, int count, qboolean delta ) {
	static eng::Command commands[MAX_RELIABLE_COMMANDS];
	static usercmd_t cmds[MAX_PACKET_USERCMDS];
	eng::ClientPacket p = {};
	eng::PacketBase base = {};

	p.serverId = cl.serverId;
	p.ack = clc.serverMessageSequence;
	p.reliableAck = clc.serverCommandSequence;
	for ( int i = clc.reliableAcknowledge + 1; i <= clc.reliableSequence && p.numCommands < MAX_RELIABLE_COMMANDS; i++ ) {
		eng::Command &c = commands[p.numCommands++];
		c.seq = i;
		c.text = clc.reliableCommands[i & ( MAX_RELIABLE_COMMANDS - 1 )];
		c.length = (uint32_t)strlen( c.text );
	}
	p.commands = commands;
	if ( count >= 1 ) {
		for ( int i = 0; i < count; i++ ) {
			cmds[i] = cl.cmds[( cl.cmdNumber - count + i + 1 ) & CMD_MASK];
		}
		p.move = delta ? eng::Move::kDelta : eng::Move::kNoDelta;
		p.userCmds = cmds;
		p.numUserCmds = (uint32_t)count;
	}

	const eng::Frame *frame = CL_NetcodecPacketBase();
	if ( frame ) {
		base = eng::PacketBaseOf( *frame );
	}
	const size_t size = Codec().EncodePacket( p, frame ? &base : NULL, buf->data, (size_t)buf->maxsize );
	if ( !size ) {
		Com_Printf( "WARNING: client packet not coded (too large)\n" );
		return qfalse;
	}
	buf->cursize = (int)size;

	const int seq = clc.netchan.outgoingSequence;
	auto &s = s_sent[seq % eng::kAckWindow];
	s.seq = seq;
	s.sent = cls.realtime;
	s.moved = count >= 1;
	s.serverTime = count >= 1 ? cmds[count - 1].serverTime : 0;

	s_stats.packets++;
	s_stats.packetBytes += (double)size;
	s_stats.packetsNoBase += frame ? 0 : 1;
	return qtrue;
}
