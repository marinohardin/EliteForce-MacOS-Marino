// coop_public.h -- single-player co-op: who "the player" is.
//
// Include after g_local.h (or cg_local.h). Player 1 is always entity 0, the story player.
// Player 2 is an extra client entity whose number lives in the co-op module (never in a
// gentity_t/gclient_t field: saves write those structs whole).
//
// Without EF_COOP every helper below is the exact expression it replaced, and every hook is
// nothing. With EF_COOP and sp_coop 0 the helpers return the same values (there is no P2).

#ifndef COOP_PUBLIC_H
#define COOP_PUBLIC_H

#ifndef EF_COOP

#define COOP_Active()				0
#define COOP_IsReplica()			0
#define COOP_IsP2(e)				0
#define COOP_IsPlayer(e)			((e)->s.number == 0)
#define COOP_IsPlayerNum(n)			((n) == 0)
#define COOP_IsPlayerPtr(e)			((e) == &g_entities[0])
#define COOP_StoryActivator(e)		(e)
#define COOP_ThirdPerson(e)			cg_thirdPerson.integer
#define COOP_ViewClientNum()		0
#define COOP_NearestPlayer(org)		(&g_entities[0])
#define COOP_NumPlayers()			1
#define COOP_PlayerByIndex(i)		(&g_entities[0])
#define COOP_AnyPlayerInPVS(org)	gi.inPVS( g_entities[0].currentOrigin, (org) )
#define COOP_AnyPlayerInPVSRev(org)	gi.inPVS( (org), g_entities[0].currentOrigin )

#define COOP_Init()					((void)0)
#define COOP_RunFrame()				((void)0)
#define COOP_EndFrame()				((void)0)
#define COOP_P2ThinkGate(e,u)		0
#define COOP_P2DeadThink(e,u)		((void)0)
#define COOP_P2Die(e,a)				((void)0)
#define COOP_PreSave()				((void)0)
#define COOP_OnCameraEnable()		((void)0)
#define COOP_OnCameraDisable()		((void)0)
#define COOP_ConsoleCommand(c)		0
#define COOP_RegisterCommands()		((void)0)
#define COOP_FindPlayerWaypoints()	((void)0)
#define COOP_OnPickup(i,p)			((void)0)
#define COOP_OnScriptWeapon(e,w)	((void)0)
#define COOP_CacheForNextLevel()	((void)0)

#else // EF_COOP

extern int	coop_enabled;		// sp_coop 1 at InitGame
extern int	coop_p2Num;			// P2's entity number, -1 when there is none
extern int	coop_replica;		// sp_coop 2 at InitGame: a guest's copy of the host's level

// this level is a guest's replica: no game frames run here, the host's entities are written in
inline int COOP_IsReplica( void ) { return coop_replica; }

// co-op is on for this level (P2 may not have spawned yet)
inline int COOP_Active( void ) { return coop_enabled; }

// e is player 2
inline bool COOP_IsP2( const gentity_t *e ) { return coop_p2Num >= 0 && e->s.number == coop_p2Num; }

// e is either player
inline bool COOP_IsPlayer( const gentity_t *e ) { return e->s.number == 0 || COOP_IsP2( e ); }

// a client number (ps->clientNum) is either player; works where there is no gentity (cgame prediction)
inline bool COOP_IsPlayerNum( int n ) { return n == 0 || ( coop_p2Num >= 0 && n == coop_p2Num ); }

// for code that compared pointers with &g_entities[0] (a freed entity also has s.number 0)
inline bool COOP_IsPlayerPtr( const gentity_t *e ) { return e == &g_entities[0] || ( e && COOP_IsP2( e ) ); }

// story scripts expect "the player" as activator: P2 becomes P1
inline gentity_t *COOP_StoryActivator( gentity_t *e ) { return ( e && COOP_IsP2( e ) ) ? &g_entities[0] : e; }

// the client whose view this machine draws: P1 on the host, P2 in a guest's replica
inline int COOP_ViewClientNum( void ) { return ( coop_replica && coop_p2Num >= 0 ) ? coop_p2Num : 0; }

// the closer of the players that are in the level (P2 only when alive and not hidden)
gentity_t	*COOP_NearestPlayer( const vec3_t org );
// loop over the players in the level: index 0 is P1, 1 is P2 (alive and not hidden)
int			COOP_NumPlayers( void );
gentity_t	*COOP_PlayerByIndex( int i );

// a player has org in its PVS (Rev: the old code passed the two points the other way round)
qboolean	COOP_AnyPlayerInPVS( const vec3_t org );
qboolean	COOP_AnyPlayerInPVSRev( const vec3_t org );
// NAV_FindPlayerWaypoint: keep P2's waypoint current too (NPCs chasing P2 path to it)
void		COOP_FindPlayerWaypoints( void );

// Touch_Item after a pickup: weapons and holdables go to the other player too
void		COOP_OnPickup( gentity_t *item, gentity_t *picker );
// the game started a sound itself, not through an entity event (the guest's replica plays it too)
void		COOP_OnDirectSound( int entNum, int channel, const char *path );
// Q3_SetWeapon gave P1 a weapon: P2 gets it too
void		COOP_OnScriptWeapon( gentity_t *ent, int wp );
// G_ChangeMap: keep P2's health, armor and ammo for the next level
void		COOP_CacheForNextLevel( void );

// the shooter's third-person setting (game code reads the host cgame's cvar for it)
int			COOP_ThirdPerson( const gentity_t *e );

// level start (InitGame)
void		COOP_Init( void );
// top of G_RunFrame: spawn P2 next to P1 if it is missing, then run its ClientThink
void		COOP_RunFrame( void );
// after P1's ClientEndFrame
void		COOP_EndFrame( void );
// ClientThink_real for P2: freeze it in cameras and while the player is locked. qtrue = stop here
qboolean	COOP_P2ThinkGate( gentity_t *ent, usercmd_t *ucmd );
// ClientThink_real for a dead P2 (instead of the player respawn path, which reloads the game)
void		COOP_P2DeadThink( gentity_t *ent, usercmd_t *ucmd );
// player_die for P2: no game over, respawn next to P1 after a delay
void		COOP_P2Die( gentity_t *ent, gentity_t *attacker );
// WriteLevel: clear every saved pointer to P2 (P2 itself is never written)
void		COOP_PreSave( void );
// CGCam_Enable / CGCam_Disable: P2 hidden and non-solid while a camera runs
void		COOP_OnCameraEnable( void );
void		COOP_OnCameraDisable( void );
// the coop_p2* console commands: the cgame registers them (CG_Init), the engine hands them to
// the game's ClientCommand, which calls this. qtrue = handled
void		COOP_RegisterCommands( void );
qboolean	COOP_ConsoleCommand( const char *cmd );

#endif // EF_COOP

#endif // COOP_PUBLIC_H
