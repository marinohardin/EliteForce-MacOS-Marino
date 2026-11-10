// coop_replica.cpp -- single-player co-op: the host's world as byte images, and the guest's copy.
//
// The host turns each entity in use (with its client, NPC and ICARUS parms) and the level's globals
// into an image the way a save does: the save code's field tables turn every pointer into an index
// and every string into its text (EnumerateFields, g_savegame.cpp), here into memory instead of a
// file. The engine sends each image's changes to the guest (efcode/sys/sys_coop_net.c). The guest
// runs the same level in replica mode (sp_coop 2: no game frames, no scripts, no AI) and writes the
// host's images into its own entities the way a load does (EvaluateFields), so its cgame draws the
// host's world from real game data.
//
// What stays the guest's own: the media its cgame registered for a client (model, skin, sound and
// animation handles differ between two copies of the game), and the guest-only parts of entities
// (ICARUS, which never runs on the guest).

#include "g_local.h"
#include "fields.h"
#include "coop_public.h"
#include "coop_local.h"
#include "coop_bridge.h"
#include "../cgame/cg_camera.h"

extern field_t	savefields_gEntity[];
extern field_t	savefields_gNPC[];
extern field_t	savefields_LevelLocals[];
extern field_t	savefields_gClient[];
extern void		EnumerateFields( field_t *pFields, byte *pbData, unsigned long ulChid, int iLen );
extern void		EvaluateFields( field_t *pFields, byte *pbData, byte *pbOriginalRefData, unsigned long ulChid, int iSize, qboolean bOkToSizeMisMatch );
extern void		CG_RegisterClientModels( int entityNum );
extern void		CG_RegisterNPCCustomSounds( clientInfo_t *ci );
extern void		CG_RegisterNPCEffects( team_t team );
extern void		CG_RegisterWeapon( int weaponNum );
extern void		G_SoundOnEnt( gentity_t *ent, soundChannel_t channel, const char *soundPath );
extern qboolean	player_locked;
extern int		BMS_START, BMS_MID, BMS_END;

int		coop_replica = 0;		// sp_coop 2 at InitGame: this level mirrors a host's

// what an image's entity has besides the gentity_t
#define REP_CLIENT		1
#define REP_NPC			2
#define REP_PARMS		4

// A door's or lift's sound event (EV_BMODEL_SOUND) carries a sound handle of the host's sound
// system, and the guest's handles for the same sounds differ (they number sounds in the order each
// copy registered them). The host sends which sound of the entity's sound set it was (start, loop
// or end) in an 'RBMS' chunk, and the guest puts its own handle for that sound in the event.
static int REP_BModelStage( const gentity_t *ent )
{
	const int stages[3] = { BMS_START, BMS_MID, BMS_END };

	if ( ( ent->s.event & ~EV_EVENT_BITS ) != EV_BMODEL_SOUND || !VALIDSTRING( ent->soundSet ) )
	{
		return -1;
	}
	for ( int i = 0; i < 3; i++ )
	{
		if ( CAS_GetBModelSound( ent->soundSet, stages[i] ) == ent->s.eventParm )
		{
			return stages[i];
		}
	}
	return -1;
}

// globals that aren't in level_locals_t
typedef struct
{
	int		inCamera;
	int		playerLocked;
	int		p2Num;
	int		numEntities;
} repGlobals_t;

// sounds the host's game started directly (G_SoundOnEnt: script voice lines, some NPC sounds), not
// through an entity event; the last second's go with the globals and the guest plays the new ones
#define REP_SOUNDS		16
#define REP_SOUND_KEEP	1000	// ms
typedef struct
{
	int		seq;
	int		time;			// level.time when it started
	int		ent;
	int		chan;
	char	path[MAX_QPATH];
} repSound_t;

static repSound_t	rep_sounds[REP_SOUNDS];		// host: the newest at rep_soundSeq % REP_SOUNDS
static int			rep_soundSeq;
static int			rep_soundSeen;				// guest: the newest one played (or skipped on joining)
static qboolean		rep_soundSynced;
static repSound_t	rep_soundPending[REP_SOUNDS];
static int			rep_soundPendingCount;

/*
==============================================================================
Images in memory: the save code's chunks ([id][length][data]) into a buffer, and read back
==============================================================================
*/

static byte			*rep_out;
static int			rep_outSize, rep_outLen;
static qboolean		rep_outOverflow;

static const byte	*rep_in;
static int			rep_inLen, rep_inPos;
static qboolean		rep_inBad;

