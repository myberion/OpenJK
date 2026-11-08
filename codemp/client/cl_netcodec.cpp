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

// cl_netcodec.cpp -- the server's messages are decoded by the message codec (codemp/netcodec, plan step 7). The
// codec writes the snapshot's entities into cl.parseEntities in place, and its player states and the gamestate's
// baselines where it is told; the CL_Parse* functions of cl_parse.cpp then apply what it decoded, as they applied
// what the legacy reader parsed. Next to cl.snapshots, one netcodec Frame per message slot describes the snapshot a
// message held, so that later messages can be decoded against it.

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

static eng::ClientCodec &Codec( void ) {
	static eng::ClientCodec codec;
	return codec;
}

static eng::Frame s_frames[PACKET_BACKUP];

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
	if ( size <= 0 ) {
		return;	// the server could not code this message and sent it empty
	}

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
		// we will have read any new server commands in this message before the snapshot
		newSnap.serverCommandNum = clc.serverCommandSequence;
		newSnap.serverTime = cm.time;
		newSnap.messageNum = seq;
		newSnap.deltaNum = cm.delta ? seq - (int)cm.delta : -1;
		newSnap.snapFlags = (int)cm.flags;
		Com_Memcpy( newSnap.areamask, cm.areas, cm.numAreas );
		newSnap.parseEntitiesNum = cl.parseEntitiesNum;
		newSnap.numEntities = (int)cm.numEntities;
		cl.parseEntitiesNum += (int)cm.numEntities;

		if ( show ) {
			Com_Printf( "snapshot: delta %u, %u entities%s\n", cm.delta, cm.numEntities, cm.vehicle ? ", vehicle" : "" );
		}
		CL_ParseSnapshot( &newSnap );	// copies it to cl.snapshots[slot]

		eng::Frame &f = s_frames[slot];
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
