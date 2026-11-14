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

// cl.input.c  -- builds an intended movement command to send to the server

#include "client.h"
#include "cl_cgameapi.h"
#include "cl_uiapi.h"
#ifndef _WIN32
#include <cmath>
#endif
unsigned	frame_msec;		// the frame's length: the view turns every frame
int			old_com_frameTime;
// One user command per tick (plan step 14): the key states are measured over the time since the last command, and the
// movement the mouse gives while strafing or without mouse look is gathered over the frames until the next command.
static unsigned	s_cmdMsec;			// the input's time since the last command was made
static int		s_lastCmdFrameTime;
// The input's time, on the clock of com_frameTime and of the key events: the frame's time when a frame reads the input,
// the present time when a tick's command is made between two frames (CL_InputTick)
static int		s_inputTime;
static float	s_mouseSide, s_mouseForward;

float cl_mPitchOverride = 0.0f;
float cl_mYawOverride = 0.0f;
float cl_mSensitivityOverride = 0.0f;
qboolean cl_bUseFighterPitch = qfalse;
qboolean cl_crazyShipControls = qfalse;

#ifdef VEH_CONTROL_SCHEME_4
#define	OVERRIDE_MOUSE_SENSITIVITY 5.0f//20.0f = 180 degree turn in one mouse swipe across keyboard
#else// VEH_CONTROL_SCHEME_4
#define	OVERRIDE_MOUSE_SENSITIVITY 10.0f//20.0f = 180 degree turn in one mouse swipe across keyboard
#endif// VEH_CONTROL_SCHEME_4
/*
===============================================================================

KEY BUTTONS

Continuous button event tracking is complicated by the fact that two different
input sources (say, mouse button 1 and the control key) can both press the
same button, but the button should only be released when both of the
pressing key have been released.

When a key event issues a button command (+forward, +attack, etc), it appends
its key number as argv(1) so it can be matched up with the release.

argv(2) will be set to the time the event happened, which allows exact
control even at low framerates when the down and up events may both get qued
at the same time.

===============================================================================
*/


kbutton_t	in_left, in_right, in_forward, in_back;
kbutton_t	in_lookup, in_lookdown, in_moveleft, in_moveright;
kbutton_t	in_strafe, in_speed;
kbutton_t	in_up, in_down;

#define MAX_KBUTTONS 16

kbutton_t	in_buttons[MAX_KBUTTONS];


qboolean	in_mlooking;

void IN_Button11Down(void);
void IN_Button11Up(void);
void IN_Button10Down(void);
void IN_Button10Up(void);
void IN_Button6Down(void);
void IN_Button6Up(void);
void IN_UseGivenForce(void)
{
	char *c = Cmd_Argv(1);
	int forceNum =-1;
	int genCmdNum = 0;

	if(c) {
		forceNum = atoi(c);
	} else {
		return;
	}

	switch(forceNum) {
	case FP_DRAIN:
		IN_Button11Down();
		IN_Button11Up();
		break;
	case FP_PUSH:
		genCmdNum = GENCMD_FORCE_THROW;
		break;
	case FP_SPEED:
		genCmdNum = GENCMD_FORCE_SPEED;
		break;
	case FP_PULL:
		genCmdNum = GENCMD_FORCE_PULL;
		break;
	case FP_TELEPATHY:
		genCmdNum = GENCMD_FORCE_DISTRACT;
		break;
	case FP_GRIP:
		IN_Button6Down();
		IN_Button6Up();
		break;
	case FP_LIGHTNING:
		IN_Button10Down();
		IN_Button10Up();
		break;
	case FP_RAGE:
		genCmdNum = GENCMD_FORCE_RAGE;
		break;
	case FP_PROTECT:
		genCmdNum = GENCMD_FORCE_PROTECT;
		break;
	case FP_ABSORB:
		genCmdNum = GENCMD_FORCE_ABSORB;
		break;
	case FP_SEE:
		genCmdNum = GENCMD_FORCE_SEEING;
		break;
	case FP_HEAL:
		genCmdNum = GENCMD_FORCE_HEAL;
		break;
	case FP_TEAM_HEAL:
		genCmdNum = GENCMD_FORCE_HEALOTHER;
		break;
	case FP_TEAM_FORCE:
		genCmdNum = GENCMD_FORCE_FORCEPOWEROTHER;
		break;
	default:
		assert(0);
		break;
	}

	if(genCmdNum != 0) {
		cl.gcmdSendValue = qtrue;
		cl.gcmdValue = genCmdNum;
	}
}

void IN_MLookDown( void ) {
	in_mlooking = qtrue;
}

void IN_CenterView( void );
void IN_MLookUp( void ) {
	in_mlooking = qfalse;
	if ( !cl_freelook->integer ) {
		IN_CenterView ();
	}
}

void IN_GenCMD1( void )
{
	cl.gcmdSendValue = qtrue;
	cl.gcmdValue = GENCMD_SABERSWITCH;
}

void IN_GenCMD2( void )
{
	cl.gcmdSendValue = qtrue;
	cl.gcmdValue = GENCMD_ENGAGE_DUEL;
}

void IN_GenCMD3( void )
{
	cl.gcmdSendValue = qtrue;
	cl.gcmdValue = GENCMD_FORCE_HEAL;
}

void IN_GenCMD4( void )
{
	cl.gcmdSendValue = qtrue;
	cl.gcmdValue = GENCMD_FORCE_SPEED;
}

void IN_GenCMD5( void )
{
	cl.gcmdSendValue = qtrue;
	cl.gcmdValue = GENCMD_FORCE_PULL;
}

void IN_GenCMD6( void )
{
	cl.gcmdSendValue = qtrue;
	cl.gcmdValue = GENCMD_FORCE_DISTRACT;
}

void IN_GenCMD7( void )
{
	cl.gcmdSendValue = qtrue;
	cl.gcmdValue = GENCMD_FORCE_RAGE;
}

void IN_GenCMD8( void )
{
	cl.gcmdSendValue = qtrue;
	cl.gcmdValue = GENCMD_FORCE_PROTECT;
}

void IN_GenCMD9( void )
{
	cl.gcmdSendValue = qtrue;
	cl.gcmdValue = GENCMD_FORCE_ABSORB;
}

void IN_GenCMD10( void )
{
	cl.gcmdSendValue = qtrue;
	cl.gcmdValue = GENCMD_FORCE_HEALOTHER;
}

void IN_GenCMD11( void )
{
	cl.gcmdSendValue = qtrue;
	cl.gcmdValue = GENCMD_FORCE_FORCEPOWEROTHER;
}

void IN_GenCMD12( void )
{
	cl.gcmdSendValue = qtrue;
	cl.gcmdValue = GENCMD_FORCE_SEEING;
}

void IN_GenCMD13( void )
{
	cl.gcmdSendValue = qtrue;
	cl.gcmdValue = GENCMD_USE_SEEKER;
}

void IN_GenCMD14( void )
{
	cl.gcmdSendValue = qtrue;
	cl.gcmdValue = GENCMD_USE_FIELD;
}

void IN_GenCMD15( void )
{
	cl.gcmdSendValue = qtrue;
	cl.gcmdValue = GENCMD_USE_BACTA;
}

void IN_GenCMD16( void )
{
	cl.gcmdSendValue = qtrue;
	cl.gcmdValue = GENCMD_USE_ELECTROBINOCULARS;
}

void IN_GenCMD17( void )
{
	cl.gcmdSendValue = qtrue;
	cl.gcmdValue = GENCMD_ZOOM;
}

void IN_GenCMD18( void )
{
	cl.gcmdSendValue = qtrue;
	cl.gcmdValue = GENCMD_USE_SENTRY;
}

void IN_GenCMD19( void )
{
	if (Cvar_VariableIntegerValue("d_saberStanceDebug"))
	{
		Com_Printf("SABERSTANCEDEBUG: Gencmd on client set successfully.\n");
	}
	cl.gcmdSendValue = qtrue;
	cl.gcmdValue = GENCMD_SABERATTACKCYCLE;
}

void IN_GenCMD20( void )
{
	cl.gcmdSendValue = qtrue;
	cl.gcmdValue = GENCMD_FORCE_THROW;
}

