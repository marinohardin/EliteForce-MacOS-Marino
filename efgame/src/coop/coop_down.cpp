// coop_down.cpp -- single-player co-op: downed players and revives.
//
// In co-op a player whose health runs out goes down instead of dying: lies on the ground, can't
// move or fire, and enemies leave them alone. The other player revives them by standing next to
// them and holding Use (coop_revivetime seconds); they come back with a quarter of their health.
// The game is over for both when both are down at once or the bleed-out time (coop_bleedout
// seconds) runs out, and whenever player 1 dies for real (a script ending the mission, or nobody
// left to revive them): player 2 dies with them, so the guest gets the same game-over screen.
//
// Player 2 goes down only while a guest steers it; the local puppet (no guest) keeps its old
// death and respawn next to player 1, and revives player 1 by itself when it is next to them.
//
// Saving works while a player is down: a save keeps a downed player as revived (a quarter of their
// health, armor and ammo as they are), so loading it brings both back standing. Player 2 goes to the
// .coop file that way (coop_inventory.cpp); player 1 is put in the .sav as revived for the moment of
// writing it and back down after (COOP_SaveP1Revived), so the game goes on as before the save.

#include "g_local.h"
#include "g_functions.h"
#include "anims.h"
#include "coop_public.h"
#include "coop_local.h"

extern vec3_t	playerMins;
extern vec3_t	playerMaxs;
extern bool		in_camera;
extern void		NPC_SetAnim( gentity_t *ent, int type, int anim, int priority );
extern void		PM_SetLegsAnimTimer( gentity_t *ent, int *legsAnimTimer, int time );
extern void		PM_SetTorsoAnimTimer( gentity_t *ent, int *torsoAnimTimer, int time );

#define COOP_REVIVE_RANGE		96		// reviver within this of the downed player (origins)
#define COOP_REVIVE_RANGE_BOT	128		// the puppet stops further away when it follows
#define COOP_REVIVE_HEALTH		25		// percent of max health after a revive
#define COOP_DOWN_MSG_TIME		250		// ms between the on-screen updates
#define COOP_CUTOFF_TIME		2000	// ms a level barrier must stand between the bodies before the body is moved

// what a body lies on and what a revive can't reach through: walls, closed doors, forcefields and
// other movers, player clips; not people (an NPC standing over a downed player isn't a floor or a wall)
#define COOP_WORLD_MASK			( CONTENTS_SOLID | CONTENTS_PLAYERCLIP )

typedef struct
{
	qboolean	down;
	int			bleedLeft;		// ms of bleed-out left (does not run while a camera runs)
	int			reviveStart;	// level.time the partner started holding Use, 0: not reviving
	int			nextMsg;
	int			oldFlags;		// FL_NOTARGET as it was before going down (the death set FL_NO_KNOCKBACK)
	int			cutOffSince;	// level.time a level barrier first stood between the bodies (0: it doesn't)
	gentity_t	*attacker;		// for the mission status text if this ends the game
} coopDown_t;

static coopDown_t	coop_down[2];
static qboolean		coop_gameOver;
static int			coop_downLastTime;
static cvar_t		*coop_bleedout;
static cvar_t		*coop_revivetime;

void COOP_DownInit( void )
{
	memset( coop_down, 0, sizeof( coop_down ) );
	coop_gameOver = qfalse;
	coop_downLastTime = 0;
	coop_bleedout = gi.cvar( "coop_bleedout", "30", 0 );
	coop_revivetime = gi.cvar( "coop_revivetime", "3", 0 );
}

static gentity_t *COOP_DownPlayer( int idx )
{
	return idx == 0 ? &g_entities[0] : COOP_P2();
}

static int COOP_DownIndex( const gentity_t *ent )
{
	if ( ent == &g_entities[0] )
	{
		return 0;
	}
	return ( ent && COOP_IsP2( ent ) ) ? 1 : -1;
}

qboolean COOP_IsDowned( const gentity_t *ent )
{
	int idx = COOP_DownIndex( ent );
	return (qboolean)( coop_enabled && idx >= 0 && coop_down[idx].down );
}

qboolean COOP_AnyoneDown( void )
{
	return (qboolean)( coop_enabled && ( coop_down[0].down || coop_down[1].down ) );
}

