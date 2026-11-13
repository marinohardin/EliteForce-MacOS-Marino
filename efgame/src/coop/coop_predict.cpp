// coop_predict.cpp -- single-player co-op: the guest shows player 2's shots at once.
//
// On the guest, player 2's movement and its weapon's own events are predicted (cg_predict.cpp
// replays the guest's commands with the pmove code the host runs), so the fire event (muzzle flash,
// firing sound), the ammo a shot uses and when the weapon may fire again come when the guest presses
// fire. What a shot makes in the world, though, the host's game makes when the command reaches it, a
// round trip later. So the guest's cgame makes it itself when it predicts the shot, as the host's
// game will, and leaves out the host's copies:
// - instant-hit shots (compression rifle, I-MOD, proton gun, tetrion disruptor, the stasis weapon's
//   alt fire): traced against the world as the guest's screen shows it (the host traces the same
//   moment, coop_lagcomp.cpp), beam and impact drawn at once. The tetrion's spread is random: host and
//   guest draw it from a sequence seeded with the shot's view time (COOP_SeedShot), so both agree.
//   The host's temp entities of player 2's shots name it (otherEntityNum2) and are left out.
// - projectiles (scavenger rifle, stasis weapon, grenade launcher, tetrion alt, quantum burst): drawn
//   from the moment the guest fires. The host starts its own as if fired at the time the guest's
//   screen showed (coop_lagcomp.cpp), so when it arrives it is about where the drawn one is; the drawn
//   one hands over to it and the gap left (mostly where the muzzle is on each side) is eased out.
//   Impacts and explosions stay the host's.
// - the view weapon's firing animation: run from the guest's own shots instead of from player 2's
//   torso animation, which the host sends a round trip later.
// The phaser's and arc welder's continuous beams were already drawn from player 2's predicted state;
// the arc welder's alt burst, the tricorder and the hypos have nothing more to show.
//
// The idea is cMod's weapon prediction by Noah Metzger (Chomenor) (cmod-game-code,
// cg_predict_weapons.c: the client draws its own shots' effects, with the spread seeded so client
// and server agree, and skips the server's); this is our own code for the single-player cgame.
//
// coop_predictshots 0 turns it off; coop_predict_debug 1 prints each predicted shot and hand-over.

#include "../cgame/cg_local.h"
#include "../game/anims.h"
#include "../game/coop_public.h"

#define CP_RECENT		1000	// ms: the host's effects of player 2's shots are left out this long after one
#define CP_MISSILES		16		// projectiles drawn at once before the host's arrive
#define CP_MISSILE_LIFE	1000	// ms: a drawn one the host's never took over from is dropped then
#define CP_EASE			150		// ms: the host's projectile is eased onto the drawn one's path over this

static vmCvar_t		cp_enable, cp_debug;
static qboolean		cp_registered;
static int			cp_lastShot;		// cg.time of the last predicted shot (0: none)
static int			cp_lastAlt;
static int			cp_recent[WP_NUM_WEAPONS];	// cg.time of each weapon's last predicted instant-hit shot
static vec3_t		cp_muzzle;			// the view weapon's flash, as last drawn
static int			cp_muzzleTime;
static int			cp_shotTime;		// the drawn frame the newest shot was aimed in, for the host (0: told)

// the view weapon's firing animation
static int			cp_animNum;			// the torso animation the newest shot plays (0: none)
static int			cp_animStart;

static void CP_Cvars( void )
{
	if ( !cp_registered )
	{
		cp_registered = qtrue;
		cgi_Cvar_Register( &cp_enable, "coop_predictshots", "1", 0 );
		cgi_Cvar_Register( &cp_debug, "coop_predict_debug", "0", 0 );
	}
	cgi_Cvar_Update( &cp_enable );
	cgi_Cvar_Update( &cp_debug );
}

// CG_AddViewWeapon: where the guest's view weapon's flash is
void COOP_NoteViewMuzzle( const vec3_t origin )
{
	if ( COOP_IsReplica() )
	{
		VectorCopy( origin, cp_muzzle );
		cp_muzzleTime = cg.time;
	}
}

/*
==============================================================================
Projectiles
==============================================================================
*/

typedef struct
{
	qboolean	active;
	int			created;			// cg.time
	vec3_t		mins, maxs;
	vec3_t		dir;				// of travel at the start, to pair it with the host's
	centity_t	cent;				// what CG_Missile and the weapon's trail draw from
	gentity_t	gent;
} cpMissile_t;

