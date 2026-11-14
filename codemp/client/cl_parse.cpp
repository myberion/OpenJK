/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2005 - 2015, ioquake3 contributors
Copyright (C) 2013 - 2015, OpenJK contributors

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

// cl_parse.c  -- parse a message received from the server

#include "client.h"
#include "cl_cgameapi.h"
#include "qcommon/stringed_ingame.h"

static char hiddenCvarVal[128];

/*
=========================================================================

MESSAGE PARSING

The message codec decodes the server's messages (cl_netcodec.cpp); the functions below apply what it decoded.

=========================================================================
*/

/*
================
CL_ParseSnapshot

The snapshot of a message the codec decoded (its player states, its entities in cl.parseEntities from
newSnap->parseEntitiesNum, the other fields filled by the caller): it is copied to cl.snap and saved in
cl.snapshots[]. A message whose base the client does not hold is not decoded at all.
================
*/
void CL_ParseSnapshot( clSnapshot_t *newSnap ) {
	int			oldMessageNum;
	int			i, packetNum;

	// if we were just unpaused, we can only *now* really let the
	// change come into effect or the client hangs.
	cl_paused->modified = qfalse;

	newSnap->valid = qtrue;

	// clear the valid flags of any snapshots between the last
	// received and this one, so if there was a dropped packet
	// it won't look like something valid to delta from next
	// time we wrap around in the buffer
	oldMessageNum = cl.snap.messageNum + 1;

	if ( newSnap->messageNum - oldMessageNum >= PACKET_BACKUP ) {
		oldMessageNum = newSnap->messageNum - ( PACKET_BACKUP - 1 );
	}
	for ( ; oldMessageNum < newSnap->messageNum ; oldMessageNum++ ) {
		cl.snapshots[oldMessageNum & PACKET_MASK].valid = qfalse;
	}

	// copy to the current good spot
	cl.snap = *newSnap;
	cl.snap.ping = 999;
	// calculate ping time
	for ( i = 0 ; i < OUT_PACKET_BACKUP ; i++ ) {
		packetNum = ( clc.netchan.outgoingSequence - 1 - i ) & OUT_PACKET_MASK;
		if ( !cl.outPackets[ packetNum ].p_realtime ) {
			break;	// never sent
		}
		if ( cl.snap.ps.commandTime >= cl.outPackets[ packetNum ].p_serverTime ) {
			// in real time: cls.realtime is the last frame's, up to a frame before this message arrived
			cl.snap.ping = Sys_Milliseconds() - cl.outPackets[ packetNum ].p_realtime;
			break;
		}
	}
	// save the frame off in the backup array for later delta comparisons
	cl.snapshots[cl.snap.messageNum & PACKET_MASK] = cl.snap;

	if (cl_shownet->integer == 3) {
		Com_Printf( "   snapshot:%i  delta:%i  ping:%i\n", cl.snap.messageNum,
		cl.snap.deltaNum, cl.snap.ping );
	}

	cl.newSnapshots = qtrue;
}


/*
================
CL_ParseSetGame

rww - Update fs_game, this message is so we can use the ext_data
*_overrides.txt files for mods.
(The message codec has its own field tables: the overrides files are no longer read here.)
================
*/
void FS_UpdateGamedir(void);
void CL_ParseSetGame( const char *gameDir )
{
	char newGameDir[MAX_QPATH];

	Q_strncpyz( newGameDir, gameDir, sizeof( newGameDir ) );

	if(FS_CheckDirTraversal(newGameDir))
	{
		Com_Printf(S_COLOR_YELLOW "WARNING: Server sent invalid fs_game value %s\n", newGameDir);
		return;
	}

	if(!FS_FilenameCompare(newGameDir, BASEGAME))
		Cvar_Set("fs_game", "");
	else
		Cvar_Set("fs_game", newGameDir);

	if(!(Cvar_Flags("fs_game") & CVAR_MODIFIED))
		return;

	//Update the search path for the mod dir
	FS_UpdateGamedir();
}


//=====================================================================

int cl_connectedToPureServer;
int cl_connectedToCheatServer;

