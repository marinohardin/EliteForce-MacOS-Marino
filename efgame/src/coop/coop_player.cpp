// coop_player.cpp -- single-player co-op: the player 2 entity.
//
// P2 is a client entity built like an NPC (its own client struct, driven through ClientThink)
// but with no NPC struct, no ICARUS sequencer and no script name: scripts find the player by
// the name "Munro", which only P1 has. In M0 it is a local puppet: it follows P1, or the
// coop_p2_* console commands move it.

#include "g_local.h"
#include "g_functions.h"
#include "anims.h"
#include "boltons.h"
#include "coop_public.h"
#include "coop_local.h"
#include "g_nav.h"
#include "g_navigator.h"

extern CNavigator	navigator;

extern vec3_t	playerMins;
extern vec3_t	playerMaxs;
extern bool		in_camera;
extern qboolean	player_locked;
extern void		NPC_SetAnim( gentity_t *ent, int type, int anim, int priority );
extern void		PM_SetLegsAnimTimer( gentity_t *ent, int *legsAnimTimer, int time );
extern void		PM_SetTorsoAnimTimer( gentity_t *ent, int *torsoAnimTimer, int time );

const char		coop_p2Classname[] = "coop_p2";
coopP2State_t	coop_p2;

// P2's client. Not in level.clients and never written to a save.
static gclient_t	coop_p2Client;

#define COOP_P2_HEALTH			100
#define COOP_RESPAWN_DELAY		4000	// ms from death to respawn next to P1
#define COOP_FOLLOW_NEAR		112		// follow: stop this close to P1 (2D)
#define COOP_FOLLOW_TELEPORT	1024	// follow: further than this, jump to P1
#define COOP_STUCK_FRAMES		60		// follow: 3 s without progress, jump to P1

gentity_t *COOP_P2( void )
{
	if ( coop_p2Num < 0 )
	{
		return NULL;
	}
	gentity_t *ent = &g_entities[coop_p2Num];
	if ( !ent->inuse || ent->classname != coop_p2Classname || ent->client != &coop_p2Client )
	{
		// freed, or the slot was reused by something else
		coop_p2Num = -1;
		return NULL;
	}
	return ent;
}

/*
COOP_FindSpotNear

A clear standing spot next to P1 that P1 could walk to in a straight line (so never through a
wall), with floor under it. Behind P1 first, then the sides, then in front, then further out.
*/
static qboolean COOP_FindSpotNear2( gentity_t *p1, vec3_t out );

qboolean COOP_FindSpotNear( gentity_t *p1, vec3_t out )
{
	// P2's own body must not block the spots around P1
	gentity_t	*p2 = COOP_P2();
	int			contents = p2 ? p2->contents : 0;
	qboolean	found;

	if ( p2 )
	{
		p2->contents = 0;
	}
	found = COOP_FindSpotNear2( p1, out );
	if ( p2 )
	{
		p2->contents = contents;
	}
	return found;
}

static qboolean COOP_FindSpotNear2( gentity_t *p1, vec3_t out )
{
	static const float offsets[][2] = {
		{ -64, 0 }, { 0, 64 }, { 0, -64 }, { -64, 64 }, { -64, -64 }, { 64, 0 }, { 64, 64 }, { 64, -64 },
		{ -128, 0 }, { 0, 128 }, { 0, -128 }, { 128, 0 }, { -96, 96 }, { -96, -96 }, { 96, 96 }, { 96, -96 },
	};
	vec3_t	yawOnly, forward, right, start, spot, below;
	trace_t	tr;
	int		ignore = p1->s.number;

	VectorSet( yawOnly, 0, p1->client ? p1->client->ps.viewangles[YAW] : p1->currentAngles[YAW], 0 );
	AngleVectors( yawOnly, forward, right, NULL );
	VectorCopy( p1->currentOrigin, start );

	for ( unsigned i = 0; i < sizeof( offsets ) / sizeof( offsets[0] ); i++ )
	{
		VectorMA( start, offsets[i][0], forward, spot );
		VectorMA( spot, offsets[i][1], right, spot );

		gi.trace( &tr, start, playerMins, playerMaxs, spot, ignore, MASK_PLAYERSOLID );
		if ( tr.startsolid || tr.allsolid || tr.fraction < 1.0f )
		{
			continue;
		}
		VectorCopy( spot, below );
		below[2] -= 96;
		gi.trace( &tr, spot, playerMins, playerMaxs, below, ignore, MASK_PLAYERSOLID );
		if ( tr.startsolid || tr.allsolid || tr.fraction >= 1.0f )
		{
			continue;	// no floor (ledge, pit)
		}
		VectorCopy( tr.endpos, out );
		return qtrue;
	}
	return qfalse;
}

