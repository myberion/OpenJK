// Scripted bot scenarios for plan step 14: two bots, an attacker and a defender, placed face to face and reset at every
// repetition, play a fixed sequence of commands timed in milliseconds (one every 10 ms, as the bots of
// bot_debugClientRate), so that a scenario run at 20 Hz and at the tick's rate compares the game per swing, per shot or
// per power use rather than through the chaos of a free-for-all. A first repetition, not counted, replaces the bots'
// state left by the game before the scenario (animations); the statistics of the counted repetitions are printed at the
// end (gamestats), and the bots then stand still until "scenario off".
//
// scenario saber <reps> <fast|medium|strong|dual|staff> <idle|attack> [distance]
// scenario force <reps> <lightning|grip|drain|rage|protect|absorb> [distance]
// scenario weapon <reps> <weapon number> <0|1 alternate> [distance]
// scenario off

#include "g_local.h"

extern void WP_ForcePowerStop( gentity_t *self, forcePowers_t forcePower );
extern qboolean G_SetSaber( gentity_t *ent, int saberNum, char *saberName, qboolean siegeOverride );
extern void Svcmd_GameStats_f( void );

#define SCN_REP_MSEC		2000	// one repetition
#define SCN_SETTLE_MSEC		500		// still, after the reset, before the script starts

typedef enum {
	SCN_NONE,
	SCN_SABER,
	SCN_FORCE,
	SCN_WEAPON
} scenarioType_t;

static struct {
	scenarioType_t	type;
	int				reps;			// repetitions asked
	int				rep;			// the current repetition: 0 is the warm-up, then 1 to reps
	qboolean		done;			// all repetitions played: the bots stand still
	int				repStart;		// level.time of the current repetition's reset, 0 before the first
	int				client[2];		// attacker, defender
	int				style;			// the attacker's saber style
	qboolean		counter;		// the defender attacks at the same time
	forcePowers_t	power;
	int				weapon;
	qboolean		alt;
	float			dist;			// between the two origins
	vec3_t			origin;			// the attacker's place, facing yaw
	float			yaw;
	int				traceReps;		// repetitions traced at every 20 Hz instant (cvar scenario_trace), to diff two rates
} scn;

// the eight attack directions, as forward and right moves
static const signed char scnDirs[8][2] = {
	{ 127, 0 }, { -127, 0 }, { 0, -127 }, { 0, 127 }, { 127, -127 }, { 127, 127 }, { -127, -127 }, { -127, 127 }
};

qboolean G_ScenarioControls( int clientNum ) {
	return ( scn.type != SCN_NONE && ( clientNum == scn.client[0] || clientNum == scn.client[1] ) ) ? qtrue : qfalse;
}

// a place where the attacker stands on a spawn point and the defender on the floor in front of it
static qboolean G_ScenarioFindPlace( void ) {
	const vec3_t mins = { -15, -15, DEFAULT_MINS_2 };
	const vec3_t maxs = { 15, 15, DEFAULT_MAXS_2 };
	gentity_t *spot = NULL;

	while ( ( spot = G_Find( spot, FOFS( classname ), "info_player_deathmatch" ) ) != NULL ) {
		vec3_t fwd, start, front, below;
		trace_t tr;

		VectorSet( fwd, cos( DEG2RAD( spot->s.angles[YAW] ) ), sin( DEG2RAD( spot->s.angles[YAW] ) ), 0 );
		VectorCopy( spot->s.origin, start );
		start[2] += 9;
		VectorMA( start, scn.dist + 32, fwd, front );
		trap->Trace( &tr, start, mins, maxs, front, ENTITYNUM_NONE, MASK_SOLID, qfalse, 0, 0 );
		if ( tr.fraction < 1.0f || tr.startsolid || tr.allsolid ) {
			continue;
		}
		VectorMA( start, scn.dist, fwd, front );
		VectorCopy( front, below );
		below[2] -= 48;
		trap->Trace( &tr, front, mins, maxs, below, ENTITYNUM_NONE, MASK_SOLID, qfalse, 0, 0 );
		if ( tr.fraction >= 1.0f || tr.startsolid || tr.endpos[2] < start[2] - 24 ) {
			continue;	// no floor at the attacker's height in front
		}
		{ // away from doors and lifts, which crush the bots standing in their way
			int e;
			qboolean nearMover = qfalse;

			for ( e = MAX_CLIENTS; e < level.num_entities && !nearMover; e++ ) {
				const gentity_t *m = &g_entities[e];
				vec3_t center;

				if ( !m->inuse || m->s.eType != ET_MOVER ) {
					continue;
				}
				VectorAdd( m->r.absmin, m->r.absmax, center );
				VectorScale( center, 0.5f, center );
				nearMover = ( Distance( center, start ) < 512 ) ? qtrue : qfalse;
			}
			if ( nearMover ) {
				continue;
			}
		}
		VectorCopy( start, scn.origin );
		scn.yaw = spot->s.angles[YAW];
		return qtrue;
	}
	return qfalse;
}

