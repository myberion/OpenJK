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

	// the view angles of now, for cgame's drawing of the local player (plan step 14: commands are made per tick)
	if ( cmdNumber == cl.cmdNumber + 1 ) {
		return CL_PendingCmd( ucmd );
	}

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

The client shows the world as late as the arrivals of the snapshots require and no later (plan step 11,
docs_rewrite/modernization/surveys/network-settings.md, point 5; redone in plan step 14, part 4e). At every frame its
time must not pass the newest snapshot's, else that frame extrapolates: serverTimeDelta must stay at or below the
newest snapshot's serverTime minus the frame's real time. Each interval between two snapshot arrivals gives one
sample, that value at the last frame drawn before the next snapshot came. The window keeps the smallest sample of
each of its last CLOCK_SLOTS slots of CLOCK_SLOT_MSEC (about 2 s), and the target is the second smallest of them
minus CLOCK_SAFETY_MSEC: one isolated late snapshot does not raise the delay, two do, and so do lost snapshots that
come more than once in the window (the frames before the snapshot after a lost one give a sample one interval
lower). An interval longer than CLOCK_MAX_GAP_MSEC is an interruption, not jitter: no delay bridges it, and it gives
no sample.

The time moves towards the target every frame, by at most 1 ms per CLOCK_SPEEDUP_MSEC or CLOCK_SLOWDOWN_MSEC of
real time (about 6 and 7 %, the limits of Counter-Strike 2's clock: surveys/tick-and-lag-compensation.md, section 11).
=================
*/

#define	RESET_TIME	500

// netstats: the interpolation delay since the last netstats
static struct {
	int		snapshots;		// frames that brought snapshots
	double	delaySum;		// how far behind the newest one the client's time was in each, in ms
	int		extrapolated;	// intervals in which a frame passed the newest snapshot before the next one arrived
	int		jitter;			// the last computed: the window's median sample minus its smallest, in ms
} s_interpStats;

void CL_InterpStats( void ) {
	if ( !s_interpStats.snapshots ) {
		return;
	}
	Com_Printf( "interpolation: %.1f ms behind the newest snapshot when it arrives, jitter %d ms, %d of %d intervals "
		"extrapolated%s\n", s_interpStats.delaySum / s_interpStats.snapshots, s_interpStats.jitter,
		s_interpStats.extrapolated, s_interpStats.snapshots,
		cl_debugInterpForce->integer ? " (delay forced by cl_debugInterpForce)" : "" );
	Com_Memset( &s_interpStats, 0, sizeof( s_interpStats ) );
}

static void CL_ClockClear( clockWindow_t *w ) {
	for ( int i = 0; i < CLOCK_SLOTS; i++ ) {
		w->low[i] = INT_MAX;
	}
	w->slot = cls.realtime / CLOCK_SLOT_MSEC;
	w->started = qtrue;
}

// a sample enters the slot of the present real time; the slots without a sample stay empty
static void CL_ClockPush( clockWindow_t *w, int sample ) {
	const int slot = cls.realtime / CLOCK_SLOT_MSEC;

	if ( !w->started || slot < w->slot || slot - w->slot >= CLOCK_SLOTS ) {
		CL_ClockClear( w );
	}
	while ( w->slot < slot ) {
		w->slot++;
		w->low[w->slot % CLOCK_SLOTS] = INT_MAX;
	}
	w->low[slot % CLOCK_SLOTS] = Q_min( w->low[slot % CLOCK_SLOTS], sample );
}

// the smallest samples of the slots of the last CLOCK_SLOTS * CLOCK_SLOT_MSEC, in rising order; returns how many
// slots hold one (the older slots have expired, also when no sample came since)
static int CL_ClockSorted( const clockWindow_t *w, int *sorted ) {
	const int now = cls.realtime / CLOCK_SLOT_MSEC;
	int count = 0;

	if ( !w->started || now < w->slot ) {
		return 0;
	}
	for ( int s = Q_max( w->slot, now ) - CLOCK_SLOTS + 1; s <= w->slot; s++ ) {
		if ( s >= 0 && w->low[s % CLOCK_SLOTS] != INT_MAX ) {
			sorted[count++] = w->low[s % CLOCK_SLOTS];
		}
	}
	std::sort( sorted, sorted + count );
	return count;
}

// the clock the samples are measured against jumped
static void CL_ClockShift( clockWindow_t *w, int by ) {
	for ( int i = 0; w->started && i < CLOCK_SLOTS; i++ ) {
		if ( w->low[i] != INT_MAX ) {
			w->low[i] += by;
		}
	}
}

// moves *delta towards target by the steps msec of real time allow; credit holds the time not yet used
static void CL_ClockSlew( int *delta, int target, int *credit, int msec ) {
	*credit += msec;
	while ( *delta < target && *credit >= CLOCK_SPEEDUP_MSEC ) {
		( *delta )++;
		*credit -= CLOCK_SPEEDUP_MSEC;
	}
	while ( *delta > target && *credit >= CLOCK_SLOWDOWN_MSEC ) {
		( *delta )--;
		*credit -= CLOCK_SLOWDOWN_MSEC;
	}
	if ( *delta == target ) {
		*credit = 0;
	}
}

/*
=================
CL_AdjustCmdDelta

The command clock (plan step 14, part 4c). The server reports in each snapshot the input margin: how early the command
of the snapshot's tick reached it, in ms (negative: after its tick; SV_RunClientCommands). A command made with a larger
cmdTimeDelta is made earlier, so its margin is larger by as much: each snapshot gives one sample, its margin minus the
cmdTimeDelta its command was made with, the margin a cmdTimeDelta of 0 would have had. The window keeps the smallest
sample of each of its last CLOCK_SLOTS slots, and cmdTimeDelta moves, at the clocks' bounded speed, to where the second
smallest of them becomes CMD_MARGIN_MSEC: one late command in the window does not move the clock, two do, and so does
loss (a command that arrives with the next packet is a tick late). The delay of a key press to the server is then the
trip of a packet plus that margin and the jitter of the client's own packets, for every player alike.

Until the window has CLOCK_MIN_SLOTS slots (a connection) and the target is more than a tick away, or when it is more
than 100 ms away, the clock is set at once; the margins of the commands made before are then left out.
=================
*/
static struct {
	int		snapshots;		// input margins received
	double	marginSum;
	int		marginMin;
	int		late;			// of them, negative ones
} s_cmdStats;

void CL_CmdClockStats( void ) {
	if ( !s_cmdStats.snapshots ) {
		return;
	}
	Com_Printf( "command clock: %d ms ahead of the drawn time; input margin on the server: mean %.1f ms, lowest %d ms, "
		"%d of %d after their tick\n", cl.cmdTimeDelta - cl.serverTimeDelta, s_cmdStats.marginSum / s_cmdStats.snapshots,
		s_cmdStats.marginMin, s_cmdStats.late, s_cmdStats.snapshots );
	Com_Memset( &s_cmdStats, 0, sizeof( s_cmdStats ) );
}

// the server's clock jumped by `jump` ms (a map_restart): the command clock follows, its samples with it
static void CL_CmdClockJump( int jump ) {
	cl.cmdTimeDelta += jump;
	CL_ClockShift( &cl.cmdWindow, -jump );
}

// the client's time was reset: the command clock starts again from the drawn time, as at the first snapshot
static void CL_CmdClockReset( void ) {
	cl.cmdTimeDelta = cl.serverTimeDelta;
	cl.cmdWindow.started = qfalse;
	cl.cmdCredit = 0;
	cl.oldCmdTime = 0;
	cl.cmdSettleTime = 0;
}

static void CL_AdjustCmdDelta( int msec ) {
	int		sorted[CLOCK_SLOTS];
	int		count;

	if ( clc.demoplaying ) {
		return;
	}

	if ( cl.snap.messageNum != cl.cmdMarginMessage ) {
		cl.cmdMarginMessage = cl.snap.messageNum;

		// A margin is a sample only once (the server sends each measure in one snapshot), and only when it is about a
		// command made on the present clock: after the clock was set at once, the commands made before keep arriving
		// with the old clock's margins until the server's time reaches the first command made after.
		if ( cl.snap.inputMarginValid && cl.snap.serverTime >= cl.cmdSettleTime ) {
			// the command the margin is about: the newest one up to the snapshot's tick
			for ( int i = 0; i < CMD_BACKUP && i < cl.cmdNumber; i++ ) {
				const int n = ( cl.cmdNumber - i ) & CMD_MASK;
				if ( cl.cmds[n].serverTime > cl.snap.serverTime ) {
					continue;
				}
				const int sample = cl.snap.inputMargin - cl.cmdDeltaAt[n];

				// a command later than the window's by more than CLOCK_MAX_GAP_MSEC is an interruption, not jitter: no
				// margin covers it, and it is no sample (if the link itself changed, the window empties in its length and
				// the clock is set again)
				count = CL_ClockSorted( &cl.cmdWindow, sorted );
				if ( count < CLOCK_MIN_SLOTS || sample >= sorted[count / 2] - CLOCK_MAX_GAP_MSEC ) {
					CL_ClockPush( &cl.cmdWindow, sample );
				}
				break;
			}
			s_cmdStats.marginMin = s_cmdStats.snapshots ? Q_min( s_cmdStats.marginMin, cl.snap.inputMargin ) : cl.snap.inputMargin;
			s_cmdStats.snapshots++;
			s_cmdStats.marginSum += cl.snap.inputMargin;
			s_cmdStats.late += cl.snap.inputMargin < 0 ? 1 : 0;
		}
	}

	count = CL_ClockSorted( &cl.cmdWindow, sorted );
	if ( !count ) {
		return;
	}
	const int target = CMD_MARGIN_MSEC - sorted[count >= CLOCK_MIN_SLOTS ? 1 : 0];
	const int off = abs( target - cl.cmdTimeDelta );
	if ( off > 100 || ( count < CLOCK_MIN_SLOTS && off > TICK_MSEC ) ) {
		// far off, or not yet set (a connection): at once
		cl.cmdTimeDelta = target;
		cl.cmdCredit = 0;
		cl.cmdSettleTime = Q_max( cls.realtime + cl.cmdTimeDelta, cl.oldCmdTime ) + TICK_MSEC;
	} else if ( com_timescale->value == 0 || com_timescale->value == 1 ) {
		CL_ClockSlew( &cl.cmdTimeDelta, target, &cl.cmdCredit, msec );
	}
}

// called every frame of the game
void CL_AdjustTimeDelta( void ) {
	const qboolean newSnapshots = cl.newSnapshots;
	int		sorted[CLOCK_SLOTS];
	int		count;

	cl.newSnapshots = qfalse;

	// the delta never drifts when replaying a demo
	if ( clc.demoplaying ) {
		return;
	}

	const int msec = Q_min( Q_max( cls.realtime - cl.interpRealtime, 0 ), 200 );
	cl.interpRealtime = cls.realtime;

	CL_AdjustCmdDelta( msec );

	// the largest serverTimeDelta this frame allows
	const int newDelta = cl.snap.serverTime - cls.realtime;

	if ( newSnapshots ) {
		int deltaDelta = abs( newDelta - cl.serverTimeDelta );

		// A snapshot that arrived more than 100 ms earlier against its serverTime than any sample of the window: jitter
		// only makes arrivals later, so the server's clock jumped forward against the client's (map_restart runs three
		// frames of 100 ms at once; or a much shorter path). The client's time and the window move forward by the jump,
		// estimated against the window's median, at once: the delay stays about what it was. Kept as they were, the older
		// samples held the client 350 ms behind after a map_restart for the length of the window. The jump is measured
		// on the second such snapshot: the first one after a map_restart leaves late, by the time the server took to
		// restart the map (about 40 ms short, seen in game).
		count = CL_ClockSorted( &cl.interpWindow, sorted );
		if ( deltaDelta <= RESET_TIME && count && newDelta > sorted[count - 1] + 100 ) {
			if ( !cl.interpJumpPending ) {
				cl.interpJumpPending = qtrue;	// this frame is left out
				cl.interpPendingValid = qfalse;
				return;
			}
			// a sample is taken up to an interval after its snapshot arrived, this one at the arrival
			const int jump = newDelta - TICK_MSEC - sorted[count / 2];
			CL_ClockShift( &cl.interpWindow, jump );
			cl.serverTimeDelta += jump;
			CL_CmdClockJump( jump );
			deltaDelta = abs( newDelta - cl.serverTimeDelta );
			if ( cl_showTimeDelta->integer ) {
				Com_Printf( "<JUMP %i> ", jump );
			}
		}
		cl.interpJumpPending = qfalse;

		if ( deltaDelta > RESET_TIME ) {
			// a reset of the time: the window restarts, one interval behind the newest snapshot until it has samples
			cl.interpWindow.started = qfalse;
			cl.serverTimeDelta = newDelta - TICK_MSEC;
			cl.oldServerTime = cl.snap.serverTime;	// FIXME: is this a problem for cgame?
			cl.serverTime = cl.snap.serverTime;
			CL_CmdClockReset();
			if ( cl_showTimeDelta->integer ) {
				Com_Printf( "<RESET> " );
			}
		} else {
			s_interpStats.snapshots++;
			s_interpStats.delaySum += newDelta - cl.serverTimeDelta;
			s_interpStats.extrapolated += cl.extrapolatedSnapshot ? 1 : 0;

			// the interval that just ended: its sample is the last frame's before these snapshots
			if ( cl.interpPendingValid && cls.realtime - cl.interpArrival <= CLOCK_MAX_GAP_MSEC ) {
				CL_ClockPush( &cl.interpWindow, cl.interpPending );
			}
		}
		cl.extrapolatedSnapshot = qfalse;
		cl.interpPending = newDelta;
		cl.interpPendingValid = qtrue;
		cl.interpArrival = cls.realtime;
	} else if ( cl.interpPendingValid ) {
		cl.interpPending = Q_min( cl.interpPending, newDelta );
	}

	count = CL_ClockSorted( &cl.interpWindow, sorted );
	if ( !count ) {
		return;	// nothing measured yet: one interval behind the first snapshot
	}
	s_interpStats.jitter = sorted[count / 2] - sorted[0];

	// with few slots filled, none is set aside
	int target = sorted[count >= CLOCK_MIN_SLOTS ? 1 : 0] - CLOCK_SAFETY_MSEC;
	if ( cl_debugInterpForce->integer ) {
		// the median sample is about an interval below the median arrival
		target = sorted[count / 2] + TICK_MSEC - cl_debugInterpDelay->integer;
	}

	if ( abs( target - cl.serverTimeDelta ) > 100 ) {
		// fast adjust, cut the difference to the target in half
		if ( cl_showTimeDelta->integer ) {
			Com_Printf( "<FAST> " );
		}
		cl.serverTimeDelta = ( cl.serverTimeDelta + target ) >> 1;
	} else if ( com_timescale->value == 0 || com_timescale->value == 1 ) {
		// the steps of 1 ms are too coarse for timescale modified frametimes
		CL_ClockSlew( &cl.serverTimeDelta, target, &cl.interpCredit, msec );
	}

	if ( cl_showTimeDelta->integer && newSnapshots ) {
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

	// set the timedelta so we are exactly on this first frame; in a game one interval behind it, the delay the
	// client keeps until its window has samples (CL_AdjustTimeDelta): at no delay it would extrapolate
	cl.serverTimeDelta = cl.snap.serverTime - cls.realtime - ( clc.demoplaying ? 0 : TICK_MSEC );
	// the command clock starts on the drawn time: the first input margins the server reports set it
	cl.cmdTimeDelta = cl.serverTimeDelta;
	cl.cmdWindow.started = qfalse;
	cl.oldCmdTime = 0;
	cl.cmdSettleTime = 0;
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

	// drift serverTimeDelta: the target moves only with new snapshots, so a period of packet loss makes no
	// adjustment; the time moves towards it every frame
	CL_AdjustTimeDelta();

	// the command clock (a demo's local player is drawn at the demo's time)
	cl.cmdTime = clc.demoplaying ? cl.serverTime : cls.realtime + cl.cmdTimeDelta;
	if ( cl.cmdTime < cl.oldCmdTime ) {
		cl.cmdTime = cl.oldCmdTime;
	}
	cl.oldCmdTime = cl.cmdTime;

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
