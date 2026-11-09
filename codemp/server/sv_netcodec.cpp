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

// the last server command the client acknowledged (for a bot recording a demo, the last one it was sent)
static int SV_NetcodecAcknowledged( const client_t *client ) {
	if ( client->demo.isBot && client->demo.demorecording ) {
		return client->demo.botReliableAcknowledge;
	}
	return client->reliableAcknowledge;
}

// every message starts with the last client command received and the server commands not yet acknowledged
static void SV_NetcodecBegin( client_t *client, eng::ServerMessage &sm ) {
	sm.seq = client->netchan.outgoingSequence;
	sm.ack = client->lastClientCommand;
	sm.serverId = sv.serverId;
	sm.firstCommand = SV_NetcodecAcknowledged( client ) + 1;	// the commands firstCommand to reliableSequence (none if above it)
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

// codes the message, keeps its snapshot's Frame and sends it
static void SV_NetcodecSend( client_t *client, eng::ServerMessage &sm, clientSnapshot_t *oldframe, int lastframe ) {
	static byte buf[MAX_MSGLEN];
	eng::Frame *frames = s_frames[ClientIndex( client )];
	eng::Record record = {};
	uint32_t delta = 0;
	msg_t msg;

	const size_t size = SV_NetcodecEncode( client, sm, buf, sizeof( buf ), delta, record );

	eng::Frame &slot = frames[sm.seq % PACKET_BACKUP];
	slot.seq = -1;
	if ( !size ) {
		// the legacy code cleared an overflowed message too, and sent it empty
		Com_Printf( "WARNING: message for %s not coded (too large or malformed)\n", client->name );
	} else {
		netcodecStats_t &st = client->netcodecStats;
		st.messages++;
		st.bytes += (double)size;
		st.escaped += Codec().Escaped() ? 1 : 0;
		if ( sm.snapshot ) {
			slot = sm.frame;
			slot.seq = sm.seq;
			slot.record = record;
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

// svnetstats: the message codec's statistics of each client since its connection or the last svnetstats, then reset
void SV_NetcodecStats_f( void ) {
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}
	for ( int i = 0; i < sv_maxclients->integer; i++ ) {
		client_t *cl = &svs.clients[i];
		netcodecStats_t &st = cl->netcodecStats;
		if ( cl->state < CS_CONNECTED || !st.messages ) {
			continue;
		}
		Com_Printf( "%2d %s^7: %d messages, %.1f bytes per message, %d escaped; %d snapshots: %d without a base asked, "
			"%d with the asked base refused, base 1: %d, 2: %d, 3: %d, 4: %d, 5-8: %d, 9+: %d messages back\n",
			i, cl->name, st.messages, st.bytes / st.messages, st.escaped, st.snapshots, st.noBase, st.baseRefused,
			st.baseAge[0], st.baseAge[1], st.baseAge[2], st.baseAge[3], st.baseAge[4], st.baseAge[5] );
		Com_Memset( &st, 0, sizeof( st ) );
	}
}

// sv_recordClientPackets (debug, plan step 8): each client's packets are written, as SV_ExecuteClientMessage reads them
// (after the netchan header, the obfuscation XOR already removed), to clientpackets/client<n>_<date>.jkcp under the
// home path, one file per connection, for the measurements and the training data of the client-to-server format.
// File: ClientPacketFileHeader, then per packet a ClientPacketRecord followed by its length bytes; little-endian.
struct ClientPacketFileHeader {
	char magic[4];              // "JKCP"
	int32_t version;            // 1
	int32_t recordSize;         // sizeof( ClientPacketRecord )
};

struct ClientPacketRecord {
	int32_t svTime;             // sv.time at arrival
	int32_t realTime;           // Sys_Milliseconds() at arrival
	int32_t key;                // the usercmd key SV_UserMove derives for this packet
	int32_t snapTime;           // server time of the acknowledged snapshot, -1 if the server no longer holds it
	int32_t commandTime;        // then its player state's fields that match usercmd fields (0 without a snapshot)
	float viewangles[3];
	int32_t deltaAngles[3];
	int32_t weapon;
	int32_t forcePowerSelected;
	int32_t holdableItem;
	int32_t length;             // bytes that follow
};
static_assert( sizeof( ClientPacketRecord ) == 15 * 4, "no padding in the file records" );

static fileHandle_t s_recordFiles[MAX_CLIENTS];

void SV_NetcodecRecordClose( const client_t *client ) {
	fileHandle_t &f = s_recordFiles[ClientIndex( client )];
	if ( f ) {
		FS_FCloseFile( f );
		f = 0;
	}
}

void SV_NetcodecRecordCloseAll( void ) {
	for ( fileHandle_t &f : s_recordFiles ) {
		if ( f ) {
			FS_FCloseFile( f );
			f = 0;
		}
	}
}

void SV_NetcodecRecordClientPacket( client_t *client, const msg_t *msg ) {
	const int ci = ClientIndex( client );
	fileHandle_t &f = s_recordFiles[ci];
	if ( !sv_recordClientPackets->integer ) {
		SV_NetcodecRecordClose( client );
		return;
	}
	if ( !f ) {
		qtime_t t;
		Com_RealTime( &t );
		const char *name = va( "clientpackets/client%d_%04d%02d%02d-%02d%02d%02d.jkcp", ci, 1900 + t.tm_year, 1 + t.tm_mon,
			t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec );
		f = FS_FOpenFileWrite( name );
		if ( !f ) {
			Com_Printf( S_COLOR_YELLOW "Cannot write %s, sv_recordClientPackets set to 0\n", name );
			Cvar_Set( "sv_recordClientPackets", "0" );
			return;
		}
		const ClientPacketFileHeader header = { { 'J', 'K', 'C', 'P' }, 1, (int32_t)sizeof( ClientPacketRecord ) };
		FS_Write( &header, sizeof( header ), f );
		Com_Printf( "Recording the packets of client %d to %s\n", ci, name );
	}

	// the header as SV_ExecuteClientMessage reads it, for the key of SV_UserMove and the acknowledged snapshot
	msg_t peek = *msg;
	MSG_Bitstream( &peek );
	MSG_ReadLong( &peek );  // serverId
	const int ack = MSG_ReadLong( &peek );
	const int reliableAck = MSG_ReadLong( &peek );

	ClientPacketRecord rec = {};
	rec.svTime = sv.time;
	rec.realTime = Sys_Milliseconds();
	rec.key = sv.checksumFeed ^ ack ^ Com_HashKey( client->reliableCommands[reliableAck & ( MAX_RELIABLE_COMMANDS - 1 )], 32 );
	rec.snapTime = -1;
	const eng::Frame &frame = s_frames[ci][( ack & 0x7fffffff ) % PACKET_BACKUP];
	if ( ack >= 0 && frame.seq == ack ) {
		const playerState_t *ps = static_cast<const playerState_t *>( frame.ps );
		rec.snapTime = (int32_t)frame.record.time;
		rec.commandTime = ps->commandTime;
		for ( int i = 0; i < 3; i++ ) {
			rec.viewangles[i] = ps->viewangles[i];
			rec.deltaAngles[i] = ps->delta_angles[i];
		}
		rec.weapon = ps->weapon;
		rec.forcePowerSelected = ps->fd.forcePowerSelected;
		rec.holdableItem = ps->stats[STAT_HOLDABLE_ITEM];
	}
	rec.length = msg->cursize - msg->readcount;
	FS_Write( &rec, sizeof( rec ), f );
	FS_Write( msg->data + msg->readcount, rec.length, f );
}
