/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
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

// cl_cgame.c  -- client system interaction with client game
#include "client.h"
#include "cl_cgameapi.h"
#include "botlib/botlib.h"
#include "FXExport.h"
#include "FxUtil.h"
#include "qcommon/RoffSystem.h"
#include "qcommon/stringed_ingame.h"
#include "ghoul2/G2_gore.h"

extern IHeapAllocator *G2VertSpaceClient;

#include "snd_ambient.h"
#include "qcommon/timing.h"

#include <algorithm>

/*
Ghoul2 Insert End
*/

extern	botlib_export_t	*botlib_export;

extern qboolean loadCamera(const char *name);
extern void startCamera(int time);
extern qboolean getCameraInfo(int time, vec3_t *origin, vec3_t *angles);

/*
====================
CL_GetUserCmd
====================
*/
qboolean CL_GetUserCmd( int cmdNumber, usercmd_t *ucmd ) {
	// cmds[cmdNumber] is the last properly generated command

	// can't return anything that we haven't created yet
	if ( cmdNumber > cl.cmdNumber ) {
		Com_Error( ERR_DROP, "CL_GetUserCmd: %i >= %i", cmdNumber, cl.cmdNumber );
	}

	// the usercmd has been overwritten in the wrapping
	// buffer because it is too far out of date
	if ( cmdNumber <= cl.cmdNumber - CMD_BACKUP ) {
		return qfalse;
	}

	*ucmd = cl.cmds[ cmdNumber & CMD_MASK ];

	return qtrue;
}

/*
====================
CL_GetParseEntityState
====================
*/
qboolean	CL_GetParseEntityState( int parseEntityNumber, entityState_t *state ) {
	// can't return anything that hasn't been parsed yet
	if ( parseEntityNumber >= cl.parseEntitiesNum ) {
		Com_Error( ERR_DROP, "CL_GetParseEntityState: %i >= %i",
			parseEntityNumber, cl.parseEntitiesNum );
	}

	// can't return anything that has been overwritten in the circular buffer
	if ( parseEntityNumber <= cl.parseEntitiesNum - MAX_PARSE_ENTITIES ) {
		return qfalse;
	}

	*state = cl.parseEntities[ parseEntityNumber & ( MAX_PARSE_ENTITIES - 1 ) ];
	return qtrue;
}

/*
====================
CL_GetSnapshot
====================
*/
qboolean CL_GetSnapshot( int snapshotNumber, snapshot_t *snapshot ) {
	clSnapshot_t	*clSnap;
	int				i, count;

	if ( snapshotNumber > cl.snap.messageNum ) {
		Com_Error( ERR_DROP, "CL_GetSnapshot: snapshotNumber > cl.snapshot.messageNum" );
	}

	// if the frame has fallen out of the circular buffer, we can't return it
	if ( cl.snap.messageNum - snapshotNumber >= PACKET_BACKUP ) {
		return qfalse;
	}

	// if the frame is not valid, we can't return it
	clSnap = &cl.snapshots[snapshotNumber & PACKET_MASK];
	if ( !clSnap->valid ) {
		return qfalse;
	}

	// if the entities in the frame have fallen out of their
	// circular buffer, we can't return it
	if ( cl.parseEntitiesNum - clSnap->parseEntitiesNum >= MAX_PARSE_ENTITIES ) {
		return qfalse;
	}

	// write the snapshot
	snapshot->snapFlags = clSnap->snapFlags;
	snapshot->serverCommandSequence = clSnap->serverCommandNum;
	snapshot->ping = clSnap->ping;
	snapshot->serverTime = clSnap->serverTime;
	Com_Memcpy( snapshot->areamask, clSnap->areamask, sizeof( snapshot->areamask ) );
	snapshot->ps = clSnap->ps;
	snapshot->vps = clSnap->vps; //get the vehicle ps
	count = clSnap->numEntities;
	if ( count > MAX_ENTITIES_IN_SNAPSHOT ) {
		Com_DPrintf( "CL_GetSnapshot: truncated %i entities to %i\n", count, MAX_ENTITIES_IN_SNAPSHOT );
		count = MAX_ENTITIES_IN_SNAPSHOT;
	}
	snapshot->numEntities = count;

 	for ( i = 0 ; i < count ; i++ ) {

		int entNum =  ( clSnap->parseEntitiesNum + i ) & (MAX_PARSE_ENTITIES-1) ;

		// copy everything but the ghoul2 pointer
		memcpy(&snapshot->entities[i], &cl.parseEntities[ entNum ], sizeof(entityState_t));
	}

	// FIXME: configstring changes and server commands!!!

	return qtrue;
}

