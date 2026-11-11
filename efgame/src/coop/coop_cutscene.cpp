// coop_cutscene.cpp -- single-player co-op: player 2 during cameras (cutscenes).
//
// P1 watches the camera; in M0 P2 has no view of its own, so while a camera runs P2 is not
// drawn, not solid, cannot be hurt or targeted, and does not move (COOP_P2ThinkGate).

#include "g_local.h"
#include "coop_public.h"
#include "coop_local.h"

#define COOP_CAMERA_REJOIN_DIST	160		// after a camera, rejoin P1 if further away than this
#define COOP_CAMERA_REJOIN_DELAY	500		// ms: ...and again this long after it

static qboolean COOP_ClearWayTo( gentity_t *ent, gentity_t *target );

void COOP_OnCameraEnable( void )
{
	gentity_t *ent;

	if ( !coop_enabled || ( ent = COOP_P2() ) == NULL )
	{
		return;
	}
	COOP_ApplyHidden( ent, qtrue );
}

void COOP_OnCameraDisable( void )
{
	gentity_t	*ent;

	if ( !coop_enabled || ( ent = COOP_P2() ) == NULL )
	{
		return;
	}
	COOP_ApplyHidden( ent, qfalse );
	COOP_CameraRejoin( ent );
	// scripts often put P1 in its place a moment after the camera ends: look again then
	coop_p2.rejoinAt = level.time + COOP_CAMERA_REJOIN_DELAY;
}

// P2 stood still while the camera ran, often where it spawned at the level start, and scripts
// often move P1 during a cutscene: rejoin P1 unless P2 is already close with a clear way to it
// (a spot just behind P1 can be below a ledge P1 stood on; P2 then can't follow at all)
void COOP_CameraRejoin( gentity_t *ent )
{
	gentity_t	*p1 = &g_entities[0];
	vec3_t		org;

	if ( ent->health > 0 && p1->inuse && ( DistanceSquared( p1->currentOrigin, ent->currentOrigin ) > COOP_CAMERA_REJOIN_DIST * COOP_CAMERA_REJOIN_DIST
		|| !COOP_ClearWayTo( ent, p1 ) ) && COOP_FindSpotNear( p1, org ) )
	{
		COOP_PlaceP2( ent, org );
	}
}

/*
COOP_ClearWayTo

P2 (ent) could walk to target in a straight line: a player-sized box moved from one to the
other hits nothing but the target.
*/
static qboolean COOP_ClearWayTo( gentity_t *ent, gentity_t *target )
{
	trace_t	tr;

	gi.trace( &tr, ent->currentOrigin, ent->mins, ent->maxs, target->currentOrigin, ent->s.number, MASK_PLAYERSOLID );
	return ( !tr.startsolid && !tr.allsolid && ( tr.fraction >= 1.0f || tr.entityNum == target->s.number ) ) ? qtrue : qfalse;
}