/*
==================
CL_SystemInfoChanged

The systeminfo configstring has been changed, so parse
new information out of it.  This will happen at every
gamestate, and possibly during gameplay.
==================
*/
void CL_SystemInfoChanged( void ) {
	char			*systemInfo;
	const char		*s, *t;
	char			key[BIG_INFO_KEY];
	char			value[BIG_INFO_VALUE];
	qboolean		gameSet;

	systemInfo = cl.gameState.stringData + cl.gameState.stringOffsets[ CS_SYSTEMINFO ];
	// NOTE TTimo:
	// when the serverId changes, any further messages we send to the server will use this new serverId
	// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=475
	// in some cases, outdated cp commands might get sent with this new serverId
	cl.serverId = atoi( Info_ValueForKey( systemInfo, "sv_serverid" ) );

	// don't set any vars when playing a demo
	if ( clc.demoplaying ) {
		return;
	}

	s = Info_ValueForKey( systemInfo, "sv_cheats" );
	cl_connectedToCheatServer = atoi( s );
	if ( !cl_connectedToCheatServer )
	{
		Cvar_SetCheatState();
	}

	// check pure server string
	s = Info_ValueForKey( systemInfo, "sv_paks" );
	t = Info_ValueForKey( systemInfo, "sv_pakNames" );
	FS_PureServerSetLoadedPaks( s, t );

	s = Info_ValueForKey( systemInfo, "sv_referencedPaks" );
	t = Info_ValueForKey( systemInfo, "sv_referencedPakNames" );
	FS_PureServerSetReferencedPaks( s, t );

	gameSet = qfalse;
	// scan through all the variables in the systeminfo and locally set cvars to match
	s = systemInfo;
	while ( s ) {
		Info_NextPair( &s, key, value );
		if ( !key[0] ) {
			break;
		}
		// ehw!
		if ( !Q_stricmp( key, "fs_game" ) ) {
			if(FS_CheckDirTraversal(value))
			{
				Com_Printf(S_COLOR_YELLOW "WARNING: Server sent invalid fs_game value %s\n", value);
				continue;
			}

			if(!FS_FilenameCompare(value, BASEGAME))
			{
				Q_strncpyz(value, "", sizeof(value));
			}

			gameSet = qtrue;
		}
		Cvar_Server_Set( key, value );
	}
	// if game folder should not be set and it is set at the client side
	if ( !gameSet && *Cvar_VariableString("fs_game") ) {
		Cvar_Set( "fs_game", "" );
	}
	cl_connectedToPureServer = Cvar_VariableValue( "sv_pure" );
}

/*
==================
CL_ParseGamestate
==================
*/
void CL_ParseGamestate( int serverCommandSequence ) {
	Con_Close();

	clc.connectPacketCount = 0;

	// wipe local client state
	CL_ClearState();

	// a gamestate always marks a server command sequence
	clc.serverCommandSequence = serverCommandSequence;

	cl.gameState.dataCount = 1;	// leave a 0 at the beginning for uninitialized configstrings
}

// a config string of the gamestate begun by CL_ParseGamestate
void CL_ParseGamestateString( int i, const char *s ) {
	int len;

	if ( i < 0 || i >= MAX_CONFIGSTRINGS ) {
		Com_Error( ERR_DROP, "configstring > MAX_CONFIGSTRINGS" );
	}
	if ( cl_shownet->integer >= 2 ) {
		Com_Printf( "%d: %s\n", i, s );
	}

	len = strlen( s );

	if ( len + 1 + cl.gameState.dataCount > MAX_GAMESTATE_CHARS ) {
		Com_Error( ERR_DROP, "MAX_GAMESTATE_CHARS exceeded" );
	}

	// append it to the gameState string buffer
	cl.gameState.stringOffsets[ i ] = cl.gameState.dataCount;
	Com_Memcpy( cl.gameState.stringData + cl.gameState.dataCount, s, len + 1 );
	cl.gameState.dataCount += len + 1;
}

// the end of the gamestate: the baselines (MAX_GENTITIES, by number), the client number and the checksum feed
void CL_ParseGamestateEnd( const entityState_t *baselines, int clientNum, int checksumFeed ) {
	Com_Memcpy( cl.entityBaselines, baselines, sizeof( cl.entityBaselines ) );

	clc.clientNum = clientNum;
	// read the checksum feed
	clc.checksumFeed = checksumFeed;

	// parse serverId and other cvars
	CL_SystemInfoChanged();

	// reinitialize the filesystem if the game directory has changed
	if( FS_ConditionalRestart( clc.checksumFeed ) ) {
		// don't set to true because we yet have to start downloading
		// enabling this can cause double loading of a map when connecting to
		// a server which has a different game directory set
		//clc.downloadRestart = qtrue;
	}

	// This used to call CL_StartHunkUsers, but now we enter the download state before loading the
	// cgame
	CL_InitDownloads();

	// make sure the game starts
	Cvar_Set( "cl_paused", "0" );
}


//=====================================================================

