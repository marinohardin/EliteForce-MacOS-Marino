// coop_inventory.cpp -- single-player co-op: what the two players share.
//
// Weapons and holdable items are shared: whoever picks one up, both have it. Ammo, health and armor
// go to whoever touches them first. Item targets fire once, with P1 as the activator. A script that
// gives the player a weapon gives it to both. P2's health, armor and ammo carry over a map change in
// the cvar coop_p2save (the game library is reloaded for every map, like P1's playersave cvars).

#include "g_local.h"
#include "coop_public.h"
#include "coop_local.h"

#define COOP_CARRY_CVAR		"coop_p2save"

static char		coop_carry[MAX_STRING_CHARS];	// read at InitGame, used by P2's first spawn

void COOP_OnPickup( gentity_t *item, gentity_t *picker )
{
	gentity_t	*p2 = COOP_P2();
	gentity_t	*other;

	if ( !coop_enabled || !p2 || !item->item )
	{
		return;
	}
	other = COOP_IsP2( picker ) ? &g_entities[0] : ( picker == &g_entities[0] ? p2 : NULL );
	if ( !other || !other->client )
	{
		return;
	}
	switch ( item->item->giType )
	{
	case IT_WEAPON:
		other->client->ps.stats[STAT_WEAPONS] |= ( 1 << item->item->giTag );
		break;
	case IT_HOLDABLE:
		other->client->ps.stats[STAT_HOLDABLE_ITEM] = item->item - bg_itemlist;
		break;
	default:
		break;
	}
}

void COOP_OnScriptWeapon( gentity_t *ent, int wp )
{
	gentity_t	*p2 = COOP_P2();

	if ( !coop_enabled || !p2 || ent != &g_entities[0] )
	{
		return;
	}
	playerState_t *ps = &p2->client->ps;
	ps->stats[STAT_WEAPONS] &= ~( 1 << WP_BLUE_HYPO );
	ps->stats[STAT_WEAPONS] &= ~( 1 << WP_RED_HYPO );
	ps->stats[STAT_WEAPONS] |= ( 1 << wp );
	ps->ammo[weaponData[wp].ammoIndex] = ammoData[weaponData[wp].ammoIndex].max;
	coop_p2.weapon = wp;
}

// G_ChangeMap, after Player_CacheForNextLevel
void COOP_CacheForNextLevel( void )
{
	gentity_t	*p2 = COOP_P2();

	if ( !coop_enabled )
	{
		return;
	}
	if ( !p2 || p2->health <= 0 )
	{
		gi.cvar_set( COOP_CARRY_CVAR, "" );
		return;
	}
	char	s[MAX_STRING_CHARS];
	Com_sprintf( s, sizeof( s ), "%i %i %i", p2->client->ps.stats[STAT_HEALTH], p2->client->ps.stats[STAT_ARMOR],
		p2->client->ps.weapon );
	for ( int i = 0; i < AMMO_MAX; i++ )
	{
		Q_strcat( s, sizeof( s ), va( " %i", p2->client->ps.ammo[i] ) );
	}
	gi.cvar_set( COOP_CARRY_CVAR, s );
}

// COOP_Init: take the carry left by the previous level (a load or new game starts without one)
void COOP_ReadCarry( void )
{
	coop_carry[0] = 0;
	if ( !coop_enabled )
	{
		return;
	}
	gi.Cvar_VariableStringBuffer( COOP_CARRY_CVAR, coop_carry, sizeof( coop_carry ) );
	gi.cvar_set( COOP_CARRY_CVAR, "" );
}

// P2's first spawn in a level: restore what it had when the previous level ended
void COOP_ApplyCarry( gentity_t *ent )
{
	int		health, armor, weapon;
	int		n;
	const char *p = coop_carry;

	if ( !coop_carry[0] )
	{
		return;
	}
	if ( sscanf( p, "%i %i %i%n", &health, &armor, &weapon, &n ) == 3 && health > 0 )
	{
		gclient_t *cl = ent->client;

		p += n;
		for ( int i = 0; i < AMMO_MAX; i++ )
		{
			int v;
			if ( sscanf( p, "%i%n", &v, &n ) != 1 )
			{
				break;
			}
			cl->ps.ammo[i] = v;
			p += n;
		}
		ent->health = cl->ps.stats[STAT_HEALTH] = health;
		cl->ps.stats[STAT_ARMOR] = armor;
		if ( weapon > WP_NONE && weapon < WP_NUM_WEAPONS && ( cl->ps.stats[STAT_WEAPONS] & ( 1 << weapon ) ) )
		{
			cl->ps.weapon = weapon;
		}
		gi.Printf( "co-op: player 2 carried over (health %d, armor %d)\n", health, armor );
	}
	coop_carry[0] = 0;
}