// a player that can revive (or be the partner a downed one is moved to): alive, up, in the level
static qboolean COOP_PlayerUp( int idx )
{
	gentity_t *ent = COOP_DownPlayer( idx );

	if ( !ent || !ent->inuse || !ent->client || ent->health <= 0 || coop_down[idx].down || ent->client->ps.pm_type >= PM_DEAD )
	{
		return qfalse;
	}
	if ( idx == 1 && coop_p2.hidden )
	{
		return qfalse;
	}
	return qtrue;
}

static const char *COOP_PlayerName( int idx )
{
	if ( idx == 1 )
	{
		const char *g = COOP_GuestConnected() ? COOP_GuestName() : "";
		return g[0] ? g : "Player 2";
	}
	return COOP_HostName();
}

// a status line on one player's screen (coop_hud.cpp) (player 2's only while a guest shows it)
static void COOP_Tell( int idx, const char *text )
{
	if ( idx == 0 )
	{
		gi.SendServerCommand( 0, "coopmsg \"%s\"", text );
	}
	else if ( coop_p2Num >= 0 && COOP_GuestConnected() )
	{
		gi.SendServerCommand( coop_p2Num, "coopmsg \"%s\"", text );
	}
}

static void COOP_MovePlayer( gentity_t *ent, const vec3_t org )
{
	if ( COOP_IsP2( ent ) )
	{
		COOP_PlaceP2( ent, org );
		return;
	}
	vec3_t o;
	VectorCopy( org, o );
	G_SetOrigin( ent, o );
	VectorCopy( o, ent->client->ps.origin );
	VectorClear( ent->client->ps.velocity );
	ent->client->ps.eFlags ^= EF_TELEPORT_BIT;
	gi.linkentity( ent );
}

/*
COOP_DeathNeedsMove

Deaths a body on the spot can't come back from: the downed player is put next to the partner.
Besides hazards (pits, lava, crushing...), that is every death the level itself dealt rather than an
enemy: a trigger_hurt, a mover or forcefield closing on them, a scripted explosion. Those happen
where the story closes an area off (voy2's forcefield seals the burning section, and its blasts go
off behind it), so a body left there could end up where the partner can't reach it any more, or
where players aren't meant to be once it gets up. A player an enemy downed stays where they fell.
*/
static qboolean COOP_DeathNeedsMove( gentity_t *ent, gentity_t *attacker, int mod )
{
	trace_t	tr;
	vec3_t	below;

	if ( !attacker || !attacker->client )
	{
		return qtrue;	// the world, a trigger, a mover, an effect, a script
	}
	switch ( mod )
	{
	case MOD_WATER:
	case MOD_SLIME:
	case MOD_LAVA:
	case MOD_CRUSH:
	case MOD_TELEFRAG:
	case MOD_FALLING:
	case MOD_TRIGGER_HURT:
		return qtrue;
	default:
		break;
	}
	// in the air with no floor close below (knocked off a ledge, a pit)
	VectorCopy( ent->currentOrigin, below );
	below[2] -= 256;
	gi.trace( &tr, ent->currentOrigin, playerMins, playerMaxs, below, ent->s.number, COOP_WORLD_MASK );
	// a box that starts a little inside a raised floor plate is still standing on something:
	// only one stuck in a solid all the way down, or with nothing below, needs moving
	return (qboolean)( tr.allsolid || tr.fraction >= 1.0f );
}

// settle a downed body onto the floor below it (it doesn't fall while down)
static void COOP_DropToFloor( gentity_t *ent )
{
	trace_t	tr;
	vec3_t	below;

	VectorCopy( ent->currentOrigin, below );
	below[2] -= 256;
	gi.trace( &tr, ent->currentOrigin, playerMins, playerMaxs, below, ent->s.number, COOP_WORLD_MASK );
	if ( !tr.startsolid && !tr.allsolid && tr.fraction < 1.0f )
	{
		COOP_MovePlayer( ent, tr.endpos );
	}
}

static void COOP_GameOver( gentity_t *attacker );