static qboolean (*rep_savedAppend)( unsigned long chid, void *data, int length );
static int (*rep_savedRead)( unsigned long chid, void *pvAddress, int iLength, void **ppvAddressPtr );
static int (*rep_savedReadOpt)( unsigned long chid, void *pvAddress, int iLength, void **ppvAddressPtr );

static qboolean REP_Append( unsigned long chid, void *data, int length )
{
	if ( length < 0 || rep_outLen + 8 + length > rep_outSize )
	{
		rep_outOverflow = qtrue;
		return qtrue;
	}
	unsigned id = (unsigned)chid;
	memcpy( rep_out + rep_outLen, &id, 4 );
	memcpy( rep_out + rep_outLen + 4, &length, 4 );
	if ( length > 0 )
	{
		memcpy( rep_out + rep_outLen + 8, data, length );
	}
	rep_outLen += 8 + length;
	return qtrue;
}

static int REP_ReadChunk( unsigned long chid, void *addr, int len, void **pp, qboolean optional )
{
	unsigned	id;
	int			stored;

	if ( rep_inBad )
	{
		return 0;
	}
	if ( rep_inPos + 8 > rep_inLen )
	{
		if ( !optional )
		{
			rep_inBad = qtrue;		// (an optional chunk may be missing at the end)
		}
		return 0;
	}
	memcpy( &id, rep_in + rep_inPos, 4 );
	memcpy( &stored, rep_in + rep_inPos + 4, 4 );
	if ( id != (unsigned)chid )
	{
		if ( !optional )
		{
			rep_inBad = qtrue;
		}
		return 0;
	}
	if ( stored < 0 || rep_inPos + 8 + stored > rep_inLen )
	{
		rep_inBad = qtrue;
		return 0;
	}
	const byte *src = rep_in + rep_inPos + 8;
	rep_inPos += 8 + stored;
	if ( addr )
	{
		int n = ( len > 0 && len < stored ) ? len : stored;
		memcpy( addr, src, n );
	}
	else if ( pp )
	{
		*pp = gi.Malloc( stored ? stored : 1 );
		memcpy( *pp, src, stored );
	}
	return stored;
}

static int REP_Read( unsigned long chid, void *addr, int len, void **pp )
{
	return REP_ReadChunk( chid, addr, len, pp, qfalse );
}

static int REP_ReadOpt( unsigned long chid, void *addr, int len, void **pp )
{
	return REP_ReadChunk( chid, addr, len, pp, qtrue );
}

static void REP_BeginWrite( byte *buf, int size )
{
	rep_out = buf;
	rep_outSize = size;
	rep_outLen = 0;
	rep_outOverflow = qfalse;
	rep_savedAppend = gi.AppendToSaveGame;
	gi.AppendToSaveGame = REP_Append;
}

static int REP_EndWrite( void )
{
	gi.AppendToSaveGame = rep_savedAppend;
	return rep_outOverflow ? -1 : rep_outLen;
}

static void REP_BeginRead( const byte *data, int len )
{
	rep_in = data;
	rep_inLen = len;
	rep_inPos = 0;
	rep_inBad = qfalse;
	rep_savedRead = gi.ReadFromSaveGame;
	rep_savedReadOpt = gi.ReadFromSaveGameOptional;
	gi.ReadFromSaveGame = REP_Read;
	gi.ReadFromSaveGameOptional = REP_ReadOpt;
}

static qboolean REP_EndRead( void )
{
	gi.ReadFromSaveGame = rep_savedRead;
	gi.ReadFromSaveGameOptional = rep_savedReadOpt;
	return !rep_inBad;
}

/*
==============================================================================
Host
==============================================================================
*/

static int REP_WriteEntity( int num, unsigned char *buf, int size )
{
	gentity_t	*ent;
	int			flags = 0;

	if ( num < 0 || num >= globals.num_entities )
	{
		return 0;
	}
	ent = &g_entities[num];
	if ( !ent->inuse )
	{
		return 0;
	}
	if ( ent->client )		flags |= REP_CLIENT;
	if ( ent->NPC )			flags |= REP_NPC;
	if ( ent->parms )		flags |= REP_PARMS;

	REP_BeginWrite( buf, size );
	gi.AppendToSaveGame( 'RFLG', &flags, sizeof( flags ) );
	{
		gentity_t temp = *ent;
		temp.client = NULL;		// the client goes in a chunk of its own (P1's, P2's and NPCs' alike)
		EnumerateFields( savefields_gEntity, (byte *)&temp, 'GENT', sizeof( temp ) );
	}
	if ( ent->NPC )
	{
		gNPC_t npc = *ent->NPC;
		EnumerateFields( savefields_gNPC, (byte *)&npc, 'GNPC', sizeof( npc ) );
	}
	if ( ent->client )
	{
		gclient_t client = *ent->client;
		EnumerateFields( savefields_gClient, (byte *)&client, 'GCLI', sizeof( client ) );
	}
	if ( ent->parms )
	{
		gi.AppendToSaveGame( 'PARM', ent->parms, sizeof( *ent->parms ) );
	}
	{
		int stage = REP_BModelStage( ent );
		if ( stage >= 0 )
		{
			gi.AppendToSaveGame( 'RBMS', &stage, sizeof( stage ) );
		}
	}
	return REP_EndWrite();
}