static void G_ScenarioPlace( gentity_t *ent, const vec3_t origin, float yaw ) {
	gclient_t *client = ent->client;
	vec3_t angles = { 0, 0, 0 };
	int i;

	if ( ent->health <= 0 || client->ps.stats[STAT_HEALTH] <= 0 ) {
		ClientRespawn( ent );
	}
	for ( i = 0; i < NUM_FORCE_POWERS; i++ ) {
		if ( client->ps.fd.forcePowersActive & ( 1 << i ) ) {
			WP_ForcePowerStop( ent, (forcePowers_t)i );
		}
		client->ps.fd.forcePowerDuration[i] = 0;
		client->ps.fd.forcePowerDebounce[i] = 0;
	}
	ent->health = client->ps.stats[STAT_HEALTH] = client->ps.stats[STAT_MAX_HEALTH] = 100;
	client->ps.stats[STAT_ARMOR] = 0;
	client->ps.fd.forcePower = 100;
	client->ps.fd.forcePowerRegenDebounceTime = level.time;
	client->ps.fd.forceGripBeingGripped = 0;
	client->ps.fd.forceRageRecoveryTime = 0;
	client->ps.forceHandExtend = HANDEXTEND_NONE;
	client->ps.forceHandExtendTime = 0;
	client->ps.weaponTime = 0;
	client->ps.weaponstate = WEAPON_READY;
	client->ps.saberBlocked = BLOCKED_NONE;
	client->ps.saberMove = LS_READY;
	client->ps.saberHolstered = 0;
	client->ps.legsTimer = client->ps.torsoTimer = 0;
	client->ps.electrifyTime = 0;
	client->ps.pm_time = 0;
	client->ps.pm_flags &= ~( PMF_TIME_KNOCKBACK | PMF_TIME_WATERJUMP );
	client->ps.saberAttackWound = client->ps.saberIdleWound = 0;
	client->invulnerableTimer = 0;
	client->ps.eFlags &= ~EF_INVULNERABLE;

	trap->UnlinkEntity( (sharedEntity_t *)ent );
	VectorCopy( origin, client->ps.origin );
	VectorClear( client->ps.velocity );
	angles[YAW] = yaw;
	SetClientViewAngle( ent, angles );
	client->ps.eFlags ^= EF_TELEPORT_BIT;
	BG_PlayerStateToEntityState( &client->ps, &ent->s, qtrue );
	VectorCopy( client->ps.origin, ent->r.currentOrigin );
	trap->LinkEntity( (sharedEntity_t *)ent );
}