void IN_GenCMD21( void )
{
	cl.gcmdSendValue = qtrue;
	cl.gcmdValue = GENCMD_USE_JETPACK;
}

void IN_GenCMD22( void )
{
	cl.gcmdSendValue = qtrue;
	cl.gcmdValue = GENCMD_USE_BACTABIG;
}

void IN_GenCMD23( void )
{
	cl.gcmdSendValue = qtrue;
	cl.gcmdValue = GENCMD_USE_HEALTHDISP;
}

void IN_GenCMD24( void )
{
	cl.gcmdSendValue = qtrue;
	cl.gcmdValue = GENCMD_USE_AMMODISP;
}

void IN_GenCMD25( void )
{
	cl.gcmdSendValue = qtrue;
	cl.gcmdValue = GENCMD_USE_EWEB;
}

void IN_GenCMD26( void )
{
	cl.gcmdSendValue = qtrue;
	cl.gcmdValue = GENCMD_USE_CLOAK;
}

void IN_GenCMD27( void )
{
	cl.gcmdSendValue = qtrue;
	cl.gcmdValue = GENCMD_TAUNT;
}

void IN_GenCMD28( void )
{
	cl.gcmdSendValue = qtrue;
	cl.gcmdValue = GENCMD_BOW;
}

void IN_GenCMD29( void )
{
	cl.gcmdSendValue = qtrue;
	cl.gcmdValue = GENCMD_MEDITATE;
}

void IN_GenCMD30( void )
{
	cl.gcmdSendValue = qtrue;
	cl.gcmdValue = GENCMD_FLOURISH;
}

void IN_GenCMD31( void )
{
	cl.gcmdSendValue = qtrue;
	cl.gcmdValue = GENCMD_GLOAT;
}


//toggle automap view mode
static bool g_clAutoMapMode = false;
void IN_AutoMapButton(void)
{
	g_clAutoMapMode = !g_clAutoMapMode;
}

//toggle between automap, radar, nothing
extern cvar_t *r_autoMap;
void IN_AutoMapToggle(void)
{
	Cvar_User_SetValue("cg_drawRadar", !Cvar_VariableValue("cg_drawRadar"));
	/*
	if (r_autoMap && r_autoMap->integer)
	{ //automap off, radar on
		Cvar_Set("r_autoMap", "0");
		Cvar_Set("cg_drawRadar", "1");
	}
	else if (Cvar_VariableIntegerValue("cg_drawRadar"))
	{ //radar off, automap should be off too
		Cvar_Set("cg_drawRadar", "0");
	}
	else
	{ //turn automap on
		Cvar_Set("r_autoMap", "1");
	}
	*/
}

void IN_VoiceChatButton(void)
{
	if (!cls.uiStarted)
	{ //ui not loaded so this command is useless
		return;
	}
	UIVM_SetActiveMenu( UIMENU_VOICECHAT );
}

void IN_KeyDown( kbutton_t *b ) {
	int		k;
	char	*c;

	c = Cmd_Argv(1);
	if ( c[0] ) {
		k = atoi(c);
	} else {
		k = -1;		// typed manually at the console for continuous down
	}

	if ( k == b->down[0] || k == b->down[1] ) {
		return;		// repeating key
	}

	if ( !b->down[0] ) {
		b->down[0] = k;
	} else if ( !b->down[1] ) {
		b->down[1] = k;
	} else {
		Com_Printf ("Three keys down for a button!\n");
		return;
	}

	if ( b->active ) {
		return;		// still down
	}

	// save timestamp for partial frame summing
	c = Cmd_Argv(2);
	b->downtime = atoi(c);

	b->active = qtrue;
	b->wasPressed = qtrue;
}

void IN_KeyUp( kbutton_t *b ) {
	int		k;
	char	*c;
	unsigned	uptime;

	c = Cmd_Argv(1);
	if ( c[0] ) {
		k = atoi(c);
	} else {
		// typed manually at the console, assume for unsticking, so clear all
		b->down[0] = b->down[1] = 0;
		b->active = qfalse;
		return;
	}

	if ( b->down[0] == k ) {
		b->down[0] = 0;
	} else if ( b->down[1] == k ) {
		b->down[1] = 0;
	} else {
		return;		// key up without coresponding down (menu pass through)
	}
	if ( b->down[0] || b->down[1] ) {
		return;		// some other key is still holding it down
	}

	b->active = qfalse;

	// save timestamp for partial frame summing
	c = Cmd_Argv(2);
	uptime = atoi(c);
	if ( uptime ) {
		b->msec += uptime - b->downtime;
	} else {
		b->msec += frame_msec / 2;
	}

	b->active = qfalse;
}



/*
===============
CL_KeyState

Returns the fraction of the span (the frame for the view angles, the time since the last
command for the movement) that the key was down
===============
*/
float CL_KeyState( kbutton_t *key, unsigned span ) {
	float		val;
	int			msec;

	msec = key->msec;
	key->msec = 0;

	if ( key->active ) {
		// still down
		if ( !key->downtime ) {
			msec = s_inputTime;
		} else {
			msec += s_inputTime - key->downtime;
		}
		key->downtime = s_inputTime;
	}

#if 0
	if (msec) {
		Com_Printf ("%i ", msec);
	}
#endif

	val = (float)msec / span;
	if ( val < 0 ) {
		val = 0;
	}
	if ( val > 1 ) {
		val = 1;
	}

	return val;
}

#define		AUTOMAP_KEY_FORWARD			1
#define		AUTOMAP_KEY_BACK			2
#define		AUTOMAP_KEY_YAWLEFT			3
#define		AUTOMAP_KEY_YAWRIGHT		4
#define		AUTOMAP_KEY_PITCHUP			5
#define		AUTOMAP_KEY_PITCHDOWN		6
#define		AUTOMAP_KEY_DEFAULTVIEW		7
static autoMapInput_t			g_clAutoMapInput;
//intercept certain keys during automap mode
static void CL_AutoMapKey(int autoMapKey, qboolean up)
{
	autoMapInput_t *data = (autoMapInput_t *)cl.mSharedMemory;

	switch (autoMapKey)
	{
	case AUTOMAP_KEY_FORWARD:
        if (up)
		{
			g_clAutoMapInput.up = 0.0f;
		}
		else
		{
			g_clAutoMapInput.up = 16.0f;
		}
		break;
	case AUTOMAP_KEY_BACK:
        if (up)
		{
			g_clAutoMapInput.down = 0.0f;
		}
		else
		{
			g_clAutoMapInput.down = 16.0f;
		}
		break;
	case AUTOMAP_KEY_YAWLEFT:
		if (up)
		{
			g_clAutoMapInput.yaw = 0.0f;
		}
		else
		{
			g_clAutoMapInput.yaw = -4.0f;
		}
		break;
	case AUTOMAP_KEY_YAWRIGHT:
		if (up)
		{
			g_clAutoMapInput.yaw = 0.0f;
		}
		else
		{
			g_clAutoMapInput.yaw = 4.0f;
		}
		break;
	case AUTOMAP_KEY_PITCHUP:
		if (up)
		{
			g_clAutoMapInput.pitch = 0.0f;
		}
		else
		{
			g_clAutoMapInput.pitch = -4.0f;
		}
		break;
	case AUTOMAP_KEY_PITCHDOWN:
		if (up)
		{
			g_clAutoMapInput.pitch = 0.0f;
		}
		else
		{
			g_clAutoMapInput.pitch = 4.0f;
		}
		break;
	case AUTOMAP_KEY_DEFAULTVIEW:
		memset(&g_clAutoMapInput, 0, sizeof(autoMapInput_t));
		g_clAutoMapInput.goToDefaults = qtrue;
		break;
	default:
		break;
	}

	memcpy(data, &g_clAutoMapInput, sizeof(autoMapInput_t));

	if (cls.cgameStarted)
	{
		CGVM_AutomapInput();
	}

	g_clAutoMapInput.goToDefaults = qfalse;
}


