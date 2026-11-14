// coop_fixes.cpp -- single-player co-op: fixes for spots in the campaign where two players break
// what a level expects of one.
//
// Two kinds:
// - Generic: things levels do in many places (a script moves player 1 somewhere else, ...). These
//   run on every map.
// - Per map: a table keyed by the map's name with handlers for that level's own trouble spots
//   (an entity, a trigger or a script by name). Only the current map's entry runs.
//
// Everything here runs only in co-op (sp_coop 1 on the host); single player never gets here.

#include "g_local.h"
#include "coop_public.h"
#include "coop_local.h"

extern bool		in_camera;

#define COOP_SCRIPT_MOVE_DELAY	100		// ms after a script moved player 1: bring player 2 along

typedef struct
{
	const char	*map;
	void		( *start )( void );		// the level's first frames are done (P2 is in the level)
	void		( *frame )( void );		// every host frame
	qboolean	( *p2Touch )( gentity_t *trigger, gentity_t *p2 );	// P2 touches a trigger; qtrue: handled, don't fire it
} coopMapFix_t;

// the trigger runs this script when used (its "usescript")
static qboolean COOP_TriggerRuns( const gentity_t *trigger, const char *script )
{
	return (qboolean)( trigger->behaviorSet[BSET_USE] && !Q_stricmp( trigger->behaviorSet[BSET_USE], script ) );
}

/*
dn1, forge1: the level opens onto space. Drifting out there plays a scene of the player floating away
and kills player 1 (mission failed). Player 2 drifting out would kill player 1 the same way, so for
player 2 it is a fall into a pit instead: down next to player 1 (with a guest), or the puppet's death
and respawn.
*/
static qboolean COOP_P2IntoSpace( gentity_t *p2 )
{
	if ( p2->health > 0 && p2->takedamage )
	{
		gi.Printf( "co-op: player 2 drifted out into space\n" );
		G_Damage( p2, NULL, NULL, NULL, p2->currentOrigin, p2->health + 1000,
			DAMAGE_NO_KNOCKBACK | DAMAGE_NO_ARMOR | DAMAGE_NO_PROTECTION, MOD_TRIGGER_HURT );
	}
	return qtrue;
}

// dn1: the hangar's opening runs dn1/space
static qboolean COOP_Dn1Touch( gentity_t *trigger, gentity_t *p2 )
{
	return COOP_TriggerRuns( trigger, "dn1/space" ) ? COOP_P2IntoSpace( p2 ) : qfalse;
}

// forge1: the space below the hull spawns the drifting Munro (deathmunro), whose spawn script runs forge1/space
static qboolean COOP_Forge1Touch( gentity_t *trigger, gentity_t *p2 )
{
	return ( trigger->target && !Q_stricmp( trigger->target, "deathmunro" ) ) ? COOP_P2IntoSpace( p2 ) : qfalse;
}

/*
dn2: after the fight in the corridor the team falls back into the control room and seals its door
(blockdoor). The script only waits for player 1 to be inside; player 2 left in the corridor would be
shut out for the rest of the level. When the door seals with the players on opposite sides of it,
player 2 is brought in next to player 1.
*/
static gentity_t	*coop_dn2Door;
static int			coop_dn2DoorContents;

static void COOP_Dn2Start( void )
{
	coop_dn2Door = G_Find( NULL, FOFS( targetname ), "blockdoor" );
	coop_dn2DoorContents = coop_dn2Door ? coop_dn2Door->contents : 0;
}

static void COOP_Dn2Frame( void )
{
	gentity_t	*door = coop_dn2Door, *p1 = &g_entities[0], *p2 = COOP_P2();

	if ( !door || !door->inuse || !p2 || !p1->client )
	{
		return;
	}
	if ( door->contents && !coop_dn2DoorContents && p2->health > 0 )
	{
		// the door is a wall across the corridor: which side of its middle (along y) is each player on
		float middle = ( door->absmin[1] + door->absmax[1] ) * 0.5f;
		if ( ( p1->currentOrigin[1] > middle ) != ( p2->currentOrigin[1] > middle ) )
		{
			gi.Printf( "co-op: the control room is sealed with player 2 outside; bringing player 2 in\n" );
			COOP_CameraRejoin( p2 );
		}
	}
	coop_dn2DoorContents = door->contents;
}

static const coopMapFix_t coop_mapFixes[] = {
	{ "dn1", NULL, NULL, COOP_Dn1Touch },
	{ "dn2", COOP_Dn2Start, COOP_Dn2Frame, NULL },
	{ "forge1", NULL, NULL, COOP_Forge1Touch },
	{ NULL, NULL, NULL, NULL }
};

static const coopMapFix_t	*coop_fix;			// this map's entry, NULL: none
static qboolean				coop_fixLooked;		// looked it up for this level
static qboolean				coop_fixStarted;
static int					coop_scriptMoveAt;	// a script moved player 1: check player 2 at this time