static int REP_WriteGlobals( unsigned char *buf, int size )
{
	repGlobals_t	g;

	memset( &g, 0, sizeof( g ) );
	g.inCamera = in_camera ? 1 : 0;
	g.playerLocked = player_locked ? 1 : 0;
	g.p2Num = coop_p2Num;
	g.numEntities = globals.num_entities;

	REP_BeginWrite( buf, size );
	gi.AppendToSaveGame( 'RGLB', &g, sizeof( g ) );
	{
		level_locals_t temp = level;
		EnumerateFields( savefields_LevelLocals, (byte *)&temp, 'LVLC', LLOFS( LEVEL_LOCALS_T_SAVESTOP ) );
	}
	{
		repSound_t	recent[REP_SOUNDS];
		int			n = 0;
		for ( int seq = rep_soundSeq - REP_SOUNDS + 1; seq <= rep_soundSeq; seq++ )
		{
			const repSound_t *s = &rep_sounds[seq % REP_SOUNDS];
			if ( seq > 0 && s->seq == seq && level.time - s->time <= REP_SOUND_KEEP )
			{
				recent[n++] = *s;
			}
		}
		if ( n )
		{
			gi.AppendToSaveGame( 'RSND', recent, n * sizeof( recent[0] ) );
		}
	}
	return REP_EndWrite();
}

/*
==============================================================================
Guest
==============================================================================
*/

// the guest's own allocations for the host's clients, NPC structs and parms (an entity's are kept
// when the host frees and reuses its slot)
static gclient_t	*rep_clients[MAX_GENTITIES];
static gNPC_t		*rep_npcs[MAX_GENTITIES];
static parms_t		*rep_parms[MAX_GENTITIES];
static byte			rep_needMedia[MAX_GENTITIES];	// a client whose models this cgame hasn't registered
static int			rep_weapon[MAX_GENTITIES];		// the weapon last registered for a client

static gclient_t *REP_Client( int num )
{
	if ( num == 0 )
	{
		return &level.clients[0];
	}
	if ( !rep_clients[num] )
	{
		rep_clients[num] = (gclient_t *)G_Alloc( sizeof( gclient_t ) );
		memset( rep_clients[num], 0, sizeof( gclient_t ) );
	}
	return rep_clients[num];
}

// the guest's registered media for a client, kept while its models stay the same
static void REP_KeepMedia( clientInfo_t *to, const clientInfo_t *from )
{
	to->infoValid = from->infoValid;
	to->legsModel = from->legsModel;
	to->legsSkin = from->legsSkin;
	to->torsoModel = from->torsoModel;
	to->torsoSkin = from->torsoSkin;
	to->headModel = from->headModel;
	to->headSkin = from->headSkin;
	to->extensions = from->extensions;
	to->animFileIndex = from->animFileIndex;
	memcpy( to->sounds, from->sounds, sizeof( to->sounds ) );
}

static qboolean REP_SameModels( const renderInfo_t *a, const renderInfo_t *b )
{
	return !strcmp( a->legsModelName, b->legsModelName ) && !strcmp( a->torsoModelName, b->torsoModelName ) &&
		!strcmp( a->headModelName, b->headModelName );
}

static void REP_FreeEntity( gentity_t *ent )
{
	if ( ent->inuse )
	{
		int num = ent - g_entities;
		G_FreeEntity( ent );
		ent->s.number = num;
	}
}

