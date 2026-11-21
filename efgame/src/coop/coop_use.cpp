// coop_use.cpp -- single-player co-op: what the Use key does besides using things.
//
// Two things, both only when Use has nothing to use (nothing usable in the crosshair, no use-button
// trigger the player stands in, no downed partner to revive within reach):
//
// - Hold Use (coop_teleporthold seconds, default 5) as player 2 with a guest steering it: player 2
//   is put next to player 1, on the spot the catch-up teleport uses (COOP_FindSpotNear). The guest
//   sees a fill bar for the whole hold. Never while a script holds the players, a camera runs,
//   player 1 is down or dead, or player 2 is down; short cooldown after each.
// - Double-tap Use (two presses within COOP_TAP_WINDOW ms), either player: a "look here" ping at
//   the aimed spot, sent to both players' cgames as "cooping" (coop_hud.cpp draws it).
//
// Revive always wins over the teleport (coop_down.cpp): while either player is down the teleport
// hold doesn't run at all, a hold that is going when someone goes down (or a revive becomes possible)
// is cancelled for good, and a new hold needs Use released and pressed again. A press that starts a
// revive doesn't count toward a double tap.
//
// The game sees each player's buttons once a game frame (ClientThink_real). The guest's commands
// carry every press made since its last one, but a late packet can leave one host frame without a
// command (no buttons): player 2's hold survives a single empty frame.

#include "g_local.h"
#include "g_functions.h"
#include "coop_public.h"
#include "coop_local.h"

extern bool		in_camera;
extern qboolean	player_locked;
extern qboolean	ValidUseTarget( gentity_t *ent );

#define COOP_USE_REACH		64.0f	// as far as Use reaches (TryUse)
#define COOP_TAP_WINDOW		350		// ms from the first press to the second for a double tap
#define COOP_HOLD_SHOW		400		// ms of holding before the bar shows (a double tap is not a hold)
#define COOP_TELE_COOLDOWN	3000	// ms after a teleport before the next can start
#define COOP_PING_REPEAT	1000	// ms between one player's pings
#define COOP_PING_RANGE		8192
#define COOP_USE_MSG_TIME	250		// ms between the bar's updates (the cgame fills it in between)

typedef struct
{
	qboolean	held;			// Use was down in the last command
	int			gap;			// commands without Use since it was last down
	int			pressTime;		// level.time of this press
	qboolean	pressUsable;	// this press had something to use (or revive): not a tap
	int			lastTap;		// press time of the last tap with nothing to use, 0: none
	qboolean	noHold;			// this press can't become a teleport hold (pinged, blocked, revived)
	int			holdStart;		// level.time the teleport hold started, 0: not holding
	qboolean	msgShown;		// the bar (or a teleport line) is on the guest's screen
	int			nextMsg;
	int			cooldownUntil;
	int			nextPing;
} coopUse_t;

static coopUse_t	coop_use[2];
static cvar_t		*coop_teleporthold;
static cvar_t		*coop_use_debug;		// (tests) print what each press finds

void COOP_UseInit( void )
{
	memset( coop_use, 0, sizeof( coop_use ) );
	coop_teleporthold = gi.cvar( "coop_teleporthold", "5", 0 );
	coop_use_debug = gi.cvar( "coop_use_debug", "0", 0 );
}

static const char *COOP_UseName( int idx )
{
	if ( idx == 1 )
	{
		const char *g = COOP_GuestConnected() ? COOP_GuestName() : "";
		return g[0] ? g : "Player 2";
	}
	return COOP_HostName();
}

/*
COOP_HasUseTarget

Use would do something for this player now: the thing Use reaches in the crosshair (as TryUse looks
for it), a trigger that fires on Use that the player stands in, or a downed partner to revive.
*/
static const char *coop_useWhat;	// what COOP_HasUseTarget found (coop_use_debug)

