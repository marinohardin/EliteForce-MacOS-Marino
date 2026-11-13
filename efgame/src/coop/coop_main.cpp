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
	coop_replica = ( sp_coop->integer == 2 );
	coop_p2Num = -1;
	COOP_ReplicaInit();
	COOP_AIInit();
	COOP_DownInit();
	COOP_UseInit();
	memset( &coop_p2, 0, sizeof( coop_p2 ) );
	coop_p2.follow = qtrue;
	COOP_ReadCarry();
	COOP_FixesInit();
	if ( coop_enabled )
	{
		gi.Printf( "co-op: on (player 2 is a local puppet)\n" );
	}
	else if ( coop_replica )
	{
		gi.Printf( "co-op: this level mirrors the host's (player 2's view)\n" );
	}
}

static const char *coop_commands[] = {
	"coop_p2", "coop_p2_follow", "coop_p2_move", "coop_p2_turn", "coop_p2_fire", "coop_p2_use",
	"coop_p2_weapon", "coop_p2_tele", "coop_p2_damage", "coop_p2_target", "coop_p2_npcs", "coop_p2_npc", "coop_p2_aim",
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
	coop_p2_use [frames]             press use once (or hold it for frames)
	coop_p2_weapon <n>               switch to weapon n (if P2 has it)
	coop_p2_tele [x y z]             move P2 next to P1 (or to x y z)
	coop_p2_damage <n> [fall|pit] [p1] [npc] [near]  hurt P2 (p1: player 1) by n (tests: death, down, respawn;
	                                 fall: as falling damage, pit: as a trigger_hurt, npc: dealt by an NPC;
	                                 near: P2 deals it to the nearest breakable instead)
	coop_p2_target <n|npc> [amp] [ms] (tests) put entity n (or the nearest NPC) in front of P2's view, swaying side to side
	                                 amp units over a period of ms; "coop_p2_target off" lets it go
	coop_p2_aim <n>                  (tests) turn player 2's view to entity n's chest
	coop_p2_npcs [radius]            (tests) the NPCs near either player and their enemies
	coop_p2_npc spawn <type> [name]  (tests) the game's "npc" console command (spawn, kill...),
	                                 which single player doesn't reach otherwise
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
	if ( !Q_stricmp( cmd, "coop_p2_npcs" ) )
	{
		COOP_ListNPCs();
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "coop_p2_npc" ) )
	{
		extern void Svcmd_NPC_f( void );
		Svcmd_NPC_f();	// reads its arguments from 1 on, like "npc"
		return qtrue;
	}
	if ( !ent )
	{
		gi.Printf( "co-op: player 2 is not in the level (yet)\n" );
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "coop_p2" ) )
	{
		extern qboolean player_locked;
		extern bool in_camera;
		gentity_t *p1 = &g_entities[0];
		gi.Printf( "co-op: player 2 = entity %d, health %d/%d, armor %d, weapon %d (has 0x%x), origin %s, %s%s%s%s%s\n",
			ent->s.number, ent->health, ent->client->ps.stats[STAT_MAX_HEALTH], ent->client->ps.stats[STAT_ARMOR], ent->client->ps.weapon,
			ent->client->ps.stats[STAT_WEAPONS], vtos( ent->currentOrigin ),
			coop_p2.guestSteering ? "guest" : ( coop_p2.follow ? "following" : "idle" ),
			coop_p2.hidden ? ", hidden (camera)" : "", ent->client->ps.pm_type == PM_DEAD ? ", dead" : "",
			COOP_IsDowned( ent ) ? ", down" : "", ( ent->client->ps.pm_flags & PMF_DUCKED ) ? ", crouched" : "" );
		if ( p1->client )
		{
			gi.Printf( "co-op: player 1 health %d/%d, armor %d, weapon %d (has 0x%x), origin %s, distance %.0f%s%s%s%s\n",
				p1->health, p1->client->ps.stats[STAT_MAX_HEALTH], p1->client->ps.stats[STAT_ARMOR], p1->client->ps.weapon, p1->client->ps.stats[STAT_WEAPONS], vtos( p1->currentOrigin ),
				Distance( p1->currentOrigin, ent->currentOrigin ), player_locked ? ", locked" : "",
				in_camera ? ", camera" : "", COOP_IsDowned( p1 ) ? ", down" : "", ( p1->client->ps.pm_flags & PMF_DUCKED ) ? ", crouched" : "" );
			char bolts[64] = "";
			for ( int i = 0; i < MAX_BOLT_ONS; i++ )
			{
				Q_strcat( bolts, sizeof( bolts ), va( " %d/%d", p1->client->renderInfo.boltOns[i].index,
					ent->client->renderInfo.boltOns[i].index ) );
			}
			gi.Printf( "co-op: bolt-ons (player 1/player 2):%s\n", bolts );
		}
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
	else if ( !Q_stricmp( cmd, "coop_p2_aim" ) )
	{
		int n = atoi( gi.argv( 1 ) );
		if ( n > 0 && n < globals.num_entities && g_entities[n].inuse )
		{
			vec3_t eye, spot, dir;
			VectorCopy( ent->client->ps.origin, eye );
			eye[2] += ent->client->ps.viewheight;
			VectorAdd( g_entities[n].absmin, g_entities[n].absmax, spot );
			VectorScale( spot, 0.5f, spot );
			VectorSubtract( spot, eye, dir );
			vectoangles( dir, coop_p2.viewAngles );
			coop_p2.haveAngles = qtrue;
		}
	}
	else if ( !Q_stricmp( cmd, "coop_p2_fire" ) )
	{
		coop_p2.buttonFrames = ( gi.argc() > 1 ) ? atoi( gi.argv( 1 ) ) : 1;
		coop_p2.buttons = ( gi.argc() > 2 && !Q_stricmp( gi.argv( 2 ), "alt" ) ) ? BUTTON_ALT_ATTACK : BUTTON_ATTACK;
	}
	else if ( !Q_stricmp( cmd, "coop_p2_use" ) )
	{
		coop_p2.buttonFrames = ( gi.argc() > 1 ) ? atoi( gi.argv( 1 ) ) : 1;
		coop_p2.buttons = BUTTON_USE;
	}
	else if ( !Q_stricmp( cmd, "coop_p2_weapon" ) )
	{
		coop_p2.weapon = atoi( gi.argv( 1 ) );
	}
	else if ( !Q_stricmp( cmd, "coop_p2_tele" ) )
	{
		vec3_t org;
		if ( gi.argc() > 3 )
		{
			VectorSet( org, atof( gi.argv( 1 ) ), atof( gi.argv( 2 ) ), atof( gi.argv( 3 ) ) );
			COOP_PlaceP2( ent, org );
		}
		else if ( COOP_FindSpotNear( &g_entities[0], org ) )
		{
			COOP_PlaceP2( ent, org );
		}
		else
		{
			gi.Printf( "co-op: no clear spot next to player 1\n" );
		}
	}
	else if ( !Q_stricmp( cmd, "coop_p2_target" ) )
	{
		COOP_TestTargetCommand();
	}
	else if ( !Q_stricmp( cmd, "coop_p2_damage" ) )
	{
		vec3_t		dir = { 0, 0, 1 };
		gentity_t	*victim = ent;
		gentity_t	*attacker = NULL;
		int			mod = MOD_UNKNOWN;
		for ( int i = 2; i < gi.argc(); i++ )
		{
			if ( !Q_stricmp( gi.argv( i ), "fall" ) )
			{
				mod = MOD_FALLING;
			}
			else if ( !Q_stricmp( gi.argv( i ), "pit" ) )
			{
				mod = MOD_TRIGGER_HURT;
			}
			else if ( !Q_stricmp( gi.argv( i ), "p1" ) )
			{
				victim = &g_entities[0];
			}
			else if ( !Q_stricmp( gi.argv( i ), "near" ) )
			{
				// the nearest damageable thing that isn't a person (a breakable), dealt by player 2
				float best = 0;
				for ( int n = 1; n < globals.num_entities; n++ )
				{
					gentity_t	*t = &g_entities[n];
					vec3_t		mid;
					if ( !t->inuse || t->client || !t->takedamage )
					{
						continue;
					}
					VectorAdd( t->absmin, t->absmax, mid );
					VectorScale( mid, 0.5f, mid );
					float d = Distance( mid, ent->currentOrigin );
					if ( victim == ent || d < best )
					{
						victim = t;
						best = d;
					}
				}
				attacker = ent;
				gi.Printf( "co-op: damaging entity %d (%s), %.0f units from player 2\n", victim->s.number,
					victim->classname ? victim->classname : "?", best );
			}
			else if ( !Q_stricmp( gi.argv( i ), "npc" ) )
			{
				// dealt by an enemy NPC (an enemy's hit, not the level's): the first one in the level
				for ( int n = 1; n < globals.num_entities && !attacker; n++ )
				{
					if ( g_entities[n].inuse && g_entities[n].NPC && g_entities[n].client
						&& g_entities[n].client->playerTeam != TEAM_STARFLEET )
					{
						attacker = &g_entities[n];
					}
				}
			}
		}
		G_Damage( victim, attacker, attacker, dir, victim->currentOrigin, atoi( gi.argv( 1 ) ), DAMAGE_NO_KNOCKBACK | DAMAGE_NO_ARMOR | DAMAGE_NO_PROTECTION, mod );
	}
	else
	{
		gi.Printf( "co-op: unknown command %s\n", cmd );
	}
	return qtrue;
}