static void G_ScenarioEquip( gentity_t *ent, qboolean attacker ) {
	gclient_t *client = ent->client;

	client->ps.fd.forcePowerLevel[FP_SABER_OFFENSE] = FORCE_LEVEL_3;
	client->ps.fd.forcePowerLevel[FP_SABER_DEFENSE] = FORCE_LEVEL_3;
	client->ps.fd.forcePowersKnown |= ( 1 << FP_SABER_OFFENSE ) | ( 1 << FP_SABER_DEFENSE );
	if ( scn.type == SCN_FORCE && attacker ) {
		client->ps.fd.forcePowerLevel[scn.power] = FORCE_LEVEL_3;
		client->ps.fd.forcePowersKnown |= ( 1 << scn.power );
		client->ps.fd.forceSide = ( scn.power == FP_PROTECT || scn.power == FP_ABSORB ) ? FORCE_LIGHTSIDE : FORCE_DARKSIDE;
	}

	if ( scn.type == SCN_WEAPON ) {
		const int weapon = attacker ? scn.weapon : WP_MELEE;	// the defender cannot block with a saber

		client->ps.stats[STAT_WEAPONS] |= ( 1 << weapon );
		client->ps.ammo[weaponData[weapon].ammoIndex] = 999;
		client->ps.weapon = weapon;
		return;
	}

	client->ps.stats[STAT_WEAPONS] |= ( 1 << WP_SABER );
	client->ps.weapon = WP_SABER;
	if ( attacker && scn.type == SCN_SABER ) {
		G_SetSaber( ent, 0, scn.style == SS_STAFF ? "dual_1" : "single_1", qtrue );
		G_SetSaber( ent, 1, scn.style == SS_DUAL ? "single_1" : "none", qtrue );
		client->ps.fd.saberAnimLevel = client->ps.fd.saberAnimLevelBase = client->ps.fd.saberDrawAnimLevel = scn.style;
		client->saberCycleQueue = scn.style;
	}
	else {
		G_SetSaber( ent, 0, "single_1", qtrue );
		G_SetSaber( ent, 1, "none", qtrue );
		client->ps.fd.saberAnimLevel = client->ps.fd.saberAnimLevelBase = client->ps.fd.saberDrawAnimLevel = SS_MEDIUM;
		client->saberCycleQueue = SS_MEDIUM;
	}
}

// called at the start of every server frame: resets the two bots at the start of each repetition
void G_ScenarioFrame( void ) {
	vec3_t fwd, front;
	int seed;

	if ( scn.type == SCN_NONE || scn.done ) {
		return;
	}
	if ( scn.repStart && level.time < scn.repStart + SCN_REP_MSEC ) {
		return;
	}
	if ( scn.rep >= scn.reps ) {
		trap->Print( "scenario done: %d repetitions\n", scn.rep );
		Svcmd_GameStats_f();
		scn.done = qtrue;
		return;
	}
	if ( !g_entities[scn.client[0]].inuse || !g_entities[scn.client[1]].inuse ) {
		trap->Print( "scenario stopped: a bot left\n" );
		scn.type = SCN_NONE;
		return;
	}

	VectorSet( fwd, cos( DEG2RAD( scn.yaw ) ), sin( DEG2RAD( scn.yaw ) ), 0 );
	VectorMA( scn.origin, scn.dist, fwd, front );
	G_ScenarioPlace( &g_entities[scn.client[0]], scn.origin, scn.yaw );
	G_ScenarioPlace( &g_entities[scn.client[1]], front, AngleNormalize360( scn.yaw + 180 ) );
	G_ScenarioEquip( &g_entities[scn.client[0]], qtrue );
	G_ScenarioEquip( &g_entities[scn.client[1]], qfalse );

	// the same random numbers at the start of a repetition at any rate
	seed = 7919 * ( scn.rep + 1 );
	srand( seed );
	Rand_Init( seed );

	scn.repStart = level.time;
	scn.rep++;
	if ( scn.rep == 1 ) {
		memset( &g_stats, 0, sizeof( g_stats ) );	// the counted repetitions start
	}
}

// called at the end of the 20 Hz instants: the two bots' state, printed for the first scenario_trace repetitions, so that
// runs at two rates can be diffed line by line
void G_ScenarioTrace( void ) {
	int n;

	if ( scn.type == SCN_NONE || scn.done || scn.rep < 1 || scn.rep > scn.traceReps ) {
		return;
	}
	for ( n = 0; n < 2; n++ ) {
		const gentity_t *ent = &g_entities[scn.client[n]];
		const playerState_t *ps = &ent->client->ps;
		const bladeInfo_t *blade = &ent->client->saber[0].blade[0];

		trap->Print( "SCN %d %4d %c cmd%+d hp%d mv%d blk%d wt%d ta%d tt%d la%d lt%d o%.1f,%.1f,%.1f m%.1f,%.1f,%.1f\n",
			scn.rep, level.time - scn.repStart, n ? 'D' : 'A', ps->commandTime - level.time, ent->health, ps->saberMove,
			ps->saberBlocked, ps->weaponTime, ps->torsoAnim, ps->torsoTimer, ps->legsAnim, ps->legsTimer,
			ps->origin[0], ps->origin[1], ps->origin[2], blade->muzzlePoint[0], blade->muzzlePoint[1], blade->muzzlePoint[2] );
	}
}