void IN_UpDown(void)
{
	if (g_clAutoMapMode)
	{
		CL_AutoMapKey(AUTOMAP_KEY_PITCHUP, qfalse);
	}
	else
	{
		IN_KeyDown(&in_up);
	}
}
void IN_UpUp(void)
{
	if (g_clAutoMapMode)
	{
		CL_AutoMapKey(AUTOMAP_KEY_PITCHUP, qtrue);
	}
	else
	{
		IN_KeyUp(&in_up);
	}
}
void IN_DownDown(void)
{
	if (g_clAutoMapMode)
	{
		CL_AutoMapKey(AUTOMAP_KEY_PITCHDOWN, qfalse);
	}
	else
	{
		IN_KeyDown(&in_down);
	}
}
void IN_DownUp(void)
{
	if (g_clAutoMapMode)
	{
		CL_AutoMapKey(AUTOMAP_KEY_PITCHDOWN, qtrue);
	}
	else
	{
		IN_KeyUp(&in_down);
	}
}
void IN_LeftDown(void) {IN_KeyDown(&in_left);}
void IN_LeftUp(void) {IN_KeyUp(&in_left);}
void IN_RightDown(void) {IN_KeyDown(&in_right);}
void IN_RightUp(void) {IN_KeyUp(&in_right);}
void IN_ForwardDown(void)
{
	if (g_clAutoMapMode)
	{
		CL_AutoMapKey(AUTOMAP_KEY_FORWARD, qfalse);
	}
	else
	{
		IN_KeyDown(&in_forward);
	}
}
void IN_ForwardUp(void)
{
	if (g_clAutoMapMode)
	{
		CL_AutoMapKey(AUTOMAP_KEY_FORWARD, qtrue);
	}
	else
	{
		IN_KeyUp(&in_forward);
	}
}
void IN_BackDown(void)
{
	if (g_clAutoMapMode)
	{
		CL_AutoMapKey(AUTOMAP_KEY_BACK, qfalse);
	}
	else
	{
		IN_KeyDown(&in_back);
	}
}
void IN_BackUp(void)
{
	if (g_clAutoMapMode)
	{
		CL_AutoMapKey(AUTOMAP_KEY_BACK, qtrue);
	}
	else
	{
		IN_KeyUp(&in_back);
	}
}
void IN_LookupDown(void) {IN_KeyDown(&in_lookup);}
void IN_LookupUp(void) {IN_KeyUp(&in_lookup);}
void IN_LookdownDown(void) {IN_KeyDown(&in_lookdown);}
void IN_LookdownUp(void) {IN_KeyUp(&in_lookdown);}
void IN_MoveleftDown(void)
{
	if (g_clAutoMapMode)
	{
		CL_AutoMapKey(AUTOMAP_KEY_YAWLEFT, qfalse);
	}
	else
	{
		IN_KeyDown(&in_moveleft);
	}
}
void IN_MoveleftUp(void)
{
	if (g_clAutoMapMode)
	{
		CL_AutoMapKey(AUTOMAP_KEY_YAWLEFT, qtrue);
	}
	else
	{
		IN_KeyUp(&in_moveleft);
	}
}
void IN_MoverightDown(void)
{
	if (g_clAutoMapMode)
	{
		CL_AutoMapKey(AUTOMAP_KEY_YAWRIGHT, qfalse);
	}
	else
	{
		IN_KeyDown(&in_moveright);
	}
}
void IN_MoverightUp(void)
{
	if (g_clAutoMapMode)
	{
		CL_AutoMapKey(AUTOMAP_KEY_YAWRIGHT, qtrue);
	}
	else
	{
		IN_KeyUp(&in_moveright);
	}
}

void IN_SpeedDown(void) {IN_KeyDown(&in_speed);}
void IN_SpeedUp(void) {IN_KeyUp(&in_speed);}
void IN_StrafeDown(void) {IN_KeyDown(&in_strafe);}
void IN_StrafeUp(void) {IN_KeyUp(&in_strafe);}

void IN_Button0Down(void) {IN_KeyDown(&in_buttons[0]);}
void IN_Button0Up(void) {IN_KeyUp(&in_buttons[0]);}
void IN_Button1Down(void) {IN_KeyDown(&in_buttons[1]);}
void IN_Button1Up(void) {IN_KeyUp(&in_buttons[1]);}
void IN_Button2Down(void) {IN_KeyDown(&in_buttons[2]);}
void IN_Button2Up(void) {IN_KeyUp(&in_buttons[2]);}
void IN_Button3Down(void) {IN_KeyDown(&in_buttons[3]);}
void IN_Button3Up(void) {IN_KeyUp(&in_buttons[3]);}
void IN_Button4Down(void) {IN_KeyDown(&in_buttons[4]);}
void IN_Button4Up(void) {IN_KeyUp(&in_buttons[4]);}
void IN_Button5Down(void) //use key
{
	if (g_clAutoMapMode)
	{
		CL_AutoMapKey(AUTOMAP_KEY_DEFAULTVIEW, qfalse);
	}
	else
	{
		IN_KeyDown(&in_buttons[5]);
	}
}
void IN_Button5Up(void) {IN_KeyUp(&in_buttons[5]);}
void IN_Button6Down(void) {IN_KeyDown(&in_buttons[6]);}
void IN_Button6Up(void) {IN_KeyUp(&in_buttons[6]);}
void IN_Button7Down(void) {IN_KeyDown(&in_buttons[7]);}
void IN_Button7Up(void){IN_KeyUp(&in_buttons[7]);}
void IN_Button8Down(void) {IN_KeyDown(&in_buttons[8]);}
void IN_Button8Up(void) {IN_KeyUp(&in_buttons[8]);}
void IN_Button9Down(void) {IN_KeyDown(&in_buttons[9]);}
void IN_Button9Up(void) {IN_KeyUp(&in_buttons[9]);}
void IN_Button10Down(void) {IN_KeyDown(&in_buttons[10]);}
void IN_Button10Up(void) {IN_KeyUp(&in_buttons[10]);}
void IN_Button11Down(void) {IN_KeyDown(&in_buttons[11]);}
void IN_Button11Up(void) {IN_KeyUp(&in_buttons[11]);}
void IN_Button12Down(void) {IN_KeyDown(&in_buttons[12]);}
void IN_Button12Up(void) {IN_KeyUp(&in_buttons[12]);}
void IN_Button13Down(void) {IN_KeyDown(&in_buttons[13]);}
void IN_Button13Up(void) {IN_KeyUp(&in_buttons[13]);}
void IN_Button14Down(void) {IN_KeyDown(&in_buttons[14]);}
void IN_Button14Up(void) {IN_KeyUp(&in_buttons[14]);}
void IN_Button15Down(void) {IN_KeyDown(&in_buttons[15]);}
void IN_Button15Up(void) {IN_KeyUp(&in_buttons[15]);}

void IN_CenterView (void) {
	cl.viewangles[PITCH] = -SHORT2ANGLE(cl.snap.ps.delta_angles[PITCH]);
}

//==========================================================================

cvar_t	*cl_upspeed;
cvar_t	*cl_forwardspeed;
cvar_t	*cl_sidespeed;

cvar_t	*cl_yawspeed;
cvar_t	*cl_pitchspeed;

cvar_t	*cl_run;

cvar_t	*cl_anglespeedkey;


