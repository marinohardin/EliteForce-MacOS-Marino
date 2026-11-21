// coop_bridge.h -- single-player co-op: what the engine hands the game's co-op code.
//
// EF_COOP builds only. The engine's SP bridge (sp/sp_bridge.cpp) looks up COOP_SetBridge in the
// game module after every load and calls it before the level starts, with the functions below.
// The game copies the table; a game without co-op has no such symbol and nothing happens.

#ifndef COOP_BRIDGE_H
#define COOP_BRIDGE_H

#define COOP_BRIDGE_VERSION	5

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
	// the host level time the guest's screen showed when it made the command GuestCmd returned
	// last (player 2's shots are checked against the world as it was then); 0 when unknown
	int			(*GuestViewTime)( void );
	// how the guest wants player 2 to look: "male", "female", or "" (like player 1, or no guest)
	const char	*(*GuestSex)( void );
	// the character the guest chose for player 2 (an id from coop_characters.h), "" for the
	// default Munro or without a guest; the game checks it before using it
	const char	*(*GuestLook)( void );
	// qtrue when the copy of qpath the game would load comes from the game's own pak0-pak2
	qboolean	(*CorePakFile)( const char *qpath );
} coopBridge_t;

extern "C" void COOP_SetBridge( const coopBridge_t *bridge );

// The replica (M1): the host writes each entity, and the level's globals, as a byte image; the
// guest (sp_coop 2) writes the host's images into its own entities. The engine moves the images
// (efcode/sys/sys_coop_net.c: changes against what the guest acknowledged, compressed). The bridge
// looks up COOP_GetReplica in the game module; a game without co-op has none.
#define COOP_REPLICA_VERSION	3

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
	// host: how often entity num's image is worth sending, in frames (1: every frame; more for one
	// player 2 can neither see nor hear, whose last image the guest keeps meanwhile)
	int			(*SendEvery)( int num );
	// guest: the time of the drawn frame player 2's newest shot was aimed in (the positions its
	// cgame traced it against), once; 0 when it fired none since the last call
	int			(*TakeShotTime)( void );
} coopReplica_t;

extern "C" const coopReplica_t *COOP_GetReplica( void );

#endif // COOP_BRIDGE_H