/*
COOP_IsBarrier

A brush the level itself opens and closes (a script or a trigger), which a player can't open by
walking up to it, using it or shooting it: forcefields and walls that switch on and off, and doors
that only the level opens.
*/
static qboolean COOP_IsBarrier( gentity_t *e )
{
	if ( !e->inuse || e->client || !e->classname )
	{
		return qfalse;
	}
	if ( !Q_stricmp( e->classname, "func_usable" ) || !Q_stricmp( e->classname, "func_wall" )
		|| !Q_stricmp( e->classname, "func_static" ) || !Q_stricmp( e->classname, "func_stasis_door" ) )
	{
		return qtrue;
	}
	if ( !Q_stricmp( e->classname, "func_door" ) )
	{
		gentity_t *master = ( ( e->flags & FL_TEAMSLAVE ) && e->teammaster ) ? e->teammaster : e;
		// a door with a name opens only when the level uses it; not one a player can use or shoot open
		return (qboolean)( master->targetname && !( master->spawnflags & 128 ) && !master->takedamage );
	}
	return qfalse;
}

/*
COOP_CutOff

A barrier of the level (COOP_IsBarrier) stands between the downed player and the partner: on both
lines between the bodies (as for a revive) the first thing hit is such a barrier. A player an enemy
downed stays where they fell, but when the story then closes a forcefield or door between the two,
nobody can get to them any more and they would bleed out (game over).
*/
static qboolean COOP_CutOff( gentity_t *partner, gentity_t *downed )
{
	trace_t	tr;
	vec3_t	from, to;

	for ( int i = 0; i < 2; i++ )
	{
		float up = i ? 20.0f : 0.0f;
		VectorCopy( partner->currentOrigin, from );
		VectorCopy( downed->currentOrigin, to );
		from[2] += up;
		to[2] += up;
		gi.trace( &tr, from, NULL, NULL, to, partner->s.number, COOP_WORLD_MASK );
		if ( tr.startsolid || tr.allsolid || tr.fraction >= 1.0f || tr.entityNum == downed->s.number
			|| tr.entityNum >= ENTITYNUM_WORLD || !COOP_IsBarrier( &g_entities[tr.entityNum] ) )
		{
			return qfalse;
		}
	}
	return qtrue;
}

/*
COOP_CanReach

The reviver must be next to the downed player with nothing solid between them: no wall, closed door,
forcefield or other mover (a revive through a forcefield got player 2 up on the far side of voy2's).
Two lines between the bodies, at the middle of the body and higher up, so a low console or railing
between them doesn't stop it but anything as tall as a person does. People in the way don't count.
*/
static qboolean COOP_CanReach( gentity_t *reviver, gentity_t *downed, float range )
{
	trace_t	tr;
	vec3_t	from, to;

	if ( DistanceSquared( downed->currentOrigin, reviver->currentOrigin ) > range * range )
	{
		return qfalse;
	}
	for ( int i = 0; i < 2; i++ )
	{
		float up = i ? 20.0f : 0.0f;
		VectorCopy( reviver->currentOrigin, from );
		VectorCopy( downed->currentOrigin, to );
		from[2] += up;
		to[2] += up;
		gi.trace( &tr, from, NULL, NULL, to, reviver->s.number, COOP_WORLD_MASK );
		if ( !tr.startsolid && !tr.allsolid && ( tr.fraction >= 1.0f || tr.entityNum == downed->s.number ) )
		{
			return qtrue;
		}
	}
	return qfalse;
}

// a revive is what this player's Use does now: the partner is down and within reach (coop_use.cpp:
// the teleport hold and the double tap leave such a press alone)
qboolean COOP_CanReviveNow( gentity_t *reviver )
{
	int idx = COOP_DownIndex( reviver );

	if ( !coop_enabled || idx < 0 || !coop_down[idx ^ 1].down || !COOP_PlayerUp( idx ) )
	{
		return qfalse;
	}
	gentity_t *downed = COOP_DownPlayer( idx ^ 1 );
	return (qboolean)( downed && COOP_CanReach( reviver, downed, COOP_REVIVE_RANGE ) );
}