/*
================
CL_AdjustAngles

Moves the local angle positions
================
*/
void CL_AdjustAngles( void ) {
	float	speed;

	if ( in_speed.active ) {
		speed = 0.001 * cls.frametime * cl_anglespeedkey->value;
	} else {
		speed = 0.001 * cls.frametime;
	}

	if ( !in_strafe.active ) {
		if ( cl_mYawOverride )
		{
			if ( cl_mSensitivityOverride )
			{
				cl.viewangles[YAW] -= cl_mYawOverride*cl_mSensitivityOverride*speed*cl_yawspeed->value*CL_KeyState (&in_right, frame_msec);
				cl.viewangles[YAW] += cl_mYawOverride*cl_mSensitivityOverride*speed*cl_yawspeed->value*CL_KeyState (&in_left, frame_msec);
			}
			else
			{
				cl.viewangles[YAW] -= cl_mYawOverride*OVERRIDE_MOUSE_SENSITIVITY*speed*cl_yawspeed->value*CL_KeyState (&in_right, frame_msec);
				cl.viewangles[YAW] += cl_mYawOverride*OVERRIDE_MOUSE_SENSITIVITY*speed*cl_yawspeed->value*CL_KeyState (&in_left, frame_msec);
			}
		}
		else
		{
			cl.viewangles[YAW] -= speed*cl_yawspeed->value*CL_KeyState (&in_right, frame_msec);
			cl.viewangles[YAW] += speed*cl_yawspeed->value*CL_KeyState (&in_left, frame_msec);
		}
	}

	if ( cl_mPitchOverride )
	{
		if ( cl_mSensitivityOverride )
		{
			cl.viewangles[PITCH] -= cl_mPitchOverride*cl_mSensitivityOverride*speed*cl_pitchspeed->value * CL_KeyState (&in_lookup, frame_msec);
			cl.viewangles[PITCH] += cl_mPitchOverride*cl_mSensitivityOverride*speed*cl_pitchspeed->value * CL_KeyState (&in_lookdown, frame_msec);
		}
		else
		{
			cl.viewangles[PITCH] -= cl_mPitchOverride*OVERRIDE_MOUSE_SENSITIVITY*speed*cl_pitchspeed->value * CL_KeyState (&in_lookup, frame_msec);
			cl.viewangles[PITCH] += cl_mPitchOverride*OVERRIDE_MOUSE_SENSITIVITY*speed*cl_pitchspeed->value * CL_KeyState (&in_lookdown, frame_msec);
		}
	}
	else
	{
		cl.viewangles[PITCH] -= speed*cl_pitchspeed->value * CL_KeyState (&in_lookup, frame_msec);
		cl.viewangles[PITCH] += speed*cl_pitchspeed->value * CL_KeyState (&in_lookdown, frame_msec);
	}
}

/*
================
CL_KeyMove

Sets the usercmd_t based on key states
================
*/
void CL_KeyMove( usercmd_t *cmd ) {
	int		movespeed;
	int		forward, side, up;

	//
	// adjust for speed key / running
	// the walking flag is to keep animations consistant
	// even during acceleration and develeration
	//
	if ( in_speed.active ^ cl_run->integer ) {
		movespeed = 127;
		cmd->buttons &= ~BUTTON_WALKING;
	} else {
		cmd->buttons |= BUTTON_WALKING;
		movespeed = 46;
	}

	forward = 0;
	side = 0;
	up = 0;
	if ( in_strafe.active ) {
		side += movespeed * CL_KeyState (&in_right, s_cmdMsec);
		side -= movespeed * CL_KeyState (&in_left, s_cmdMsec);
	}

	side += movespeed * CL_KeyState (&in_moveright, s_cmdMsec);
	side -= movespeed * CL_KeyState (&in_moveleft, s_cmdMsec);


	up += movespeed * CL_KeyState (&in_up, s_cmdMsec);
	up -= movespeed * CL_KeyState (&in_down, s_cmdMsec);

	forward += movespeed * CL_KeyState (&in_forward, s_cmdMsec);
	forward -= movespeed * CL_KeyState (&in_back, s_cmdMsec);

	cmd->forwardmove = ClampChar( forward );
	cmd->rightmove = ClampChar( side );
	cmd->upmove = ClampChar( up );
}

/*
=================
CL_MouseEvent
=================
*/
void CL_MouseEvent( int dx, int dy, int time ) {
	if (cls.cursorActive) {
		CL_UpdateCursorPosition( dx, dy );
	} else if (g_clAutoMapMode && cls.cgameStarted)
	{ //automap input
		autoMapInput_t *data = (autoMapInput_t *)cl.mSharedMemory;

		g_clAutoMapInput.yaw = dx;
		g_clAutoMapInput.pitch = dy;
		memcpy(data, &g_clAutoMapInput, sizeof(autoMapInput_t));
		CGVM_AutomapInput();

		g_clAutoMapInput.yaw = 0.0f;
		g_clAutoMapInput.pitch = 0.0f;
	}
	else if ( Key_GetCatcher( ) & KEYCATCH_UI ) {
		UIVM_MouseEvent( dx, dy );
	} else if ( Key_GetCatcher( ) & KEYCATCH_CGAME ) {
		CGVM_MouseEvent( dx, dy );
	} else {
		cl.mouseDx[cl.mouseIndex] += dx;
		cl.mouseDy[cl.mouseIndex] += dy;
	}
}

/*
=================
CL_JoystickEvent

Joystick values stay set until changed
=================
*/
void CL_JoystickEvent( int axis, int value, int time ) {
	if ( axis < 0 || axis >= MAX_JOYSTICK_AXIS ) {
		Com_Error( ERR_DROP, "CL_JoystickEvent: bad axis %i", axis );
	}
	cl.joystickAxis[axis] = value;
}

/*
=================
CL_JoystickAngles

The view angles the joystick gives, every frame
=================
*/
extern cvar_t *in_joystick;
static void CL_JoystickAngles( void ) {
	float	anglespeed;

	if ( !in_joystick->integer )
	{
		return;
	}

	if ( in_speed.active ) {
		anglespeed = 0.001 * cls.frametime * cl_anglespeedkey->value;
	} else {
		anglespeed = 0.001 * cls.frametime;
	}

	if ( !in_strafe.active ) {
		if ( cl_mYawOverride )
		{
			if ( cl_mSensitivityOverride )
			{
				cl.viewangles[YAW] += cl_mYawOverride * cl_mSensitivityOverride * cl.joystickAxis[AXIS_SIDE]/2.0f;
			}
			else
			{
				cl.viewangles[YAW] += cl_mYawOverride * OVERRIDE_MOUSE_SENSITIVITY * cl.joystickAxis[AXIS_SIDE]/2.0f;
			}
		}
		else
		{
			cl.viewangles[YAW] += anglespeed * (cl_yawspeed->value / 100.0f) * cl.joystickAxis[AXIS_SIDE];
		}
	}

	if ( in_mlooking || cl_freelook->integer ) {
		if ( cl_mPitchOverride )
		{
			if ( cl_mSensitivityOverride )
			{
				cl.viewangles[PITCH] += cl_mPitchOverride * cl_mSensitivityOverride * cl.joystickAxis[AXIS_FORWARD]/2.0f;
			}
			else
			{
				cl.viewangles[PITCH] += cl_mPitchOverride * OVERRIDE_MOUSE_SENSITIVITY * cl.joystickAxis[AXIS_FORWARD]/2.0f;
			}
		}
		else
		{
			cl.viewangles[PITCH] += anglespeed * (cl_pitchspeed->value / 100.0f) * cl.joystickAxis[AXIS_FORWARD];
		}
	}
}

/*
=================
CL_JoystickMove

The movement the joystick gives, per command
=================
*/
static void CL_JoystickMove( usercmd_t *cmd ) {
	if ( !in_joystick->integer )
	{
		return;
	}

	if ( !(in_speed.active ^ cl_run->integer) ) {
		cmd->buttons |= BUTTON_WALKING;
	}

	if ( in_strafe.active ) {
		cmd->rightmove = ClampChar( cmd->rightmove + cl.joystickAxis[AXIS_SIDE] );
	}

	if ( !in_mlooking && !cl_freelook->integer ) {
		cmd->forwardmove = ClampChar( cmd->forwardmove + cl.joystickAxis[AXIS_FORWARD] );
	}

	cmd->upmove = ClampChar( cmd->upmove + cl.joystickAxis[AXIS_UP] );
}