qboolean CL_GetDefaultState(int index, entityState_t *state)
{
	if (index < 0 || index >= MAX_GENTITIES)
	{
		return qfalse;
	}

	if (!(cl.entityBaselines[index].eFlags & EF_PERMANENT))
	{
		return qfalse;
	}

	*state = cl.entityBaselines[index];

	return qtrue;
}

/*
=====================
CL_SetUserCmdValue
=====================
*/
extern float cl_mPitchOverride;
extern float cl_mYawOverride;
extern float cl_mSensitivityOverride;
void CL_SetUserCmdValue( int userCmdValue, float sensitivityScale, float mPitchOverride, float mYawOverride, float mSensitivityOverride, int fpSel, int invenSel ) {
	cl.cgameUserCmdValue = userCmdValue;
	cl.cgameSensitivity = sensitivityScale;
	cl_mPitchOverride = mPitchOverride;
	cl_mYawOverride = mYawOverride;
	cl_mSensitivityOverride = mSensitivityOverride;
	cl.cgameForceSelection = fpSel;
	cl.cgameInvenSelection = invenSel;
}

int gCLTotalClientNum = 0;
//keep track of the total number of clients
extern cvar_t	*cl_autolodscale;
//if we want to do autolodscaling

void CL_DoAutoLODScale(void)
{
	float finalLODScaleFactor = 0;

	if ( gCLTotalClientNum >= 8 )
	{
		finalLODScaleFactor = (gCLTotalClientNum/-8.0f);
	}


	Cvar_Set( "r_autolodscalevalue", va("%f", finalLODScaleFactor) );
}

/*
=====================
CL_ConfigstringModified
=====================
*/
void CL_ConfigstringModified( void ) {
	char		*old, *s;
	int			i, index;
	char		*dup;
	gameState_t	oldGs;
	int			len;

	index = atoi( Cmd_Argv(1) );
	if ( index < 0 || index >= MAX_CONFIGSTRINGS ) {
		Com_Error( ERR_DROP, "CL_ConfigstringModified: bad index %i", index );
	}
	// get everything after "cs <num>"
	s = Cmd_ArgsFrom(2);

	old = cl.gameState.stringData + cl.gameState.stringOffsets[ index ];
	if ( !strcmp( old, s ) ) {
		return;		// unchanged
	}

	// build the new gameState_t
	oldGs = cl.gameState;

	Com_Memset( &cl.gameState, 0, sizeof( cl.gameState ) );

	// leave the first 0 for uninitialized strings
	cl.gameState.dataCount = 1;

	for ( i = 0 ; i < MAX_CONFIGSTRINGS ; i++ ) {
		if ( i == index ) {
			dup = s;
		} else {
			dup = oldGs.stringData + oldGs.stringOffsets[ i ];
		}
		if ( !dup[0] ) {
			continue;		// leave with the default empty string
		}

		len = strlen( dup );

		if ( len + 1 + cl.gameState.dataCount > MAX_GAMESTATE_CHARS ) {
			Com_Error( ERR_DROP, "MAX_GAMESTATE_CHARS exceeded" );
		}

		// append it to the gameState string buffer
		cl.gameState.stringOffsets[ i ] = cl.gameState.dataCount;
		Com_Memcpy( cl.gameState.stringData + cl.gameState.dataCount, dup, len + 1 );
		cl.gameState.dataCount += len + 1;
	}

	if (cl_autolodscale && cl_autolodscale->integer)
	{
		if (index >= CS_PLAYERS &&
			index < CS_G2BONES)
		{ //this means that a client was updated in some way. Go through and count the clients.
			int clientCount = 0;
			i = CS_PLAYERS;

			while (i < CS_G2BONES)
			{
				s = cl.gameState.stringData + cl.gameState.stringOffsets[ i ];

				if (s && s[0])
				{
					clientCount++;
				}

				i++;
			}

			gCLTotalClientNum = clientCount;

#ifdef _DEBUG
			Com_DPrintf("%i clients\n", gCLTotalClientNum);
#endif

			CL_DoAutoLODScale();
		}
	}

	if ( index == CS_SYSTEMINFO ) {
		// parse serverId and other cvars
		CL_SystemInfoChanged();
	}

}
#ifndef MAX_STRINGED_SV_STRING
	#define MAX_STRINGED_SV_STRING 1024
