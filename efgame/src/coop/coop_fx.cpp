// coop_fx.cpp -- single-player co-op: the guest sees the effects the host's game draws directly.
//
// Some game code draws an effect by calling the cgame itself (game/coop_fx.h lists them): a
// breakable's fireball and debris (misc_model_breakable, func_breakable, turrets), the Borg head
// bot's explosion, a stasis flier child's burst, the psycho bolt, the spawn shimmer, a turret's
// laser, the Borg beam-in (an NPC spawn or the script event borgteleport), the sparks and green
// flash of a shot on a Borg that has (or hasn't yet) adapted, an Etherian's charge, the Vohrsoth's
// ground tendrils, and a camera shake started outside a cutscene (scripts, the warrior bot's steps). Nothing of it is in an entity, so the replica alone would never show it. The host draws it
// as before and keeps it in a short numbered ring that goes with the level's globals, like the
// direct sounds (coop_replica.cpp); the guest's cgame draws the ones it hasn't seen yet.
//
// The cgame's own randomness (debris directions, sparks) is the guest's own, so the two screens show
// the same effect in the same place, not the same particles.

#include "g_local.h"
#include "coop_public.h"
#include "coop_local.h"

// the cgame's (same module in single player); this file doesn't include coop_fx.h
extern void CG_SurfaceExplosion( vec3_t origin, vec3_t normal, float radius, float shake_speed, qboolean smoke );
extern void CG_MiscModelExplosion( vec3_t origin, vec3_t normal );
extern void CG_Chunks( int owner, vec3_t origin, const vec3_t normal, float speed, int numChunks, material_t chunkType, int customChunk, float baseScale );
extern void CG_StasisFlierChildDeath( vec3_t pos );
extern void CG_PsychoJism( vec3_t start, vec3_t end, qboolean open );
extern void CG_ShimmeryThing_Spawner( vec3_t start, vec3_t end, float radius, qboolean taper, int duration );
extern void CG_FireLaser( vec3_t start, vec3_t end, vec3_t normal, vec4_t laserRGB, qboolean hit_ent );
extern void CG_AimLaser( vec3_t start, vec3_t end, vec3_t normal );
extern void FX_BorgTeleport( vec3_t origin );
extern void FX_BorgHit( vec3_t origin, vec3_t dir );
extern void FX_BorgShield( vec3_t position, vec3_t dir );
extern void FX_StasisCharge( vec3_t origin, vec3_t dir, float perc_done );
extern void FX_GroundTendrilSpawner( vec3_t start, vec3_t end );
extern void FX_Vohrsoth_CreateGroundWarning( vec3_t origin );
extern void CGCam_Shake( float intensity, int duration );

typedef enum
{
	FX_NONE,
	FX_SURFACE_EXPLOSION,
	FX_MISC_MODEL_EXPLOSION,
	FX_CHUNKS,
	FX_FLIER_CHILD_DEATH,
	FX_PSYCHO_JISM,
	FX_SHIMMER,
	FX_FIRE_LASER,
	FX_AIM_LASER,
	FX_BORG_TELEPORT,
	FX_BORG_HIT,
	FX_BORG_SHIELD,
	FX_STASIS_CHARGE,
	FX_GROUND_TENDRIL,
	FX_GROUND_WARNING,
	FX_CAMERA_SHAKE
} coopFxType_t;

#define REP_FX			32
#define REP_FX_KEEP		500		// ms: a turret laser is one every frame, so keep the window short

typedef struct
{
	int		seq;
	int		time;		// level.time when it was drawn
	int		type;		// coopFxType_t
	int		i[4];
	float	f[3];
	vec3_t	v[3];
	vec4_t	color;
} repFx_t;

static repFx_t	rep_fx[REP_FX];				// host: the newest at rep_fxSeq % REP_FX
static int		rep_fxSeq;
static int		rep_fxSeen;					// guest: the newest one drawn (or skipped on joining)
static qboolean	rep_fxSynced;
static repFx_t	rep_fxIn[REP_FX];			// guest: as read from the globals, until they read back whole
static int		rep_fxInCount;
static repFx_t	rep_fxPending[REP_FX];
static int		rep_fxPendingCount;

static repFx_t *FX_New( int type )
{
	repFx_t *fx;

	if ( !coop_enabled )
	{
		return NULL;
	}
	rep_fxSeq++;
	fx = &rep_fx[rep_fxSeq % REP_FX];
	memset( fx, 0, sizeof( *fx ) );
	fx->seq = rep_fxSeq;
	fx->time = level.time;
	fx->type = type;
	return fx;
}