static cpMissile_t	cp_missiles[CP_MISSILES];

// the host's projectiles of player 2, as this cgame has seen them
static int			cp_frame;
static int			cp_seenFrame[MAX_GENTITIES];	// cp_frame it was last in a snapshot
static int			cp_firstSeen[MAX_GENTITIES];	// cg.time it came into them
static int			cp_taken[MAX_GENTITIES];		// cp_firstSeen when a drawn one handed over to it
static vec3_t		cp_easeOffset[MAX_GENTITIES];	// the drawn one's place minus its own, then
static int			cp_easeStart[MAX_GENTITIES];

/*
CP_Missile

A projectile of player 2 as the host's game will launch it (g_weapon.cpp), drawn from the view
weapon's flash. Its flight starts at the shot's view time, as the host's will.
*/
static void CP_Missile( int weapon, qboolean alt, const vec3_t start, const vec3_t dir, float speed, float up,
	trType_t trType, float size, int count, int eFlags, qboolean formation )
{
	cpMissile_t	*m = NULL;
	int			oldest = 0;

	for ( int i = 0; i < CP_MISSILES; i++ )
	{
		if ( !cp_missiles[i].active )
		{
			m = &cp_missiles[i];
			break;
		}
		if ( cp_missiles[i].created < cp_missiles[oldest].created )
		{
			oldest = i;
		}
	}
	if ( !m )
	{
		m = &cp_missiles[oldest];
	}
	memset( m, 0, sizeof( *m ) );
	m->active = qtrue;
	m->created = cg.time;
	VectorSet( m->maxs, size, size, size );
	VectorScale( m->maxs, -1, m->mins );
	VectorCopy( dir, m->dir );

	gentity_t *g = &m->gent;
	g->inuse = qtrue;
	g->alt_fire = alt;
	g->count = count;
	g->trigger_formation = formation;
	g->s.number = COOP_ViewClientNum();
	g->s.eType = ET_MISSILE;
	g->s.weapon = weapon;
	g->s.eFlags = eFlags;
	g->s.pos.trType = trType;
	g->s.pos.trTime = cp_shotTime;
	VectorCopy( start, g->s.pos.trBase );
	VectorScale( dir, speed, g->s.pos.trDelta );
	g->s.pos.trDelta[2] += up;
	SnapVector( g->s.pos.trDelta );
	VectorCopy( start, g->currentOrigin );
	VectorCopy( start, g->pos1 );
	VectorCopy( start, g->pos2 );

	m->cent.gent = g;
	m->cent.currentState = g->s;
	VectorCopy( start, m->cent.lerpOrigin );
}

// a host entity is one of player 2's projectiles
static qboolean CP_HostMissile( int num )
{
	const gentity_t *g = &g_entities[num];

	return g->inuse && g->s.eType == ET_MISSILE && g->owner && g->owner->s.number == COOP_ViewClientNum();
}

// the host's projectile a drawn one hands over to: one of player 2's, of the same weapon and fire
// mode, that came in after it was drawn and that no other took; the closest start time first, then
// the closest direction (the stasis weapon fires three at once)
static int CP_FindHostMissile( const cpMissile_t *m )
{
	int		best = -1, bestDt = 0;
	float	bestDot = 0;

	for ( int i = 0; i < cg.snap->numEntities; i++ )
	{
		int				num = cg.snap->entities[i].number;
		const gentity_t	*g = &g_entities[num];
		vec3_t			dir;

		if ( !CP_HostMissile( num ) || g->s.weapon != m->gent.s.weapon || !g->alt_fire != !m->gent.alt_fire ||
			cp_firstSeen[num] < m->created || cp_taken[num] == cp_firstSeen[num] )
		{
			continue;
		}
		VectorNormalize2( g->s.pos.trDelta, dir );
		float	dot = DotProduct( dir, m->dir );
		int		dt = abs( g->s.pos.trTime - m->gent.s.pos.trTime );
		if ( dot < 0.7f )
		{
			continue;
		}
		if ( best < 0 || dt < bestDt - 20 || ( dt <= bestDt + 20 && dot > bestDot ) )
		{
			best = num;
			bestDt = dt;
			bestDot = dot;
		}
	}
	return best;
}

extern void CG_CoopMissile( centity_t *cent );

