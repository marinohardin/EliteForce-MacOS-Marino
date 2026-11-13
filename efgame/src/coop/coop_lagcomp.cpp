// coop_lagcomp.cpp -- single-player co-op: player 2's instant-hit shots hit what the guest saw.
//
// The guest sees the host's world a little in the past: its cgame draws one host frame behind, and
// the frames and its commands spend time on the network. A shot it aims at a moving NPC reaches the
// host's game after the NPC has moved on, so it would miss on the host although it hit on the
// guest's screen. The host keeps where the NPCs (and other clients, and moving brush models) were
// over the last second and a half; while player 2 fires an instant-hit weapon, each of the shot's
// traces runs against the world as it was at the host time the guest's screen showed (each guest
// command carries it, efcode/sys/sys_coop_net.c), and everything goes back at once after the trace,
// so damage, deaths and all the rest happen where things really are.
//
// The idea is cMod's ping compensation by Noah Metzger (Chomenor) (cmod-game-code, the pingcomp
// "position shift" and "instant weapons" modules): keep a short history of positions, shift the
// other players back to the shooter's time for the weapon's traces only, and cap how far back. This
// is our own code for the single-player game; the shooter is only player 2, player 1's shots (the
// host's own) are not shifted. Player 2's projectiles start at the view time instead (see
// LC_ShiftMissiles), and the spread of its scavenger rifle and tetrion shots is drawn from a
// sequence the guest's cgame repeats (COOP_SeedShot), so the guest can show them as it fires.
//
// Console (host): coop_lagcomp 0|1 (default 1), coop_lagcomp_max <ms> (default 400: up to 300 ms of
// round trip plus the guest's one-frame view delay and one queued command), coop_lagcomp_debug 1
// (print each shifted shot, with what it would have hit without the shift).

#include "g_local.h"
#include "coop_public.h"
#include "coop_local.h"

#define LC_FRAMES		32		// frames of history (a second and a half at 20 frames a second)
#define LC_MAX_ENTS		256		// entities kept per frame (the rest: not shifted)

typedef struct
{
	short		num;
	byte		isClient;
	vec3_t		origin;
	vec3_t		angles;
	vec3_t		mins, maxs;
} lcEnt_t;

typedef struct
{
	int			time;			// level.time at the end of that frame (0: unused)
	int			count;
	lcEnt_t		ents[LC_MAX_ENTS];
} lcFrame_t;

static lcFrame_t	lc_frames[LC_FRAMES];
static int			lc_newest = -1;			// index of the newest frame
static int			lc_levelTime;			// guards against a new level's times

static cvar_t		*lc_enable, *lc_max, *lc_debug;

// during player 2's FireWeapon
static int			lc_shooter = -1;
static int			lc_shiftTime;			// the level time to shift to; 0: no shift for this shot
static void			(*lc_realTrace)( trace_t *results, const vec3_t start, const vec3_t mins, const vec3_t maxs,
						const vec3_t end, int passEntityNum, int contentmask );

// what was moved for one trace, to put back
static struct
{
	int			num;
	vec3_t		origin, angles, mins, maxs;
} lc_saved[LC_MAX_ENTS * 2];
static int			lc_numSaved;

static void LC_Cvars( void )
{
	if ( !lc_enable )
	{
		lc_enable = gi.cvar( "coop_lagcomp", "1", 0 );
		lc_max = gi.cvar( "coop_lagcomp_max", "400", 0 );
		lc_debug = gi.cvar( "coop_lagcomp_debug", "0", 0 );
	}
}

// what can be hit and moves: clients (NPCs, the players) and moving brush models
static qboolean LC_Tracked( const gentity_t *ent )
{
	if ( !ent->inuse || !ent->linked || !ent->contents || ent->freeAfterEvent )
	{
		return qfalse;
	}
	if ( ent->client )
	{
		return qtrue;
	}
	return ent->bmodel && ( ent->s.pos.trType != TR_STATIONARY || ent->s.apos.trType != TR_STATIONARY );
}

