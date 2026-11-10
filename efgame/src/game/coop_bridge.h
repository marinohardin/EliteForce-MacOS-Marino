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

// The replica (M1): the host writes each entity, and the level's globals, as a byte image; the
// guest (sp_coop 2) writes the host's images into its own entities. The engine moves the images
// (efcode/sys/sys_coop_net.c: changes against what the guest acknowledged, compressed). The bridge
// looks up COOP_GetReplica in the game module; a game without co-op has none.
#define COOP_REPLICA_VERSION	1

typedef struct
{
	int			version;		// COOP_REPLICA_VERSION
	// host: entity num as an image in buf (at most size bytes): its length, 0 when the slot is
	// free, -1 when it didn't fit
	int			(*WriteEntity)( int num, unsigned char *buf, int size );
	// host: the level's globals (level locals, camera and lock flags, player 2's number)
	int			(*WriteGlobals)( unsigned char *buf, int size );
	// guest: the host's images into this level (len 0: the slot is free)
	void		(*ApplyGlobals)( const unsigned char *data, int len );
	void		(*ApplyEntity)( int num, const unsigned char *data, int len );
	// guest: after a frame's images; cgameUp: the cgame is running (register what's new for it)
	void		(*ApplyDone)( qboolean cgameUp );
	// guest: the entity this machine views from (player 2), -1 while there is none
	int			(*ViewEntity)( void );
} coopReplica_t;

extern "C" const coopReplica_t *COOP_GetReplica( void );

#endif // COOP_BRIDGE_H
