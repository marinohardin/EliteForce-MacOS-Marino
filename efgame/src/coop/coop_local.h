// coop_local.h -- single-player co-op: declarations shared by the files in src/coop/ only.

#ifndef COOP_LOCAL_H
#define COOP_LOCAL_H

// P2's classname. The pointer itself marks the entity as ours, so a slot that was freed and
// reused by something else is never mistaken for P2.
extern const char	coop_p2Classname[];

// P2 state that must not live in gentity_t / gclient_t (both are saved whole)
typedef struct
{
	int			levelFrames;		// game frames since this level started
	int			respawnTime;		// dead P2 comes back next to P1 at this level.time
	int			nextSpawnTry;		// no clear spot next to P1 yet: try again at this time
	qboolean	hidden;				// a camera is running: P2 is not drawn and not solid
	qboolean	follow;				// follow P1 when no console move is running
	int			stuckFrames;		// follow: frames without progress
	vec3_t		lastOrigin;

	// console control (coop_p2_move / coop_p2_turn / coop_p2_fire / coop_p2_use)
	int			moveFrames;
	signed char	forwardmove, rightmove, upmove;
	int			buttonFrames;
	int			buttons;
	qboolean	haveAngles;
	vec3_t		viewAngles;
	int			weapon;				// > 0: switch to it

	// the guest's input (coop_net.cpp) steers P2; its view angles were re-based on the guest's
	qboolean	guestSteering;
} coopP2State_t;

extern coopP2State_t	coop_p2;

gentity_t	*COOP_P2( void );						// P2's entity or NULL
qboolean	COOP_FindSpotNear( gentity_t *p1, vec3_t out );
void		COOP_PlaceP2( gentity_t *ent, const vec3_t org );	// move without a lerp
void		COOP_SpawnP2( void );
void		COOP_ApplyHidden( gentity_t *ent, qboolean hidden );
void		COOP_ReadCarry( void );						// coop_inventory.cpp
void		COOP_ApplyCarry( gentity_t *ent );
void		COOP_PublishInfo( void );					// coop_info.cpp
qboolean	COOP_GuestCmd( usercmd_t *cmd );			// coop_net.cpp
const char	*COOP_GuestName( void );

#endif // COOP_LOCAL_H
