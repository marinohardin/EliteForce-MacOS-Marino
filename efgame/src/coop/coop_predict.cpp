// coop_predict.cpp -- single-player co-op: the guest shows player 2's shots at once.
//
// On the guest, player 2's movement and its weapon's own events are predicted (cg_predict.cpp
// replays the guest's commands), so the view weapon's muzzle flash and firing sound come when the
// guest presses fire. A shot's beam and impact, though, are temp entities the host's game makes when
// the command reaches it, a round trip later. For the instant-hit shots below, the guest's cgame
// traces the shot itself, against the world as its screen shows it (the host traces the same moment,
// coop_lagcomp.cpp), and draws the beam and the impact at once; the host's temp entities for that
// shot name player 2 (otherEntityNum2) and are left out, so nothing shows twice.
//
// The idea is cMod's weapon prediction by Noah Metzger (Chomenor) (cmod-game-code,
// cg_predict_weapons.c: the client draws its own shot effects and skips the server's for them);
// this is our own code for the single-player cgame, kept to the weapons whose shots are a trace
// without randomness.
//
// Predicted: the compression rifle (both fire modes) and the I-MOD (both). Already immediate
// without this: the muzzle flash and fire sound of every weapon, and the phaser's and the arc
// welder's continuous beams (drawn from player 2's predicted state). Not predicted (the host's
// effects come a round trip later): the tetrion disruptor's spread of bullets (random on the host),
// the stasis weapon's beam and every projectile (scavenger, stasis, grenade launcher, tetrion alt,
// quantum burst, arc welder alt), whose missiles are host entities.
//
// coop_predictshots 0 turns it off; coop_predict_debug 1 prints each predicted shot.

#include "../cgame/cg_local.h"
#include "../game/coop_public.h"

#define CP_RECENT		1000	// ms: the host's effects of player 2's shots are left out this long after one

static vmCvar_t		cp_enable, cp_debug;
static qboolean		cp_registered;
static int			cp_lastShot;		// cg.time of the last predicted shot (0: none)
static int			cp_lastWeapon;
static vec3_t		cp_muzzle;			// the view weapon's flash, as last drawn
static int			cp_muzzleTime;
static int			cp_shotTime;		// the drawn frame the newest shot was aimed in, for the host (0: told)

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

static qboolean CP_Predicted( int weapon )
{
	return weapon == WP_COMPRESSION_RIFLE || weapon == WP_IMOD;
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
COOP_PredictShot

CG_FireWeapon, for the guest's own player 2 (its predicted fire event): the shot's beam and impact,
as the host's game will make them.
*/
void COOP_PredictShot( centity_t *cent, qboolean alt )
{
	const playerState_t	*ps = &cg.predicted_player_state;
	vec3_t				eye, forward, right, up, start, end, muzzle, dir;
	trace_t				tr;
	int					weapon = cent->currentState.weapon;
	qboolean			impact, hitClient;

	if ( !COOP_IsReplica() || cent->currentState.number != COOP_ViewClientNum() || COOP_ViewClientNum() <= 0 ||
		cent->currentState.number != ps->clientNum )
	{
		return;
	}
	// every shot: the host traces an instant-hit one against the world as the frame drawn last
	// showed it (CG_Trace's entities are where that frame drew them)
	cp_shotTime = cg.oldTime ? cg.oldTime : cg.time;
	if ( !CP_Predicted( weapon ) || cg_thirdPerson.integer )
	{
		return;
	}
	CP_Cvars();
	if ( !cp_enable.integer )
	{
		return;
	}

	// the host's game fires from player 2's eyes, a little behind them (g_weapon.cpp)
	VectorCopy( ps->origin, eye );
	eye[2] += ps->viewheight;
	AngleVectors( ps->viewangles, forward, right, up );
	VectorMA( eye, -8, forward, start );
	VectorMA( start, weapon == WP_IMOD ? weaponData[WP_IMOD].range : 8192, forward, end );
	CG_Trace( &tr, start, NULL, NULL, end, ps->clientNum, MASK_SHOT );

	if ( cp_muzzleTime && cg.time - cp_muzzleTime < 200 )
	{
		VectorCopy( cp_muzzle, muzzle );
	}
	else
	{
		VectorMA( eye, 28, forward, muzzle );
		VectorMA( muzzle, 6, right, muzzle );
		VectorMA( muzzle, -16, up, muzzle );
	}
	impact = !( tr.surfaceFlags & SURF_NOIMPACT );
	hitClient = tr.entityNum < ENTITYNUM_WORLD && g_entities[tr.entityNum].inuse && g_entities[tr.entityNum].client &&
		g_entities[tr.entityNum].takedamage;
	ByteToDir( DirToByte( tr.plane.normal ), dir );		// as the host's event carries it

	if ( weapon == WP_COMPRESSION_RIFLE )
	{
		if ( alt )
			FX_CompressionAltShot( tr.endpos, muzzle );
		else
			FX_CompressionShot( tr.endpos, muzzle );
		if ( impact )
		{
			if ( hitClient )
				FX_CompressionHit( tr.endpos );
			else if ( alt )
				FX_CompressionAltMiss( tr.endpos, dir );
			else
				FX_CompressionExplosion( tr.endpos, dir );
		}
	}
	else
	{
		if ( alt )
			FX_AltIMODShot( tr.endpos, muzzle, dir );
		else
			FX_IMODShot( tr.endpos, muzzle, dir );
		if ( impact )
		{
			if ( alt )
				FX_AltIMODExplosion( tr.endpos, dir );
			else
				FX_IMODExplosion( tr.endpos, dir );
		}
	}
	cp_lastShot = cg.time;
	cp_lastWeapon = weapon;
	if ( cp_debug.integer )
	{
		CG_Printf( "co-op predict: player 2 shot at %d: hit %d%s (aimed at %d)\n", cg.time, tr.entityNum, hitClient ? " (a client)" : "", cp_shotTime );
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
	if ( !COOP_IsReplica() || COOP_ViewClientNum() <= 0 || es->otherEntityNum2 != COOP_ViewClientNum() || !cp_lastShot ||
		cg.time - cp_lastShot > CP_RECENT || cg.time < cp_lastShot - CP_RECENT )
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
		return cp_lastWeapon == WP_COMPRESSION_RIFLE;
	case EV_IMOD:
	case EV_IMOD_ALTFIRE:
	case EV_IMOD_MISS:
	case EV_IMOD_HIT:
	case EV_IMOD_ALTFIRE_HIT:
	case EV_IMOD_ALTFIRE_MISS:
		return cp_lastWeapon == WP_IMOD;
	default:
		return qfalse;
	}
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