void COOP_PlaceP2( gentity_t *ent, const vec3_t org )
{
	vec3_t	o;

	VectorCopy( org, o );
	G_SetOrigin( ent, o );
	VectorCopy( o, ent->client->ps.origin );
	VectorClear( ent->client->ps.velocity );
	ent->client->ps.eFlags ^= EF_TELEPORT_BIT;	// no lerp from the old spot
	VectorCopy( o, coop_p2.lastOrigin );
	coop_p2.stuckFrames = 0;
	gi.linkentity( ent );
}

void COOP_ApplyHidden( gentity_t *ent, qboolean hidden )
{
	coop_p2.hidden = hidden;
	if ( hidden )
	{
		ent->svFlags |= SVF_NOCLIENT;
		ent->contents = 0;
		ent->takedamage = qfalse;
		ent->flags |= FL_NOTARGET;
		VectorClear( ent->client->ps.velocity );
	}
	else
	{
		ent->svFlags &= ~SVF_NOCLIENT;
		ent->contents = ( ent->health > 0 ) ? CONTENTS_BODY : CONTENTS_CORPSE;
		ent->takedamage = ( ent->health > 0 ) ? qtrue : qfalse;
		ent->flags &= ~FL_NOTARGET;
	}
	gi.linkentity( ent );
}

/*
COOP_UpdatePassable

A script that holds the player still (player_locked) is usually staging a scene around them: an
NPC walks up to player 1, talks, and only then lets go. P2 is held too, so if it stands where that
NPC has to go (P2 is placed in front of P1 when there is no room behind, as at the turbolift where
voy4 starts), the NPC never arrives, the script never finishes and nobody can move again. While the
players are held, P2 lets people through (corpse contents: shots still hit it) and passes through
them; once the scene is over it is solid again as soon as nobody is inside it.
*/
static void COOP_UpdatePassable( gentity_t *ent )
{
	qboolean held = (qboolean)( player_locked && !coop_p2.hidden && ent->health > 0
		&& ent->client->ps.pm_type < PM_DEAD && !COOP_IsDowned( ent ) );

	if ( !coop_p2.passable )
	{
		if ( held && ent->contents == CONTENTS_BODY )
		{
			coop_p2.passable = qtrue;
			ent->contents = CONTENTS_CORPSE;
			ent->clipmask = MASK_PLAYERSOLID & ~CONTENTS_BODY;
			gi.linkentity( ent );
		}
		return;
	}
	if ( ent->contents != CONTENTS_CORPSE || ent->health <= 0 || coop_p2.hidden )
	{
		// hidden, down, dead or respawned since: that code set the contents; only the clip mask is ours
		coop_p2.passable = qfalse;
		ent->clipmask = MASK_PLAYERSOLID;
		return;
	}
	if ( held )
	{
		return;
	}
	trace_t tr;
	gi.trace( &tr, ent->currentOrigin, ent->mins, ent->maxs, ent->currentOrigin, ent->s.number, CONTENTS_BODY );
	if ( tr.startsolid || tr.allsolid )
	{
		return;	// someone is still inside P2: walk out of each other first
	}
	coop_p2.passable = qfalse;
	ent->contents = CONTENTS_BODY;
	ent->clipmask = MASK_PLAYERSOLID;
	gi.linkentity( ent );
}