#endif
// just copied it from CG_CheckSVStringEdRef(
void CL_CheckSVStringEdRef(char *buf, const char *str)
{ //I don't really like doing this. But it utilizes the system that was already in place.
	int i = 0;
	int b = 0;
	int strLen = 0;
	qboolean gotStrip = qfalse;

	if (!str || !str[0])
	{
		if (str)
		{
			strcpy(buf, str);
		}
		return;
	}

	strcpy(buf, str);

	strLen = strlen(str);

	if (strLen >= MAX_STRINGED_SV_STRING)
	{
		return;
	}

	while (i < strLen && str[i])
	{
		gotStrip = qfalse;

		if (str[i] == '@' && (i+1) < strLen)
		{
			if (str[i+1] == '@' && (i+2) < strLen)
			{
				if (str[i+2] == '@' && (i+3) < strLen)
				{ //@@@ should mean to insert a StringEd reference here, so insert it into buf at the current place
					char stringRef[MAX_STRINGED_SV_STRING];
					int r = 0;

					while (i < strLen && str[i] == '@')
					{
						i++;
					}

					while (i < strLen && str[i] && str[i] != ' ' && str[i] != ':' && str[i] != '.' && str[i] != '\n')
					{
						stringRef[r] = str[i];
						r++;
						i++;
					}
					stringRef[r] = 0;

					buf[b] = 0;
					Q_strcat(buf, MAX_STRINGED_SV_STRING, SE_GetString("MP_SVGAME", stringRef));
					b = strlen(buf);
				}
			}
		}

		if (!gotStrip)
		{
			buf[b] = str[i];
			b++;
		}
		i++;
	}

	buf[b] = 0;
}
/*
===================
CL_GetServerCommand

Set up argc/argv for the given command
===================
*/
qboolean CL_GetServerCommand( int serverCommandNumber ) {
	char	*s;
	char	*cmd;
	static char bigConfigString[BIG_INFO_STRING];

	// if we have irretrievably lost a reliable command, drop the connection
	if ( serverCommandNumber <= clc.serverCommandSequence - MAX_RELIABLE_COMMANDS )
	{
		int i = 0;

		// when a demo record was started after the client got a whole bunch of
		// reliable commands then the client never got those first reliable commands
		if ( clc.demoplaying )
			return qfalse;

		while (i < MAX_RELIABLE_COMMANDS)
		{ //spew out the reliable command buffer
			if (clc.reliableCommands[i][0])
			{
				Com_Printf("%i: %s\n", i, clc.reliableCommands[i]);
			}
			i++;
		}
		Com_Error( ERR_DROP, "CL_GetServerCommand: a reliable command was cycled out" );
		return qfalse;
	}

	if ( serverCommandNumber > clc.serverCommandSequence ) {
		Com_Error( ERR_DROP, "CL_GetServerCommand: requested a command not received" );
		return qfalse;
	}

	s = clc.serverCommands[ serverCommandNumber & ( MAX_RELIABLE_COMMANDS - 1 ) ];
	clc.lastExecutedServerCommand = serverCommandNumber;

	Com_DPrintf( "serverCommand: %i : %s\n", serverCommandNumber, s );

rescan:
	Cmd_TokenizeString( s );
	cmd = Cmd_Argv(0);

	if ( !strcmp( cmd, "disconnect" ) ) {
		char strEd[MAX_STRINGED_SV_STRING];
		CL_CheckSVStringEdRef(strEd, Cmd_Argv(1));
		Com_Error (ERR_SERVERDISCONNECT, "%s: %s\n", SE_GetString("MP_SVGAME_SERVER_DISCONNECTED"), strEd );
	}

	if ( !strcmp( cmd, "bcs0" ) ) {
		Com_sprintf( bigConfigString, BIG_INFO_STRING, "cs %s \"%s", Cmd_Argv(1), Cmd_Argv(2) );
		return qfalse;
	}

	if ( !strcmp( cmd, "bcs1" ) ) {
		s = Cmd_Argv(2);
		if( strlen(bigConfigString) + strlen(s) >= BIG_INFO_STRING ) {
			Com_Error( ERR_DROP, "bcs exceeded BIG_INFO_STRING" );
		}
		strcat( bigConfigString, s );
		return qfalse;
	}

	if ( !strcmp( cmd, "bcs2" ) ) {
		s = Cmd_Argv(2);
		if( strlen(bigConfigString) + strlen(s) + 1 >= BIG_INFO_STRING ) {
			Com_Error( ERR_DROP, "bcs exceeded BIG_INFO_STRING" );
		}
		strcat( bigConfigString, s );
		strcat( bigConfigString, "\"" );
		s = bigConfigString;
		goto rescan;
	}

	if ( !strcmp( cmd, "cs" ) ) {
		CL_ConfigstringModified();
		// reparse the string, because CL_ConfigstringModified may have done another Cmd_TokenizeString()
		Cmd_TokenizeString( s );
		return qtrue;
	}

	if ( !strcmp( cmd, "map_restart" ) ) {
		// clear notify lines and outgoing commands before passing
		// the restart to the cgame
		Con_ClearNotify();
		// reparse the string, because Con_ClearNotify() may have done another Cmd_TokenizeString()
		Cmd_TokenizeString( s );
		Com_Memset( cl.cmds, 0, sizeof( cl.cmds ) );
		return qtrue;
	}

	// the clientLevelShot command is used during development
	// to generate 128*128 screenshots from the intermission
	// point of levels for the menu system to use
	// we pass it along to the cgame to make appropriate adjustments,
	// but we also clear the console and notify lines here
	if ( !strcmp( cmd, "clientLevelShot" ) ) {
		// don't do it if we aren't running the server locally,
		// otherwise malicious remote servers could overwrite
		// the existing thumbnails
		if ( !com_sv_running->integer ) {
			return qfalse;
		}
		// close the console
		Con_Close();
		// take a special screenshot next frame
		Cbuf_AddText( "wait ; wait ; wait ; wait ; screenshot levelshot\n" );
		return qtrue;
	}

	// we may want to put a "connect to other server" command here

	// cgame can now act on the command
	return qtrue;
}

