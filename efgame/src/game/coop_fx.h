// coop_fx.h -- single-player co-op: effects the game starts in the cgame directly.
//
// In single player the game and the cgame are one module, so some game code draws an effect by
// calling the cgame itself (a breakable's explosion and debris, a turret's laser), with no entity
// event that a guest's replica would see. Included last in the game files that make such calls, in
// EF_COOP builds it routes them through the co-op module, which draws them as before and, on a host,
// keeps them for the guest (coop_fx.cpp). Without EF_COOP it is empty.

#ifndef COOP_FX_H
#define COOP_FX_H

#ifdef EF_COOP

void	COOP_FxSurfaceExplosion( vec3_t origin, vec3_t normal, float radius, float shake_speed, qboolean smoke );
void	COOP_FxMiscModelExplosion( vec3_t origin, vec3_t normal );
void	COOP_FxChunks( int owner, vec3_t origin, const vec3_t normal, float speed, int numChunks, material_t chunkType, int customChunk, float baseScale );
void	COOP_FxStasisFlierChildDeath( vec3_t pos );
void	COOP_FxPsychoJism( vec3_t start, vec3_t end, qboolean open );
void	COOP_FxShimmeryThing_Spawner( vec3_t start, vec3_t end, float radius, qboolean taper, int duration );
void	COOP_FxFireLaser( vec3_t start, vec3_t end, vec3_t normal, vec4_t laserRGB, qboolean hit_ent );
void	COOP_FxAimLaser( vec3_t start, vec3_t end, vec3_t normal );

#define CG_SurfaceExplosion			COOP_FxSurfaceExplosion
#define CG_MiscModelExplosion		COOP_FxMiscModelExplosion
#define CG_Chunks					COOP_FxChunks
#define CG_StasisFlierChildDeath	COOP_FxStasisFlierChildDeath
#define CG_PsychoJism				COOP_FxPsychoJism
#define CG_ShimmeryThing_Spawner	COOP_FxShimmeryThing_Spawner
#define CG_FireLaser				COOP_FxFireLaser
#define CG_AimLaser					COOP_FxAimLaser

#endif	// EF_COOP

#endif	// COOP_FX_H