/*
COOP_SetupP2

Fresh client state for P2 at org, facing yaw. Looks like P1 (same models), or the guest's
choice of Munro (COOP_UpdateP2Look). Weapons are shared
(P1's plus whatever P2 had); a respawn keeps P2's ammo, a first spawn copies P1's.
*/
static void COOP_SetupP2( gentity_t *ent, const vec3_t org, float yaw, qboolean respawn )
{
	gentity_t	*p1 = &g_entities[0];
	gclient_t	*cl = &coop_p2Client;
	int			weapons = p1->client->ps.stats[STAT_WEAPONS];
	int			weapon = p1->client->ps.weapon;
	int			ammo[MAX_AMMO];
	int			teleportBit = ( cl->ps.eFlags & EF_TELEPORT_BIT ) ^ EF_TELEPORT_BIT;
	vec3_t		angles;

	if ( respawn )
	{
		weapons |= cl->ps.stats[STAT_WEAPONS];
		weapon = cl->ps.weapon ? cl->ps.weapon : weapon;
		memcpy( ammo, cl->ps.ammo, sizeof( ammo ) );
	}
	else
	{
		memcpy( ammo, p1->client->ps.ammo, sizeof( ammo ) );
	}

	memset( cl, 0, sizeof( *cl ) );
	cl->clientInfo = p1->client->clientInfo;
	cl->renderInfo = p1->client->renderInfo;
	cl->renderInfo.lookTarget = ENTITYNUM_NONE;
	cl->pers.connected = CON_CONNECTED;
	cl->pers.localClient = qtrue;			// never dropped for inactivity
	cl->pers.maxHealth = COOP_P2_HEALTH;
	cl->pers.enterTime = level.time;
	cl->playerTeam = TEAM_STARFLEET;
	cl->crouchheight = CROUCH_MAXS_2;
	cl->standheight = DEFAULT_MAXS_2;

	cl->ps.clientNum = ent->s.number;
	cl->ps.stats[STAT_MAX_HEALTH] = COOP_P2_HEALTH;
	cl->ps.stats[STAT_HEALTH] = COOP_P2_HEALTH;
	cl->ps.stats[STAT_WEAPONS] = weapons;
	memcpy( cl->ps.ammo, ammo, sizeof( cl->ps.ammo ) );
	cl->ps.weapon = ( weapons & ( 1 << weapon ) ) ? weapon : WP_NONE;
	cl->ps.weaponstate = WEAPON_READY;
	cl->ps.friction = 6;
	cl->ps.gravity = g_gravity->value;
	cl->ps.pm_flags |= PMF_RESPAWNED;
	cl->ps.commandTime = level.time - 100;
	cl->ps.torsoAnim = BOTH_STAND2;
	cl->ps.legsAnim = BOTH_STAND2;
	cl->ps.groundEntityNum = ENTITYNUM_NONE;
	cl->ps.eFlags = teleportBit;
	cl->respawnTime = level.time;
	cl->inactivityTime = level.time + 60 * 1000;
	cl->lastCmdTime = level.time;

	ent->client = cl;
	ent->classname = (char *)coop_p2Classname;
	ent->NPC_type = "munro";		// never NULL: some checks compare it by string
	ent->s.eType = ET_PLAYER;
	ent->s.groundEntityNum = ENTITYNUM_NONE;
	ent->health = ent->max_health = COOP_P2_HEALTH;
	ent->takedamage = qtrue;
	ent->contents = CONTENTS_BODY;
	ent->clipmask = MASK_PLAYERSOLID;
	ent->svFlags &= ~SVF_NOCLIENT;
	ent->flags &= ~FL_NOTARGET;
	ent->e_DieFunc = dieF_player_die;	// player_die hands P2 to COOP_P2Die
	ent->e_PainFunc = painF_NULL;
	ent->e_TouchFunc = touchF_NULL;
	ent->e_UseFunc = useF_NULL;
	ent->e_ThinkFunc = thinkF_NULL;
	ent->nextthink = 0;
	ent->enemy = NULL;
	ent->waterlevel = 0;
	ent->watertype = 0;
	VectorCopy( playerMins, ent->mins );
	VectorCopy( playerMaxs, ent->maxs );

	COOP_PlaceP2( ent, org );
	cl->ps.eFlags = teleportBit;
	VectorSet( angles, 0, yaw, 0 );
	SetClientViewAngle( ent, angles );

	COOP_UpdateP2Look( ent );			// (the netname too: the guest's, or "Player 2")
	G_InitBoltOnData( ent );
	PlayerStateToEntityState( &cl->ps, &ent->s );
	gi.linkentity( ent );

	if ( !respawn )
	{
		COOP_ApplyCarry( ent );
	}
	coop_p2.respawnTime = 0;
	coop_p2.stuckFrames = 0;
	coop_p2.guestSteering = qfalse;		// the fresh client's angles start over: re-base on the guest's
	if ( coop_p2.hidden || in_camera )
	{
		COOP_ApplyHidden( ent, qtrue );
	}
}