static void REP_ApplyEntity( int num, const unsigned char *data, int len )
{
	gentity_t	*ent;
	gentity_t	temp;
	int			flags = 0;

	if ( num < 0 || num >= MAX_GENTITIES )
	{
		return;
	}
	ent = &g_entities[num];
	if ( len <= 0 )
	{
		REP_FreeEntity( ent );
		return;
	}

	REP_BeginRead( data, len );
	gi.ReadFromSaveGame( 'RFLG', &flags, sizeof( flags ), NULL );
	EvaluateFields( savefields_gEntity, (byte *)&temp, (byte *)ent, 'GENT', sizeof( temp ), qfalse );

	temp.NPC = NULL;
	if ( flags & REP_NPC )
	{
		if ( !rep_npcs[num] )
		{
			rep_npcs[num] = (gNPC_t *)G_Alloc( sizeof( gNPC_t ) );
		}
		gNPC_t npc;
		EvaluateFields( savefields_gNPC, (byte *)&npc, (byte *)rep_npcs[num], 'GNPC', sizeof( npc ), qfalse );
		*rep_npcs[num] = npc;
		temp.NPC = rep_npcs[num];
	}

	temp.client = NULL;
	if ( flags & REP_CLIENT )
	{
		gclient_t	*cl = REP_Client( num );
		gclient_t	*fresh = (gclient_t *)gi.Malloc( sizeof( gclient_t ) );		// big: not on the stack

		EvaluateFields( savefields_gClient, (byte *)fresh, (byte *)cl, 'GCLI', sizeof( gclient_t ), qtrue );
		if ( cl->clientInfo.infoValid && ent->inuse && ent->client == cl && REP_SameModels( &fresh->renderInfo, &cl->renderInfo ) )
		{
			REP_KeepMedia( &fresh->clientInfo, &cl->clientInfo );
		}
		else
		{
			fresh->clientInfo.infoValid = qfalse;	// the host's handles mean nothing here
			rep_needMedia[num] = 1;
			rep_weapon[num] = -1;
		}
		*cl = *fresh;
		gi.Free( fresh );
		temp.client = cl;
	}

	temp.parms = NULL;
	if ( flags & REP_PARMS )
	{
		if ( !rep_parms[num] )
		{
			rep_parms[num] = (parms_t *)G_Alloc( sizeof( parms_t ) );
		}
		gi.ReadFromSaveGame( 'PARM', rep_parms[num], sizeof( parms_t ), NULL );
		temp.parms = rep_parms[num];
	}

	int bmsStage = -1;
	gi.ReadFromSaveGameOptional( 'RBMS', &bmsStage, sizeof( bmsStage ), NULL );

	if ( !REP_EndRead() )
	{
		gi.Printf( S_COLOR_YELLOW "co-op: entity %d's image from the host didn't read back; skipped\n", num );
		return;
	}

	gi.unlinkentity( ent );
	if ( ent->sequencer || ent->taskManager )
	{
		ICARUS_FreeEnt( ent );		// the guest's own spawn had scripts; ICARUS never runs here
	}
	temp.sequencer = NULL;
	temp.taskManager = NULL;
	*ent = temp;

	// a mover's looping sound is a handle of this copy's sound sets (as a load does)
	if ( ent->s.eType == ET_MOVER && ent->s.loopSound > 0 )
	{
		ent->s.loopSound = CAS_GetBModelSound( ent->soundSet, BMS_MID );
		if ( ent->s.loopSound == -1 )
		{
			ent->s.loopSound = 0;
		}
	}
	// ...and so is a door sound event's (see REP_BModelStage); one this copy can't name plays nothing
	if ( ( ent->s.event & ~EV_EVENT_BITS ) == EV_BMODEL_SOUND )
	{
		int sfx = ( bmsStage >= 0 && VALIDSTRING( ent->soundSet ) ) ? CAS_GetBModelSound( ent->soundSet, bmsStage ) : -1;
		if ( sfx > 0 )
		{
			ent->s.eventParm = sfx;
		}
		else
		{
			ent->s.event = 0;
		}
	}
	if ( num >= globals.num_entities )
	{
		globals.num_entities = num + 1;
	}
	qboolean linked = ent->linked;
	ent->linked = qfalse;
	if ( linked )
	{
		gi.linkentity( ent );
	}
}