/*
COOP_LagRecord

The end of every game frame while a guest is connected: where everything that can be hit and
moves is now.
*/
void COOP_LagRecord( void )
{
	lcFrame_t	*f;

	LC_Cvars();
	if ( !coop_enabled || !lc_enable->integer || !COOP_GuestConnected() )
	{
		lc_newest = -1;
		return;
	}
	if ( lc_newest >= 0 && level.time <= lc_frames[lc_newest].time )
	{
		if ( level.time < lc_frames[lc_newest].time )
		{
			lc_newest = -1;		// time went back: a new level
		}
		else
		{
			return;				// the same frame again (a held level start)
		}
	}
	lc_newest = ( lc_newest + 1 ) % LC_FRAMES;
	f = &lc_frames[lc_newest];
	f->time = level.time;
	f->count = 0;
	for ( int i = 0; i < globals.num_entities && f->count < LC_MAX_ENTS; i++ )
	{
		gentity_t *ent = &g_entities[i];
		if ( !LC_Tracked( ent ) )
		{
			continue;
		}
		lcEnt_t *e = &f->ents[f->count++];
		e->num = (short)i;
		e->isClient = ent->client ? 1 : 0;
		VectorCopy( ent->currentOrigin, e->origin );
		VectorCopy( ent->currentAngles, e->angles );
		VectorCopy( ent->mins, e->mins );
		VectorCopy( ent->maxs, e->maxs );
	}
}

static const lcEnt_t *LC_Find( const lcFrame_t *f, int num )
{
	for ( int i = 0; i < f->count; i++ )		// (sorted by number: a short walk would do, this is cheap enough)
	{
		if ( f->ents[i].num == num )
		{
			return &f->ents[i];
		}
	}
	return NULL;
}

static void LC_Move( gentity_t *ent, const vec3_t origin, const vec3_t angles, const vec3_t mins, const vec3_t maxs )
{
	if ( lc_numSaved >= (int)( sizeof( lc_saved ) / sizeof( lc_saved[0] ) ) )
	{
		return;
	}
	if ( VectorCompare( origin, ent->currentOrigin ) && VectorCompare( angles, ent->currentAngles ) &&
		VectorCompare( mins, ent->mins ) && VectorCompare( maxs, ent->maxs ) )
	{
		return;
	}
	lc_saved[lc_numSaved].num = ent->s.number;
	VectorCopy( ent->currentOrigin, lc_saved[lc_numSaved].origin );
	VectorCopy( ent->currentAngles, lc_saved[lc_numSaved].angles );
	VectorCopy( ent->mins, lc_saved[lc_numSaved].mins );
	VectorCopy( ent->maxs, lc_saved[lc_numSaved].maxs );
	lc_numSaved++;
	VectorCopy( origin, ent->currentOrigin );
	VectorCopy( angles, ent->currentAngles );
	VectorCopy( mins, ent->mins );
	VectorCopy( maxs, ent->maxs );
	gi.linkentity( ent );
}

// everything tracked to where it was at time t (between two kept frames: in between, as the
// guest's cgame drew it)
static void LC_Shift( int t )
{
	int			older = -1, newer = -1;
	float		frac = 0;

	lc_numSaved = 0;
	if ( lc_newest < 0 )
	{
		return;
	}
	// the newest frame at or before t, and the one after it
	for ( int k = 0; k < LC_FRAMES; k++ )
	{
		int i = ( lc_newest - k + LC_FRAMES ) % LC_FRAMES;
		if ( !lc_frames[i].time )
		{
			break;
		}
		if ( lc_frames[i].time <= t )
		{
			older = i;
			break;
		}
		newer = i;
		if ( k == LC_FRAMES - 1 )
		{
			older = i;		// older than everything kept: the oldest
		}
	}
	if ( older < 0 )
	{
		return;
	}
	if ( newer >= 0 && lc_frames[newer].time > lc_frames[older].time )
	{
		frac = (float)( t - lc_frames[older].time ) / (float)( lc_frames[newer].time - lc_frames[older].time );
		frac = Com_Clamp( 0, 1, frac );
	}
	else
	{
		newer = -1;
	}

	const lcFrame_t *a = &lc_frames[older], *b = newer >= 0 ? &lc_frames[newer] : NULL;
	for ( int i = 0; i < a->count; i++ )
	{
		const lcEnt_t	*ea = &a->ents[i], *eb = b ? LC_Find( b, ea->num ) : NULL;
		gentity_t		*ent = &g_entities[ea->num];
		vec3_t			org, ang;

		if ( ea->num == lc_shooter || !LC_Tracked( ent ) || ( ent->client != NULL ) != ( ea->isClient != 0 ) )
		{
			continue;
		}
		VectorCopy( ea->origin, org );
		VectorCopy( ea->angles, ang );
		if ( eb )
		{
			for ( int j = 0; j < 3; j++ )
			{
				org[j] += ( eb->origin[j] - ea->origin[j] ) * frac;
				ang[j] = LerpAngle( ea->angles[j], eb->angles[j], frac );
			}
		}
		else if ( b )
		{
			// gone from the newer frame (stopped moving): from where it was to where it is
			for ( int j = 0; j < 3; j++ )
			{
				org[j] += ( ent->currentOrigin[j] - ea->origin[j] ) * frac;
			}
		}
		LC_Move( ent, org, ang, frac < 0.5f || !eb ? ea->mins : eb->mins, frac < 0.5f || !eb ? ea->maxs : eb->maxs );
	}
}