void COOP_SpawnP2( void )
{
	gentity_t	*p1 = &g_entities[0];
	gentity_t	*ent;
	vec3_t		org;
	usercmd_t	cmd;

	if ( !p1->inuse || !p1->client || p1->health <= 0 || !p1->client->clientInfo.infoValid )
	{
		return;
	}
	if ( level.time < coop_p2.nextSpawnTry )
	{
		return;
	}
	if ( !COOP_FindSpotNear( p1, org ) )
	{
		coop_p2.nextSpawnTry = level.time + 500;
		return;
	}
	ent = G_Spawn();
	if ( !ent )
	{
		coop_p2.nextSpawnTry = level.time + 2000;
		return;
	}
	memset( &coop_p2Client, 0, sizeof( coop_p2Client ) );
	coop_p2Num = ent->s.number;
	COOP_SetupP2( ent, org, p1->client->ps.viewangles[YAW], qfalse );

	memset( &cmd, 0, sizeof( cmd ) );
	cmd.serverTime = level.time;
	ClientThink( ent->s.number, &cmd );
	gi.Printf( "co-op: player 2 spawned as entity %d at %s\n", ent->s.number, vtos( ent->currentOrigin ) );
}

/*
COOP_GuestBuildCmd

The guest's input, when a guest steers P2 (coop_net.cpp). The guest's view angles are its own;
when it takes over, P2's delta angles are set so that P2 keeps looking where it looked.
*/
static qboolean COOP_GuestBuildCmd( gentity_t *ent, usercmd_t *cmd )
{
	gclient_t	*cl = ent->client;
	usercmd_t	g;

	memset( &g, 0, sizeof( g ) );
	if ( !COOP_GuestCmd( &g ) )
	{
		if ( coop_p2.guestSteering )
		{
			gi.Printf( COOP_GuestConnected() ? "co-op: player 2 holds still (no input from the guest)\n" :
				"co-op: player 2 follows player 1 again\n" );
		}
		coop_p2.guestSteering = qfalse;
		return qfalse;
	}
	if ( !coop_p2.guestSteering )
	{
		vec3_t view;
		VectorCopy( cl->ps.viewangles, view );
		for ( int i = 0; i < 3; i++ )
		{
			cl->pers.cmd_angles[i] = (short)g.angles[i];
		}
		SetClientViewAngle( ent, view );
		coop_p2.guestSteering = qtrue;
		gi.Printf( "co-op: %s steers player 2\n", COOP_GuestName()[0] ? COOP_GuestName() : "the guest" );
	}
	*cmd = g;
	cmd->serverTime = level.time;
	coop_p2.shotViewTime = COOP_GuestViewTime();
	// a weapon P2 doesn't have (or none: the guest isn't in a level) keeps P2's
	if ( g.weapon <= WP_NONE || g.weapon >= WP_NUM_WEAPONS || !( cl->ps.stats[STAT_WEAPONS] & ( 1 << g.weapon ) ) )
	{
		cmd->weapon = cl->ps.weapon;
	}
	coop_p2.stuckFrames = 0;
	VectorCopy( ent->currentOrigin, coop_p2.lastOrigin );
	return qtrue;
}