/*
COOP_AddPredictedMissiles

CG_AddPacketEntities on the guest, before the host's entities: drawn projectiles hand over to the
host's that arrived, the others move on and are drawn (one that hits something is dropped: the
host's game makes the impact).
*/
void COOP_AddPredictedMissiles( void )
{
	cpMissile_t	*order[CP_MISSILES];
	int			n = 0;

	if ( !COOP_IsReplica() || COOP_ViewClientNum() <= 0 || !cg.snap )
	{
		return;
	}
	CP_Cvars();
	cp_frame++;
	for ( int i = 0; i < cg.snap->numEntities; i++ )
	{
		int num = cg.snap->entities[i].number;

		if ( !CP_HostMissile( num ) )
		{
			continue;
		}
		if ( cp_seenFrame[num] != cp_frame - 1 )
		{
			cp_firstSeen[num] = cg.time;
			cp_taken[num] = 0;
		}
		cp_seenFrame[num] = cp_frame;
	}

	// the view weapon's flash model follows the newest predicted shot's fire mode
	if ( cp_lastShot && cg.time >= cp_lastShot && cg.time - cp_lastShot < CP_RECENT )
	{
		g_entities[COOP_ViewClientNum()].alt_fire = (qboolean)cp_lastAlt;
	}

	// oldest first, so each takes the oldest of the host's
	for ( int i = 0; i < CP_MISSILES; i++ )
	{
		if ( cp_missiles[i].active )
		{
			int j = n++;
			while ( j > 0 && order[j - 1]->created > cp_missiles[i].created )
			{
				order[j] = order[j - 1];
				j--;
			}
			order[j] = &cp_missiles[i];
		}
	}
	for ( int i = 0; i < n; i++ )
	{
		cpMissile_t	*m = order[i];
		vec3_t		pos, hostPos;
		trace_t		tr;
		int			num;

		if ( cg.time < m->created || cg.time - m->created > CP_MISSILE_LIFE )
		{
			m->active = qfalse;
			continue;
		}
		EvaluateTrajectory( &m->gent.s.pos, cg.time, pos );
		num = CP_FindHostMissile( m );
		if ( num >= 0 )
		{
			EvaluateTrajectory( &g_entities[num].s.pos, cg.time, hostPos );
			VectorSubtract( pos, hostPos, cp_easeOffset[num] );
			cp_easeStart[num] = cg.time;
			cp_taken[num] = cp_firstSeen[num];
			m->active = qfalse;
			if ( cp_debug.integer )
			{
				CG_Printf( "co-op predict: projectile (weapon %d) handed to host entity %d after %d ms, %.1f units apart\n",
					m->gent.s.weapon, num, cg.time - m->created, VectorLength( cp_easeOffset[num] ) );
			}
			continue;
		}
		CG_Trace( &tr, m->gent.currentOrigin, m->mins, m->maxs, pos, COOP_ViewClientNum(), MASK_SHOT );
		if ( tr.fraction < 1.0f || tr.startsolid )
		{
			m->active = qfalse;
			if ( cp_debug.integer )
			{
				CG_Printf( "co-op predict: projectile (weapon %d) hit something after %d ms, before the host's came\n",
					m->gent.s.weapon, cg.time - m->created );
			}
			continue;
		}
		VectorCopy( pos, m->gent.currentOrigin );
		VectorCopy( pos, m->cent.lerpOrigin );
		m->cent.currentState = m->gent.s;
		CG_CoopMissile( &m->cent );
	}
}

// CG_Missile: a host projectile a drawn one handed over to starts where that one was
void COOP_MissileLerp( centity_t *cent )
{
	if ( !COOP_IsReplica() || cent < cg_entities || cent >= cg_entities + MAX_GENTITIES )
	{
		return;
	}
	int num = cent - cg_entities;
	int age = cg.time - cp_easeStart[num];

	if ( !cp_taken[num] || cp_taken[num] != cp_firstSeen[num] || age < 0 || age >= CP_EASE )
	{
		return;
	}
	VectorMA( cent->lerpOrigin, 1.0f - (float)age / CP_EASE, cp_easeOffset[num], cent->lerpOrigin );
}

/*
==============================================================================
The view weapon's firing animation
==============================================================================
*/

extern qboolean ValidAnimFileIndex( int index );