static void LC_Restore( void )
{
	for ( int i = lc_numSaved - 1; i >= 0; i-- )
	{
		gentity_t *ent = &g_entities[lc_saved[i].num];
		VectorCopy( lc_saved[i].origin, ent->currentOrigin );
		VectorCopy( lc_saved[i].angles, ent->currentAngles );
		VectorCopy( lc_saved[i].mins, ent->mins );
		VectorCopy( lc_saved[i].maxs, ent->maxs );
		gi.linkentity( ent );
	}
	lc_numSaved = 0;
}

// gi.trace while player 2's shot is being fired: against the shifted world, then everything back
static void LC_Trace( trace_t *results, const vec3_t start, const vec3_t mins, const vec3_t maxs,
	const vec3_t end, int passEntityNum, int contentmask )
{
	if ( !( contentmask & ( CONTENTS_BODY | CONTENTS_SOLID ) ) )
	{
		lc_realTrace( results, start, mins, maxs, end, passEntityNum, contentmask );
		return;
	}
	LC_Shift( lc_shiftTime );
	lc_realTrace( results, start, mins, maxs, end, passEntityNum, contentmask );
	LC_Restore();
	if ( lc_debug->integer && ( contentmask & CONTENTS_BODY ) )
	{
		trace_t plain;
		lc_realTrace( &plain, start, mins, maxs, end, passEntityNum, contentmask );
		gi.Printf( "co-op lagcomp: player 2 shot at %d, seen at %d (%d ms back): hit %d%s; unshifted: %d\n",
			level.previousTime, lc_shiftTime, level.previousTime - lc_shiftTime, results->entityNum,
			results->entityNum < ENTITYNUM_WORLD && g_entities[results->entityNum].client ? " (a client)" : "",
			plain.entityNum );
	}
}

// the weapons whose shots are traces (the rest launch projectiles, which aren't shifted)
static qboolean LC_InstantHit( int weapon, qboolean alt )
{
	switch ( weapon )
	{
	case WP_PHASER:
	case WP_COMPRESSION_RIFLE:
	case WP_IMOD:
	case WP_PROTON_GUN:
		return qtrue;
	case WP_TETRION_DISRUPTOR:
	case WP_DREADNOUGHT:
		return !alt;
	case WP_STASIS:
		return alt;
	default:
		return qfalse;
	}
}

/*
Player 2's projectiles

The guest draws a projectile of player 2 from the moment it fires (coop_predict.cpp), flying from the
host time its screen showed then (the shot's view time). The host's own projectile starts when the
command arrives, a round trip later on the guest's screen; it is started as if fired at the view
time instead (its first frame's move traces the whole way from the muzzle, so nothing is skipped),
and the guest's drawn one hands over to it where they meet. At most coop_lagcomp_max back, as for
the traces. Player 1 sees player 2's projectiles start that far along.
*/
static byte			lc_hadMissile[MAX_GENTITIES];	// player 2's projectiles that were there before this shot

static qboolean LC_P2Missile( const gentity_t *m, const gentity_t *shooter )
{
	return m->inuse && m->s.eType == ET_MISSILE && m->owner == shooter;
}

static void LC_NoteMissiles( gentity_t *shooter )
{
	for ( int i = 0; i < globals.num_entities; i++ )
	{
		lc_hadMissile[i] = LC_P2Missile( &g_entities[i], shooter ) ? 1 : 0;
	}
}

