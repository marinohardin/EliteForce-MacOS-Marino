// coop_info.cpp -- single-player co-op: what the host tells a friend who checks for its game.
//
// The engine answers co-op info queries for the launcher's Join check (sys/sys_osx_coop.m). It
// can read the map and the skill itself; the mission title, the number of players and the time
// on this level are only known here, so they are published in three cvars about once a second:
//	coop_mission	the level's long name (the worldspawn message), as the loading screen shows it
//	coop_players	1, or 2 while player 2 is in the level
//	coop_time		seconds of game time on this level
// Nothing here runs unless co-op is on (COOP_RunFrame returns first otherwise).

#include "g_local.h"
#include "g_infostrings.h"
#include "coop_public.h"
#include "coop_local.h"

extern ginfoitem_t	bg_infoItemList[];

void COOP_PublishInfo( void )
{
	char	msg[MAX_QPATH * 2];
	const char *mission;
	int		index;

	if ( coop_p2.levelFrames % 10 != 1 )
	{
		return;
	}
	// as cg_info.cpp draws it: a number is an infostring item, anything else the text itself
	gi.GetConfigstring( CS_MESSAGE, msg, sizeof( msg ) );
	mission = msg;
	index = atoi( msg );
	if ( index > 0 && index < II_NUM_ITEMS )
	{
		mission = bg_infoItemList[index].infoString ? bg_infoItemList[index].infoString : "";
	}
	gi.cvar_set( "coop_mission", mission );
	gi.cvar_set( "coop_players", COOP_P2() ? "2" : "1" );
	gi.cvar_set( "coop_time", va( "%i", level.time / 1000 ) );
}