/*
=================
CL_MouseMove

Every frame: the view angles now, and the movement of strafing or of no mouse look gathered for the next command
=================
*/
static void CL_MouseMove( void ) {
	float	mx, my;
	const float	speed = static_cast<float>(frame_msec);

	// allow mouse smoothing
	if ( m_filter->integer ) {
		mx = ( cl.mouseDx[0] + cl.mouseDx[1] ) * 0.5;
		my = ( cl.mouseDy[0] + cl.mouseDy[1] ) * 0.5;
	} else {
		mx = cl.mouseDx[cl.mouseIndex];
		my = cl.mouseDy[cl.mouseIndex];
	}

	cl.mouseIndex ^= 1;
	cl.mouseDx[cl.mouseIndex] = 0;
	cl.mouseDy[cl.mouseIndex] = 0;

	if ( mx == 0.0f && my == 0.0f )
		return;

	if ( cl_mouseAccel->value != 0.0f )
	{
		if ( cl_mouseAccelStyle->integer == 0 )
		{
			float accelSensitivity;
			float rate;

			rate = SQRTFAST( mx * mx + my * my ) / speed;

			if ( cl_mYawOverride || cl_mPitchOverride )
			{//FIXME: different people have different speed mouses,
				if ( cl_mSensitivityOverride )
				{
					//this will fuck things up for them, need to clamp
					//max input?
					accelSensitivity = cl_mSensitivityOverride;
				}
				else
				{
					accelSensitivity = cl_sensitivity->value + rate * cl_mouseAccel->value;
				}
			}
			else
			{
				accelSensitivity = cl_sensitivity->value + rate * cl_mouseAccel->value;
			}
			mx *= accelSensitivity;
			my *= accelSensitivity;

			if ( cl_showMouseRate->integer )
				Com_Printf( "rate: %f, accelSensitivity: %f\n", rate, accelSensitivity );
		}
		else
		{
			float rate[2];
			float power[2];

			// sensitivity remains pretty much unchanged at low speeds
			// cl_mouseAccel is a power value to how the acceleration is shaped
			// cl_mouseAccelOffset is the rate for which the acceleration will have doubled the non accelerated amplification
			// NOTE: decouple the config cvars for independent acceleration setup along X and Y?

			rate[0] = Q_fabs( mx ) / speed;
			rate[1] = Q_fabs( my ) / speed;
			power[0] = Q_powf( rate[0] / cl_mouseAccelOffset->value, cl_mouseAccel->value );
			power[1] = Q_powf( rate[1] / cl_mouseAccelOffset->value, cl_mouseAccel->value );

			if ( cl_mYawOverride || cl_mPitchOverride )
			{//FIXME: different people have different speed mouses,
				if ( cl_mSensitivityOverride )
				{
					//this will fuck things up for them, need to clamp
					//max input?
					mx = cl_mSensitivityOverride * (mx + ((mx < 0) ? -power[0] : power[0]) * cl_mouseAccelOffset->value);
					my = cl_mSensitivityOverride * (my + ((my < 0) ? -power[1] : power[1]) * cl_mouseAccelOffset->value);
				}
				else
				{
					mx = cl_sensitivity->value * (mx + ((mx < 0) ? -power[0] : power[0]) * cl_mouseAccelOffset->value);
					my = cl_sensitivity->value * (my + ((my < 0) ? -power[1] : power[1]) * cl_mouseAccelOffset->value);
				}
			}
			else
			{
				mx = cl_sensitivity->value * (mx + ((mx < 0) ? -power[0] : power[0]) * cl_mouseAccelOffset->value);
				my = cl_sensitivity->value * (my + ((my < 0) ? -power[1] : power[1]) * cl_mouseAccelOffset->value);
			}

			if ( cl_showMouseRate->integer )
				Com_Printf( "ratex: %f, ratey: %f, powx: %f, powy: %f\n", rate[0], rate[1], power[0], power[1] );
		}
	}
	else
	{
		if ( cl_mYawOverride || cl_mPitchOverride )
		{//FIXME: different people have different speed mouses,
			if ( cl_mSensitivityOverride )
			{
				//this will fuck things up for them, need to clamp
				//max input?
				mx *= cl_mSensitivityOverride;
				my *= cl_mSensitivityOverride;
			}
			else
			{
				mx *= cl_sensitivity->value;
				my *= cl_sensitivity->value;
			}
		}
		else
		{
			mx *= cl_sensitivity->value;
			my *= cl_sensitivity->value;
		}
	}

	// ingame FOV
	mx *= cl.cgameSensitivity;
	my *= cl.cgameSensitivity;

	// add mouse X/Y movement to cmd
	if ( in_strafe.active )
		s_mouseSide += m_side->value * mx;
	else {
		if ( cl_mYawOverride )
			cl.viewangles[YAW] -= cl_mYawOverride * mx;
		else
			cl.viewangles[YAW] -= m_yaw->value * mx;
	}

	if ( (in_mlooking || cl_freelook->integer) && !in_strafe.active ) {
		// VVFIXME - This is supposed to be a CVAR
		const float cl_pitchSensitivity = 1.0f;
		const float pitch = cl_bUseFighterPitch ? m_pitchVeh->value : m_pitch->value;
		if ( cl_mPitchOverride ) {
			if ( pitch > 0 )
				cl.viewangles[PITCH] += cl_mPitchOverride * my * cl_pitchSensitivity;
			else
				cl.viewangles[PITCH] -= cl_mPitchOverride * my * cl_pitchSensitivity;
		}
		else
			cl.viewangles[PITCH] += pitch * my * cl_pitchSensitivity;
	}
	else
		s_mouseForward -= m_forward->value * my;
}

qboolean CL_NoUseableForce(void)
{
	if (!cls.cgameStarted)
	{ //ahh, no cgame loaded
		return qfalse;
	}

	return CGVM_NoUseableForce();
}

/*
==============
CL_CmdButtons
==============
*/
void CL_CmdButtons( usercmd_t *cmd ) {
	int		i;

	//
	// figure button bits
	// send a button bit even if the key was pressed and released in
	// less than a frame
	//
	for (i = 0 ; i < MAX_KBUTTONS ; i++) {
		if ( in_buttons[i].active || in_buttons[i].wasPressed ) {
			cmd->buttons |= 1 << i;
		}
		in_buttons[i].wasPressed = qfalse;
	}

	if (cmd->buttons & BUTTON_FORCEPOWER)
	{ //check for transferring a use force to a use inventory...
		if ((cmd->buttons & BUTTON_USE) || CL_NoUseableForce())
		{ //it's pushed, remap it!
			cmd->buttons &= ~BUTTON_FORCEPOWER;
			cmd->buttons |= BUTTON_USE_HOLDABLE;
		}
	}

	if ( Key_GetCatcher( ) ) {
		cmd->buttons |= BUTTON_TALK;
	}

	// allow the game to know if any key at all is
	// currently pressed, even if it isn't bound to anything
	if ( kg.anykeydown && Key_GetCatcher( ) == 0 ) {
		cmd->buttons |= BUTTON_ANY;
	}
}


/*
==============
CL_FinishMove
==============
*/
vec3_t cl_sendAngles={0};
vec3_t cl_lastViewAngles={0};
void CL_FinishMove( usercmd_t *cmd ) {
	int		i;

	// copy the state that the cgame is currently sending
	cmd->weapon = cl.cgameUserCmdValue;
	cmd->forcesel = cl.cgameForceSelection;
	cmd->invensel = cl.cgameInvenSelection;

	if (cl.gcmdSendValue)
	{
		cmd->generic_cmd = cl.gcmdValue;
		//cl.gcmdSendValue = qfalse;
		cl.gcmdSentValue = qtrue;
	}
	else
	{
		cmd->generic_cmd = 0;
	}

	// send the current server time so the amount of movement
	// can be determined without allowing cheating
	cmd->serverTime = cl.serverTime;

	if (cl.cgameViewAngleForceTime > cl.serverTime)
	{
		cl.cgameViewAngleForce[YAW] -= SHORT2ANGLE(cl.snap.ps.delta_angles[YAW]);

		cl.viewangles[YAW] = cl.cgameViewAngleForce[YAW];
		cl.cgameViewAngleForceTime = 0;
	}

	if ( cl_crazyShipControls )
	{
		float pitchSubtract, pitchDelta, yawDelta;

		yawDelta = AngleSubtract(cl.viewangles[YAW],cl_lastViewAngles[YAW]);
		//yawDelta *= (4.0f*pVeh->m_fTimeModifier);
		cl_sendAngles[ROLL] -= yawDelta;

		float nRoll = Q_fabs(cl_sendAngles[ROLL]);

		pitchDelta = AngleSubtract(cl.viewangles[PITCH],cl_lastViewAngles[PITCH]);
		//pitchDelta *= (2.0f*pVeh->m_fTimeModifier);
		pitchSubtract = pitchDelta * (nRoll/90.0f);
		cl_sendAngles[PITCH] += pitchDelta-pitchSubtract;

		//yaw-roll calc should be different
		if (nRoll > 90.0f)
		{
			nRoll -= 180.0f;
		}
		if (nRoll < 0.0f)
		{
			nRoll = -nRoll;
		}
		pitchSubtract = pitchDelta * (nRoll/90.0f);
		if ( cl_sendAngles[ROLL] > 0.0f )
		{
			cl_sendAngles[YAW] += pitchSubtract;
		}
		else
		{
			cl_sendAngles[YAW] -= pitchSubtract;
		}

		cl_sendAngles[PITCH] = AngleNormalize180( cl_sendAngles[PITCH] );
		cl_sendAngles[YAW] = AngleNormalize360( cl_sendAngles[YAW] );
		cl_sendAngles[ROLL] = AngleNormalize180( cl_sendAngles[ROLL] );

		for (i=0 ; i<3 ; i++) {
			cmd->angles[i] = ANGLE2SHORT(cl_sendAngles[i]);
		}
	}
	else
	{
		for (i=0 ; i<3 ; i++) {
			cmd->angles[i] = ANGLE2SHORT(cl.viewangles[i]);
		}
		//in case we switch to the cl_crazyShipControls
		VectorCopy( cl.viewangles, cl_sendAngles );
	}
	//always needed in for the cl_crazyShipControls
	VectorCopy( cl.viewangles, cl_lastViewAngles );
}