static void LC_ShiftMissiles( gentity_t *shooter )
{
	int viewTime = coop_p2.shotViewTime;

	if ( !lc_enable->integer || viewTime >= level.time )
	{
		return;
	}
	if ( viewTime < level.time - lc_max->integer )
	{
		viewTime = level.time - lc_max->integer;
	}
	for ( int i = 0; i < globals.num_entities; i++ )
	{
		gentity_t *m = &g_entities[i];

		if ( lc_hadMissile[i] || !LC_P2Missile( m, shooter ) || m->s.pos.trTime != level.time ||
			( m->s.pos.trType != TR_LINEAR && m->s.pos.trType != TR_GRAVITY ) || VectorCompare( m->s.pos.trDelta, vec3_origin ) )
		{
			continue;		// (the arc welder's burst doesn't fly)
		}
		m->s.pos.trTime = viewTime;
		if ( lc_debug->integer )
		{
			gi.Printf( "co-op lagcomp: player 2's projectile %d (weapon %d) started %d ms back\n", i, m->s.weapon,
				level.time - viewTime );
		}
	}
}

/*
COOP_ShotStart / COOP_ShotEnd

Around FireWeapon. For player 2: its temp entities name it (the guest's cgame leaves out the host's
effects of shots it already showed), it fires from where its eyes are after this command's move
(where the guest's prediction fired), and an instant-hit shot steered by the guest is traced against
the world the guest saw.
*/
void COOP_ShotStart( gentity_t *ent, qboolean alt )
{
	int now, viewTime, back;

	lc_shooter = -1;
	lc_shiftTime = 0;
	if ( !coop_enabled || !COOP_IsP2( ent ) || !ent->client )
	{
		return;
	}
	lc_shooter = ent->s.number;
	VectorCopy( ent->client->ps.origin, ent->client->renderInfo.eyePoint );
	ent->client->renderInfo.eyePoint[2] += ent->client->ps.viewheight;
	VectorCopy( ent->client->ps.viewangles, ent->client->renderInfo.eyeAngles );

	LC_Cvars();
	viewTime = coop_p2.shotViewTime;
	if ( viewTime )
	{
		COOP_SeedShot( viewTime );		// the guest's prediction draws the same spread
		LC_NoteMissiles( ent );
	}
	if ( !lc_enable->integer || !viewTime || lc_newest < 0 || !LC_InstantHit( ent->s.weapon, alt ) )
	{
		if ( lc_debug->integer )
		{
			gi.Printf( "co-op lagcomp: player 2 shot at %d (weapon %d%s), not shifted\n", level.time, ent->s.weapon,
				alt ? " alt" : "" );
		}
		return;
	}
	// what the world looks like now is the last frame's end (player 2 moves first in a game frame)
	now = lc_frames[lc_newest].time;
	back = now - viewTime;
	if ( back <= 0 )
	{
		return;
	}
	if ( back > lc_max->integer )
	{
		back = lc_max->integer;
	}
	lc_shiftTime = now - back;
	lc_realTrace = gi.trace;
	gi.trace = LC_Trace;
}

void COOP_ShotEnd( void )
{
	if ( lc_shiftTime && lc_realTrace )
	{
		gi.trace = lc_realTrace;
		lc_realTrace = NULL;
	}
	if ( lc_shooter > 0 && coop_p2.shotViewTime )
	{
		LC_ShiftMissiles( &g_entities[lc_shooter] );
	}
	COOP_UnseedShot();
	lc_shiftTime = 0;
	lc_shooter = -1;
}

// G_TempEntity: a shot's effect made while player 2 fires names player 2
void COOP_TagTempEntity( gentity_t *tent )
{
	if ( lc_shooter > 0 )
	{
		tent->s.otherEntityNum2 = lc_shooter;
	}
}

// the spread's random numbers (COOP_SeedShot): the host seeds them around player 2's shot with the
// view time the guest sent, the guest's cgame around its prediction of the same shot
static int			lc_seed;
static qboolean		lc_seeded;

void COOP_SeedShot( int seed )
{
	lc_seed = seed;
	lc_seeded = qtrue;
}

void COOP_UnseedShot( void )
{
	lc_seeded = qfalse;
}

float COOP_WeaponRandom( void )
{
	return lc_seeded ? Q_random( &lc_seed ) : random();
}

float COOP_WeaponCrandom( void )
{
	return lc_seeded ? Q_crandom( &lc_seed ) : crandom();
}

int COOP_WeaponIrand( int min, int max )
{
	if ( !lc_seeded )
	{
		return Q_irand( min, max );
	}
	int n = min + (int)( Q_random( &lc_seed ) * ( max - min + 1 ) );
	return n > max ? max : n;
}

