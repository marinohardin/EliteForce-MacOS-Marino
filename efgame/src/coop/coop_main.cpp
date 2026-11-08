// coop_main.cpp -- single-player co-op: the sp_coop switch and per-level state.
//
// Built only with EF_COOP (efgame/CMakeLists.txt). Everything here is inert unless sp_coop is 1
// when a level starts (the cvar is latched).

#include "g_local.h"
#include "coop_public.h"
#include "coop_local.h"

int		coop_enabled = 0;
int		coop_p2Num = -1;

static cvar_t	*sp_coop;

/*
COOP_Init

Called from InitGame for every level (new game, load, map change). The entity array was just
cleared, so P2 does not exist yet.
*/
void COOP_Init( void )
{
	sp_coop = gi.cvar( "sp_coop", "0", CVAR_LATCH );
	coop_enabled = ( sp_coop->integer == 1 );
	coop_p2Num = -1;
	memset( &coop_p2, 0, sizeof( coop_p2 ) );
	coop_p2.follow = qtrue;
	if ( coop_enabled )
	{
		gi.Printf( "co-op: on (player 2 is a local puppet)\n" );
	}
}

static const char *coop_commands[] = {
	"coop_p2", "coop_p2_follow", "coop_p2_move", "coop_p2_turn", "coop_p2_fire", "coop_p2_use",
	"coop_p2_weapon", "coop_p2_tele", "coop_p2_damage",
};

extern void cgi_AddCommand( const char *cmdName );

void COOP_RegisterCommands( void )
{
	if ( !coop_enabled )
	{
		return;
	}
	for ( unsigned i = 0; i < sizeof( coop_commands ) / sizeof( coop_commands[0] ); i++ )
	{
		cgi_AddCommand( coop_commands[i] );
	}
}

/*
COOP_ConsoleCommand

The M0 puppet controls (host console; reached through the game's ClientCommand):
	coop_p2                          where P2 is and what it is doing
	coop_p2_follow 0|1               follow P1 when no move is running (default 1)
	coop_p2_move <fwd> <right> [up] [frames]   -127..127 each, for frames (default 20 = 1 s)
	coop_p2_turn <yaw> [pitch]       hold this view; "coop_p2_turn off" lets follow aim again
	coop_p2_fire [frames] [alt]      hold attack (alt: alt attack)
	coop_p2_use                      press use once
	coop_p2_weapon <n>               switch to weapon n (if P2 has it)
	coop_p2_tele                     move P2 next to P1
	coop_p2_damage <n>               hurt P2 by n (test death and respawn)
*/
qboolean COOP_ConsoleCommand( const char *cmd )
{
	if ( !coop_enabled || Q_stricmpn( cmd, "coop_p2", 7 ) )
	{
		return qfalse;
	}
	gentity_t *ent = COOP_P2();

	if ( !Q_stricmp( cmd, "coop_p2_follow" ) )
	{
		coop_p2.follow = ( gi.argc() < 2 ) ? !coop_p2.follow : ( atoi( gi.argv( 1 ) ) != 0 );
		gi.Printf( "co-op: player 2 follow %s\n", coop_p2.follow ? "on" : "off" );
		return qtrue;
	}
	if ( !ent )
	{
		gi.Printf( "co-op: player 2 is not in the level (yet)\n" );
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "coop_p2" ) )
	{
		gi.Printf( "co-op: player 2 = entity %d, health %d, armor %d, weapon %d, origin %s, %s%s%s\n",
			ent->s.number, ent->health, ent->client->ps.stats[STAT_ARMOR], ent->client->ps.weapon,
			vtos( ent->currentOrigin ), coop_p2.follow ? "following" : "idle",
			coop_p2.hidden ? ", hidden (camera)" : "", ent->client->ps.pm_type == PM_DEAD ? ", dead" : "" );
	}
	else if ( !Q_stricmp( cmd, "coop_p2_move" ) )
	{
		coop_p2.forwardmove = (signed char)Com_Clamp( -127, 127, atoi( gi.argv( 1 ) ) );
		coop_p2.rightmove = (signed char)Com_Clamp( -127, 127, atoi( gi.argv( 2 ) ) );
		coop_p2.upmove = (signed char)Com_Clamp( -127, 127, atoi( gi.argv( 3 ) ) );
		coop_p2.moveFrames = ( gi.argc() > 4 ) ? atoi( gi.argv( 4 ) ) : 20;
	}
	else if ( !Q_stricmp( cmd, "coop_p2_turn" ) )
	{
		if ( !Q_stricmp( gi.argv( 1 ), "off" ) )
		{
			coop_p2.haveAngles = qfalse;
		}
		else
		{
			coop_p2.haveAngles = qtrue;
			VectorSet( coop_p2.viewAngles, atof( gi.argv( 2 ) ), atof( gi.argv( 1 ) ), 0 );
		}
	}
	else if ( !Q_stricmp( cmd, "coop_p2_fire" ) )
	{
		coop_p2.buttonFrames = ( gi.argc() > 1 ) ? atoi( gi.argv( 1 ) ) : 1;
		coop_p2.buttons = ( gi.argc() > 2 && !Q_stricmp( gi.argv( 2 ), "alt" ) ) ? BUTTON_ALT_ATTACK : BUTTON_ATTACK;
	}
	else if ( !Q_stricmp( cmd, "coop_p2_use" ) )
	{
		coop_p2.buttonFrames = 1;
		coop_p2.buttons = BUTTON_USE;
	}
	else if ( !Q_stricmp( cmd, "coop_p2_weapon" ) )
	{
		coop_p2.weapon = atoi( gi.argv( 1 ) );
	}
	else if ( !Q_stricmp( cmd, "coop_p2_tele" ) )
	{
		vec3_t org;
		if ( COOP_FindSpotNear( &g_entities[0], org ) )
		{
			COOP_PlaceP2( ent, org );
		}
		else
		{
			gi.Printf( "co-op: no clear spot next to player 1\n" );
		}
	}
	else if ( !Q_stricmp( cmd, "coop_p2_damage" ) )
	{
		vec3_t dir = { 0, 0, 1 };
		G_Damage( ent, NULL, NULL, dir, ent->currentOrigin, atoi( gi.argv( 1 ) ), DAMAGE_NO_KNOCKBACK | DAMAGE_NO_ARMOR, MOD_UNKNOWN );
	}
	else
	{
		gi.Printf( "co-op: unknown command %s\n", cmd );
	}
	return qtrue;
}