// the torso animation player 2's pmove starts for a shot (PM_Weapon), and whether it starts it again
// while it is still playing; 0: one the view weapon doesn't show
static int CP_AttackAnim( int weapon, qboolean *restart )
{
	*restart = qtrue;
	switch ( weapon )
	{
	case WP_PHASER:
		return BOTH_ATTACK1;
	case WP_COMPRESSION_RIFLE:
	case WP_DESPERADO:
		return BOTH_ATTACK2;
	case WP_TETRION_DISRUPTOR:
		*restart = qfalse;
		return BOTH_ATTACK1;
	case WP_SCAVENGER_RIFLE:
	case WP_CHAOTICA_GUARD_GUN:
		*restart = qfalse;
		return BOTH_ATTACK2;
	case WP_TRICORDER:
	case WP_BLUE_HYPO:
	case WP_RED_HYPO:
	case WP_VOYAGER_HYPO:
	case WP_KLINGON_BLADE:
	case WP_IMPERIAL_BLADE:
	case WP_MELEE:
	case WP_BOT_WELDER:
		return 0;
	default:
		return BOTH_ATTACK3;
	}
}

static const animation_t *CP_Animation( int anim )
{
	const gentity_t *p2 = &g_entities[COOP_ViewClientNum()];

	if ( !anim || !p2->client || !ValidAnimFileIndex( p2->client->clientInfo.animFileIndex ) )
	{
		return NULL;
	}
	return &knownAnimFileSets[p2->client->clientInfo.animFileIndex].animations[anim];
}

static void CP_StartAttackAnim( int weapon )
{
	qboolean			restart;
	int					anim = CP_AttackAnim( weapon, &restart );
	const animation_t	*a = CP_Animation( anim );

	if ( !restart && a && anim == cp_animNum && cg.time >= cp_animStart &&
		cg.time - cp_animStart < a->numFrames * a->frameLerp )
	{
		return;		// still playing (PM_SetAnim without SETANIM_FLAG_RESTART)
	}
	cp_animNum = anim;
	cp_animStart = cg.time;
}

/*
COOP_ViewWeaponFrames

CG_AddViewWeapon on the guest, after it mapped player 2's torso animation to the view weapon's frames:
the firing frames come from the guest's own shots (CG_MapTorsoToWeaponFrame's mapping: the attack
animation's first six frames are weapon frames 1-6), the host's late copy of them is left out.
*/
void COOP_ViewWeaponFrames( clientInfo_t *ci, centity_t *cent, int *frame, int *oldframe, float *backlerp )
{
	if ( !COOP_IsReplica() || COOP_ViewClientNum() <= 0 || cent->currentState.number != COOP_ViewClientNum() )
	{
		return;
	}
	CP_Cvars();
	if ( !cp_enable.integer )
	{
		return;
	}
	int hostAnim = cent->pe.torso.animationNumber & ~ANIM_TOGGLEBIT;

	// the phaser's held frame: player 2's predicted state is firing
	if ( cent->currentState.weapon == WP_PHASER && ( cent->currentState.eFlags & EF_FIRING ) )
	{
		*frame = *oldframe = 2;		// (PHASER_HOLDFRAME)
		*backlerp = 0;
		return;
	}
	if ( hostAnim == BOTH_ATTACK1 || hostAnim == BOTH_ATTACK2 || hostAnim == BOTH_ATTACK3 )
	{
		*frame = *oldframe = 0;
		*backlerp = 0;
	}
	if ( !cp_animNum || !ValidAnimFileIndex( ci->animFileIndex ) )
	{
		return;
	}
	const animation_t	*a = &knownAnimFileSets[ci->animFileIndex].animations[cp_animNum];
	int					t = cg.time - cp_animStart;
	int					frames = a->numFrames < 6 ? a->numFrames : 6;

	if ( a->frameLerp <= 0 || t < 0 || t >= frames * a->frameLerp )
	{
		return;
	}
	int f = t / a->frameLerp;
	*oldframe = 1 + f;
	*frame = f + 1 < frames ? 2 + f : 0;
	*backlerp = 1.0f - (float)( t - f * a->frameLerp ) / a->frameLerp;
}

/*
==============================================================================
The shot
==============================================================================
*/