/*
==============================================================================
coop_p2_target (tests): a moving target for player 2's shots
==============================================================================
*/

static struct
{
	int		num;			// 0: none
	vec3_t	base, side;
	float	amplitude;
	int		period, start;
} lc_target;

// coop_p2_target <n> [amplitude] [period ms]: entity n stands 192 units in front of player 2's view
// and sways across it (made unhurt and thoughtless meanwhile); "coop_p2_target off" lets it go
qboolean COOP_TestTargetCommand( void )
{
	gentity_t	*p2 = COOP_P2(), *ent;
	vec3_t		eye, forward, right, end;
	trace_t		tr;
	int			num;

	if ( !Q_stricmp( gi.argv( 1 ), "off" ) || gi.argc() < 2 )
	{
		if ( lc_target.num )
		{
			g_entities[lc_target.num].flags &= ~FL_GODMODE;
			g_entities[lc_target.num].nextthink = level.time + FRAMETIME;
		}
		lc_target.num = 0;
		return qtrue;
	}
	num = atoi( gi.argv( 1 ) );
	if ( !Q_stricmp( gi.argv( 1 ), "npc" ) && p2 )
	{
		// the nearest NPC
		float	best = 0, d;
		num = 0;
		for ( int i = 2; i < globals.num_entities; i++ )
		{
			if ( g_entities[i].inuse && g_entities[i].client && g_entities[i].NPC && g_entities[i].health > 0 )
			{
				d = DistanceSquared( g_entities[i].currentOrigin, p2->currentOrigin );
				if ( !num || d < best )
				{
					num = i;
					best = d;
				}
			}
		}
	}
	if ( !p2 || num <= 0 || num >= globals.num_entities || num == p2->s.number || !g_entities[num].inuse )
	{
		gi.Printf( "co-op: no entity %d to use as a target\n", num );
		return qtrue;
	}
	ent = &g_entities[num];
	VectorCopy( p2->client->ps.origin, eye );
	eye[2] += p2->client->ps.viewheight;
	AngleVectors( p2->client->ps.viewangles, forward, right, NULL );
	forward[2] = 0;
	VectorNormalize( forward );
	VectorMA( eye, 192, forward, end );
	// stand it on the floor there, its centre at player 2's eye height or below
	gi.trace( &tr, end, ent->mins, ent->maxs, end, num, MASK_SOLID );
	VectorCopy( end, lc_target.base );
	lc_target.base[2] -= ent->client ? ent->client->ps.viewheight : 0;
	VectorCopy( right, lc_target.side );
	lc_target.side[2] = 0;
	VectorNormalize( lc_target.side );
	lc_target.amplitude = gi.argc() > 2 ? atof( gi.argv( 2 ) ) : 48;
	lc_target.period = gi.argc() > 3 ? atoi( gi.argv( 3 ) ) : 1600;
	if ( lc_target.period < 100 )
	{
		lc_target.period = 100;
	}
	lc_target.start = level.time;
	lc_target.num = num;
	ent->flags |= FL_GODMODE;
	gi.Printf( "co-op: entity %d sways %.0f units across player 2's view at %s every %d ms%s\n", num,
		lc_target.amplitude, vtos( lc_target.base ), lc_target.period, tr.startsolid ? " (in a wall there?)" : "" );
	return qtrue;
}

// the end of every game frame: the target to this frame's place
void COOP_TestTargetFrame( void )
{
	gentity_t	*ent;
	vec3_t		org;
	float		s;

	if ( !lc_target.num )
	{
		return;
	}
	ent = &g_entities[lc_target.num];
	if ( !ent->inuse )
	{
		lc_target.num = 0;
		return;
	}
	s = sin( 2 * M_PI * ( level.time - lc_target.start ) / lc_target.period );
	VectorMA( lc_target.base, lc_target.amplitude * s, lc_target.side, org );
	ent->nextthink = level.time + 1000;		// no thinking (an NPC would walk off, or shoot back)
	if ( ent->client )
	{
		VectorCopy( org, ent->client->ps.origin );
		VectorClear( ent->client->ps.velocity );
	}
	G_SetOrigin( ent, org );
	VectorCopy( org, ent->s.pos.trBase );
	if ( ent->client )
	{
		ent->s.pos.trType = TR_INTERPOLATE;
	}
	gi.linkentity( ent );
}