/*
=====================
CL_ParseDownload

A download message has been received from the server
=====================
*/
void CL_ParseDownload ( int block, int fileSize, const byte *data, int size ) {
	char	error[MAX_STRING_CHARS];

	if (!*clc.downloadTempName) {
		Com_Printf("Server sending download, but no download was requested\n");
		CL_AddReliableCommand("stopdl", qfalse);
		return;
	}

	block &= 0xFFFF;	// 16 bits, as the legacy format sent it

	if ( !block && !clc.downloadBlock )
	{
		// block zero is special, contains file size
		clc.downloadSize = fileSize;

		Cvar_SetValue( "cl_downloadSize", clc.downloadSize );

		if (clc.downloadSize < 0)
		{
			// the error text, with format specifiers made harmless as the legacy string reader did
			int i;
			for ( i = 0; i < size && i < (int)sizeof( error ) - 1; i++ ) {
				error[i] = ( data[i] == '%' || !data[i] ) ? '.' : (char)data[i];
			}
			error[i] = 0;
			Com_Error(ERR_DROP, "%s", error );
			return;
		}
	}

	if (size < 0 || size > MAX_MSGLEN)
	{
		Com_Error(ERR_DROP, "CL_ParseDownload: Invalid size %d for download chunk", size);
		return;
	}

	if((clc.downloadBlock & 0xFFFF) != block)
	{
		Com_DPrintf( "CL_ParseDownload: Expected block %d, got %d\n", (clc.downloadBlock & 0xFFFF), block);
		return;
	}

	// open the file if not opened yet
	if (!clc.download)
	{
		clc.download = FS_SV_FOpenFileWrite( clc.downloadTempName );

		if (!clc.download) {
			Com_Printf( "Could not create %s\n", clc.downloadTempName );
			CL_AddReliableCommand( "stopdl", qfalse );
			CL_NextDownload();
			return;
		}
	}

	if (size)
		FS_Write( data, size, clc.download );

	CL_AddReliableCommand( va("nextdl %d", clc.downloadBlock), qfalse );
	clc.downloadBlock++;

	clc.downloadCount += size;

	// So UI gets access to it
	Cvar_SetValue( "cl_downloadCount", clc.downloadCount );

	if (!size) { // A zero length block means EOF
		if (clc.download) {
			FS_FCloseFile( clc.download );
			clc.download = 0;

			// rename the file
			FS_SV_Rename ( clc.downloadTempName, clc.downloadName, qfalse );
		}

		// send intentions now
		// We need this because without it, we would hold the last nextdl and then start
		// loading right away.  If we take a while to load, the server is happily trying
		// to send us that last block over and over.
		// Write it twice to help make sure we acknowledge the download
		CL_WritePacket();
		CL_WritePacket();

		// get another file if needed
		CL_NextDownload ();
	}
}

int CL_GetValueForHidden(const char *s)
{ //string arg here just in case I want to add more sometime and make a lookup table
	return atoi(hiddenCvarVal);
}

/*
=====================
CL_ParseCommandString

Command strings are just saved off until cgame asks for them
when it transitions a snapshot
=====================
*/
void CL_CheckSVStringEdRef( char *buf, const char *str );	// cl_cgame.cpp

void CL_ParseCommandString( int seq, const char *s ) {
	int		index;

	// see if we have already executed stored it off
	if ( clc.serverCommandSequence >= seq ) {
		return;
	}
	clc.serverCommandSequence = seq;

	index = seq & (MAX_RELIABLE_COMMANDS-1);
	/*
	if (s[0] == 'c' && s[1] == 's' && s[2] == ' ' && s[3] == '0' && s[4] == ' ')
	{ //yes.. we seem to have an incoming server info.
		char *f = strstr(s, "g_debugMelee");
		if (f)
		{
			while (*f && *f != '\\')
			{ //find the \ after it
				f++;
			}
			if (*f == '\\')
			{ //got it
				int i = 0;

				f++;
				while (*f && *f != '\\' && i < 128)
				{
					hiddenCvarVal[i] = *f;
					i++;
					f++;
				}
				hiddenCvarVal[i] = 0;

				//don't worry about backing over beginning of string I guess,
				//we already know we successfully strstr'd the initial string
				//which exceeds this length.
				//MSG_ReadString appears to just return a static buffer so I
				//can stomp over its contents safely.
				f--;
				*f = '\"';
				f--;
				*f = ' ';
				f--;
				*f = '0';
				f--;
				*f = ' ';
				f--;
				*f = 's';
				f--;
				*f = 'c';

				//the normal configstring gets to start here...
				s = f;
			}
		}
	}
	*/
	Q_strncpyz( clc.serverCommands[ index ], s, sizeof( clc.serverCommands[ index ] ) );

	// Before cgame runs (while connecting or loading) nothing executes the server commands, so a "disconnect" (the
	// server dropped this client, for example because its gamestate does not fit in a message) is handled here, as
	// CL_GetServerCommand handles it once cgame runs; otherwise the client would keep waiting for the gamestate, the
	// server sending to the dropped slot until it frees it
	if ( !cls.cgameStarted && !Q_strncmp( s, "disconnect", 10 ) && ( s[10] == '\0' || s[10] == ' ' ) ) {
		char strEd[MAX_STRING_CHARS];	// MAX_STRINGED_SV_STRING of cl_cgame.cpp
		Cmd_TokenizeString( s );
		CL_CheckSVStringEdRef( strEd, Cmd_Argv( 1 ) );
		Com_Error( ERR_SERVERDISCONNECT, "%s: %s\n", SE_GetString( "MP_SVGAME_SERVER_DISCONNECTED" ), strEd );
	}
}


/*
=====================
CL_ParseServerMessage

Decoded by the message codec (cl_netcodec.cpp)
=====================
*/
void CL_ParseServerMessage( msg_t *msg ) {
	CL_NetcodecParseServerMessage( msg );
}
