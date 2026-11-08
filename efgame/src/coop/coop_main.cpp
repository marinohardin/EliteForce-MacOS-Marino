// coop_main.cpp -- single-player co-op: the sp_coop switch and per-level state.
//
// Built only with EF_COOP (efgame/CMakeLists.txt). Everything here is inert unless sp_coop is 1
// when a level starts (the cvar is latched).

#include "g_local.h"
#include "coop_public.h"
#include "coop_local.h"

int		coop_enabled = 0;
int		coop_p2Num = -1;

static cvar_t	*sp_coop;

/*
COOP_Init

Called from InitGame for every level (new game, load, map change). The entity array was just
cleared, so P2 does not exist yet.
*/
void COOP_Init( void )
{
	sp_coop = gi.cvar( "sp_coop", "0", CVAR_LATCH );
	coop_enabled = ( sp_coop->integer == 1 );
	coop_p2Num = -1;
	if ( coop_enabled )
	{
		gi.Printf( "co-op: on (player 2 is a local puppet)\n" );
	}
}
