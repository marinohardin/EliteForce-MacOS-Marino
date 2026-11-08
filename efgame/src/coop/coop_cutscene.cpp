// coop_cutscene.cpp -- single-player co-op: player 2 during cameras (cutscenes).
//
// P1 watches the camera; in M0 P2 has no view of its own, so while a camera runs P2 is not
// drawn, not solid, cannot be hurt or targeted, and does not move (COOP_P2ThinkGate).

#include "g_local.h"
#include "coop_public.h"
#include "coop_local.h"

#define COOP_CAMERA_REJOIN_DIST	512		// after a camera, rejoin P1 if further away than this

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
	gentity_t	*p1 = &g_entities[0];
	vec3_t		org;

	if ( !coop_enabled || ( ent = COOP_P2() ) == NULL )
	{
		return;
	}
	COOP_ApplyHidden( ent, qfalse );
	// scripts often move P1 during a cutscene
	if ( p1->inuse && DistanceSquared( p1->currentOrigin, ent->currentOrigin ) > COOP_CAMERA_REJOIN_DIST * COOP_CAMERA_REJOIN_DIST
		&& COOP_FindSpotNear( p1, org ) )
	{
		COOP_PlaceP2( ent, org );
	}
}