/*
====================
CL_ShutdonwCGame

====================
*/
void CL_ShutdownCGame( void ) {
	Key_SetCatcher( Key_GetCatcher( ) & ~KEYCATCH_CGAME );

	if ( !cls.cgameStarted )
		return;

	cls.cgameStarted = qfalse;

	CL_UnbindCGame();
}

/*
====================
CL_InitCGame

Should only be called by CL_StartHunkUsers
====================
*/

void CL_InitCGame( void ) {
	const char			*info;
	const char			*mapname;
	int					t1, t2;

	t1 = Sys_Milliseconds();

	// put away the console
	Con_Close();

	// find the current mapname
	info = cl.gameState.stringData + cl.gameState.stringOffsets[ CS_SERVERINFO ];
	mapname = Info_ValueForKey( info, "mapname" );
	Com_sprintf( cl.mapname, sizeof( cl.mapname ), "maps/%s.bsp", mapname );

	// load the dll
	CL_BindCGame();

	cls.state = CA_LOADING;

	// init for this gamestate
	// use the lastExecutedServerCommand instead of the serverCommandSequence
	// otherwise server commands sent just before a gamestate are dropped
	CGVM_Init( clc.serverMessageSequence, clc.lastExecutedServerCommand, clc.clientNum );

	// reset any CVAR_CHEAT cvars registered by cgame
	if ( !clc.demoplaying && !cl_connectedToCheatServer )
		Cvar_SetCheatState();

	// we will send a usercmd this frame, which
	// will cause the server to send us the first snapshot
	cls.state = CA_PRIMED;

	t2 = Sys_Milliseconds();

	Com_Printf( "CL_InitCGame: %5.2f seconds\n", (t2-t1)/1000.0 );

	// have the renderer touch all its images, so they are present
	// on the card even if the driver does deferred loading
	re->EndRegistration();

	// make sure everything is paged in
//	if (!Sys_LowPhysicalMemory())
	{
		Com_TouchMemory();
	}

	// clear anything that got printed
	Con_ClearNotify ();
}


