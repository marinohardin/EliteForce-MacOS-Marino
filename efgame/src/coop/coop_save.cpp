// coop_save.cpp -- single-player co-op: keeping player 2 out of saves.
//
// A .sav written in co-op must load in plain single player. P2 itself is skipped by
// WriteGEntities; here every saved pointer to P2 in other entities is cleared first (saves
// store pointers as entity numbers, which in plain SP would point at an empty slot). P2's own
// health, armor and ammo go to a .coop file next to the .sav instead (coop_inventory.cpp).
// A player that is down when the game is saved is kept as revived (coop_down.cpp).

#include "g_local.h"
#include "fields.h"
#include "coop_public.h"
#include "coop_local.h"

extern field_t	savefields_gEntity[];
extern field_t	savefields_gNPC[];
extern field_t	savefields_gClient[];

typedef std::map < std::string, int > timer_m;
extern timer_m	g_timers[ MAX_GENTITIES ];

// every F_GENTITY field of the struct at base that points at p2 becomes replacement
// ("activator" becomes P1, so a script that runs on the activator still has the player)
static void COOP_ScrubFields( const field_t *fields, byte *base, gentity_t *p2 )
{
	for ( const field_t *f = fields; f->psName; f++ )
	{
		if ( f->eFieldType != F_GENTITY )
		{
			continue;
		}
		gentity_t **pp = (gentity_t **)( base + f->iOffset );
		if ( *pp == p2 )
		{
			*pp = !strcmp( f->psName, "activator" ) ? &g_entities[0] : NULL;
		}
	}
}

void COOP_PreSave( void )
{
	gentity_t *p2;

	if ( !coop_enabled )
	{
		return;
	}
	COOP_CacheForSave();
	COOP_SaveP1Revived( qtrue );
	if ( ( p2 = COOP_P2() ) == NULL )
	{
		return;
	}
	for ( int i = 0; i < globals.num_entities; i++ )
	{
		gentity_t *ent = &g_entities[i];
		if ( !ent->inuse || ent == p2 )
		{
			continue;
		}
		COOP_ScrubFields( savefields_gEntity, (byte *)ent, p2 );
		if ( ent->NPC )
		{
			COOP_ScrubFields( savefields_gNPC, (byte *)ent->NPC, p2 );
		}
		if ( ent->client )
		{
			COOP_ScrubFields( savefields_gClient, (byte *)ent->client, p2 );
			if ( ent->client->renderInfo.lookTarget == p2->s.number )
			{
				ent->client->renderInfo.lookTarget = ENTITYNUM_NONE;
			}
		}
	}
	// sounds P2 made: P1 made them
	for ( int i = 0; i < level.numAlertEvents; i++ )
	{
		if ( level.alertEvents[i].owner == p2 )
		{
			level.alertEvents[i].owner = &g_entities[0];
		}
	}
	// timers are saved for every slot, in use or not
	g_timers[p2->s.number].clear();
}

// end of WriteLevel: a downed player 1 goes back down (see COOP_SaveP1Revived)
void COOP_PostSave( void )
{
	COOP_SaveP1Revived( qfalse );
}
