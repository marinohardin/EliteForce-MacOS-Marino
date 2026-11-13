// coop_net.cpp -- single-player co-op: the guest's input, from the engine's co-op connection.
//
// The engine (sys/sys_coop_net.c) receives the guest's commands; sp/sp_bridge.cpp hands this module
// a table to fetch them with (COOP_SetBridge, coop_bridge.h). The module is reloaded for every
// level, and the table is set again each time before InitGame.

#include "g_local.h"
#include "coop_public.h"
#include "coop_local.h"
#include "coop_bridge.h"

static coopBridge_t	coop_bridge;	// zero: no engine side (nothing steers P2 from the network)

extern "C" __attribute__((visibility("default"))) void COOP_SetBridge( const coopBridge_t *bridge )
{
	memset( &coop_bridge, 0, sizeof( coop_bridge ) );
	if ( bridge && bridge->version == COOP_BRIDGE_VERSION )
	{
		coop_bridge = *bridge;
	}
}

qboolean COOP_GuestCmd( usercmd_t *cmd )
{
	return coop_bridge.GuestCmd ? coop_bridge.GuestCmd( cmd ) : qfalse;
}

const char *COOP_GuestName( void )
{
	const char *name = coop_bridge.GuestName ? coop_bridge.GuestName() : NULL;
	return name ? name : "";
}

const char *COOP_GuestSex( void )
{
	const char *sex = coop_bridge.GuestSex ? coop_bridge.GuestSex() : NULL;
	return sex ? sex : "";
}

qboolean COOP_GuestConnected( void )
{
	return coop_bridge.GuestConnected ? coop_bridge.GuestConnected() : qfalse;
}

int COOP_GuestViewTime( void )
{
	return coop_bridge.GuestViewTime ? coop_bridge.GuestViewTime() : 0;
}