/*
====================
CL_GameCommand

See if the current console command is claimed by the cgame
====================
*/
qboolean CL_GameCommand( void ) {
	if ( !cls.cgameStarted )
		return qfalse;

	return CGVM_ConsoleCommand();
}



/*
=====================
CL_CGameRendering
=====================
*/
void CL_CGameRendering( stereoFrame_t stereo ) {
	//rww - RAGDOLL_BEGIN
	if (!com_sv_running->integer)
	{ //set the server time to match the client time, if we don't have a server going.
		re->G2API_SetTime(cl.serverTime, 0);
	}
	re->G2API_SetTime(cl.serverTime, 1);
	//rww - RAGDOLL_END

	CGVM_DrawActiveFrame( cl.serverTime, stereo, clc.demoplaying );
}


/*
=================
CL_AdjustTimeDelta

Adjust the clients view of server time.

The client shows the world at the time at which the next snapshot has
already arrived in INTERP_WINDOW - 1 cases out of INTERP_WINDOW (plan step 11,
docs_rewrite/netcode/10-network-settings-survey.md, point 5): interpolated,
at real positions, as recent as the measured jitter of the arrivals allows.

Adjustments are only made when a new snapshot arrives with a rational
latency, which keeps the adjustment process framerate independent and
prevents massive overadjustment during times of significant packet loss
or bursted delayed packets.
=================
*/

#define	RESET_TIME	500

// netstats: the interpolation delay since the last netstats
static struct {
	int		snapshots;		// snapshots that adjusted the client's time
	double	delaySum;		// how far behind each one the client's time was when it arrived, in ms
	int		extrapolated;	// intervals in which a frame passed the newest snapshot before the next one arrived
	int		lossMargin;		// snapshots that found the loss margin on
	int		jitter;			// the last computed: how much later than the median the second latest arrival was, in ms
} s_interpStats;

void CL_InterpStats( void ) {
	if ( !s_interpStats.snapshots ) {
		return;
	}
	Com_Printf( "interpolation: %.1f ms behind the newest snapshot when it arrives, jitter %d ms, %d of %d intervals "
		"extrapolated, loss margin %s%s\n", s_interpStats.delaySum / s_interpStats.snapshots, s_interpStats.jitter,
		s_interpStats.extrapolated, s_interpStats.snapshots,
		s_interpStats.lossMargin ? va( "on for %d snapshots", s_interpStats.lossMargin ) : "off",
		cl_debugInterpForce->integer ? " (delay forced by cl_debugInterpForce)" : "" );
	Com_Memset( &s_interpStats, 0, sizeof( s_interpStats ) );
}