/*
COOP_PlayerDown

player_die for either player. qtrue: handled here (the player went down, or player 2's death
was handled); qfalse: the normal death runs (player 1: game over; player 2: COOP_P2Die).
*/
qboolean COOP_PlayerDown( gentity_t *ent, gentity_t *attacker, int mod )
{
	int			idx = COOP_DownIndex( ent );
	int			other;
	gclient_t	*cl;

	if ( !coop_enabled || idx < 0 || !ent->client || coop_gameOver )
	{
		return qfalse;
	}
	cl = ent->client;
	if ( coop_down[idx].down || cl->ps.pm_type == PM_DEAD )
	{
		return qtrue;	// already down (or dead)
	}
	if ( idx == 1 && !COOP_GuestConnected() )
	{
		return qfalse;	// the puppet: its old death and respawn
	}
	other = idx ^ 1;
	if ( !COOP_PlayerUp( other ) )
	{
		// nobody left to revive: game over for both
		if ( idx == 0 )
		{
			return qfalse;	// player 1's own death; COOP_DownFrame takes player 2 along
		}
		coop_down[idx].attacker = attacker;
		COOP_GameOver( attacker );
		return qtrue;
	}

	gentity_t *partner = COOP_DownPlayer( other );
	if ( COOP_DeathNeedsMove( ent, attacker, mod ) )
	{
		vec3_t org;
		int contents = ent->contents;
		ent->contents = 0;	// not in the way of the spot search
		qboolean found = COOP_FindSpotNear( partner, org );
		ent->contents = contents;
		if ( !found )
		{
			if ( idx == 0 )
			{
				return qfalse;
			}
			COOP_GameOver( attacker );
			return qtrue;
		}
		COOP_MovePlayer( ent, org );
	}

	coop_down[idx].down = qtrue;
	coop_down[idx].bleedLeft = (int)( coop_bleedout->value * 1000 );
	if ( coop_down[idx].bleedLeft < 1000 )
	{
		coop_down[idx].bleedLeft = 1000;
	}
	coop_down[idx].reviveStart = 0;
	coop_down[idx].nextMsg = 0;
	coop_down[idx].cutOffSince = 0;
	coop_down[idx].oldFlags = ent->flags & FL_NOTARGET;
	coop_down[idx].attacker = attacker;

	ent->health = 0;
	cl->ps.stats[STAT_HEALTH] = 0;
	ent->takedamage = qfalse;
	ent->flags |= FL_NOTARGET;
	ent->contents = CONTENTS_CORPSE;
	ent->enemy = NULL;
	VectorClear( cl->ps.velocity );
	cl->ps.pm_type = PM_FREEZE;
	cl->renderInfo.lookTarget = ENTITYNUM_NONE;
	memset( cl->ps.powerups, 0, sizeof( cl->ps.powerups ) );
	G_AddEvent( ent, Q_irand( EV_DEATH1, EV_DEATH3 ), ent->health );
	NPC_SetAnim( ent, SETANIM_BOTH, BOTH_DEATH1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD );
	PM_SetLegsAnimTimer( ent, &cl->ps.legsAnimTimer, -1 );
	PM_SetTorsoAnimTimer( ent, &cl->ps.torsoAnimTimer, -1 );
	COOP_DropToFloor( ent );
	gi.linkentity( ent );
	gi.Printf( "co-op: player %d is down (%d s to revive; by %s, means %d)\n", idx + 1, coop_down[idx].bleedLeft / 1000,
		attacker ? ( attacker->classname ? attacker->classname : "?" ) : "nothing", mod );
	return qtrue;
}

// the health a revive gives this player (also what a save keeps for a downed or dead one)
int COOP_ReviveHealth( const gentity_t *ent )
{
	int maxHealth = ( ent->client && ent->client->ps.stats[STAT_MAX_HEALTH] > 0 ) ? ent->client->ps.stats[STAT_MAX_HEALTH] : 100;
	int health = maxHealth * COOP_REVIVE_HEALTH / 100;
	return health < 1 ? 1 : health;
}