void COOP_FixesInit( void )
{
	coop_fix = NULL;
	coop_fixLooked = qfalse;
	coop_fixStarted = qfalse;
	coop_scriptMoveAt = 0;
}

/*
COOP_OnScriptOrigin

A script put an entity somewhere (Q3_SetOrigin). When that is player 1 outside a cutscene (a
cutscene's end already brings player 2 back, COOP_CameraRejoin), the story has moved the player on,
and player 2, steered by a guest, would be left where it was: bring it along, as after a camera.
*/
void COOP_OnScriptOrigin( gentity_t *ent )
{
	if ( !coop_enabled || ent != &g_entities[0] || in_camera )
	{
		return;
	}
	coop_scriptMoveAt = level.time + COOP_SCRIPT_MOVE_DELAY;
}

static void COOP_LookUpFix( void )
{
	if ( !coop_fixLooked )
	{
		coop_fixLooked = qtrue;
		for ( const coopMapFix_t *f = coop_mapFixes; f->map; f++ )
		{
			if ( !Q_stricmp( f->map, level.mapname ) )
			{
				coop_fix = f;
				gi.Printf( "co-op: fixes for %s\n", f->map );
				break;
			}
		}
	}
}

/*
COOP_P2GravityTrigger

Gravity switches (dn6's zero-gravity section): a trigger whose targets are target_gravity_change
(not GLOBAL) sets the gravity of its activator. A trigger player 2 touches fires with player 1 as the
activator (the story's player), so player 2 walking through switched player 1's gravity, wherever
player 1 was, and kept its own. For these the one who walked through counts: player 2 gets the
gravity, player 1 keeps its own.

Two exceptions keep player 2 in step with what player 1 gets in single player:
- Player 1 is inside the same trigger: player 2 takes player 1's gravity, so two players standing in
  one zone have the same gravity whoever won the touch. NPCs fire these triggers too: forge1's start
  corridor (800 there, 500 in the rest of the level) has the squad standing in it, who fire it every
  0.5 s, so in single player player 1 standing in it never wins the touch and keeps 500.
- The trigger is waiting out its re-trigger time: somebody else just set their gravity with it, and
  a player touching it now gets nothing from it, so neither does player 2.
*/
static qboolean COOP_P2GravityTrigger( gentity_t *trigger, gentity_t *p2 )
{
	gentity_t	*t = NULL, *grav = NULL, *p1 = &g_entities[0];

	if ( !trigger->target || trigger->target2 || trigger->behaviorSet[BSET_USE] )
	{
		return qfalse;
	}
	while ( ( t = G_Find( t, FOFS( targetname ), trigger->target ) ) != NULL )
	{
		if ( !t->classname || Q_stricmp( t->classname, "target_gravity_change" ) || ( t->spawnflags & 1 )
			|| t->behaviorSet[BSET_USE] )
		{
			return qfalse;	// it does more than a gravity switch, or the switch is for the whole level
		}
		grav = t;
	}
	if ( !grav )
	{
		return qfalse;
	}
	if ( p1->client && p1->health > 0 && gi.EntityContact( p1->absmin, p1->absmax, trigger ) )
	{
		p2->client->ps.gravity = p1->client->ps.gravity;
		p2->svFlags = ( p2->svFlags & ~SVF_CUSTOM_GRAVITY ) | ( p1->svFlags & SVF_CUSTOM_GRAVITY );
		return qtrue;
	}
	if ( trigger->nextthink )
	{
		return qtrue;	// waiting out its re-trigger time: nobody gets anything from it now
	}
	p2->client->ps.gravity = (int)floor( grav->speed );
	p2->svFlags |= SVF_CUSTOM_GRAVITY;
	return qtrue;
}

qboolean COOP_P2Trigger( gentity_t *trigger, gentity_t *other )
{
	if ( !coop_enabled || !COOP_IsP2( other ) )
	{
		return qfalse;
	}
	if ( COOP_P2GravityTrigger( trigger, other ) )
	{
		return qtrue;
	}
	COOP_LookUpFix();
	return (qboolean)( coop_fix && coop_fix->p2Touch && coop_fix->p2Touch( trigger, other ) );
}

void COOP_FixesFrame( void )
{
	gentity_t *p2 = COOP_P2();

	COOP_LookUpFix();
	if ( !p2 )
	{
		return;
	}
	if ( coop_scriptMoveAt && level.time >= coop_scriptMoveAt && !in_camera )
	{
		coop_scriptMoveAt = 0;
		COOP_CameraRejoin( p2 );
	}
	if ( coop_fix )
	{
		if ( !coop_fixStarted && coop_fix->start )
		{
			coop_fix->start();
		}
		coop_fixStarted = qtrue;
		if ( coop_fix->frame )
		{
			coop_fix->frame();
		}
	}
}
