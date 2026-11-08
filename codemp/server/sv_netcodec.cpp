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

// sv_netcodec.cpp -- every server-to-client message is coded by the message codec (codemp/netcodec, plan step 7).
// The codec reads the engine's own data in place: the client's snapshots (client_t::frames), the shared entity ring
// (svs.snapshotEntities), the reliable commands (client_t::reliableCommands) and the baselines
// (sv.svEntities[].baseline). Next to the engine's frames, each client has one netcodec Frame per message slot, which
// describes the snapshot a message held, so that later messages can be coded against it.

#include "server.h"
#include "netcodec/engine.h"

#include <cstddef>

namespace eng = netcodec::engine;

static_assert( PACKET_BACKUP == eng::kPacketBackup, "message slots" );
static_assert( MAX_RELIABLE_COMMANDS >= eng::kMinCommands, "the codec needs 256 server commands on both sides" );
static_assert( MAX_SNAPSHOT_ENTITIES == eng::kMaxSnapshotEntities, "entities per snapshot" );
static_assert( MAX_GENTITIES == eng::kMaxEntities, "entity numbers" );
static_assert( sizeof( entityState_t ) == 532 && offsetof( entityState_t, number ) == 0, "entity images of the codec" );
static_assert( sizeof( playerState_t ) == 1552, "player-state images of the codec" );

// one encoder for every client: it keeps only scratch buffers (created on first use, once the zone is up)
static eng::ServerCodec &Codec( void ) {
	static eng::ServerCodec codec;
	return codec;
}

static eng::Frame s_frames[MAX_CLIENTS][PACKET_BACKUP];

static int ClientIndex( const client_t *client ) {
	return (int)( client - svs.clients );
}

void SV_NetcodecResetClient( const client_t *client ) {
	eng::Frame *frames = s_frames[ClientIndex( client )];
	for ( int i = 0; i < PACKET_BACKUP; i++ ) {
		frames[i].seq = -1;
	}
}

// sv_netcodecCompare: the same message in the legacy format (huffman bitstream), for its size only
static int s_cmpMessages, s_cmpTime;
static double s_cmpCodec, s_cmpLegacy;

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

	s_cmpMessages++;
	s_cmpCodec += (double)codecBytes;
	s_cmpLegacy += (double)msg.cursize;
	if ( sv_netcodecCompare->integer > 1 || svs.time - s_cmpTime >= 10000 || svs.time < s_cmpTime ) {
		Com_Printf( "netcodec: %d messages, %.1f bytes per message against %.1f in the legacy format (ratio %.3f)\n",
			s_cmpMessages, s_cmpCodec / s_cmpMessages, s_cmpLegacy / s_cmpMessages,
			s_cmpLegacy > 0 ? s_cmpCodec / s_cmpLegacy : 0.0 );
		s_cmpMessages = 0;
		s_cmpCodec = s_cmpLegacy = 0;
		s_cmpTime = svs.time;
	}
}

// every message starts with the last client command received and the server commands not yet acknowledged
static void SV_NetcodecBegin( client_t *client, eng::ServerMessage &sm ) {
	int acknowledged;

	if ( client->demo.isBot && client->demo.demorecording ) {
		acknowledged = client->demo.botReliableAcknowledge;
	} else {
		acknowledged = client->reliableAcknowledge;
	}
	sm.seq = client->netchan.outgoingSequence;
	sm.ack = client->lastClientCommand;
	sm.firstCommand = acknowledged + 1;	// the commands firstCommand to reliableSequence (none if above it)
	client->reliableSent = client->reliableSequence;
}

// codes the message, keeps its snapshot's Frame and sends it
static void SV_NetcodecSend( client_t *client, eng::ServerMessage &sm, clientSnapshot_t *oldframe, int lastframe ) {
	static byte buf[MAX_MSGLEN];
	eng::Frame *frames = s_frames[ClientIndex( client )];
	eng::EntityRing ring;
	eng::CommandRing commands;
	eng::Baselines baselines;
	eng::Record record = {};
	uint32_t delta = 0;
	msg_t msg;

	ring.states = svs.snapshotEntities;
	ring.count = (uint32_t)svs.numSnapshotEntities;
	commands.texts = client->reliableCommands[0];
	commands.stride = MAX_STRING_CHARS;
	commands.count = MAX_RELIABLE_COMMANDS;
	commands.last = client->reliableSequence;
	baselines.bytes = &sv.svEntities[0].baseline;
	baselines.stride = sizeof( svEntity_t );

	const size_t size = Codec().Encode( sm, frames, ring, svs.nextSnapshotEntities, commands, baselines, buf, sizeof( buf ), delta, record );

	eng::Frame &slot = frames[sm.seq % PACKET_BACKUP];
	slot.seq = -1;
	if ( !size ) {
		// the legacy code cleared an overflowed message too, and sent it empty
		Com_Printf( "WARNING: message for %s not coded (too large or malformed)\n", client->name );
	} else if ( sm.snapshot ) {
		slot = sm.frame;
		slot.seq = sm.seq;
		slot.record = record;
	}

	if ( sv_netcodecCompare->integer && size ) {
		SV_NetcodecCompare( client, sm, oldframe, lastframe, size );
	}

	Com_Memset( &msg, 0, sizeof( msg ) );
	msg.data = buf;
	msg.maxsize = sizeof( buf );
	msg.cursize = (int)size;
	SV_SendMessageToClient( &msg, client );
}

void SV_NetcodecSendGameState( client_t *client ) {
	eng::ServerMessage sm = {};

	SV_NetcodecResetClient( client );	// the client clears its state with a gamestate: no earlier snapshot is a base
	SV_NetcodecBegin( client, sm );
	sm.gameState = true;
	sm.relSeq = client->reliableSequence;
	sm.configStrings = sv.configstrings;
	sm.numConfigStrings = MAX_CONFIGSTRINGS;
	sm.clientNum = ClientIndex( client );
	sm.checksum = sv.checksumFeed;
	SV_NetcodecSend( client, sm, NULL, 0 );
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