/*
=================
CL_CreateCmd
=================
*/
usercmd_t CL_CreateCmd( void ) {
	usercmd_t	cmd;

	Com_Memset( &cmd, 0, sizeof( cmd ) );

	CL_CmdButtons( &cmd );

	// get basic movement from keyboard
	CL_KeyMove( &cmd );

	// get basic movement from mouse, gathered over the frames since the last command
	cmd.rightmove = ClampChar( cmd.rightmove + s_mouseSide );
	cmd.forwardmove = ClampChar( cmd.forwardmove + s_mouseForward );
	s_mouseSide = s_mouseForward = 0.0f;

	// get basic movement from joystick
	CL_JoystickMove( &cmd );

	// store out the final values
	CL_FinishMove( &cmd );

	return cmd;
}

/*
=================
CL_UpdateViewAngles

The view angles, every frame (plan step 14: the commands are made per tick, the view turns at the frame rate)
=================
*/
static void CL_UpdateViewAngles( void ) {
	vec3_t		oldAngles;

	VectorCopy( cl.viewangles, oldAngles );

	// keyboard angle adjustment
	CL_AdjustAngles ();

	// the mouse
	CL_MouseMove();

	// the joystick
	CL_JoystickAngles();

	// check to make sure the angles haven't wrapped
	if ( cl.viewangles[PITCH] - oldAngles[PITCH] > 90 ) {
		cl.viewangles[PITCH] = oldAngles[PITCH] + 90;
	} else if ( oldAngles[PITCH] - cl.viewangles[PITCH] > 90 ) {
		cl.viewangles[PITCH] = oldAngles[PITCH] - 90;
	}

	// draw debug graphs of turning for mouse testing
	if ( cl_debugMove->integer ) {
		if ( cl_debugMove->integer == 1 ) {
			SCR_DebugGraph( abs(cl.viewangles[YAW] - oldAngles[YAW]), 0 );
		}
		if ( cl_debugMove->integer == 2 ) {
			SCR_DebugGraph( abs(cl.viewangles[PITCH] - oldAngles[PITCH]), 0 );
		}
	}
}


/*
=================
CL_CreateNewCommands

Turns the view, then creates a usercmd_t for each tick of cl.serverTime this frame
entered (plan step 14): every command moves TICK_MSEC, whatever the frame rate. A
command is made as the clock enters its tick, with the input gathered over the tick
before, and carries the time the tick ends (option C, owner, 13 November 2026), so that
cgame predicts the tick in progress and draws the local player between two ticks
=================
*/
void CL_CreateNewCommands( void ) {
	// no need to create usercmds until we have a gamestate
	if ( cls.state < CA_PRIMED )
		return;

	frame_msec = s_inputTime - old_com_frameTime;	// since the input was last read (a frame, or a tick between two)

	// if running over 1000fps, act as if each frame is 1ms
	// prevents divisions by zero
	if ( frame_msec < 1 )
		frame_msec = 1;

	// if running less than 5fps, truncate the extra time to prevent
	// unexpected moves after a hitch
	if ( frame_msec > 200 )
		frame_msec = 200;

	old_com_frameTime = s_inputTime;

	CL_UpdateViewAngles();

	// until the first snapshot the client's clock does not run: a command every frame, as before, so that the packets
	// whose commands make the server put the client in the world keep coming
	if ( cls.state != CA_ACTIVE ) {
		s_cmdMsec = frame_msec;
		s_lastCmdFrameTime = s_inputTime;
		cl.cmdNumber++;
		cl.cmds[cl.cmdNumber & CMD_MASK] = CL_CreateCmd();
		return;
	}

	// the end of the tick in progress on the command clock (plan step 14, part 4c: ahead of the server's clock, so that
	// the command reaches the server just before that tick runs); after the first command, a jump of the clock (by more
	// than 200 ms either way: a map_restart, a hitch) gives one command at the new time, as one frame's command did
	const int tick = cl.cmdTime - cl.cmdTime % TICK_MSEC + TICK_MSEC;
	int last = cl.cmds[cl.cmdNumber & CMD_MASK].serverTime;
	if ( cl.cmdNumber == 0 || abs( tick - last ) > 200 ) {
		last = tick - TICK_MSEC;
	}
	if ( last >= tick ) {
		return;	// the gathered input waits for the next tick
	}

	s_cmdMsec = Q_min( Q_max( s_inputTime - s_lastCmdFrameTime, 1 ), 200 );
	s_lastCmdFrameTime = s_inputTime;
	usercmd_t cmd = CL_CreateCmd();
	for ( int t = last + TICK_MSEC; t <= tick; t += TICK_MSEC ) {
		cmd.serverTime = t;
		cl.cmdNumber++;
		cl.cmds[cl.cmdNumber & CMD_MASK] = cmd;
		cl.cmdDeltaAt[cl.cmdNumber & CMD_MASK] = cl.cmdTimeDelta;
		cmd.generic_cmd = 0;	// a one-off command (a style change, a force power): in the first tick only
	}
}

/*
=================
CL_PendingCmd

The view angles and the command clock of now, for cgame's drawing of the local player
(plan step 14: the commands are made per tick, the view turns every frame): the last
command with the view angles of the frame and the command clock as its time, never
sent. qfalse before the first command.
=================
*/
qboolean CL_PendingCmd( usercmd_t *cmd ) {
	if ( cl.cmdNumber == 0 ) {
		return qfalse;
	}
	*cmd = cl.cmds[cl.cmdNumber & CMD_MASK];
	cmd->serverTime = cl.cmdTime;
	for ( int i = 0; i < 3; i++ ) {
		cmd->angles[i] = ANGLE2SHORT( cl.viewangles[i] );
	}
	return qtrue;
}

