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
#define COOP_IsP2(e)				0
#define COOP_IsPlayer(e)			((e)->s.number == 0)
#define COOP_IsPlayerNum(n)			((n) == 0)
#define COOP_StoryActivator(e)		(e)

#define COOP_Init()					((void)0)

#else // EF_COOP

extern int	coop_enabled;		// sp_coop 1 at InitGame
extern int	coop_p2Num;			// P2's entity number, -1 when there is none

// co-op is on for this level (P2 may not have spawned yet)
inline int COOP_Active( void ) { return coop_enabled; }

// e is player 2
inline bool COOP_IsP2( const gentity_t *e ) { return coop_p2Num >= 0 && e->s.number == coop_p2Num; }

// e is either player
inline bool COOP_IsPlayer( const gentity_t *e ) { return e->s.number == 0 || COOP_IsP2( e ); }

// a client number (ps->clientNum) is either player; works where there is no gentity (cgame prediction)
inline bool COOP_IsPlayerNum( int n ) { return n == 0 || ( coop_p2Num >= 0 && n == coop_p2Num ); }

// story scripts expect "the player" as activator: P2 becomes P1
inline gentity_t *COOP_StoryActivator( gentity_t *e ) { return ( e && COOP_IsP2( e ) ) ? &g_entities[0] : e; }

void		COOP_Init( void );

#endif // EF_COOP

#endif // COOP_PUBLIC_H