// every health loss of a client during the traced repetitions: the tick, the inflictor's position and the point hit
void G_ScenarioDamageTrace( gentity_t *targ, gentity_t *inflictor, const vec3_t point, int take, int mod ) {
	if ( scn.type == SCN_NONE || scn.done || scn.rep < 1 || scn.rep > scn.traceReps ) {
		return;
	}
	trap->Print( "SCN %d %4d dmg tick%+d targ%d take%d mod%d surf%d@%+d inf%d p%.1f,%.1f,%.1f io%.1f,%.1f,%.1f\n", scn.rep,
		level.time - scn.repStart, level.tickTime - level.time, targ->s.number, take, mod, targ->client->g2LastSurfaceHit,
		targ->client->g2LastSurfaceTime - level.time, inflictor ? inflictor->s.number : -1,
		point ? point[0] : 0.0f, point ? point[1] : 0.0f, point ? point[2] : 0.0f,
		inflictor ? inflictor->r.currentOrigin[0] : 0.0f, inflictor ? inflictor->r.currentOrigin[1] : 0.0f,
		inflictor ? inflictor->r.currentOrigin[2] : 0.0f );
}

// fills a bot's command for the time t; false when the bot is not in the scenario
qboolean G_ScenarioCommand( int clientNum, int t, usercmd_t *ucmd ) {
	gentity_t *ent = &g_entities[clientNum];
	const qboolean attacker = ( clientNum == scn.client[0] ) ? qtrue : qfalse;
	const int phase = t - scn.repStart - SCN_SETTLE_MSEC;
	vec3_t view = { 0, 0, 0 };
	int i;

	if ( !G_ScenarioControls( clientNum ) || !ent->client ) {
		return qfalse;
	}

	memset( ucmd, 0, sizeof( *ucmd ) );
	ucmd->serverTime = t;
	ucmd->weapon = ent->client->ps.weapon;
	view[YAW] = attacker ? scn.yaw : AngleNormalize360( scn.yaw + 180 );
	for ( i = 0; i < 3; i++ ) {
		ucmd->angles[i] = ANGLE2SHORT( view[i] ) - ent->client->ps.delta_angles[i];
	}
	if ( !scn.repStart || phase < 0 || scn.done ) {
		return qtrue;	// still
	}

	switch ( scn.type ) {
	case SCN_SABER:
		if ( phase < 100 && ( attacker || scn.counter ) ) {
			const signed char *dir = scnDirs[( scn.rep + 7 ) % 8];	// repetition 1 in the first direction

			ucmd->buttons = BUTTON_ATTACK;
			ucmd->forwardmove = dir[0];
			ucmd->rightmove = attacker ? dir[1] : -dir[1];
		}
		break;
	case SCN_FORCE:
		if ( !attacker ) {
			break;
		}
		switch ( scn.power ) {
		case FP_LIGHTNING:
			if ( phase < 1000 ) ucmd->buttons = BUTTON_FORCE_LIGHTNING;
			break;
		case FP_GRIP:
			if ( phase < 1500 ) ucmd->buttons = BUTTON_FORCEGRIP;
			break;
		case FP_DRAIN:
			if ( phase < 1000 ) ucmd->buttons = BUTTON_FORCE_DRAIN;
			break;
		default:	// powers used on oneself: switched on once, held until the next reset
			ucmd->forcesel = scn.power;
			ent->client->ps.fd.forcePowerSelected = scn.power;
			if ( phase < 50 ) ucmd->buttons = BUTTON_FORCEPOWER;
			break;
		}
		break;
	case SCN_WEAPON:
		if ( attacker && phase < 1000 ) {
			ucmd->buttons = scn.alt ? BUTTON_ALT_ATTACK : BUTTON_ATTACK;
		}
		break;
	default:
		break;
	}
	return qtrue;
}