static void FX_Draw( repFx_t *fx )
{
	switch ( fx->type )
	{
	case FX_SURFACE_EXPLOSION:
		CG_SurfaceExplosion( fx->v[0], fx->v[1], fx->f[0], fx->f[1], (qboolean)fx->i[0] );
		break;
	case FX_MISC_MODEL_EXPLOSION:
		CG_MiscModelExplosion( fx->v[0], fx->v[1] );
		break;
	case FX_CHUNKS:
		CG_Chunks( fx->i[0], fx->v[0], fx->v[1], fx->f[0], fx->i[1], (material_t)fx->i[2], fx->i[3], fx->f[1] );
		break;
	case FX_FLIER_CHILD_DEATH:
		CG_StasisFlierChildDeath( fx->v[0] );
		break;
	case FX_PSYCHO_JISM:
		CG_PsychoJism( fx->v[0], fx->v[1], (qboolean)fx->i[0] );
		break;
	case FX_SHIMMER:
		CG_ShimmeryThing_Spawner( fx->v[0], fx->v[1], fx->f[0], (qboolean)fx->i[0], fx->i[1] );
		break;
	case FX_FIRE_LASER:
		CG_FireLaser( fx->v[0], fx->v[1], fx->v[2], fx->color, (qboolean)fx->i[0] );
		break;
	case FX_AIM_LASER:
		CG_AimLaser( fx->v[0], fx->v[1], fx->v[2] );
		break;
	case FX_BORG_TELEPORT:
		FX_BorgTeleport( fx->v[0] );
		break;
	case FX_BORG_HIT:
		FX_BorgHit( fx->v[0], fx->v[1] );
		break;
	case FX_BORG_SHIELD:
		FX_BorgShield( fx->v[0], fx->v[1] );
		break;
	case FX_STASIS_CHARGE:
		FX_StasisCharge( fx->v[0], fx->v[1], fx->f[0] );
		break;
	case FX_GROUND_TENDRIL:
		FX_GroundTendrilSpawner( fx->v[0], fx->v[1] );
		break;
	case FX_GROUND_WARNING:
		FX_Vohrsoth_CreateGroundWarning( fx->v[0] );
		break;
	case FX_CAMERA_SHAKE:
		CGCam_Shake( fx->f[0], fx->i[0] );
		break;
	default:
		break;
	}
}

/*
==============================================================================
The game's calls (game/coop_fx.h): draw, and on a host keep it for the guest
==============================================================================
*/

void COOP_FxSurfaceExplosion( vec3_t origin, vec3_t normal, float radius, float shake_speed, qboolean smoke )
{
	repFx_t *fx = FX_New( FX_SURFACE_EXPLOSION );
	if ( fx )
	{
		VectorCopy( origin, fx->v[0] );
		VectorCopy( normal, fx->v[1] );
		fx->f[0] = radius;
		fx->f[1] = shake_speed;
		fx->i[0] = smoke;
	}
	CG_SurfaceExplosion( origin, normal, radius, shake_speed, smoke );
}

void COOP_FxMiscModelExplosion( vec3_t origin, vec3_t normal )
{
	repFx_t *fx = FX_New( FX_MISC_MODEL_EXPLOSION );
	if ( fx )
	{
		VectorCopy( origin, fx->v[0] );
		VectorCopy( normal, fx->v[1] );
	}
	CG_MiscModelExplosion( origin, normal );
}

void COOP_FxChunks( int owner, vec3_t origin, const vec3_t normal, float speed, int numChunks, material_t chunkType, int customChunk, float baseScale )
{
	repFx_t *fx = FX_New( FX_CHUNKS );
	if ( fx )
	{
		VectorCopy( origin, fx->v[0] );
		VectorCopy( normal, fx->v[1] );
		fx->i[0] = owner;
		fx->i[1] = numChunks;
		fx->i[2] = chunkType;
		fx->i[3] = customChunk;		// a model index: configstrings are the same on both copies
		fx->f[0] = speed;
		fx->f[1] = baseScale;
	}
	CG_Chunks( owner, origin, normal, speed, numChunks, chunkType, customChunk, baseScale );
}

void COOP_FxStasisFlierChildDeath( vec3_t pos )
{
	repFx_t *fx = FX_New( FX_FLIER_CHILD_DEATH );
	if ( fx )
	{
		VectorCopy( pos, fx->v[0] );
	}
	CG_StasisFlierChildDeath( pos );
}

void COOP_FxPsychoJism( vec3_t start, vec3_t end, qboolean open )
{
	repFx_t *fx = FX_New( FX_PSYCHO_JISM );
	if ( fx )
	{
		VectorCopy( start, fx->v[0] );
		VectorCopy( end, fx->v[1] );
		fx->i[0] = open;
	}
	CG_PsychoJism( start, end, open );
}

void COOP_FxShimmeryThing_Spawner( vec3_t start, vec3_t end, float radius, qboolean taper, int duration )
{
	repFx_t *fx = FX_New( FX_SHIMMER );
	if ( fx )
	{
		VectorCopy( start, fx->v[0] );
		VectorCopy( end, fx->v[1] );
		fx->f[0] = radius;
		fx->i[0] = taper;
		fx->i[1] = duration;
	}
	CG_ShimmeryThing_Spawner( start, end, radius, taper, duration );
}

void COOP_FxFireLaser( vec3_t start, vec3_t end, vec3_t normal, vec4_t laserRGB, qboolean hit_ent )
{
	repFx_t *fx = FX_New( FX_FIRE_LASER );
	if ( fx )
	{
		VectorCopy( start, fx->v[0] );
		VectorCopy( end, fx->v[1] );
		VectorCopy( normal, fx->v[2] );
		Vector4Copy( laserRGB, fx->color );
		fx->i[0] = hit_ent;
	}
	CG_FireLaser( start, end, normal, laserRGB, hit_ent );
}

