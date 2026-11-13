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
} coopMapFix_t;

static const coopMapFix_t coop_mapFixes[] = {
	{ NULL, NULL, NULL }
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

void COOP_FixesFrame( void )
{
	gentity_t *p2 = COOP_P2();

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