static void COOP_Revive( int idx )
{
	gentity_t	*ent = COOP_DownPlayer( idx );
	gclient_t	*cl;

	coop_down[idx].down = qfalse;
	if ( !ent || !ent->client )
	{
		return;
	}
	cl = ent->client;
	ent->health = COOP_ReviveHealth( ent );
	cl->ps.stats[STAT_HEALTH] = ent->health;
	ent->takedamage = qtrue;
	ent->flags = ( ent->flags & ~( FL_NOTARGET | FL_NO_KNOCKBACK ) ) | coop_down[idx].oldFlags;
	if ( idx == 1 )
	{
		ent->flags = coop_p2.notarget ? ( ent->flags | FL_NOTARGET ) : ( ent->flags & ~FL_NOTARGET );	// (the cheat may have changed meanwhile)
	}
	ent->contents = CONTENTS_BODY;
	VectorCopy( playerMins, ent->mins );
	VectorCopy( playerMaxs, ent->maxs );
	cl->ps.pm_type = PM_NORMAL;
	cl->ps.legsAnimTimer = 0;
	cl->ps.torsoAnimTimer = 0;
	NPC_SetAnim( ent, SETANIM_BOTH, BOTH_STAND2, SETANIM_FLAG_OVERRIDE );
	gi.linkentity( ent );
	COOP_Tell( idx, "" );
	COOP_Tell( idx ^ 1, "" );
	gi.Printf( "co-op: player %d revived (health %d)\n", idx + 1, ent->health );
}

/*
COOP_SaveP1Revived

A save while player 1 is down. The .sav has player 1 only and no downed state (a load starts the
co-op state afresh), so player 1 is written the way a revive leaves them: up, a quarter of their
health, solid, a target again, standing. Nothing else changes: COOP_PreSave calls this with qtrue
before WriteLevel writes anything and COOP_PostSave with qfalse after it, which puts back exactly
what was there, so the game goes on with player 1 still down. Fields only (no linking, no
animation calls: those could finish a script's wait).
*/
static struct
{
	qboolean	active;
	int			health, statHealth, flags, contents, pmType;
	qboolean	takedamage;
	int			legsAnim, torsoAnim, legsAnimTimer, torsoAnimTimer, sLegsAnim, sTorsoAnim;
	vec3_t		mins, maxs;		// a frozen player's box is empty (the load would put them in the floor)
} coop_saveP1;

void COOP_SaveP1Revived( qboolean revived )
{
	gentity_t	*ent = &g_entities[0];
	gclient_t	*cl = ent->client;

	if ( !revived )
	{
		if ( coop_saveP1.active && cl )
		{
			ent->health = coop_saveP1.health;
			cl->ps.stats[STAT_HEALTH] = coop_saveP1.statHealth;
			ent->takedamage = coop_saveP1.takedamage;
			ent->flags = coop_saveP1.flags;
			ent->contents = coop_saveP1.contents;
			cl->ps.pm_type = coop_saveP1.pmType;
			cl->ps.legsAnim = coop_saveP1.legsAnim;
			cl->ps.torsoAnim = coop_saveP1.torsoAnim;
			cl->ps.legsAnimTimer = coop_saveP1.legsAnimTimer;
			cl->ps.torsoAnimTimer = coop_saveP1.torsoAnimTimer;
			ent->s.legsAnim = coop_saveP1.sLegsAnim;
			ent->s.torsoAnim = coop_saveP1.sTorsoAnim;
			VectorCopy( coop_saveP1.mins, ent->mins );
			VectorCopy( coop_saveP1.maxs, ent->maxs );
		}
		coop_saveP1.active = qfalse;
		return;
	}
	coop_saveP1.active = qfalse;
	if ( !coop_enabled || !coop_down[0].down || !cl || coop_gameOver )
	{
		return;
	}
	coop_saveP1.active = qtrue;
	coop_saveP1.health = ent->health;
	coop_saveP1.statHealth = cl->ps.stats[STAT_HEALTH];
	coop_saveP1.takedamage = ent->takedamage;
	coop_saveP1.flags = ent->flags;
	coop_saveP1.contents = ent->contents;
	coop_saveP1.pmType = cl->ps.pm_type;
	coop_saveP1.legsAnim = cl->ps.legsAnim;
	coop_saveP1.torsoAnim = cl->ps.torsoAnim;
	coop_saveP1.legsAnimTimer = cl->ps.legsAnimTimer;
	coop_saveP1.torsoAnimTimer = cl->ps.torsoAnimTimer;
	coop_saveP1.sLegsAnim = ent->s.legsAnim;
	coop_saveP1.sTorsoAnim = ent->s.torsoAnim;
	VectorCopy( ent->mins, coop_saveP1.mins );
	VectorCopy( ent->maxs, coop_saveP1.maxs );

	// as COOP_Revive leaves them
	ent->health = cl->ps.stats[STAT_HEALTH] = COOP_ReviveHealth( ent );
	ent->takedamage = qtrue;
	ent->flags = ( ent->flags & ~( FL_NOTARGET | FL_NO_KNOCKBACK ) ) | coop_down[0].oldFlags;
	ent->contents = CONTENTS_BODY;
	VectorCopy( playerMins, ent->mins );
	VectorCopy( playerMaxs, ent->maxs );
	cl->ps.pm_type = PM_NORMAL;
	cl->ps.legsAnim = ( ( cl->ps.legsAnim & ANIM_TOGGLEBIT ) ^ ANIM_TOGGLEBIT ) | BOTH_STAND2;
	cl->ps.torsoAnim = ( ( cl->ps.torsoAnim & ANIM_TOGGLEBIT ) ^ ANIM_TOGGLEBIT ) | BOTH_STAND2;
	cl->ps.legsAnimTimer = 0;
	cl->ps.torsoAnimTimer = 0;
	ent->s.legsAnim = cl->ps.legsAnim;
	ent->s.torsoAnim = cl->ps.torsoAnim;
	gi.Printf( "co-op: player 1 is down: saved as revived (health %d)\n", ent->health );
}