static qboolean COOP_HasUseTarget( gentity_t *ent )
{
	coop_useWhat = "nothing";
	trace_t		tr;
	vec3_t		src, dest, fwd;
	gentity_t	*list[64];
	int			n;

	if ( COOP_CanReviveNow( ent ) )
	{
		coop_useWhat = "a downed partner";
		return qtrue;
	}
	VectorCopy( ent->client->renderInfo.eyePoint, src );
	AngleVectors( ent->client->renderInfo.eyeAngles, fwd, NULL, NULL );
	VectorMA( src, COOP_USE_REACH, fwd, dest );
	gi.trace( &tr, src, vec3_origin, vec3_origin, dest, ent->s.number, MASK_OPAQUE | CONTENTS_BODY | CONTENTS_ITEM | CONTENTS_CORPSE );
	if ( tr.fraction < 1.0f && tr.entityNum >= 1 && tr.entityNum < ENTITYNUM_WORLD )
	{
		gentity_t *t = &g_entities[tr.entityNum];
		coop_useWhat = t->classname ? t->classname : "?";
		if ( ValidUseTarget( t ) )
		{
			return qtrue;
		}
		if ( ( t->svFlags & SVF_STASIS_DOOR ) && t->trigger_formation )
		{
			return qtrue;
		}
		if ( t->client && t->NPC && t->client->ps.pm_type < PM_DEAD && t->client->playerTeam
			&& t->client->playerTeam == ent->client->playerTeam && !( t->NPC->scriptFlags & SCF_NO_RESPONSE ) )
		{
			return qtrue;	// a crew member who answers
		}
	}
	n = gi.EntitiesInBox( ent->absmin, ent->absmax, list, 64 );
	for ( int i = 0; i < n; i++ )
	{
		if ( list[i]->e_TouchFunc == touchF_Touch_Multi && ( list[i]->spawnflags & 4 ) && !( list[i]->svFlags & SVF_INACTIVE ) )
		{
			coop_useWhat = "a use trigger";
			return qtrue;	// a trigger_multiple / trigger_once that fires on Use
		}
	}
	return qfalse;
}

// why player 2 can't teleport now (NULL: it can). "" = blocked without a line of its own (someone
// is down: the revive lines are on screen; a script or camera holds the players)
static const char *COOP_TeleportBlocked( gentity_t *ent )
{
	gentity_t *p1 = &g_entities[0];

	if ( COOP_AnyoneDown() || COOP_GameIsOver() || !p1->inuse || !p1->client || p1->health <= 0
		|| p1->client->ps.pm_type >= PM_DEAD || ent->health <= 0 || ent->client->ps.pm_type >= PM_DEAD )
	{
		return "";
	}
	if ( in_camera || player_locked || coop_p2.hidden || !COOP_GuestConnected() )
	{
		return "";
	}
	return NULL;
}

static void COOP_UseTell( const char *text )
{
	if ( coop_p2Num >= 0 && COOP_GuestConnected() )
	{
		gi.SendServerCommand( coop_p2Num, "coopmsg \"%s\"", text );
	}
}

static void COOP_HoldCancel( coopUse_t *u )
{
	u->holdStart = 0;
	u->nextMsg = 0;
	if ( u->msgShown )
	{
		u->msgShown = qfalse;
		COOP_UseTell( "" );
	}
}

static void COOP_Teleport( gentity_t *ent, coopUse_t *u )
{
	gentity_t	*p1 = &g_entities[0];
	vec3_t		org;

	if ( !COOP_FindSpotNear( p1, org ) )
	{
		u->cooldownUntil = level.time + 1000;
		u->msgShown = qtrue;
		u->nextMsg = level.time + 1000;
		COOP_UseTell( va( "No room next to %s", COOP_UseName( 0 ) ) );
		gi.Printf( "co-op: player 2 teleport: no room next to player 1\n" );
		return;
	}
	COOP_PlaceP2( ent, org );
	G_AddEvent( ent, EV_GENERAL_SOUND, G_SoundIndex( "sound/world/transporter.wav" ) );
	u->cooldownUntil = level.time + COOP_TELE_COOLDOWN;
	u->msgShown = qfalse;
	COOP_UseTell( "" );
	gi.Printf( "co-op: player 2 teleported to player 1 at %s\n", vtos( org ) );
}

static void COOP_Ping( int idx, gentity_t *ent )
{
	coopUse_t	*u = &coop_use[idx];
	trace_t		tr;
	vec3_t		src, dest, fwd, spot;

	if ( in_camera || ent->health <= 0 || ent->client->ps.pm_type >= PM_DEAD || COOP_IsDowned( ent )
		|| ( idx == 1 && coop_p2.hidden ) || level.time < u->nextPing )
	{
		return;
	}
	VectorCopy( ent->client->renderInfo.eyePoint, src );
	AngleVectors( ent->client->renderInfo.eyeAngles, fwd, NULL, NULL );
	VectorMA( src, COOP_PING_RANGE, fwd, dest );
	gi.trace( &tr, src, NULL, NULL, dest, ent->s.number, MASK_SHOT );
	if ( tr.fraction < 1.0f && tr.entityNum < ENTITYNUM_WORLD && COOP_IsPlayer( &g_entities[tr.entityNum] ) )
	{
		// through the partner standing in the way: the spot is what is behind them
		VectorCopy( tr.endpos, src );
		gi.trace( &tr, src, NULL, NULL, dest, tr.entityNum, MASK_SHOT );
	}
	if ( tr.startsolid || tr.allsolid || tr.fraction >= 1.0f || ( tr.surfaceFlags & SURF_SKY ) )
	{
		return;	// nothing there to point at
	}
	VectorMA( tr.endpos, 4.0f, tr.plane.normal, spot );
	u->nextPing = level.time + COOP_PING_REPEAT;
	gi.SendServerCommand( COOP_EVERYONE, "cooping %d %d %d %d \"%s\"", idx, (int)spot[0], (int)spot[1], (int)spot[2],
		COOP_UseName( idx ) );
	gi.Printf( "co-op: player %d pings %s\n", idx + 1, vtos( spot ) );
}