static int G_ScenarioArgInt( int n, int def ) {
	char arg[MAX_TOKEN_CHARS];

	if ( trap->Argc() <= n ) {
		return def;
	}
	trap->Argv( n, arg, sizeof( arg ) );
	return atoi( arg );
}

void Svcmd_Scenario_f( void ) {
	char type[MAX_TOKEN_CHARS], arg[MAX_TOKEN_CHARS];
	int i, n = 0;

	trap->Argv( 1, type, sizeof( type ) );
	if ( !Q_stricmp( type, "off" ) || !type[0] ) {
		scn.type = SCN_NONE;
		return;
	}
	if ( !trap->Cvar_VariableIntegerValue( "sv_cheats" ) ) {
		trap->Print( "scenario: needs sv_cheats 1 (devmap)\n" );
		return;
	}

	memset( &scn, 0, sizeof( scn ) );
	scn.rep = -1;	// the first reset starts the warm-up, repetition 0
	for ( i = 0; i < MAX_CLIENTS && n < 2; i++ ) {
		if ( g_entities[i].inuse && g_entities[i].client && ( g_entities[i].r.svFlags & SVF_BOT )
			&& g_entities[i].client->pers.connected == CON_CONNECTED ) {
			scn.client[n++] = i;
		}
	}
	if ( n < 2 ) {
		trap->Print( "scenario: needs two bots\n" );
		return;
	}
	scn.reps = G_ScenarioArgInt( 2, 100 );
	trap->Argv( 3, arg, sizeof( arg ) );
	if ( !Q_stricmp( type, "saber" ) ) {
		static const char *styles[] = { "fast", "medium", "strong", "dual", "staff" };
		static const int styleValues[] = { SS_FAST, SS_MEDIUM, SS_STRONG, SS_DUAL, SS_STAFF };
		char mode[MAX_TOKEN_CHARS];

		scn.style = SS_MEDIUM;
		for ( i = 0; i < (int)ARRAY_LEN( styles ); i++ ) {
			if ( !Q_stricmp( arg, styles[i] ) ) {
				scn.style = styleValues[i];
			}
		}
		trap->Argv( 4, mode, sizeof( mode ) );
		scn.counter = !Q_stricmp( mode, "attack" ) ? qtrue : qfalse;
		scn.dist = (float)G_ScenarioArgInt( 5, 56 );
		scn.type = SCN_SABER;
	}
	else if ( !Q_stricmp( type, "force" ) ) {
		static const char *powers[] = { "lightning", "grip", "drain", "rage", "protect", "absorb" };
		static const forcePowers_t powerValues[] = { FP_LIGHTNING, FP_GRIP, FP_DRAIN, FP_RAGE, FP_PROTECT, FP_ABSORB };

		scn.power = FP_LIGHTNING;
		for ( i = 0; i < (int)ARRAY_LEN( powers ); i++ ) {
			if ( !Q_stricmp( arg, powers[i] ) ) {
				scn.power = powerValues[i];
			}
		}
		scn.dist = (float)G_ScenarioArgInt( 4, 128 );
		scn.type = SCN_FORCE;
	}
	else if ( !Q_stricmp( type, "weapon" ) ) {
		scn.weapon = atoi( arg );
		if ( scn.weapon <= WP_NONE || scn.weapon >= WP_NUM_WEAPONS ) {
			trap->Print( "scenario: bad weapon number\n" );
			return;
		}
		scn.alt = G_ScenarioArgInt( 4, 0 ) ? qtrue : qfalse;
		scn.dist = (float)G_ScenarioArgInt( 5, 256 );
		scn.type = SCN_WEAPON;
	}
	else {
		trap->Print( "scenario: saber, force, weapon or off\n" );
		return;
	}
	if ( !G_ScenarioFindPlace() ) {
		trap->Print( "scenario: no open spawn point for a distance of %.0f\n", scn.dist );
		scn.type = SCN_NONE;
		return;
	}
	scn.traceReps = trap->Cvar_VariableIntegerValue( "scenario_trace" );
	trap->Print( "scenario %s: %d repetitions of %d ms, bots %d and %d, distance %.0f\n", type, scn.reps, SCN_REP_MSEC,
		scn.client[0], scn.client[1], scn.dist );
}
