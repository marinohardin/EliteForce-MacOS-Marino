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
#define COOP_PostSave()				((void)0)
#define COOP_OnCameraEnable()		((void)0)
#define COOP_OnCameraDisable()		((void)0)
#define COOP_ConsoleCommand(c)		0
#define COOP_P2Notarget(e)			((void)0)
#define COOP_RegisterCommands()		((void)0)
#define COOP_FindPlayerWaypoints()	((void)0)
#define COOP_OnPickup(i,p)			((void)0)
#define COOP_OnScriptWeapon(e,w)	((void)0)
#define COOP_OnScriptOrigin(e)		((void)0)
#define COOP_OnScriptHealth(e,h)	((void)0)
#define COOP_OnScriptBeam(e,ev)		((void)0)
#define COOP_P2Trigger(t,o)			0
#define COOP_OnScriptArmor(e,a)		((void)0)
#define COOP_CacheForNextLevel()	((void)0)
#define COOP_ShotStart(e,a)			((void)0)
#define COOP_ShotEnd()				((void)0)
#define COOP_TagTempEntity(e)		((void)0)
#define COOP_WeaponRandom()			random()
#define COOP_WeaponCrandom()		crandom()
#define COOP_WeaponIrand(a,b)		Q_irand( (a), (b) )
#define COOP_AddPredictedMissiles()	((void)0)
#define COOP_MissileLerp(c)			((void)0)
#define COOP_PredictShot(c,a)		((void)0)
#define COOP_SkipHostShot(s,e)		0
#define COOP_NoteViewMuzzle(o)		((void)0)
#define COOP_PredictingEvents(p)	((void)0)
#define COOP_SkipPlayerstateEvent(e)	0
#define COOP_SkipEntityEvent(n,e)	0
#define COOP_HeldBeam(es)			0
#define COOP_ServerCommand(c)		0
#define COOP_DrawStatus()			((void)0)
#define COOP_PlayerDown(e,a,m)		0
#define COOP_IsDowned(e)			0
#define COOP_AnyoneDown()			0
#define COOP_ReviveAll()			((void)0)
#define COOP_UseButtons(e,u)		((void)0)
#define COOP_NoteAttacker(v,a)		((void)0)
#define COOP_ScriptEnemy(s,e)		(e)
#define COOP_RethinkPlayerEnemy()	((void)0)
#define COOP_EVERYONE				NULL

#else // EF_COOP

extern int	coop_enabled;		// sp_coop 1 at InitGame
extern int	coop_p2Num;			// P2's entity number, -1 when there is none
extern int	coop_replica;		// sp_coop 2 at InitGame: a guest's copy of the host's level

// the client a script's story text (centre prints, subtitles, voice text) goes to: both players.
// Without co-op it is NULL, which reads as client 0, the same as a message for player 1 only.
#define COOP_EVERYONE	-1

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
// Q3_SetHealth / Q3_SetArmor raised P1's: P2's is raised as far (also when P2 spawns later in the level)
void		COOP_OnScriptHealth( gentity_t *ent, int health );
void		COOP_OnScriptArmor( gentity_t *ent, int armor );
// Q3_SetEvent beamed P1 in or out (the transporter shimmer): P2 shimmers too
void		COOP_OnScriptBeam( gentity_t *ent, int event );
// Touch_Multi: player 2 set off a trigger the level meant for player 1 alone; the map's fix handled it
// instead (coop_fixes.cpp). qtrue: the trigger doesn't fire
qboolean	COOP_P2Trigger( gentity_t *trigger, gentity_t *other );
// Q3_SetOrigin moved an entity (coop_fixes.cpp): player 1 moved by a script takes player 2 along
void		COOP_OnScriptOrigin( gentity_t *ent );
// G_ChangeMap: keep P2's health, armor and ammo for the next level
void		COOP_CacheForNextLevel( void );

// FireWeapon (coop_lagcomp.cpp): player 2's instant-hit shots, steered by a guest, are traced against
// the world as the guest saw it; its temp entities name it (COOP_TagTempEntity, from G_TempEntity)
void		COOP_ShotStart( gentity_t *ent, qboolean alt );
void		COOP_ShotEnd( void );
void		COOP_TagTempEntity( gentity_t *tent );
// the random numbers of a shot's spread (the scavenger rifle's, the tetrion's): while a guest's
// shot of player 2 is fired (host) or predicted (guest) they come from a sequence seeded with the
// shot's view time, so both draw the same spread; otherwise random(), crandom(), Q_irand()
void		COOP_SeedShot( int seed );
void		COOP_UnseedShot( void );
float		COOP_WeaponRandom( void );
float		COOP_WeaponCrandom( void );
int			COOP_WeaponIrand( int min, int max );

