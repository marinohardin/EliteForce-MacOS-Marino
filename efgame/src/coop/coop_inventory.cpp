// coop_inventory.cpp -- single-player co-op: what the two players share.
//
// Weapons and holdable items are shared: whoever picks one up, both have it. Ammo, health and armor
// go to whoever touches them first. Item targets fire once, with P1 as the activator. A script that
// gives the player a weapon gives it to both. P2's health, armor and ammo carry over a map change in
// the cvar coop_p2save (the game library is reloaded for every map, like P1's playersave cvars); a
// save keeps them in a .coop file next to the .sav, which a load reads back into the same cvar.

#include "g_local.h"
#include "coop_public.h"
#include "coop_local.h"

#define COOP_CARRY_CVAR		"coop_p2save"		// P2's state for the next level start (a map change or a load)
#define COOP_SAVE_CVAR		"coop_p2saved"		// P2's state when the game was last saved (the engine's .coop file)

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
	coop_p2.weapon = wp;				// the puppet's next command
	coop_p2.scriptWeapon = wp;			// a guest's (COOP_GuestWeapon)
	coop_p2.scriptGuestWeapon = -1;
}

/*
Script top-ups

A script that sets player 1's health or armor is the story looking after "the player" (voy13 heals
the team after the Borg missions, borg3 hands out armor with the I-MOD). Player 2 gets the same, up
to its own max; a script never lowers player 2's (a lower value is a scene's business with player 1).
These scripts usually run at the level start, before player 2 has spawned: the values wait for its
first spawn, after what it carried over.
*/
static int		coop_topUpHealth;		// waiting for P2's first spawn in this level (0: none)
static int		coop_topUpArmor;

static gentity_t *COOP_TopUpTarget( gentity_t *ent )
{
	gentity_t *p2;

	if ( !coop_enabled || ent != &g_entities[0] )
	{
		return NULL;
	}
	p2 = COOP_P2();
	if ( !p2 || p2->health <= 0 || COOP_IsDowned( p2 ) || p2->client->ps.pm_type == PM_DEAD )
	{
		return NULL;
	}
	return p2;
}

static void COOP_RaiseHealth( gentity_t *p2, int health )
{
	gclient_t *cl = p2->client;

	if ( health > cl->ps.stats[STAT_MAX_HEALTH] )
	{
		health = cl->ps.stats[STAT_MAX_HEALTH];
	}
	if ( health > p2->health )
	{
		p2->health = cl->ps.stats[STAT_HEALTH] = health;
	}
}

static void COOP_RaiseArmor( gentity_t *p2, int armor )
{
	gclient_t *cl = p2->client;

	if ( armor > cl->ps.stats[STAT_MAX_HEALTH] )
	{
		armor = cl->ps.stats[STAT_MAX_HEALTH];
	}
	if ( armor > cl->ps.stats[STAT_ARMOR] )
	{
		cl->ps.stats[STAT_ARMOR] = armor;
	}
}

void COOP_OnScriptHealth( gentity_t *ent, int health )
{
	gentity_t *p2;

	if ( !coop_enabled || ent != &g_entities[0] || health <= 0 )
	{
		return;
	}
	if ( !COOP_P2() )
	{
		coop_topUpHealth = health > coop_topUpHealth ? health : coop_topUpHealth;
	}
	else if ( ( p2 = COOP_TopUpTarget( ent ) ) != NULL )
	{
		COOP_RaiseHealth( p2, health );
	}
}

void COOP_OnScriptArmor( gentity_t *ent, int armor )
{
	gentity_t *p2;

	if ( !coop_enabled || ent != &g_entities[0] || armor <= 0 )
	{
		return;
	}
	if ( !COOP_P2() )
	{
		coop_topUpArmor = armor > coop_topUpArmor ? armor : coop_topUpArmor;
	}
	else if ( ( p2 = COOP_TopUpTarget( ent ) ) != NULL )
	{
		COOP_RaiseArmor( p2, armor );
	}
}

// P2's first spawn in a level, after COOP_ApplyCarry: what the level's scripts gave player 1 before
void COOP_ApplyScriptTopUp( gentity_t *ent )
{
	if ( coop_topUpHealth || coop_topUpArmor )
	{
		COOP_RaiseHealth( ent, coop_topUpHealth );
		COOP_RaiseArmor( ent, coop_topUpArmor );
		gi.Printf( "co-op: player 2 gets the level's script top-up (health %d, armor %d)\n",
			ent->health, ent->client->ps.stats[STAT_ARMOR] );
	}
	coop_topUpHealth = coop_topUpArmor = 0;
}

// a script beams player 1 in or out (voy15's transporter to the Dreadnought): the away team is both
void COOP_OnScriptBeam( gentity_t *ent, int event )
{
	gentity_t *p2 = COOP_TopUpTarget( ent );

	if ( p2 && !coop_p2.hidden )
	{
		gentity_t *tent = G_TempEntity( p2->client->ps.origin, event );
		tent->owner = p2;
		gi.Printf( "co-op: player 2 %s with player 1\n", event == EV_PLAYER_TELEPORT_OUT ? "beams out" : "beams in" );
	}
}

// P2's health, armor, weapon and ammo as text ("" when it has none to keep: not in the level and no
// carry waiting for its first spawn, or dead). For a save, a P2 that is down or dead (the puppet
// waiting to respawn) is kept as revived: the revive's health, its armor and ammo as they are.
static void COOP_CarryString( char *s, int size, qboolean forSave )
{
	gentity_t	*p2 = COOP_P2();
	int			health;

	s[0] = 0;
	if ( !p2 )
	{
		Q_strncpyz( s, coop_carry, size );	// a level start: what P2 brought along, not spawned yet
		return;
	}
	health = p2->client->ps.stats[STAT_HEALTH];
	if ( p2->health <= 0 || COOP_IsDowned( p2 ) || p2->client->ps.pm_type == PM_DEAD )
	{
		if ( !forSave || COOP_GameIsOver() )
		{
			return;
		}
		health = COOP_ReviveHealth( p2 );
	}
	Com_sprintf( s, size, "%i %i %i", health, p2->client->ps.stats[STAT_ARMOR],
		p2->client->ps.weapon );
	for ( int i = 0; i < AMMO_MAX; i++ )
	{
		Q_strcat( s, size, va( " %i", p2->client->ps.ammo[i] ) );
	}
}

// G_ChangeMap, after Player_CacheForNextLevel
void COOP_CacheForNextLevel( void )
{
	char	s[MAX_STRING_CHARS];

	if ( !coop_enabled )
	{
		return;
	}
	COOP_CarryString( s, sizeof( s ), qfalse );
	gi.cvar_set( COOP_CARRY_CVAR, s );
}

// WriteLevel (every co-op save): P2's state for the engine's .coop file next to the .sav (the .sav
// itself stays plain single player). Loading that save hands it back through COOP_CARRY_CVAR. A co-op
// save always has a .coop: "new" when P2 has nothing to keep (it starts afresh after the load, as it
// would without the file). Only a plain single-player save leaves the cvar empty, and the engine
// then removes an old .coop of the same name.
void COOP_CacheForSave( void )
{
	char	s[MAX_STRING_CHARS];

	COOP_CarryString( s, sizeof( s ), qtrue );
	if ( !s[0] )
	{
		Q_strncpyz( s, "new", sizeof( s ) );
	}
	gi.cvar_set( COOP_SAVE_CVAR, s );
}

// COOP_Init: take the carry left by the previous level (a load or new game starts without one)
void COOP_ReadCarry( void )
{
	coop_carry[0] = 0;
	coop_topUpHealth = coop_topUpArmor = 0;
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