/*
=================
CL_ReadyToSendPacket

Returns qtrue when this frame made a new command (a packet per tick, plan step 14), or,
before the client is in the game (awaiting the gamestate or its first snapshot), once
per interval of MAX_PACKET_RATE
=================
*/
static qboolean CL_ReadyToSendPacket( qboolean newCommand ) {
	const int interval = 1000 / MAX_PACKET_RATE;

	// don't send anything if playing back a demo
	if ( clc.demoplaying || cls.state == CA_CINEMATIC ) {
		return qfalse;
	}

	// If we are downloading, we send no less than 50ms between packets
	if ( *clc.downloadTempName &&
		cls.realtime - clc.lastPacketSentTime < 50 ) {
		return qfalse;
	}

	// While awaiting the gamestate the client sends at the normal rate (plan step 10; it sent one packet a second): the
	// server resends a gamestate as soon as the client acknowledges a later message (SV_ExecuteClientMessage), so a
	// lost gamestate fragment now costs about one round trip instead of a second or more.

	// in the game, a packet carries the new commands (and repeats those of the last CL_PACKET_DUP packets)
	if ( cls.state == CA_ACTIVE ) {
		return newCommand;
	}

	// send every frame for loopbacks
	if ( clc.netchan.remoteAddress.type == NA_LOOPBACK ) {
		return qtrue;
	}

	// a packet every frame, up to MAX_PACKET_RATE per second (plan step 11; the server decodes no more). The unused part
	// of the interval carries over to the next frame (at most one interval), so that the client sends min(frame rate,
	// MAX_PACKET_RATE) packets per second on average: a minimum gap checked once per frame let a packet leave only every
	// second frame when the frames were slightly shorter than the gap (62.5 packets per second at 125 fps with a cap of
	// 100, 72 at 144 fps with a cap of 125).
	if ( cls.realtime < clc.nextPacketTime ) {
		// the accumulated commands will go out in the next packet
		return qfalse;
	}
	clc.nextPacketTime = Q_max( clc.nextPacketTime, cls.realtime - interval ) + interval;

	return qtrue;
}

/*
===================
CL_WritePacket

Create and send the command packet to the server
Including both the reliable commands and the usercmds

After the netchan header (sequence number, qport), a client packet holds the serverId, the last server message and
server command received, the client commands not yet acknowledged and the usercmds of the last few packets, all coded
by netcodec (CL_NetcodecWritePacket, plan step 8).
===================
*/
void CL_WritePacket( void ) {
	msg_t		buf;
	byte		data[MAX_MSGLEN];
	int			packetNum;
	int			oldPacketNum;
	int			count;

	// don't send anything if playing back a demo
	if ( clc.demoplaying || cls.state == CA_CINEMATIC ) {
		return;
	}

	MSG_Init( &buf, data, sizeof(data) );

	// we want to send all the usercmds that were generated in the last
	// few packet, so even if a couple packets are dropped in a row,
	// all the cmds will make it to the server
	oldPacketNum = (clc.netchan.outgoingSequence - 1 - CL_PACKET_DUP) & OUT_PACKET_MASK;
	count = cl.cmdNumber - cl.outPackets[ oldPacketNum ].p_cmdNumber;
	if ( count > MAX_PACKET_USERCMDS ) {
		count = MAX_PACKET_USERCMDS;
		Com_Printf("MAX_PACKET_USERCMDS\n");
	}
	if ( count >= 1 && cl_showSend->integer ) {
		Com_Printf( "(%i)", count );
	}

	// the usercmds are a delta (clc_move) unless the last message's snapshot is missing (clc_moveNoDelta: the server
	// sends a full snapshot)
	const qboolean delta = ( !cl_nodelta->integer && cl.snap.valid && clc.serverMessageSequence == cl.snap.messageNum ) ? qtrue : qfalse;
	if ( !CL_NetcodecWritePacket( &buf, count, delta ) ) {
		return;
	}

	if ( count >= 1 && cl.gcmdSentValue )
	{ //hmm, just clear here, I guess.. hoping it will resolve issues with gencmd values sometimes not going through.
		cl.gcmdSendValue = qfalse;
		cl.gcmdSentValue = qfalse;
		cl.gcmdValue = 0;
	}

	//
	// deliver the message
	//
	packetNum = clc.netchan.outgoingSequence & OUT_PACKET_MASK;
	cl.outPackets[ packetNum ].p_realtime = Sys_Milliseconds();
	cl.outPackets[ packetNum ].p_serverTime = count >= 1 ? cl.cmds[ cl.cmdNumber & CMD_MASK ].serverTime : 0;
	cl.outPackets[ packetNum ].p_cmdNumber = cl.cmdNumber;
	clc.lastPacketSentTime = cls.realtime;

	if ( cl_showSend->integer ) {
		Com_Printf( "%i ", buf.cursize );
	}

	CL_Netchan_Transmit (&clc.netchan, &buf);

	// clients never really should have messages large enough
	// to fragment, but in case they do, fire them all off
	// at once
	while ( clc.netchan.unsentFragments ) {
		CL_Netchan_TransmitNextFragment( &clc.netchan );
	}
}

/*
=================
CL_SendCmd

Called every frame to builds and sends a command packet to the server.
=================
*/
// makes the commands that are due at the input's time and sends their packet
static void CL_MakeAndSend( void ) {
	// we create commands even if a demo is playing,
	const int cmdNumber = cl.cmdNumber;
	CL_CreateNewCommands();

	// don't send a packet if the last packet was sent too recently
	if ( !CL_ReadyToSendPacket( cl.cmdNumber != cmdNumber ? qtrue : qfalse ) ) {
		if ( cl_showSend->integer ) {
			Com_Printf( ". " );
		}
		return;
	}

	CL_WritePacket();
}

void CL_SendCmd( void ) {
	// don't send any message if not connected
	if ( cls.state < CA_CONNECTED ) {
		return;
	}

	// don't send commands if paused
	if ( com_sv_running->integer && sv_paused->integer && cl_paused->integer ) {
		return;
	}

	s_inputTime = com_frameTime;
	CL_MakeAndSend();
}

/*
=================
CL_NextCommandMsec

How long until the command clock enters its next tick, in ms of real time (0: it has, the tick's command is due); -1
when no command is to be made between two frames: not in a game, a demo, a pause, or a jump of the clock (left to the
frame, as a map_restart or a hitch is). The command clock is read at the present time: cls.realtime moves once per
frame, with com_frameTime.
=================
*/
int CL_NextCommandMsec( void ) {
	if ( cls.state != CA_ACTIVE || clc.demoplaying || cl.cmdNumber == 0 ) {
		return -1;
	}
	if ( com_sv_running->integer && sv_paused->integer && cl_paused->integer ) {
		return -1;
	}
	const int now = Q_max( cls.realtime + ( Sys_Milliseconds() - com_frameTime ) + cl.cmdTimeDelta, cl.oldCmdTime );
	const int last = cl.cmds[cl.cmdNumber & CMD_MASK].serverTime;	// the end of the tick the last command is for

	if ( abs( now - last ) > 200 ) {
		return -1;
	}
	return Q_max( last - now, 0 );
}

/*
=================
CL_InputTick

Between two frames, when the command clock enters a tick (Com_Frame): reads the input (the system's events, the key
binds), turns the view, makes the tick's command and sends its packet, as a frame does, without drawing. The client's
clock is set to the present for it (the packet's send time, the command clock) and put back: the frame that follows
moves it by its whole length. Returns qfalse when no command was made (the events changed the client's state).
=================
*/
qboolean CL_InputTick( void ) {
	const int cmdNumber = cl.cmdNumber;
	const int realtime = cls.realtime;

	s_inputTime = Com_EventLoop();	// Com_Frame has just polled the input (IN_Frame)
	// the key binds go through the command buffer; not while a wait command counts frames (this is not one)
	if ( !Cbuf_Waiting() ) {
		Cbuf_Execute();
	}
	if ( cls.state < CA_CONNECTED || CL_NextCommandMsec() != 0 ) {
		return qfalse;
	}

	cls.realtime += s_inputTime - com_frameTime;
	cl.cmdTime = Q_max( cls.realtime + cl.cmdTimeDelta, cl.oldCmdTime );
	cl.oldCmdTime = cl.cmdTime;
	CL_MakeAndSend();
	cls.realtime = realtime;

	return cl.cmdNumber != cmdNumber ? qtrue : qfalse;
}