// the guest's cgame (coop_predict.cpp): player 2's shots drawn when the guest fires
// (CG_FireWeapon), the host's effects of them left out (CG_EntityEvent), and where the view
// weapon's flash is (CG_AddViewWeapon)
void		COOP_PredictShot( centity_t *cent, qboolean alt );
qboolean	COOP_SkipHostShot( const entityState_t *es, int event );
void		COOP_NoteViewMuzzle( const vec3_t origin );
// the guest's projectiles of player 2 drawn from the moment it fires until the host's arrive
// (CG_AddPacketEntities), the host's then eased onto the drawn path (CG_Missile), and the view
// weapon's firing animation run from the guest's own shots (CG_AddViewWeapon)
void		COOP_AddPredictedMissiles( void );
void		COOP_MissileLerp( centity_t *cent );
void		COOP_ViewWeaponFrames( clientInfo_t *ci, centity_t *cent, int *frame, int *oldframe, float *backlerp );
// a replica's player events: the ones its own moves make come from its prediction, the rest from the host
void		COOP_PredictingEvents( qboolean predicting );
qboolean	COOP_SkipPlayerstateEvent( int event );
qboolean	COOP_SkipEntityEvent( int num, int event );

// the other player's held beam (phaser, dreadnought's main fire): drawn every frame it is held, as
// the view weapon's is, not only in the instant after each fire event (CG_Player)
inline bool COOP_HeldBeam( const entityState_t *es )
{
	return coop_p2Num >= 0 && COOP_IsPlayerNum( es->number ) && ( es->eFlags & EF_FIRING )
		&& ( es->weapon == WP_PHASER || ( es->weapon == WP_DREADNOUGHT && !( es->eFlags & EF_ALT_FIRING ) ) );
}

// the cgame's co-op status text (coop_hud.cpp): CG_ServerCommand hands it "coopmsg" (qtrue:
// taken), CG_Draw2D draws it
qboolean	COOP_ServerCommand( const char *cmd );
void		COOP_DrawStatus( void );

// downed players (coop_down.cpp): player_die asks COOP_PlayerDown first (qtrue: the player went
// down instead, or player 2's death was handled); a downed player is frozen (ClientThink_real) and
// can't respawn; saving waits until nobody is down; a level change revives them first
qboolean	COOP_PlayerDown( gentity_t *ent, gentity_t *attacker, int mod );
qboolean	COOP_IsDowned( const gentity_t *ent );
qboolean	COOP_AnyoneDown( void );
void		COOP_ReviveAll( void );

// ClientThink_real, after the buttons were taken (coop_use.cpp): Use with nothing to use: player 2's
// hold teleports it to player 1, a double tap (either player) pings the aimed spot for both
void		COOP_UseButtons( gentity_t *ent, usercmd_t *ucmd );

// enemies and the two players (coop_ai.cpp): NPC_Pain notes which player hurt an NPC; a script
// that names player 1 as an NPC's enemy gets the player that provoked it (or the nearer one,
// Q3_SetEnemy); an NPC fighting one player turns on the other when that one is clearly the
// better target (NPC_CheckEnemy, NPC_FindEnemy; uses the NPC globals)
void		COOP_NoteAttacker( gentity_t *victim, gentity_t *attacker );
gentity_t	*COOP_ScriptEnemy( gentity_t *self, gentity_t *enemy );
void		COOP_RethinkPlayerEnemy( void );
void		COOP_TakePlayerEnemy( gentity_t *other );

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
// WriteLevel: clear every saved pointer to P2 (P2 itself is never written); a downed player 1 is
// written as revived, put back down by COOP_PostSave at the end of WriteLevel
void		COOP_PreSave( void );
void		COOP_PostSave( void );
// CGCam_Enable / CGCam_Disable: P2 hidden and non-solid while a camera runs
void		COOP_OnCameraEnable( void );
void		COOP_OnCameraDisable( void );
// the coop_p2* console commands: the cgame registers them (CG_Init), the engine hands them to
// the game's ClientCommand, which calls this. qtrue = handled
void		COOP_RegisterCommands( void );
qboolean	COOP_ConsoleCommand( const char *cmd );
// the notarget cheat for player 2 (typed on the guest, run by the host's game): kept apart from
// the notarget that hiding and going down give it, so neither undoes the other
void		COOP_P2Notarget( gentity_t *ent );

#endif // EF_COOP

#endif // COOP_PUBLIC_H