// level change while someone is down: they get up (the next level must not start with a dead player)
void COOP_ReviveAll( void )
{
	if ( !coop_enabled )
	{
		return;
	}
	for ( int i = 0; i < 2; i++ )
	{
		if ( coop_down[i].down )
		{
			COOP_Revive( i );
		}
	}
}

/*
COOP_GameOver

Both players are out: player 1 dies for real (the normal death and its mission status screen),
player 2 dies with it and doesn't come back; the host's reload starts the level again for both.
*/
static void COOP_GameOver( gentity_t *attacker )
{
	gentity_t	*p1 = &g_entities[0];
	gentity_t	*p2 = COOP_P2();

	if ( coop_gameOver )
	{
		return;
	}
	coop_gameOver = qtrue;
	gi.Printf( "co-op: game over\n" );
	for ( int i = 0; i < 2; i++ )
	{
		if ( coop_down[i].down )
		{
			COOP_Tell( i, "" );
			COOP_Tell( i ^ 1, "" );	// the partner's "revive them" line too
		}
		coop_down[i].down = qfalse;
	}
	if ( p2 && p2->client && p2->client->ps.pm_type != PM_DEAD )
	{
		p2->health = 0;
		p2->client->ps.stats[STAT_HEALTH] = 0;
		COOP_P2Die( p2, attacker );
	}
	if ( p1->client && p1->client->ps.pm_type != PM_DEAD )
	{
		if ( p1->health > 0 )
		{
			p1->health = 0;
		}
		p1->client->ps.stats[STAT_HEALTH] = p1->health;
		p1->takedamage = qfalse;
		player_die( p1, p1, attacker ? attacker : p1, 0, MOD_UNKNOWN );
	}
}

// COOP_P2DeadThink: no respawn once the game is over
qboolean COOP_GameIsOver( void )
{
	return coop_gameOver;
}

static void COOP_DownMessages( int idx, int reviver, int progress )
{
	coopDown_t	*d = &coop_down[idx];
	int			partner = idx ^ 1;
	int			secs = ( d->bleedLeft + 999 ) / 1000;
	char		text[256];

	if ( level.time < d->nextMsg )
	{
		return;
	}
	d->nextMsg = level.time + COOP_DOWN_MSG_TIME;

	if ( progress >= 0 )
	{
		char	bar[21];
		int		total = (int)( coop_revivetime->value * 1000 );
		int		n = total > 0 ? progress * 20 / total : 20;
		for ( int i = 0; i < 20; i++ )
		{
			bar[i] = i < n ? '=' : '-';
		}
		bar[20] = 0;
		Com_sprintf( text, sizeof( text ), "Reviving %s|[%s]", COOP_PlayerName( idx ), bar );
		COOP_Tell( partner, text );
		Com_sprintf( text, sizeof( text ), "%s is reviving you|[%s]", COOP_PlayerName( partner ), bar );
		COOP_Tell( idx, text );
		return;
	}
	Com_sprintf( text, sizeof( text ), "Downed: waiting for %s|%d s", COOP_PlayerName( partner ), secs );
	COOP_Tell( idx, text );
	if ( reviver >= 0 )
	{
		Com_sprintf( text, sizeof( text ), "Hold Use to revive %s|%d s", COOP_PlayerName( idx ), secs );
	}
	else
	{
		Com_sprintf( text, sizeof( text ), "%s is down: revive them|%d s", COOP_PlayerName( idx ), secs );
	}
	COOP_Tell( partner, text );
}