// where the host's game fires player 2's weapon from when it has no muzzle point of the model's
// (CalcMuzzlePoint, g_weapon.cpp); the host mostly has one, a few units off this
static void CP_HostMuzzle( const playerState_t *ps, int weapon, const vec3_t forward, const vec3_t right, vec3_t muzzle )
{
	VectorCopy( ps->origin, muzzle );
	muzzle[2] += ps->viewheight;
	switch ( weapon )
	{
	case WP_IMOD:
		muzzle[2] -= 13;
		VectorMA( muzzle, 26, forward, muzzle );
		VectorMA( muzzle, 6, right, muzzle );
		break;
	case WP_COMPRESSION_RIFLE:
	case WP_TETRION_DISRUPTOR:
	case WP_PROTON_GUN:
		muzzle[2] -= 16;
		VectorMA( muzzle, 28, forward, muzzle );
		VectorMA( muzzle, 6, right, muzzle );
		break;
	case WP_STASIS:
	case WP_QUANTUM_BURST:
	case WP_GRENADE_LAUNCHER:
		muzzle[2] -= 2;
		break;
	case WP_SCAVENGER_RIFLE:
		muzzle[2] -= 1;
		VectorMA( muzzle, 12, forward, muzzle );
		VectorMA( muzzle, 1, right, muzzle );
		break;
	default:
		break;
	}
	if ( ps->leanofs )
	{
		VectorMA( muzzle, (float)ps->leanofs, right, muzzle );
	}
}

static qboolean CP_HitClient( const trace_t *tr )
{
	const gentity_t *g = &g_entities[tr->entityNum];

	return tr->entityNum < ENTITYNUM_WORLD && g->inuse && g->client && g->takedamage;
}

// the beam weapons: the shot as WP_FireCompressionRifle / WP_FireIMOD / WP_FireProtonGun traces it,
// from player 2's eyes, and what their temp entities' events draw
static void CP_BeamShot( const playerState_t *ps, int weapon, qboolean alt, const vec3_t eye, const vec3_t forward,
	const vec3_t muzzle, trace_t *tr )
{
	vec3_t		start, end, dir, endpos, from;
	qboolean	impact, hitClient;

	VectorMA( eye, -8, forward, start );
	VectorMA( start, weapon == WP_IMOD ? weaponData[WP_IMOD].range : 8192, forward, end );
	CG_Trace( tr, start, NULL, NULL, end, ps->clientNum, MASK_SHOT );

	impact = !( tr->surfaceFlags & SURF_NOIMPACT );
	hitClient = CP_HitClient( tr );
	ByteToDir( DirToByte( tr->plane.normal ), dir );		// as the host's event carries it
	VectorCopy( tr->endpos, endpos );
	VectorCopy( muzzle, from );

	if ( weapon == WP_COMPRESSION_RIFLE )
	{
		if ( alt )
			FX_CompressionAltShot( endpos, from );
		else
			FX_CompressionShot( endpos, from );
		if ( impact )
		{
			if ( hitClient )
				FX_CompressionHit( endpos );
			else if ( alt )
				FX_CompressionAltMiss( endpos, dir );
			else
				FX_CompressionExplosion( endpos, dir );
		}
	}
	else if ( weapon == WP_IMOD )
	{
		if ( alt )
			FX_AltIMODShot( endpos, from, dir );
		else
			FX_IMODShot( endpos, from, dir );
		if ( impact )
		{
			if ( alt )
				FX_AltIMODExplosion( endpos, dir );
			else
				FX_IMODExplosion( endpos, dir );
		}
	}
	else	// the proton gun has no alt fire (FireWeapon)
	{
		FX_ProtonShot( endpos, from );
		if ( impact )
		{
			if ( hitClient )
				FX_ProtonHit( endpos );
			else
				FX_ProtonExplosion( endpos, dir );
		}
	}
}

