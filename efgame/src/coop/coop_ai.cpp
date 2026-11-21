// coop_ai.cpp -- single-player co-op: enemies choose between the two players.
//
// The NPC code was written for one player. An enemy that took player 1 kept it for as long as
// player 1 stayed a valid target, and level scripts name the player as the enemy ("Munro", which
// only player 1 has), so in co-op enemies chased player 1 and walked past player 2, even right
// after player 2 shot them. Here an enemy fighting one player turns on the other one when that
// one is clearly the better target, and a script that names player 1 as the enemy gets the
// player that provoked the NPC (or the nearer one).

#include "g_local.h"
#include "b_local.h"
#include "coop_public.h"
#include "coop_local.h"

extern qboolean NPC_CheckDisguise( gentity_t *ent );

#define COOP_HURT_MEMORY		5000	// ms a hit is remembered when a script picks the player
#define COOP_RETHINK_TIME		500		// ms between two looks at the other player
#define COOP_SWITCH_HOLD		3000	// ms after a switch before the next one
#define COOP_SWITCH_NEARER		0.75f	// the other player must be this much nearer (distance ratio)

// per entity: when each player last hurt it, and the switch timers. Not saved: after a load the
// NPCs simply start without this memory.
static int	coop_hurtTime[MAX_GENTITIES][2];
static int	coop_rethinkTime[MAX_GENTITIES];
static int	coop_switchTime[MAX_GENTITIES];

void COOP_AIInit( void )
{
	memset( coop_hurtTime, 0, sizeof( coop_hurtTime ) );
	memset( coop_rethinkTime, 0, sizeof( coop_rethinkTime ) );
	memset( coop_switchTime, 0, sizeof( coop_switchTime ) );
}

// 0 for P1, 1 for P2, -1 for anything else
static int COOP_PlayerIndex( const gentity_t *e )
{
	if ( !e )
	{
		return -1;
	}
	if ( e == &g_entities[0] )
	{
		return 0;
	}
	return COOP_IsP2( e ) ? 1 : -1;
}

// the other player, when it can be an enemy at all: in the level, alive, drawn, targetable
static gentity_t *COOP_OtherPlayer( const gentity_t *player )
{
	int			idx = COOP_PlayerIndex( player );
	gentity_t	*other;

	if ( idx < 0 || COOP_NumPlayers() < 2 )
	{
		return NULL;
	}
	other = COOP_PlayerByIndex( idx ^ 1 );
	if ( !other || !other->inuse || !other->client || other->health <= 0
		|| ( other->flags & FL_NOTARGET ) || ( other->s.eFlags & EF_NODRAW ) )
	{
		return NULL;
	}
	return other;
}

void COOP_NoteAttacker( gentity_t *victim, gentity_t *attacker )
{
	int idx;

	if ( !coop_enabled || !victim || ( idx = COOP_PlayerIndex( attacker ) ) < 0 )
	{
		return;
	}
	coop_hurtTime[victim->s.number][idx] = level.time ? level.time : 1;
}

// hurt by player idx within the last ms
static qboolean COOP_HurtBy( const gentity_t *victim, int idx, int ms )
{
	int t = coop_hurtTime[victim->s.number][idx];
	return (qboolean)( t && level.time - t <= ms );
}

/*
COOP_ScriptEnemy

A script made self's enemy enemy (Q3_SetEnemy). When that is player 1, the script means "the
player": in co-op that is the player who hurt self last (pain scripts: "attack the one who shot
me"), else the player self already fights, else player 1 unless player 2 is clearly nearer.
*/
gentity_t *COOP_ScriptEnemy( gentity_t *self, gentity_t *enemy )
{
	gentity_t	*p2;

	if ( !coop_enabled || enemy != &g_entities[0] || !self->client )
	{
		return enemy;
	}
	p2 = COOP_OtherPlayer( enemy );
	if ( !p2 )
	{
		return enemy;
	}
	qboolean byP1 = COOP_HurtBy( self, 0, COOP_HURT_MEMORY );
	qboolean byP2 = COOP_HurtBy( self, 1, COOP_HURT_MEMORY );
	if ( byP1 || byP2 )
	{
		if ( byP1 && byP2 )
		{
			return coop_hurtTime[self->s.number][1] > coop_hurtTime[self->s.number][0] ? p2 : enemy;
		}
		return byP2 ? p2 : enemy;
	}
	if ( self->enemy == p2 )
	{
		return p2;
	}
	if ( enemy->health <= 0 )
	{
		return p2;
	}
	// player 2 only when it is clearly the nearer one. The scene was staged around player 1, and
	// player 2 is placed right next to it after a camera: "a little nearer" is often just the side
	// player 2 landed on, out in front of the squad that screens player 1 (forge2's reaver ambush:
	// one reaver took player 2 at 351 units against 378 and shot it unopposed). If player 2 is the
	// one it can see, or gets clearly nearer later, COOP_RethinkPlayerEnemy switches.
	float dP1 = Distance( self->currentOrigin, enemy->currentOrigin );
	float dP2 = Distance( self->currentOrigin, p2->currentOrigin );
	return dP2 < dP1 * COOP_SWITCH_NEARER ? p2 : enemy;
}