/*
COOP_DownFrame

After both players' frames (COOP_EndFrame): game over on both, bleed-out, revives, messages.
*/
void COOP_DownFrame( void )
{
	gentity_t	*p1 = &g_entities[0];
	gentity_t	*p2 = COOP_P2();
	int			elapsed;

	if ( !coop_enabled )
	{
		return;
	}
	elapsed = coop_downLastTime ? level.time - coop_downLastTime : 0;
	coop_downLastTime = level.time;

	// player 1 died for real (no one to revive them, a script ending the mission): player 2 too
	if ( !coop_gameOver && p1->client && p1->client->ps.pm_type == PM_DEAD )
	{
		if ( p2 && ( p2->health > 0 || coop_down[1].down ) )
		{
			COOP_GameOver( coop_down[1].attacker );
		}
		else
		{
			coop_gameOver = qtrue;
		}
		return;
	}
	if ( coop_gameOver )
	{
		return;
	}

	// the guest left while player 2 was down: the puppet's old death and respawn
	if ( coop_down[1].down && ( !p2 || !COOP_GuestConnected() ) )
	{
		coop_down[1].down = qfalse;
		COOP_Tell( 0, "" );
		if ( p2 )
		{
			COOP_P2Die( p2, coop_down[1].attacker );
		}
	}

	if ( coop_down[0].down && coop_down[1].down )
	{
		COOP_GameOver( coop_down[0].attacker );
		return;
	}

	for ( int idx = 0; idx < 2; idx++ )
	{
		coopDown_t	*d = &coop_down[idx];
		gentity_t	*ent = COOP_DownPlayer( idx );
		int			partner = idx ^ 1;
		gentity_t	*pent = COOP_DownPlayer( partner );
		int			reviver = -1;
		int			progress = -1;

		if ( !d->down || !ent )
		{
			continue;
		}
		if ( !in_camera )
		{
			d->bleedLeft -= elapsed;
		}
		if ( d->bleedLeft <= 0 )
		{
			gi.Printf( "co-op: player %d bled out\n", idx + 1 );
			COOP_GameOver( d->attacker );
			return;
		}
		ent->client->ps.pm_type = PM_FREEZE;

		if ( COOP_PlayerUp( partner ) && pent )
		{
			qboolean	bot = (qboolean)( partner == 1 && !COOP_GuestConnected() );
			float		range = bot ? COOP_REVIVE_RANGE_BOT : COOP_REVIVE_RANGE;
			if ( COOP_CanReach( pent, ent, range ) )
			{
				reviver = partner;
				if ( bot || ( pent->client->buttons & BUTTON_USE ) )
				{
					if ( !d->reviveStart )
					{
						d->reviveStart = level.time;
					}
					progress = level.time - d->reviveStart;
					if ( progress >= (int)( coop_revivetime->value * 1000 ) )
					{
						COOP_Revive( idx );
						continue;
					}
				}
			}
		}
		// a barrier of the level closed between them: put the body next to the partner
		if ( progress < 0 && reviver < 0 && COOP_PlayerUp( partner ) && pent && COOP_CutOff( pent, ent ) )
		{
			if ( !d->cutOffSince )
			{
				d->cutOffSince = level.time;
			}
			else if ( level.time - d->cutOffSince >= COOP_CUTOFF_TIME )
			{
				vec3_t	org;
				int		contents = ent->contents;
				ent->contents = 0;
				qboolean found = COOP_FindSpotNear( pent, org );
				ent->contents = contents;
				if ( found )
				{
					COOP_MovePlayer( ent, org );
					COOP_DropToFloor( ent );
					gi.Printf( "co-op: player %d was cut off behind a barrier: moved next to player %d\n", idx + 1, partner + 1 );
				}
				d->cutOffSince = 0;
			}
		}
		else
		{
			d->cutOffSince = 0;
		}
		if ( progress < 0 && d->reviveStart )
		{
			d->reviveStart = 0;
			d->nextMsg = 0;		// show the cancel at once
		}
		COOP_DownMessages( idx, reviver, progress );
	}
}