// the tetrion's main fire: three traces with a random spread from the muzzle (FireTetrionBullet),
// the spread from the seeded sequence the host's game uses for this shot too
static void CP_TetrionShot( const playerState_t *ps, const vec3_t eye, const vec3_t forward, const vec3_t right,
	const vec3_t up, const vec3_t muzzle, trace_t *last )
{
	vec3_t		start, end, back, dir, from;
	trace_t		tr;
	qboolean	impact = qtrue;

	CP_HostMuzzle( ps, WP_TETRION_DISRUPTOR, forward, right, start );
	CG_Trace( &tr, eye, vec3_origin, vec3_origin, start, ps->clientNum, MASK_SOLID | CONTENTS_SHOTCLIP );
	if ( !tr.startsolid && !tr.allsolid && tr.fraction < 1.0f )
	{
		VectorCopy( tr.endpos, start );		// (W_TraceSetStart: not through a wall)
	}
	VectorScale( forward, -1, back );
	VectorCopy( muzzle, from );
	for ( int i = 0; i < 3; i++ )
	{
		float r = COOP_WeaponCrandom() * 275;
		float u = COOP_WeaponCrandom() * 275;

		VectorMA( start, 8192, forward, end );
		VectorMA( end, r, right, end );
		VectorMA( end, u, up, end );
		CG_Trace( &tr, start, NULL, NULL, end, ps->clientNum, MASK_SHOT );
		if ( tr.surfaceFlags & SURF_NOIMPACT )
		{
			impact = qfalse;		// (for the rest of the shot too, as on the host)
		}
		if ( COOP_WeaponRandom() > 0.1 )
		{
			FX_TetrionShot( from, tr.endpos, back );
		}
		if ( impact )
		{
			ByteToDir( DirToByte( tr.plane.normal ), dir );
			FX_TetrionWeaponHitWall( tr.endpos, dir );
		}
		*last = tr;
	}
}

// the stasis weapon's alt fire: one beam from the muzzle (WP_FireStasisAlt); the small side beams are
// the effect's own
static void CP_StasisBeam( centity_t *cent, const playerState_t *ps, const vec3_t forward, const vec3_t right,
	const vec3_t muzzle, trace_t *tr )
{
	vec3_t start, end, dir, from;

	CP_HostMuzzle( ps, WP_STASIS, forward, right, start );
	VectorMA( start, 8192, forward, end );
	CG_Trace( tr, start, NULL, NULL, end, ps->clientNum, MASK_SHOT );
	VectorCopy( muzzle, from );
	FX_StasisShot( cent, tr->endpos, from );
	if ( !( tr->surfaceFlags & SURF_NOIMPACT ) )
	{
		ByteToDir( DirToByte( tr->plane.normal ), dir );
		if ( CP_HitClient( tr ) )
			FX_StasisShotImpact( tr->endpos, dir );
		else
			FX_StasisShotMiss( tr->endpos, dir );
	}
}

// the projectile weapons, as WP_FireScavenger / WP_FireStasisMain / WP_FireGrenade /
// FireTetrionProjectile / WP_FireQuantumBurst launch them for a player
static void CP_Projectiles( int weapon, qboolean alt, const vec3_t forward, const vec3_t muzzle )
{
	vec3_t angles, temp, dir;

	switch ( weapon )
	{
	case WP_SCAVENGER_RIFLE:
		if ( alt )
		{
			CP_Missile( weapon, qtrue, muzzle, forward, COOP_WeaponRandom() * 100 + 900, 140, TR_GRAVITY, 1, 0, 0, qfalse );
		}
		else
		{
			vectoangles( forward, angles );
			VectorSet( temp, angles[0] + COOP_WeaponCrandom() * 0.5f, angles[1] + COOP_WeaponCrandom() * 0.5f, angles[2] );
			AngleVectors( temp, dir, NULL, NULL );
			COOP_WeaponIrand( 0, 1 );		// (which barrel: the drawn one comes from the view weapon's flash)
			CP_Missile( weapon, qfalse, muzzle, dir, 1500, 0, TR_LINEAR, 0, 0, 0, qfalse );
		}
		break;
	case WP_STASIS:		// main fire: one big, two small to the sides
		vectoangles( forward, angles );
		CP_Missile( weapon, qfalse, muzzle, forward, 1000 + 50 * 4, 0, TR_LINEAR, 2 * 4, 4, 0, qfalse );
		for ( int side = -1; side <= 1; side += 2 )
		{
			VectorCopy( angles, temp );
			temp[YAW] += side * 5.0f;
			AngleVectors( temp, dir, NULL, NULL );
			CP_Missile( weapon, qfalse, muzzle, dir, 1000 + 50 * 2, 0, TR_LINEAR, 2 * 2, 2, 0, qfalse );
		}
		break;
	case WP_GRENADE_LAUNCHER:
		if ( alt )
			CP_Missile( weapon, qtrue, muzzle, forward, 1600, 0, TR_GRAVITY, 0, 0, EF_MISSILE_STICK, qtrue );
		else
			CP_Missile( weapon, qfalse, muzzle, forward, 900, 120, TR_GRAVITY, 2, 0, EF_BOUNCE_HALF, qfalse );
		break;
	case WP_TETRION_DISRUPTOR:	// alt fire
		CP_Missile( weapon, qtrue, muzzle, forward, 1500, 0, TR_LINEAR, 2, 0, EF_BOUNCE, qfalse );
		break;
	case WP_QUANTUM_BURST:
		CP_Missile( weapon, alt, muzzle, forward, alt ? 500 : 900, 0, TR_LINEAR, 3, 0, 0, qfalse );
		break;
	default:
		break;
	}
}