static const cmdList_t inputCmds[] =
{
	{ "centerview", "Centers view on screen", IN_CenterView, NULL },
	{ "+moveup", "Jump", IN_UpDown, NULL },
	{ "-moveup", NULL, IN_UpUp, NULL },
	{ "+movedown", "Crouch", IN_DownDown, NULL },
	{ "-movedown", NULL, IN_DownUp, NULL },
	{ "+left", "Rotate camera left", IN_LeftDown, NULL },
	{ "-left", NULL, IN_LeftUp, NULL },
	{ "+right", "Rotate camera right", IN_RightDown, NULL },
	{ "-right", NULL, IN_RightUp, NULL },
	{ "+forward", "Move forward", IN_ForwardDown, NULL },
	{ "-forward", NULL, IN_ForwardUp, NULL },
	{ "+back", "Move backward", IN_BackDown, NULL },
	{ "-back", NULL, IN_BackUp, NULL },
	{ "+lookup", "Tilt camera up", IN_LookupDown, NULL },
	{ "-lookup", NULL, IN_LookupUp, NULL },
	{ "+lookdown", "Tilt camera down", IN_LookdownDown, NULL },
	{ "-lookdown", NULL, IN_LookdownUp, NULL },
	{ "+strafe", "Hold to strafe", IN_StrafeDown, NULL },
	{ "-strafe", NULL, IN_StrafeUp, NULL },
	{ "+moveleft", "Strafe left", IN_MoveleftDown, NULL },
	{ "-moveleft", NULL, IN_MoveleftUp, NULL },
	{ "+moveright", "Strafe right", IN_MoverightDown, NULL },
	{ "-moveright", NULL, IN_MoverightUp, NULL },
	{ "+speed", "Walk or run", IN_SpeedDown, NULL },
	{ "-speed", NULL, IN_SpeedUp, NULL },
	{ "+attack", "Primary Attack", IN_Button0Down, NULL },
	{ "-attack", NULL, IN_Button0Up, NULL },
	{ "+use", "Use item", IN_Button5Down, NULL },
	{ "-use", NULL, IN_Button5Up, NULL },
	{ "+force_grip", "Hold to use grip force power", IN_Button6Down, NULL },
	{ "-force_grip", NULL, IN_Button6Up, NULL },
	{ "+altattack", "Alternate Attack", IN_Button7Down, NULL },
	{ "-altattack", NULL, IN_Button7Up, NULL },
	{ "+useforce", "Use selected force power", IN_Button9Down, NULL },
	{ "-useforce", NULL, IN_Button9Up, NULL },
	{ "+force_lightning", "Hold to use lightning force power", IN_Button10Down, NULL },
	{ "-force_lightning", NULL, IN_Button10Up, NULL },
	{ "+force_drain", "Hold to use drain force power", IN_Button11Down, NULL },
	{ "-force_drain", NULL, IN_Button11Up, NULL },
	{ "+button0", "Button 0", IN_Button0Down, NULL },
	{ "-button0", NULL, IN_Button0Up, NULL },
	{ "+button1", "Button 1", IN_Button1Down, NULL },
	{ "-button1", NULL, IN_Button1Up, NULL },
	{ "+button2", "Button 2", IN_Button2Down, NULL },
	{ "-button2", NULL, IN_Button2Up, NULL },
	{ "+button3", "Button 3", IN_Button3Down, NULL },
	{ "-button3", NULL, IN_Button3Up, NULL },
	{ "+button4", "Button 4", IN_Button4Down, NULL },
	{ "-button4", NULL, IN_Button4Up, NULL },
	{ "+button5", "Button 5", IN_Button5Down, NULL },
	{ "-button5", NULL, IN_Button5Up, NULL },
	{ "+button6", "Button 6", IN_Button6Down, NULL },
	{ "-button6", NULL, IN_Button6Up, NULL },
	{ "+button7", "Button 7", IN_Button7Down, NULL },
	{ "-button7", NULL, IN_Button7Up, NULL },
	{ "+button8", "Button 8", IN_Button8Down, NULL },
	{ "-button8", NULL, IN_Button8Up, NULL },
	{ "+button9", "Button 9", IN_Button9Down, NULL },
	{ "-button9", NULL, IN_Button9Up, NULL },
	{ "+button10", "Button 10", IN_Button10Down, NULL },
	{ "-button10", NULL, IN_Button10Up, NULL },
	{ "+button11", "Button 11", IN_Button11Down, NULL },
	{ "-button11", NULL, IN_Button11Up, NULL },
	{ "+button12", "Button 12", IN_Button12Down, NULL },
	{ "-button12", NULL, IN_Button12Up, NULL },
	{ "+button13", "Button 13", IN_Button13Down, NULL },
	{ "-button13", NULL, IN_Button13Up, NULL },
	{ "+button14", "Button 14", IN_Button14Down, NULL },
	{ "-button14", NULL, IN_Button14Up, NULL },
	{ "+button15", "Button 15", IN_Button15Down, NULL },
	{ "-button15", NULL, IN_Button15Up, NULL },
	{ "+mlook", "Hold to use mouse look", IN_MLookDown, NULL },
	{ "-mlook", NULL, IN_MLookUp, NULL },
	{ "sv_saberswitch", "Holster/activate lightsaber", IN_GenCMD1, NULL },
	{ "engage_duel", "Engage private duel", IN_GenCMD2, NULL },
	{ "force_heal", "Use heal force power", IN_GenCMD3, NULL },
	{ "force_speed", "Activate speed force power", IN_GenCMD4, NULL },
	{ "force_pull", "Use pull force power", IN_GenCMD5, NULL },
	{ "force_distract", "Activate mind trick force power", IN_GenCMD6, NULL },
	{ "force_rage", "Activate rage force power", IN_GenCMD7, NULL },
	{ "force_protect", "Activate protect force power", IN_GenCMD8, NULL },
	{ "force_absorb", "Activate absorb force power", IN_GenCMD9, NULL },
	{ "force_healother", "Use team heal force power", IN_GenCMD10, NULL },
	{ "force_forcepowerother", "Use team energize force power", IN_GenCMD11, NULL },
	{ "force_seeing", "Activate seeing force power", IN_GenCMD12, NULL },
	{ "use_seeker", "Use seeker drone item", IN_GenCMD13, NULL },
	{ "use_field", "Use forcefield item", IN_GenCMD14, NULL },
	{ "use_bacta", "Use bacta item", IN_GenCMD15, NULL },
	{ "use_electrobinoculars", "Use electro binoculars item", IN_GenCMD16, NULL },
	{ "zoom", "Use binoculars item", IN_GenCMD17, NULL },
	{ "use_sentry", "Use sentry gun item", IN_GenCMD18, NULL },
	{ "saberAttackCycle", "Switch lightsaber attack styles", IN_GenCMD19, NULL },
	{ "force_throw", "Use push force power", IN_GenCMD20, NULL },
	{ "use_jetpack", "Use jetpack item", IN_GenCMD21, NULL },
	{ "use_bactabig", "Use big bacta item", IN_GenCMD22, NULL },
	{ "use_healthdisp", "Use health dispenser item", IN_GenCMD23, NULL },
	{ "use_ammodisp", "Use ammo dispenser item", IN_GenCMD24, NULL },
	{ "use_eweb", "Use e-web item", IN_GenCMD25, NULL },
	{ "use_cloak", "Use cloaking item", IN_GenCMD26, NULL },
	{ "taunt", "Taunt", IN_GenCMD27, NULL },
	{ "bow", "Bow", IN_GenCMD28, NULL },
	{ "meditate", "Meditate", IN_GenCMD29, NULL },
	{ "flourish", "Flourish", IN_GenCMD30, NULL },
	{ "gloat", "Gloat", IN_GenCMD31, NULL },
	{ "useGivenForce", "Use specified force power", IN_UseGivenForce, NULL },
	{ "automap_button", "Show/hide automap", IN_AutoMapButton, NULL },
	{ "automap_toggle", "Show/hide radar", IN_AutoMapToggle, NULL },
	{ "voicechat", "Open voice chat menu", IN_VoiceChatButton, NULL },
	{ NULL, NULL, NULL, NULL }
};

/*
============
CL_InitInput
============
*/
void CL_InitInput( void ) {
	Cmd_AddCommandList( inputCmds );

	cl_nodelta = Cvar_Get ("cl_nodelta", "0", 0);
	cl_debugMove = Cvar_Get ("cl_debugMove", "0", 0);
}

/*
============
CL_ShutdownInput
============
*/
void CL_ShutdownInput( void ) {
	Cmd_RemoveCommandList( inputCmds );
}