/*
COOP_BuildCmd

P2's input for this frame: a running console command, else the guest's input, else follow P1.
*/
static void COOP_BuildCmd( gentity_t *ent, usercmd_t *cmd )
{
	gentity_t	*p1 = &g_entities[0];
	gclient_t	*cl = ent->client;
	vec3_t		view, dir;

	if ( coop_p2.moveFrames <= 0 && COOP_GuestBuildCmd( ent, cmd ) )
	{
		coop_p2.weapon = 0;
		return;
	}
	coop_p2.shotViewTime = 0;
	if ( coop_p2.moveFrames > 0 )
	{
		coop_p2.guestSteering = qfalse;	// the console moved P2's view: re-base when the guest is back
	}
	memset( cmd, 0, sizeof( *cmd ) );
	cmd->serverTime = level.time;
	cmd->weapon = coop_p2.weapon > 0 ? coop_p2.weapon : cl->ps.weapon;
	coop_p2.weapon = 0;
	VectorCopy( cl->ps.viewangles, view );

	if ( coop_p2.moveFrames > 0 )
	{
		coop_p2.moveFrames--;
		cmd->forwardmove = coop_p2.forwardmove;
		cmd->rightmove = coop_p2.rightmove;
		cmd->upmove = coop_p2.upmove;
		coop_p2.stuckFrames = 0;
	}
	// a connected guest without fresh input (menu, console, loading, a network gap): P2 holds still
	else if ( coop_p2.follow && !COOP_GuestConnected() && p1->inuse && p1->client && cl->ps.pm_type < PM_DEAD )
	{
		VectorSubtract( p1->currentOrigin, ent->currentOrigin, dir );
		float dz = dir[2];
		dir[2] = 0;
		float dist = VectorLength( dir );

		if ( dist > COOP_FOLLOW_TELEPORT || dz > 256 || dz < -512 )
		{
			vec3_t org;
			if ( COOP_FindSpotNear( p1, org ) )
			{
				COOP_PlaceP2( ent, org );
			}
		}
		else if ( dist > COOP_FOLLOW_NEAR )
		{
			if ( !coop_p2.haveAngles )
			{
				view[YAW] = vectoyaw( dir );
				view[PITCH] = 0;
			}
			cmd->forwardmove = 127;
			// stuck = not getting closer to P1: blocked, or sliding sideways along a wall or ledge
			// (moving at all is not enough; P2 can slide along a wall for as long as P1 stays away)
			vec3_t moved;
			VectorSubtract( ent->currentOrigin, coop_p2.lastOrigin, moved );
			moved[2] = 0;
			if ( DotProduct( moved, dir ) < dist )	// less than 1 unit toward P1 this frame
			{
				coop_p2.stuckFrames++;
				if ( coop_p2.stuckFrames % 10 == 5 )
				{
					cmd->upmove = 127;	// try to hop over it
				}
				if ( coop_p2.stuckFrames > COOP_STUCK_FRAMES )
				{
					vec3_t org;
					if ( COOP_FindSpotNear( p1, org ) )
					{
						COOP_PlaceP2( ent, org );
					}
					coop_p2.stuckFrames = 0;
				}
			}
			else
			{
				coop_p2.stuckFrames = 0;
			}
		}
		else
		{
			coop_p2.stuckFrames = 0;
		}
	}
	VectorCopy( ent->currentOrigin, coop_p2.lastOrigin );

	if ( coop_p2.haveAngles )
	{
		VectorCopy( coop_p2.viewAngles, view );
	}
	if ( coop_p2.buttonFrames > 0 )
	{
		coop_p2.buttonFrames--;
		cmd->buttons = coop_p2.buttons;
	}
	for ( int i = 0; i < 3; i++ )
	{
		cmd->angles[i] = ANGLE2SHORT( view[i] ) - cl->ps.delta_angles[i];
	}
}

void COOP_RunFrame( void )
{
	gentity_t	*ent;
	usercmd_t	cmd;

	if ( !coop_enabled )
	{
		return;
	}
	coop_p2.levelFrames++;
	COOP_PublishInfo();

	ent = COOP_P2();
	if ( !ent )
	{
		// wait a few frames after a level starts, so the cgame has registered P1's models
		if ( coop_p2.levelFrames > 10 )
		{
			COOP_SpawnP2();
		}
		return;
	}
	COOP_UpdateP2Look( ent );		// a guest came or went, or P1 changed clothes
	// the cgame sets the eye point only while it draws P2; weapons fire from it
	VectorCopy( ent->client->ps.origin, ent->client->renderInfo.eyePoint );
	ent->client->renderInfo.eyePoint[2] += ent->client->ps.viewheight;
	VectorCopy( ent->client->ps.viewangles, ent->client->renderInfo.eyeAngles );

	if ( coop_p2.rejoinAt && level.time >= coop_p2.rejoinAt && !in_camera )
	{
		coop_p2.rejoinAt = 0;
		COOP_CameraRejoin( ent );
	}
	COOP_UpdatePassable( ent );
	COOP_BuildCmd( ent, &cmd );
	ClientThink( ent->s.number, &cmd );
}

void COOP_EndFrame( void )
{
	gentity_t *ent;

	if ( !coop_enabled )
	{
		return;
	}
	if ( ( ent = COOP_P2() ) != NULL )
	{
		ClientEndFrame( ent );
	}
	COOP_DownFrame();
	COOP_TestTargetFrame();
	COOP_LagRecord();
}

qboolean COOP_P2ThinkGate( gentity_t *ent, usercmd_t *ucmd )
{
	if ( in_camera )
	{
		return qtrue;	// frozen like P1 while a camera runs
	}
	if ( player_locked && ent->client->ps.pm_type < PM_DEAD )
	{
		ucmd->forwardmove = 0;
		ucmd->rightmove = 0;
		ucmd->upmove = 0;
		ucmd->buttons = 0;
	}
	return qfalse;
}