/*
COOP_PredictShot

CG_FireWeapon, for the guest's own player 2 (its predicted fire event): what the host's game will
make of the shot, drawn now.
*/
void COOP_PredictShot( centity_t *cent, qboolean alt )
{
	const playerState_t	*ps = &cg.predicted_player_state;
	vec3_t				eye, forward, right, up, muzzle;
	trace_t				tr;
	int					weapon = cent->currentState.weapon;
	qboolean			hitscan = qfalse, missile = qfalse;

	if ( !COOP_IsReplica() || cent->currentState.number != COOP_ViewClientNum() || COOP_ViewClientNum() <= 0 ||
		cent->currentState.number != ps->clientNum )
	{
		return;
	}
	// every shot: the host traces an instant-hit one against the world as the frame drawn last
	// showed it (CG_Trace's entities are where that frame drew them), and seeds its spread with it
	cp_shotTime = cg.oldTime ? cg.oldTime : cg.time;
	CP_Cvars();
	if ( !cp_enable.integer )
	{
		return;
	}
	CP_StartAttackAnim( weapon );
	cp_lastShot = cg.time;
	cp_lastAlt = alt;
	if ( cg_thirdPerson.integer )
	{
		return;		// (the view weapon's flash, which the drawn effects start at, isn't there)
	}

	// the host's game fires from player 2's eyes (coop_lagcomp.cpp puts them where they are after
	// this command's move, as here)
	VectorCopy( ps->origin, eye );
	eye[2] += ps->viewheight;
	AngleVectors( ps->viewangles, forward, right, up );
	if ( cp_muzzleTime && cg.time - cp_muzzleTime < 200 && cg.time >= cp_muzzleTime )
	{
		VectorCopy( cp_muzzle, muzzle );
	}
	else
	{
		VectorMA( eye, 28, forward, muzzle );
		VectorMA( muzzle, 6, right, muzzle );
		VectorMA( muzzle, -16, up, muzzle );
	}

	COOP_SeedShot( cp_shotTime );
	switch ( weapon )
	{
	case WP_COMPRESSION_RIFLE:
	case WP_IMOD:
		CP_BeamShot( ps, weapon, alt, eye, forward, muzzle, &tr );
		hitscan = qtrue;
		break;
	case WP_PROTON_GUN:
		CP_BeamShot( ps, weapon, qfalse, eye, forward, muzzle, &tr );
		hitscan = qtrue;
		break;
	case WP_TETRION_DISRUPTOR:
		if ( alt )
		{
			CP_Projectiles( weapon, alt, forward, muzzle );
			missile = qtrue;
		}
		else
		{
			CP_TetrionShot( ps, eye, forward, right, up, muzzle, &tr );
			hitscan = qtrue;
		}
		break;
	case WP_STASIS:
		if ( alt )
		{
			CP_StasisBeam( cent, ps, forward, right, muzzle, &tr );
			hitscan = qtrue;
		}
		else
		{
			CP_Projectiles( weapon, alt, forward, muzzle );
			missile = qtrue;
		}
		break;
	case WP_SCAVENGER_RIFLE:
	case WP_GRENADE_LAUNCHER:
	case WP_QUANTUM_BURST:
		CP_Projectiles( weapon, alt, forward, muzzle );
		missile = qtrue;
		break;
	default:
		break;
	}
	COOP_UnseedShot();

	if ( hitscan )
	{
		cp_recent[weapon] = cg.time;
	}
	if ( cp_debug.integer && ( hitscan || missile ) )
	{
		CG_Printf( "co-op predict: player 2 %s weapon %d%s at %d (aimed at %d), ammo %d (host's last %d)%s\n", hitscan ? "shot" : "launched",
			weapon, alt ? " alt" : "", cg.time, cp_shotTime, ps->ammo[weaponData[weapon].ammoIndex],
			cg.snap ? cg.snap->ps.ammo[weaponData[weapon].ammoIndex] : -1,
			hitscan ? va( ": hit %d%s", tr.entityNum, CP_HitClient( &tr ) ? " (a client)" : "" ) : "" );
	}
}