static void CL_InterpPush( int offset, int read, int lost ) {
	cl.interpOffset[cl.interpNext] = offset;
	cl.interpRead[cl.interpNext] = read;
	cl.interpLost[cl.interpNext] = lost;
	cl.interpNext = ( cl.interpNext + 1 ) % INTERP_WINDOW;
	cl.interpCount = Q_min( cl.interpCount + 1, INTERP_WINDOW );
}

// A snapshot accepted by the parser: the server messages missing before it are counted here, for every snapshot, also
// when several are read within one frame, so that only the messages really missing count as lost. The offset enters
// the window in CL_AdjustTimeDelta, once per frame and for the newest snapshot only: the older ones read in the same
// frame waited for the client (a frame that took long, the scoreboard loading models), not for the network.
void CL_InterpRecord( void ) {
	if ( cl.interpParsed && cl.snap.messageNum > cl.interpLastMessage + 1 ) {
		cl.interpLostPending += cl.snap.messageNum - cl.interpLastMessage - 1;
	}
	cl.interpReadPending++;
	cl.interpLastMessage = cl.snap.messageNum;
	cl.interpParsed = qtrue;
}

// The serverTimeDelta to move towards. A snapshot is usable from its arrival A, and the client needs it once its time
// passes the serverTime of the previous snapshot, one interval earlier: so at A the client's time must not exceed
// serverTime - interval, that is serverTimeDelta <= offset - interval, where offset = serverTime - A. The target is
// the second smallest offset of the window minus the interval (the second, so that one isolated spike does not raise
// the delay), minus one more interval while more than 5 % of the server messages of the window are lost (removed
// below 2 %): a lost snapshot is otherwise bridged by extrapolation. qfalse while the window holds fewer than two.
static qboolean CL_InterpTarget( int *target ) {
	int sorted[INTERP_WINDOW];
	int read = 0, lost = 0;
	const int interval = TICK_MSEC;	// the server sends a snapshot every tick

	if ( cl.interpCount < 2 ) {
		return qfalse;
	}
	for ( int i = 0; i < cl.interpCount; i++ ) {
		sorted[i] = cl.interpOffset[i];
		read += cl.interpRead[i];
		lost += cl.interpLost[i];
	}
	std::sort( sorted, sorted + cl.interpCount );
	const int total = read + lost;
	if ( lost * 20 > total ) {
		cl.interpLossMargin = qtrue;
	} else if ( lost * 50 < total ) {
		cl.interpLossMargin = qfalse;
	}
	const int median = sorted[cl.interpCount / 2];
	s_interpStats.jitter = median - sorted[1];
	s_interpStats.lossMargin += cl.interpLossMargin ? 1 : 0;

	if ( cl_debugInterpForce->integer ) {
		*target = median - cl_debugInterpDelay->integer;
	} else {
		*target = sorted[1] - interval - ( cl.interpLossMargin ? interval : 0 );
	}
	return qtrue;
}