void COOP_P2Die( gentity_t *ent, gentity_t *attacker )
{
	gclient_t *cl = ent->client;

	if ( cl->ps.pm_type == PM_DEAD )
	{
		return;
	}
	ent->enemy = attacker;
	ent->takedamage = qfalse;
	ent->contents = CONTENTS_CORPSE;
	cl->renderInfo.lookTarget = ENTITYNUM_NONE;
	G_AddEvent( ent, Q_irand( EV_DEATH1, EV_DEATH3 ), ent->health );
	NPC_SetAnim( ent, SETANIM_BOTH, BOTH_DEATH1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD );
	PM_SetLegsAnimTimer( ent, &cl->ps.legsAnimTimer, -1 );
	PM_SetTorsoAnimTimer( ent, &cl->ps.torsoAnimTimer, -1 );
	cl->ps.pm_type = PM_DEAD;
	cl->ps.stats[STAT_HEALTH] = ent->health;
	cl->respawnTime = level.time + COOP_RESPAWN_DELAY;
	coop_p2.respawnTime = level.time + COOP_RESPAWN_DELAY;
	gi.linkentity( ent );
	gi.Printf( "co-op: player 2 died\n" );
}

void COOP_P2DeadThink( gentity_t *ent, usercmd_t *ucmd )
{
	gentity_t	*p1 = &g_entities[0];
	vec3_t		org;

	if ( COOP_GameIsOver() )
	{
		// out with player 1: the guest's attack reloads the level too, as player 1's does
		extern void respawn( gentity_t *ent );
		if ( COOP_GuestConnected() && ( ucmd->buttons & BUTTON_ATTACK ) && p1->client
			&& p1->client->ps.pm_type == PM_DEAD && level.time > p1->client->respawnTime )
		{
			p1->client->respawnTime = level.time + 3000;	// once
			respawn( p1 );
		}
		return;
	}
	if ( COOP_IsDowned( ent ) )
	{
		return;	// waiting for a revive
	}
	if ( level.time < coop_p2.respawnTime || !p1->inuse || ( p1->health <= 0 && !COOP_IsDowned( p1 ) ) )
	{
		return;
	}
	if ( !COOP_FindSpotNear( p1, org ) )
	{
		return;
	}
	COOP_SetupP2( ent, org, p1->client->ps.viewangles[YAW], qtrue );
	gi.Printf( "co-op: player 2 respawned at %s\n", vtos( ent->currentOrigin ) );
}

// P1 uses the host's cg_thirdPerson; P2 has no view of its own in M0, so it shoots first-person
// P2 when it is in the level, alive and not hidden by a camera
static gentity_t *COOP_LiveP2( void )
{
	gentity_t *p2 = COOP_P2();
	if ( !p2 || coop_p2.hidden || p2->health <= 0 )
	{
		return NULL;
	}
	return p2;
}

int COOP_NumPlayers( void )
{
	return COOP_LiveP2() ? 2 : 1;
}

gentity_t *COOP_PlayerByIndex( int i )
{
	return i == 0 ? &g_entities[0] : COOP_LiveP2();
}

gentity_t *COOP_NearestPlayer( const vec3_t org )
{
	gentity_t	*p1 = &g_entities[0];
	gentity_t	*p2 = COOP_LiveP2();

	if ( !p2 )
	{
		return p1;
	}
	if ( p1->health <= 0 )
	{
		return p2;
	}
	return DistanceSquared( org, p2->currentOrigin ) < DistanceSquared( org, p1->currentOrigin ) ? p2 : p1;
}

qboolean COOP_AnyPlayerInPVS( const vec3_t org )
{
	gentity_t *p2 = COOP_LiveP2();
	return (qboolean)( gi.inPVS( g_entities[0].currentOrigin, org ) || ( p2 && gi.inPVS( p2->currentOrigin, org ) ) );
}

qboolean COOP_AnyPlayerInPVSRev( const vec3_t org )
{
	gentity_t *p2 = COOP_LiveP2();
	return (qboolean)( gi.inPVS( org, g_entities[0].currentOrigin ) || ( p2 && gi.inPVS( org, p2->currentOrigin ) ) );
}

void COOP_FindPlayerWaypoints( void )
{
	gentity_t *p2 = COOP_LiveP2();
	if ( p2 )
	{
		p2->waypoint = navigator.GetNearestNode( p2, p2->lastWaypoint, NF_CLEAR_PATH );
	}
}

int COOP_ThirdPerson( const gentity_t *e )
{
	extern vmCvar_t cg_thirdPerson;
	return COOP_IsP2( e ) ? 0 : cg_thirdPerson.integer;
}