/*
COOP_UseButtons

ClientThink_real for either player, after the command's buttons were taken (client->buttons).
*/
void COOP_UseButtons( gentity_t *ent, usercmd_t *ucmd )
{
	int			idx;
	coopUse_t	*u;
	qboolean	down;

	if ( !coop_enabled || !ent->client )
	{
		return;
	}
	idx = ( ent == &g_entities[0] ) ? 0 : COOP_IsP2( ent ) ? 1 : -1;
	if ( idx < 0 )
	{
		return;
	}
	u = &coop_use[idx];
	if ( idx == 1 && !coop_p2.guestSteering )
	{
		// the puppet (or the console) moves player 2: nothing to do; a guest's next press starts over
		if ( u->held || u->holdStart )
		{
			COOP_HoldCancel( u );
			memset( u, 0, sizeof( *u ) );
		}
		return;
	}
	down = (qboolean)( ( ucmd->buttons & BUTTON_USE ) != 0 );

	if ( !down )
	{
		if ( u->held )
		{
			// released: a short press with nothing to use is a tap
			u->lastTap = ( !u->pressUsable && level.time - u->pressTime <= COOP_TAP_WINDOW ) ? u->pressTime : 0;
		}
		u->held = qfalse;
		u->gap++;
		// player 2's hold survives one frame without a command (a late packet)
		if ( u->gap > ( idx == 1 ? 1 : 0 ) )
		{
			if ( u->holdStart )
			{
				COOP_HoldCancel( u );
			}
			u->noHold = qfalse;
		}
		return;
	}

	qboolean usable = COOP_HasUseTarget( ent );
	if ( !u->held )
	{
		if ( coop_use_debug->integer )
		{
			gi.Printf( "co-op: player %d presses Use: %s%s\n", idx + 1, usable ? "uses " : "nothing to use, ", coop_useWhat );
		}
		// a press. After a single empty frame mid-hold (gap 1) it is the same hold going on.
		qboolean resumed = (qboolean)( u->gap == 1 && idx == 1 && level.time - u->pressTime > COOP_TAP_WINDOW );
		if ( !resumed )
		{
			if ( !usable && u->lastTap && level.time - u->lastTap <= COOP_TAP_WINDOW )
			{
				COOP_Ping( idx, ent );
				u->lastTap = 0;
				u->noHold = qtrue;	// the second tap of a ping doesn't start a teleport hold
				COOP_HoldCancel( u );
			}
			else if ( u->gap > 1 || idx == 0 )
			{
				u->noHold = qfalse;
			}
			u->pressTime = level.time;
			u->pressUsable = usable;
		}
	}
	u->held = qtrue;
	u->gap = 0;
	if ( usable )
	{
		u->pressUsable = qtrue;
	}

	if ( idx != 1 )
	{
		return;		// only player 2 teleports
	}
	const char *blocked = COOP_TeleportBlocked( ent );
	if ( blocked || usable || COOP_IsDowned( &g_entities[0] ) )
	{
		// something to use now, someone down, or the players held: this press is done with teleporting
		if ( u->holdStart || blocked )
		{
			u->noHold = qtrue;
		}
		COOP_HoldCancel( u );
		return;
	}
	if ( u->noHold )
	{
		return;
	}
	if ( level.time < u->cooldownUntil )
	{
		// shown only once the press is clearly a hold
		if ( level.time - u->pressTime >= COOP_HOLD_SHOW && level.time >= u->nextMsg )
		{
			u->nextMsg = level.time + COOP_USE_MSG_TIME;
			u->msgShown = qtrue;
			COOP_UseTell( va( "Teleport ready in %d s", ( u->cooldownUntil - level.time + 999 ) / 1000 ) );
		}
		u->holdStart = 0;
		return;
	}
	int total = (int)( coop_teleporthold->value * 1000 );
	if ( total < 500 )
	{
		total = 500;
	}
	if ( !u->holdStart )
	{
		u->holdStart = level.time;
		u->nextMsg = 0;
	}
	int elapsed = level.time - u->holdStart;
	if ( elapsed >= total )
	{
		u->holdStart = 0;
		u->noHold = qtrue;	// one teleport per press
		COOP_Teleport( ent, u );
		return;
	}
	if ( elapsed >= COOP_HOLD_SHOW && level.time >= u->nextMsg )
	{
		u->nextMsg = level.time + COOP_USE_MSG_TIME;
		u->msgShown = qtrue;
		// "#done/total": the cgame draws a fill bar and runs it on between updates
		COOP_UseTell( va( "Teleporting to %s (keep holding Use)|#%d/%d", COOP_UseName( 0 ), elapsed, total ) );
	}
}