void CL_AdjustTimeDelta( void ) {
	int		newDelta;
	int		deltaDelta;
	int		target;

	cl.newSnapshots = qfalse;

	// the delta never drifts when replaying a demo
	if ( clc.demoplaying ) {
		return;
	}

	newDelta = cl.snap.serverTime - cls.realtime;
	deltaDelta = abs( newDelta - cl.serverTimeDelta );

	// A snapshot that arrived more than 100 ms earlier against its serverTime than any snapshot of the window: jitter
	// only makes arrivals later, so the server's clock jumped forward against the client's (map_restart runs three
	// frames of 100 ms at once; or a much shorter path). The client's time and the window move forward by the jump,
	// estimated against the window's median, at once: the delay stays what it was. Kept as they were, the older
	// offsets held the client 350 ms behind after a map_restart for INTERP_WINDOW frames, past the 64 user commands
	// it keeps ("Connection Interrupted"). The jump is measured on the second such snapshot: the first one after a
	// map_restart leaves late, by the time the server took to restart the map (about 40 ms short, seen in game).
	if ( deltaDelta <= RESET_TIME && cl.interpCount ) {
		int sorted[INTERP_WINDOW];
		for ( int i = 0; i < cl.interpCount; i++ ) {
			sorted[i] = cl.interpOffset[i];
		}
		std::sort( sorted, sorted + cl.interpCount );
		if ( newDelta > sorted[cl.interpCount - 1] + 100 ) {
			if ( !cl.interpJumpPending ) {
				cl.interpJumpPending = qtrue;	// this frame is left out: its snapshots count with the next one
				return;
			}
			const int jump = newDelta - sorted[cl.interpCount / 2];
			for ( int i = 0; i < cl.interpCount; i++ ) {
				cl.interpOffset[i] += jump;
			}
			cl.serverTimeDelta += jump;
			deltaDelta = abs( newDelta - cl.serverTimeDelta );
			if ( cl_showTimeDelta->integer ) {
				Com_Printf( "<JUMP %i> ", jump );
			}
		}
	}
	cl.interpJumpPending = qfalse;

	if ( deltaDelta <= RESET_TIME ) {	// a reset of the time is not a delay
		s_interpStats.snapshots++;
		s_interpStats.delaySum += newDelta - cl.serverTimeDelta;
		s_interpStats.extrapolated += cl.extrapolatedSnapshot ? 1 : 0;
	}
	cl.extrapolatedSnapshot = qfalse;

	// the newest snapshot's offset enters the window, which restarts with a reset of the time
	if ( deltaDelta > RESET_TIME ) {
		cl.interpCount = 0;
		cl.interpNext = 0;
		cl.interpLostPending = 0;
	}
	CL_InterpPush( newDelta, cl.interpReadPending, cl.interpLostPending );
	cl.interpReadPending = 0;
	cl.interpLostPending = 0;

	if ( deltaDelta > RESET_TIME ) {
		cl.serverTimeDelta = newDelta;
		cl.oldServerTime = cl.snap.serverTime;	// FIXME: is this a problem for cgame?
		cl.serverTime = cl.snap.serverTime;
		if ( cl_showTimeDelta->integer ) {
			Com_Printf( "<RESET> " );
		}
	} else if ( !CL_InterpTarget( &target ) ) {
		// fewer than two snapshots measured: fast adjust towards the newest one, as before
		if ( deltaDelta > 100 ) {
			if ( cl_showTimeDelta->integer ) {
				Com_Printf( "<FAST> " );
			}
			cl.serverTimeDelta = ( cl.serverTimeDelta + newDelta ) >> 1;
		}
	} else if ( abs( target - cl.serverTimeDelta ) > 100 ) {
		// fast adjust, cut the difference to the target in half (measured against the newest snapshot, as before, it
		// cut in half every delay above 100 ms, so the delay with the loss margin swung between about 50 and 100 ms)
		if ( cl_showTimeDelta->integer ) {
			Com_Printf( "<FAST> " );
		}
		cl.serverTimeDelta = ( cl.serverTimeDelta + target ) >> 1;
	} else {
		// slow drift towards the target, only move 1 msec forward or 2 msec back: time never jumps
		// the granularity of +1 / -2 is too high for timescale modified frametimes
		if ( com_timescale->value == 0 || com_timescale->value == 1 ) {
			if ( cl.serverTimeDelta < target ) {
				cl.serverTimeDelta++;
			} else if ( cl.serverTimeDelta > target ) {
				cl.serverTimeDelta -= Q_min( 2, cl.serverTimeDelta - target );
			}
		}
	}

	if ( cl_showTimeDelta->integer ) {
		Com_Printf( "%i ", cl.serverTimeDelta );
	}
}