// NPC (the one thinking) can see ent: in range, in the PVS and a clear line from its eyes
static qboolean COOP_NPCSees( gentity_t *ent )
{
	float range = NPCInfo->stats.visrange;

	if ( range > 0 && DistanceSquared( NPC->currentOrigin, ent->currentOrigin ) > range * range )
	{
		return qfalse;
	}
	if ( !gi.inPVS( NPC->currentOrigin, ent->currentOrigin ) )
	{
		return qfalse;
	}
	return NPC_ClearLOS( ent );
}

/*
COOP_TakePlayerEnemy

NPC turns from the player it fights to the other one, keeping its chase on the new one.
*/
void COOP_TakePlayerEnemy( gentity_t *other )
{
	gentity_t	*old = NPC->enemy;
	qboolean	chased = (qboolean)( NPCInfo && old && NPCInfo->goalEntity == old );

	if ( !other || other == old )
	{
		return;
	}
	G_SetEnemy( NPC, other );
	if ( NPC->enemy != other )
	{
		return;
	}
	NPC->cantHitEnemyCounter = 0;
	if ( chased )
	{
		NPCInfo->goalEntity = other;
	}
	coop_switchTime[NPC->s.number] = level.time;
}

/*
COOP_RethinkPlayerEnemy

For the NPC thinking now (NPC_CheckEnemy, NPC_FindEnemy): when it fights one player and the
other one is clearly the better target, take that one. Better: the other player is in sight
and either the current one is not, or the other is a good deal nearer. A few times a second,
and not again for a while after a switch, so NPCs don't flip back and forth.
*/
void COOP_RethinkPlayerEnemy( void )
{
	gentity_t	*enemy, *other;
	int			num;

	if ( !coop_enabled || !NPC || !NPCInfo || !NPC->client )
	{
		return;
	}
	enemy = NPC->enemy;
	if ( !enemy || COOP_PlayerIndex( enemy ) < 0 )
	{
		return;
	}
	num = NPC->s.number;
	if ( level.time < coop_rethinkTime[num] || level.time - coop_switchTime[num] < COOP_SWITCH_HOLD )
	{
		return;
	}
	coop_rethinkTime[num] = level.time + COOP_RETHINK_TIME;

	other = COOP_OtherPlayer( enemy );
	if ( !other || !other->client || other->client->playerTeam == NPC->client->playerTeam || NPC_CheckDisguise( other ) )
	{
		return;
	}
	if ( !COOP_NPCSees( other ) )
	{
		return;
	}
	float dEnemy = Distance( NPC->currentOrigin, enemy->currentOrigin );
	float dOther = Distance( NPC->currentOrigin, other->currentOrigin );
	if ( dOther < dEnemy * COOP_SWITCH_NEARER || enemy->health <= 0 || !COOP_NPCSees( enemy ) )
	{
		COOP_TakePlayerEnemy( other );
	}
}

/*
coop_p2_npcs [radius]: (tests) the NPCs near either player, with their enemies
*/
void COOP_ListNPCs( void )
{
	gentity_t	*p2 = COOP_P2();
	float		radius = ( gi.argc() > 1 ) ? atof( gi.argv( 1 ) ) : 2048;
	int			count = 0;

	for ( int i = 1; i < globals.num_entities; i++ )
	{
		gentity_t *ent = &g_entities[i];
		if ( !ent->inuse || !ent->NPC || !ent->client )
		{
			continue;
		}
		float d1 = Distance( ent->currentOrigin, g_entities[0].currentOrigin );
		float d2 = p2 ? Distance( ent->currentOrigin, p2->currentOrigin ) : -1;
		if ( d1 > radius && ( d2 < 0 || d2 > radius ) )
		{
			continue;
		}
		int enemyNum = ent->enemy ? ent->enemy->s.number : -1;
		gi.Printf( "co-op npc %d %s '%s' hp %d team %d enemyTeam %d bs %d enemy %d%s d1 %.0f d2 %.0f at %s%s%s\n",
			i, ent->NPC_type ? ent->NPC_type : "?", ent->targetname ? ent->targetname : "",
			ent->health, ent->client->playerTeam, ent->client->enemyTeam, ent->NPC->behaviorState, enemyNum,
			enemyNum == 0 ? " (P1)" : ( p2 && enemyNum == p2->s.number ) ? " (P2)" : "",
			d1, d2, vtos( ent->currentOrigin ), ( ent->svFlags & SVF_IGNORE_ENEMIES ) ? " ignoring" : "", ( ent->svFlags & SVF_LOCKEDENEMY ) ? " locked" : "" );
		count++;
	}
	gi.Printf( "co-op: %d NPCs within %.0f\n", count, radius );
}