void COOP_FxAimLaser( vec3_t start, vec3_t end, vec3_t normal )
{
	repFx_t *fx = FX_New( FX_AIM_LASER );
	if ( fx )
	{
		VectorCopy( start, fx->v[0] );
		VectorCopy( end, fx->v[1] );
		VectorCopy( normal, fx->v[2] );
	}
	CG_AimLaser( start, end, normal );
}

// one or two points (and a number): the Borg, stasis and Vohrsoth effects
static void FX_KeepPoints( int type, const vec3_t a, const vec3_t b, float f )
{
	repFx_t *fx = FX_New( type );
	if ( fx )
	{
		VectorCopy( a, fx->v[0] );
		if ( b )
		{
			VectorCopy( b, fx->v[1] );
		}
		fx->f[0] = f;
	}
}

void COOP_FxBorgTeleport( vec3_t origin )
{
	FX_KeepPoints( FX_BORG_TELEPORT, origin, NULL, 0 );
	FX_BorgTeleport( origin );
}

void COOP_FxBorgHit( vec3_t origin, vec3_t dir )
{
	FX_KeepPoints( FX_BORG_HIT, origin, dir, 0 );
	FX_BorgHit( origin, dir );
}

void COOP_FxBorgShield( vec3_t position, vec3_t dir )
{
	FX_KeepPoints( FX_BORG_SHIELD, position, dir, 0 );
	FX_BorgShield( position, dir );
}

void COOP_FxStasisCharge( vec3_t origin, vec3_t dir, float perc_done )
{
	FX_KeepPoints( FX_STASIS_CHARGE, origin, dir, perc_done );
	FX_StasisCharge( origin, dir, perc_done );
}

void COOP_FxGroundTendrilSpawner( vec3_t start, vec3_t end )
{
	FX_KeepPoints( FX_GROUND_TENDRIL, start, end, 0 );
	FX_GroundTendrilSpawner( start, end );
}

void COOP_FxVohrsothGroundWarning( vec3_t origin )
{
	FX_KeepPoints( FX_GROUND_WARNING, origin, NULL, 0 );
	FX_Vohrsoth_CreateGroundWarning( origin );
}

// A cutscene's shake also goes with the camera's state (coop_replica.cpp), which then replaces this one.
void COOP_FxCameraShake( float intensity, int duration )
{
	repFx_t *fx = FX_New( FX_CAMERA_SHAKE );
	if ( fx )
	{
		fx->f[0] = intensity;
		fx->i[0] = duration;
	}
	CGCam_Shake( intensity, duration );
}

/*
==============================================================================
The level's globals (coop_replica.cpp)
==============================================================================
*/

// host, writing the globals: the recent effects
void COOP_FxWrite( void )
{
	repFx_t	recent[REP_FX];
	int		n = 0;

	for ( int seq = rep_fxSeq - REP_FX + 1; seq <= rep_fxSeq; seq++ )
	{
		const repFx_t *fx = &rep_fx[seq % REP_FX];
		if ( seq > 0 && fx->seq == seq && level.time - fx->time <= REP_FX_KEEP )
		{
			recent[n++] = *fx;
		}
	}
	if ( n )
	{
		gi.AppendToSaveGame( 'RFX_', recent, n * sizeof( recent[0] ) );
	}
}

// guest, reading the globals (after the direct sounds)
void COOP_FxRead( void )
{
	rep_fxInCount = gi.ReadFromSaveGameOptional( 'RFX_', rep_fxIn, sizeof( rep_fxIn ), NULL ) / (int)sizeof( rep_fxIn[0] );
}

// guest: the globals read back whole; the effects not drawn yet wait for the cgame
void COOP_FxAccept( void )
{
	for ( int i = 0; i < rep_fxInCount && i < REP_FX; i++ )
	{
		if ( rep_fxIn[i].seq <= rep_fxSeen )
		{
			continue;
		}
		rep_fxSeen = rep_fxIn[i].seq;
		if ( rep_fxSynced && rep_fxPendingCount < REP_FX )
		{
			rep_fxPending[rep_fxPendingCount++] = rep_fxIn[i];
		}
	}
	rep_fxInCount = 0;
	rep_fxSynced = qtrue;	// joining: only the effects from now on
}

// guest, after a frame's images are written: draw them (cgameUp false: the cgame isn't running yet)
void COOP_FxPlay( qboolean cgameUp )
{
	if ( cgameUp )
	{
		for ( int i = 0; i < rep_fxPendingCount; i++ )
		{
			FX_Draw( &rep_fxPending[i] );
		}
	}
	rep_fxPendingCount = 0;
}

void COOP_FxInit( void )
{
	memset( rep_fx, 0, sizeof( rep_fx ) );
	rep_fxSeq = rep_fxSeen = rep_fxInCount = rep_fxPendingCount = 0;
	rep_fxSynced = qfalse;
}