/*
==================
CL_FirstSnapshot
==================
*/
void CL_FirstSnapshot( void ) {
	// ignore snapshots that don't have entities
	if ( cl.snap.snapFlags & SNAPFLAG_NOT_ACTIVE ) {
		return;
	}

	re->RegisterMedia_LevelLoadEnd();

	cls.state = CA_ACTIVE;

	// set the timedelta so we are exactly on this first frame
	cl.serverTimeDelta = cl.snap.serverTime - cls.realtime;
	cl.oldServerTime = cl.snap.serverTime;

	clc.timeDemoBaseTime = cl.snap.serverTime;

	// if this is the first frame of active play,
	// execute the contents of activeAction now
	// this is to allow scripting a timedemo to start right
	// after loading
	if ( cl_activeAction->string[0] ) {
		Cbuf_AddText( cl_activeAction->string );
		Cvar_Set( "activeAction", "" );
	}
}

/*
==================
CL_SetCGameTime
==================
*/
void CL_SetCGameTime( void ) {
	// getting a valid frame message ends the connection process
	if ( cls.state != CA_ACTIVE ) {
		if ( cls.state != CA_PRIMED ) {
			return;
		}
		if ( clc.demoplaying ) {
			// we shouldn't get the first snapshot on the same frame
			// as the gamestate, because it causes a bad time skip
			if ( !clc.firstDemoFrameSkipped ) {
				clc.firstDemoFrameSkipped = qtrue;
				return;
			}
			CL_ReadDemoMessage();
		}
		if ( cl.newSnapshots ) {
			cl.newSnapshots = qfalse;
			CL_FirstSnapshot();
		}
		if ( cls.state != CA_ACTIVE ) {
			return;
		}
	}

	// if we have gotten to this point, cl.snap is guaranteed to be valid
	if ( !cl.snap.valid ) {
		Com_Error( ERR_DROP, "CL_SetCGameTime: !cl.snap.valid" );
	}

	// allow pause in single player
	if ( sv_paused->integer && CL_CheckPaused() && com_sv_running->integer ) {
		// paused
		return;
	}

	if ( cl.snap.serverTime < cl.oldFrameServerTime ) {
		Com_Error( ERR_DROP, "cl.snap.serverTime < cl.oldFrameServerTime" );
	}
	cl.oldFrameServerTime = cl.snap.serverTime;


	// get our current view of time

	if ( clc.demoplaying && cl_freezeDemo->integer ) {
		// cl_freezeDemo is used to lock a demo in place for single frame advances
	} else
	{
		// the delay behind the newest snapshot is computed in CL_AdjustTimeDelta (cl_timeNudge removed, plan step 11)
		cl.serverTime = cls.realtime + cl.serverTimeDelta;

		// guarantee that time will never flow backwards, even if
		// serverTimeDelta made an adjustment
		if ( cl.serverTime < cl.oldServerTime ) {
			cl.serverTime = cl.oldServerTime;
		}
		cl.oldServerTime = cl.serverTime;

		// note if we passed the newest snapshot before the next one arrived (netstats)
		if ( cl.serverTime > cl.snap.serverTime ) {
			cl.extrapolatedSnapshot = qtrue;
		}
	}

	// if we have gotten new snapshots, drift serverTimeDelta
	// don't do this every frame, or a period of packet loss would
	// make a huge adjustment
	if ( cl.newSnapshots ) {
		CL_AdjustTimeDelta();
	}

	if ( !clc.demoplaying ) {
		return;
	}

	// if we are playing a demo back, we can just keep reading
	// messages from the demo file until the cgame definately
	// has valid snapshots to interpolate between

	// a timedemo will always use a deterministic set of time samples
	// no matter what speed machine it is run on,
	// while a normal demo may have different time samples
	// each time it is played back
	if ( cl_timedemo->integer ) {
		if (!clc.timeDemoStart) {
			clc.timeDemoStart = Sys_Milliseconds();
		}
		clc.timeDemoFrames++;
		cl.serverTime = clc.timeDemoBaseTime + clc.timeDemoFrames * TICK_MSEC;
	}

	while ( cl.serverTime >= cl.snap.serverTime ) {
		// feed another messag, which should change
		// the contents of cl.snap
		CL_ReadDemoMessage();
		if ( cls.state != CA_ACTIVE ) {
			return;		// end of demo
		}
	}
}