static void REP_ApplyGlobals( const unsigned char *data, int len )
{
	repGlobals_t	g;
	gclient_t		*clients = level.clients;

	if ( len <= 0 )
	{
		return;
	}
	REP_BeginRead( data, len );
	memset( &g, 0, sizeof( g ) );
	gi.ReadFromSaveGame( 'RGLB', &g, sizeof( g ), NULL );
	{
		level_locals_t temp = level;
		EvaluateFields( savefields_LevelLocals, (byte *)&temp, (byte *)&level, 'LVLC', LLOFS( LEVEL_LOCALS_T_SAVESTOP ), qfalse );
		repSound_t	recent[REP_SOUNDS];
		int			n = gi.ReadFromSaveGameOptional( 'RSND', recent, sizeof( recent ), NULL ) / (int)sizeof( recent[0] );
		if ( !REP_EndRead() )
		{
			gi.Printf( S_COLOR_YELLOW "co-op: the host's level globals didn't read back; skipped\n" );
			return;
		}
		level = temp;
		for ( int i = 0; i < n && i < REP_SOUNDS; i++ )
		{
			if ( recent[i].seq <= rep_soundSeen )
			{
				continue;
			}
			rep_soundSeen = recent[i].seq;
			if ( rep_soundSynced && rep_soundPendingCount < REP_SOUNDS )
			{
				recent[i].path[MAX_QPATH - 1] = 0;
				rep_soundPending[rep_soundPendingCount++] = recent[i];
			}
		}
		rep_soundSynced = qtrue;	// joining: only the sounds that start from now on
	}
	level.clients = clients;
	// in_camera stays the guest's own: the host's camera itself isn't mirrored yet (M2), and the
	// cgame would draw from a camera that isn't there
	player_locked = g.playerLocked ? qtrue : qfalse;
	coop_p2Num = g.p2Num;
	if ( g.numEntities > globals.num_entities && g.numEntities <= MAX_GENTITIES )
	{
		globals.num_entities = g.numEntities;
	}
}

static void REP_ApplyDone( qboolean cgameUp )
{
	if ( !cgameUp )
	{
		rep_soundPendingCount = 0;
		return;		// CG_Init registers every client's media itself
	}
	for ( int i = 0; i < globals.num_entities; i++ )
	{
		gentity_t *ent = &g_entities[i];

		if ( !ent->inuse || !ent->client )
		{
			continue;
		}
		if ( rep_needMedia[i] )
		{
			rep_needMedia[i] = 0;
			CG_RegisterClientModels( i );
			if ( i != 0 )
			{
				CG_RegisterNPCCustomSounds( &ent->client->clientInfo );
				CG_RegisterNPCEffects( ent->client->playerTeam );
			}
		}
		if ( ent->client->ps.weapon != rep_weapon[i] )
		{
			rep_weapon[i] = ent->client->ps.weapon;
			if ( rep_weapon[i] > WP_NONE && rep_weapon[i] < WP_NUM_WEAPONS )
			{
				CG_RegisterWeapon( rep_weapon[i] );
			}
		}
	}
	for ( int i = 0; i < rep_soundPendingCount; i++ )
	{
		const repSound_t *s = &rep_soundPending[i];
		if ( s->ent >= 0 && s->ent < MAX_GENTITIES && s->path[0] )
		{
			G_SoundOnEnt( &g_entities[s->ent], (soundChannel_t)s->chan, s->path );
		}
	}
	rep_soundPendingCount = 0;
}

// host: the game started a sound itself (G_SoundOnEnt); the guest plays it too
void COOP_OnDirectSound( int entNum, int channel, const char *path )
{
	repSound_t *s;

	if ( !coop_enabled || !path || !path[0] )
	{
		return;
	}
	rep_soundSeq++;
	s = &rep_sounds[rep_soundSeq % REP_SOUNDS];
	s->seq = rep_soundSeq;
	s->time = level.time;
	s->ent = entNum;
	s->chan = channel;
	Q_strncpyz( s->path, path, sizeof( s->path ) );
}

static int REP_ViewEntity( void )
{
	if ( coop_p2Num > 0 && coop_p2Num < globals.num_entities && g_entities[coop_p2Num].inuse && g_entities[coop_p2Num].client )
	{
		return coop_p2Num;
	}
	return -1;
}

// a replica level starts: nothing of the last one's allocations is valid (the module was reloaded,
// so this is all zero already; kept for clarity)
void COOP_ReplicaInit( void )
{
	memset( rep_clients, 0, sizeof( rep_clients ) );
	memset( rep_npcs, 0, sizeof( rep_npcs ) );
	memset( rep_parms, 0, sizeof( rep_parms ) );
	memset( rep_needMedia, 0, sizeof( rep_needMedia ) );
	memset( rep_weapon, 0, sizeof( rep_weapon ) );
	memset( rep_sounds, 0, sizeof( rep_sounds ) );
	rep_soundSeq = rep_soundSeen = rep_soundPendingCount = 0;
	rep_soundSynced = qfalse;
}

static const coopReplica_t coop_replicaExport = {
	COOP_REPLICA_VERSION,
	REP_WriteEntity,
	REP_WriteGlobals,
	REP_ApplyGlobals,
	REP_ApplyEntity,
	REP_ApplyDone,
	REP_ViewEntity,
};

extern "C" __attribute__((visibility("default"))) const coopReplica_t *COOP_GetReplica( void )
{
	return &coop_replicaExport;
}
