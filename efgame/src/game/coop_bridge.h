// coop_bridge.h -- single-player co-op: what the engine hands the game's co-op code.
//
// EF_COOP builds only. The engine's SP bridge (sp/sp_bridge.cpp) looks up COOP_SetBridge in the
// game module after every load and calls it before the level starts, with the functions below.
// The game copies the table; a game without co-op has no such symbol and nothing happens.

#ifndef COOP_BRIDGE_H
#define COOP_BRIDGE_H

#define COOP_BRIDGE_VERSION	2

typedef struct
{
	int			version;		// COOP_BRIDGE_VERSION
	// the guest's newest input for player 2 (serverTime left to the game), with every button
	// pressed since the last call; qfalse when no guest steers player 2 right now
	qboolean	(*GuestCmd)( usercmd_t *cmd );
	// the guest's name, "" without a guest
	const char	*(*GuestName)( void );
	// a guest is connected (it may still have no fresh input: menu, console, loading)
	qboolean	(*GuestConnected)( void );
} coopBridge_t;

extern "C" void COOP_SetBridge( const coopBridge_t *bridge );

#endif // COOP_BRIDGE_H
