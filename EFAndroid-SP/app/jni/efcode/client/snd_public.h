/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/


void S_Init( void );
void S_Shutdown( void );

// if origin is NULL, the sound will be dynamically sourced from the entity
void S_StartSound( vec3_t origin, int entnum, int entchannel, sfxHandle_t sfx );
// like S_StartSound but at an explicit volume (0-255 full-scale); origin==NULL = non-attenuated
void S_StartSoundVolume( vec3_t origin, int entnum, int entchannel, sfxHandle_t sfx, int volume );
void S_StartLocalSound( sfxHandle_t sfx, int channelNum );

void S_StartBackgroundTrack( const char *intro, const char *loop );
void S_StopBackgroundTrack( void );

// cinematics and voice-over-network will send raw samples
// 1.0 volume will be direct output of source samples
void S_RawSamples(int stream, int samples, int rate, int width, int channels,
				   const byte *data, float volume, int entityNum);

// raw stream for movie audio (Bink and RoQ), apart from the music on stream 0 and VoIP
// on 1..MAX_CLIENTS*2, so an in-game movie can play while the music is paused
#define S_RAW_STREAM_CINEMATIC	( MAX_CLIENTS * 2 + 1 )

// stop all sounds and the background track
void S_StopAllSounds( void );
int S_StopAllSoundsCount( void );	// times S_StopAllSounds has run (lets SP drop its voice timers)

// Single player pause: hold the game's playing sounds and the music in place (not painted,
// positions kept) and resume them where they were. Sounds started while held play normally.
void S_PauseGameSounds( qboolean pause );
// stop every sound effect and loop but leave the music and the raw streams playing (SP cutscene skip)
void S_StopGameSounds( void );
// single player lip sync: levels[ent] for every entity talking on CHAN_VOICE / CHAN_VOICE_ATTEN:
// -1 quiet, 5..8 mouth shapes (head skin offsets), 0 not talking. qfalse when the backend can't.
qboolean S_VoiceLipSync( int *levels, int numEntities );
void S_ResetStaleSounds( void );	// on map load: unload sounds another mod/pk3 now replaces

// all continuous looping sounds must be added before calling S_Update
void S_ClearLoopingSounds( qboolean killall );
void S_AddLoopingSound( int entityNum, const vec3_t origin, const vec3_t velocity, sfxHandle_t sfx );
void S_AddRealLoopingSound( int entityNum, const vec3_t origin, const vec3_t velocity, sfxHandle_t sfx );
void S_StopLoopingSound(int entityNum );

// recompute the relative volumes for all running sounds
// relative to the given entityNum / orientation
void S_Respatialize( int entityNum, const vec3_t origin, vec3_t axis[3], int inwater );

// let the sound system know where an entity currently is
void S_UpdateEntityPosition( int entityNum, const vec3_t origin );

void S_Update( void );

void S_DisableSounds( void );

void S_BeginRegistration( void );

// RegisterSound will allways return a valid sample, even if it
// has to create a placeholder.  This prevents continuous filesystem
// checks for missing files
sfxHandle_t	S_RegisterSound( const char *sample, qboolean compressed );
int			S_SoundDuration( sfxHandle_t sfx );   // EF1 SP: playing time in ms (0 if invalid)

void S_DisplayFreeMemory(void);

void S_ClearSoundBuffer( void );

void SNDDMA_Activate( void );

void S_UpdateBackgroundTrack( void );


#ifdef USE_VOIP
void S_StartCapture( void );
int S_AvailableCaptureSamples( void );
void S_Capture( int samples, byte *data );
void S_StopCapture( void );
void S_MasterGain( float gain );
#endif