// the bridge, making the guest's command: the time player 2's newest shot was aimed in
int COOP_TakeShotTime( void )
{
	int t = cp_shotTime;

	cp_shotTime = 0;
	return t;
}

/*
COOP_SkipHostShot

CG_EntityEvent on the guest: a host effect of a shot player 2 fired, which this cgame showed itself
when the guest fired, is left out.
*/
qboolean COOP_SkipHostShot( const entityState_t *es, int event )
{
	int weapon;

	if ( !COOP_IsReplica() || COOP_ViewClientNum() <= 0 || es->otherEntityNum2 != COOP_ViewClientNum() )
	{
		return qfalse;
	}
	switch ( event )
	{
	case EV_COMPRESSION_RIFLE:
	case EV_COMPRESSION_RIFLE_ALT:
	case EV_COMPRESSION_RIFLE_MISS:
	case EV_COMPRESSION_RIFLE_HIT:
	case EV_COMPRESSION_RIFLE_ALT_MISS:
		weapon = WP_COMPRESSION_RIFLE;
		break;
	case EV_IMOD:
	case EV_IMOD_ALTFIRE:
	case EV_IMOD_MISS:
	case EV_IMOD_HIT:
	case EV_IMOD_ALTFIRE_HIT:
	case EV_IMOD_ALTFIRE_MISS:
		weapon = WP_IMOD;
		break;
	case EV_PROTON_GUN:
	case EV_PROTON_GUN_ALT:
	case EV_PROTON_GUN_MISS:
	case EV_PROTON_GUN_HIT:
	case EV_PROTON_GUN_ALT_MISS:
		weapon = WP_PROTON_GUN;
		break;
	case EV_TETRION:
	case EV_TETRION_HIT:
	case EV_TETRION_MISS:
		weapon = WP_TETRION_DISRUPTOR;
		break;
	case EV_STASIS:
	case EV_STASIS_HIT:
	case EV_STASIS_MISS:
		weapon = WP_STASIS;
		break;
	default:
		return qfalse;
	}
	if ( !cp_recent[weapon] || cg.time < cp_recent[weapon] - CP_RECENT || cg.time - cp_recent[weapon] > CP_RECENT )
	{
		return qfalse;
	}
	if ( cp_debug.integer )
	{
		CG_Printf( "co-op predict: left out the host's event %d (weapon %d)\n", event, weapon );
	}
	return qtrue;
}

/*
COOP_PredictingEvents / COOP_SkipPlayerstateEvent

A replica runs player 2's player events twice over: its prediction (cg_predict.cpp) makes the ones
its moves make (footsteps, jumps, falls, water, weapon change and fire) when the guest makes the move,
and the host's player state brings them all again a round trip later, along with the ones only the
host's game makes (pain and the like). Each kind is taken from one place only: the moves' events from
the prediction, the rest from the host.
*/
static qboolean		cp_predicting;

void COOP_PredictingEvents( qboolean predicting )
{
	cp_predicting = predicting;
}

static qboolean CP_MoveEvent( int event )
{
	switch ( event )
	{
	case EV_FOOTSTEP:
	case EV_FOOTSTEP_METAL:
	case EV_FOOTSPLASH:
	case EV_FOOTWADE:
	case EV_SWIM:
	case EV_FALL_SHORT:
	case EV_FALL_MEDIUM:
	case EV_FALL_FAR:
	case EV_JUMP:
	case EV_WATER_TOUCH:
	case EV_WATER_LEAVE:
	case EV_WATER_UNDER:
	case EV_WATER_CLEAR:
	case EV_NOAMMO:
	case EV_CHANGE_WEAPON:
	case EV_FIRE_WEAPON:
	case EV_ALT_FIRE:
		return qtrue;
	default:
		return qfalse;
	}
}

qboolean COOP_SkipPlayerstateEvent( int event )
{
	if ( !COOP_IsReplica() || COOP_ViewClientNum() <= 0 )
	{
		return qfalse;
	}
	return CP_MoveEvent( event ) ? !cp_predicting : cp_predicting;
}

// the host's game also copies player 2's newest player event onto its entity (g_active.cpp); the
// moves' events among them were already predicted
qboolean COOP_SkipEntityEvent( int num, int event )
{
	return COOP_IsReplica() && COOP_ViewClientNum() > 0 && num == COOP_ViewClientNum() && CP_MoveEvent( event );
}
