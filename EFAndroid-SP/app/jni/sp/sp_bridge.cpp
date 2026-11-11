// sp_bridge.cpp — GetGameAPI bridge: hosts the SP game module (libefgame.so) inside the
// reused HM engine. Fills game_import_t from the engine's real FS/CM/cvar services + a
// minimal CM-backed world, loads a map, and drives ge->Init / ge->RunFrame.
//
// Compiled against the SP game headers (+compat shim). Calls the lilium engine via extern "C"
// declarations whose signatures are ABI-compatible with the SP boundary types (trace_t/vec3_t/
// cvar_t-first-9 all match — see reconciliation research).

#include "q_shared.h"
#include "g_public.h"
#include "tr_types.h"             // glconfig_t, refEntity_t, refdef_t
#include "../cgame/cg_public.h"   // cgameImport_t (CG_*), snapshot_t
#include "../client/vmachine.h"   // cgameExport_t (CG_INIT, CG_DRAW_ACTIVE_FRAME)
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <time.h>
#include <android/log.h>

// Save-game description typed in the EF Save menu (set by UII_SG_StoreSaveGameComment,
// sp_ui_bridge.cpp). SP_SaveGame writes it into the COMM chunk so the Load list shows a name.
extern char g_spSaveComment[128];

// Com_Printf/Com_Error come via the C shim (SP headers declare them C++-linkage).
extern "C" { void (*SP_ComPrintf(void))(const char*,...); void SP_ComError(int,const char*,...); const char *SP_ModulePath(const char*); }
// Bridge logging. Android: everything to logcat at INFO, as before. Desktop (macos/android/log.h):
// blog = developer-only chatter, binfo = always shown, bwarn = always shown in yellow.
#ifdef __ANDROID__
#define SP_LOG_DEV  ANDROID_LOG_INFO
#define SP_LOG_WARN ANDROID_LOG_INFO
#else
#define SP_LOG_DEV  ANDROID_LOG_DEBUG
#define SP_LOG_WARN ANDROID_LOG_WARN
#endif
static void blog( const char *fmt, ... ){ va_list a; va_start(a,fmt); __android_log_vprint(SP_LOG_DEV,"EFSP",fmt,a); va_end(a); }
static void binfo( const char *fmt, ... ){ va_list a; va_start(a,fmt); __android_log_vprint(ANDROID_LOG_INFO,"EFSP",fmt,a); va_end(a); }
static void bwarn( const char *fmt, ... ){ va_list a; va_start(a,fmt); __android_log_vprint(SP_LOG_WARN,"EFSP",fmt,a); va_end(a); }

// ---- lilium engine services (resolved at link against the engine objects) ----
extern "C" {
    void  SP_PumpLoadScreen( void );   // sp_integration.c: drive the 1:1 cgame load screen on cgi_UpdateScreen
    void  SP_ArmMenuSuppress( void );  // sp_integration.c: hide the menu 1 frame for a clean save-thumbnail capture
    void  SP_ResetViewAngles( void );  // sp_integration.c: zero cl.viewangles at level start (retail CL_ClearState)
    int   Sys_Milliseconds( void );
    int   Cmd_Argc( void );
    char *Cmd_Argv( int arg );
    void  Cbuf_AddText( const char *text );
    void  CL_SetUserCmdValue( int userCmdValue, float sensitivityScale );  // sets cl.cgameSensitivity (mouse-look scale)
    void  Cmd_TokenizeString( const char *text );
    void  Cmd_AddCommand( const char *cmd_name, void (*function)(void) );
    unsigned Com_BlockChecksum( const void *buffer, int length );   // idTech3 MD4-XOR (retail .sav checksum)
    int   FS_Seek( fileHandle_t f, long offset, int origin );
    int   FS_FOpenFileWrite( const char *qpath );
    int   FS_FOpenFileRead( const char *qpath, fileHandle_t *file, qboolean uniqueFILE );
    void  FS_Rename( const char *from, const char *to );
    cvar_t *Cvar_Get( const char *name, const char *value, int flags );
    void  Cvar_Set( const char *name, const char *value );
    int   Cvar_Flags( const char *name );   // engine flags, or CVAR_NONEXISTENT if unregistered
    int   Cvar_VariableIntegerValue( const char *name );
    void  Cvar_VariableStringBuffer( const char *name, char *buf, int bufsize );
    char *Cvar_InfoString_Big( int bit );   // all CVAR_ARCHIVE cvars as "\key\val\..." for the CVCN save bag
    int   FS_FOpenFileByMode( const char *qpath, fileHandle_t *f, fsMode_t mode );
    int   FS_Read( void *buffer, int len, fileHandle_t f );
    int   FS_Write( const void *buffer, int len, fileHandle_t f );
    void  FS_FCloseFile( fileHandle_t f );
    long  FS_ReadFile( const char *qpath, void **buffer );
    void  FS_FreeFile( void *buffer );
    int   FS_GetFileList( const char *path, const char *ext, char *listbuf, int bufsize );
    // CM (collision) — model 0 = world BSP
    void  CM_LoadMap( const char *name, qboolean clientload, int *checksum );
    void  CM_ClearMap( void );              // EF1 SP 1:1: paired with Hunk_Clear in SV_SpawnServer
    void  Hunk_Clear( void );               // reclaim the permanent hunk between maps (see SP_FinishTransition)
    char *CM_EntityString( void );
    int   CM_NumInlineModels( void );
    int   CM_InlineModel( int index );
    void  CM_ModelBounds( int model, vec3_t mins, vec3_t maxs );
    int   CM_PointContents( const vec3_t p, int model );
    void  CM_BoxTrace( trace_t *results, const vec3_t start, const vec3_t end,
                       const vec3_t mins, const vec3_t maxs, int model, int brushmask, int capsule );
    int   CM_TempBoxModel( const vec3_t mins, const vec3_t maxs, int capsule );
    void  CM_TransformedBoxTrace( trace_t *results, const vec3_t start, const vec3_t end,
                                  const vec3_t mins, const vec3_t maxs, int model, int brushmask,
                                  const vec3_t origin, const vec3_t angles, int capsule );
    int   CM_TransformedPointContents( const vec3_t p, int model, const vec3_t origin, const vec3_t angles );
    int   CM_PointLeafnum( const vec3_t p );
    int   CM_LeafArea( int leafnum );
    int   CM_LeafCluster( int leafnum );
    unsigned char *CM_ClusterPVS( int cluster );
    qboolean CM_AreasConnected( int area1, int area2 );
    void  CM_AdjustAreaPortalState( int area1, int area2, qboolean open );
    int   CM_PortalStateSize( void );                      // save 'PRTS': numAreas^2 ref counts (bytes)
    const int *CM_PortalState( void );
    qboolean CM_SetPortalState( const int *counts, int size );
    int   CM_BoxLeafnums( const vec3_t mins, const vec3_t maxs, int *list, int listsize, int *lastLeaf );
    void  Cvar_Register( vmCvar_t *vmCvar, const char *varName, const char *defaultValue, int flags );
    void  Cvar_Update( vmCvar_t *vmCvar );
    void  Cmd_ArgsBuffer( char *buffer, int bufferLength );
    // qcommon/regress.c: the regression harness (macos/regress.sh), all no-ops unless +set regress_log
    qboolean Regress_Active( void );
    int   Regress_Seed( void );
    void  Regress_SeedRand( int gameTime );
    void  Regress_BeginFrame( const char *map, int gameTime );
    void  Regress_State( int kind, int num, const char *fields );
    void  Regress_EndFrame( void );
}

// ---- game module ----
typedef game_export_t* (*GetGameAPI_t)( game_import_t* );
typedef void (*dllEntry_t)( intptr_t (*)(intptr_t,...) );
static void          *g_lib = NULL;
static game_export_t *ge    = NULL;
static int            g_levelTime = 0;
// The cgame's vmMain takes 9 fixed ints (cg_main.cpp). Call it through that exact type: a variadic
// pointer happens to work on Linux/Android AAPCS64 (varargs in registers), but on Apple arm64 varargs
// go on the stack and the callee reads garbage registers (-> "CG_DrawActive: Undefined stereoView").
typedef int (*vmMain_t)( int cmd, int, int, int, int, int, int, int, int );
static vmMain_t g_vmMain = NULL;
// The cgame's camera-position query (cg_main.cpp CG_GetCameraPos, behind its CG_CAMERA_POS export): the
// retail engine built snapshots from the cinematic camera's position while a camera was on. Called
// directly because vmMain's int arguments can't carry a 64-bit pointer. NULL if the module lacks it.
typedef int (*camPos_t)( float *pos );
static camPos_t g_camPos = NULL;
// The game's state dump for the regression harness (g_regress.cpp G_RegressDump), called after every
// game frame when the harness is on (SP_GameRunFrame). NULL if the module lacks it.
typedef void (*regressDump_t)( void (*emit)( int kind, int num, const char *fields ) );
static regressDump_t g_regressDump = NULL;

#ifdef EF_COOP
// Single-player co-op: the game's co-op code gets the guest's input from the engine's co-op
// connection (efcode/sys/sys_coop_net.c) through this table (game/coop_bridge.h).
#include "coop_bridge.h"
extern "C" {
    int         CoopNet_GuestInput( int *angles, int *buttons, int *weapon, signed char *forwardmove,
                                    signed char *rightmove, signed char *upmove );
    const char *CoopNet_GuestName( void );
    int         CoopNet_HostHasGuest( void );
    int         CoopNet_GuestViewTime( void );
}
static qboolean SP_CoopGuestCmd( usercmd_t *cmd ){
    int angles[3], buttons = 0, weapon = 0; signed char fwd = 0, side = 0, up = 0;
    if( !CoopNet_GuestInput( angles, &buttons, &weapon, &fwd, &side, &up ) ) return qfalse;
    cmd->angles[0] = angles[0]; cmd->angles[1] = angles[1]; cmd->angles[2] = angles[2];
    cmd->buttons = buttons; cmd->weapon = (byte)weapon;
    cmd->forwardmove = fwd; cmd->rightmove = side; cmd->upmove = up;
    return qtrue;
}
static qboolean SP_CoopGuestConnected( void ){ return CoopNet_HostHasGuest() ? qtrue : qfalse; }
static void SP_CoopSetBridge( void ){
    typedef void (*coopSetBridge_t)( const coopBridge_t * );
    static coopBridge_t bridge = { COOP_BRIDGE_VERSION, SP_CoopGuestCmd, CoopNet_GuestName, SP_CoopGuestConnected,
                                   CoopNet_GuestViewTime };
    coopSetBridge_t set = (coopSetBridge_t)dlsym( g_lib, "COOP_SetBridge" );   // only in EF_COOP game modules
    if( set ) set( &bridge );
}
// The replica (co-op M1): the host sends its world after every game frame; a guest's level
// (sp_coop 2 at its start) runs no game frames and writes the host's frames into its entities
// (game/coop_bridge.h, efcode/sys/sys_coop_net.c).
extern "C" {
#include "../efcode/sys/sys_coop_net.h"
    qboolean CL_InGameCinematicActive( void );   // cl_cin.c
    void     SCR_StopCinematic( void );
}
static const coopReplica_t *g_coopRep = NULL;   // the game module's replica functions
static int  g_coopReplica = 0;      // this level is a guest's replica
static int  g_coopLevelId = 0;      // host: counts level starts, so the guest knows a new level from a frame
static int  g_coopApplied = 0;      // guest: the last host frame written into this level
static int  g_coopAppliedAt = 0;    // ...and when (Sys_Milliseconds), for the cgame's clock
static int  g_coopCmdSeen = 0;      // guest: the newest host server command passed to the cgame
static int  g_coopHoldLevel = 0;    // host: this level's start waits for the guest to load it (0: no wait)
static int  g_coopHoldAt = 0;       // ...since (Sys_Milliseconds; 0 until the first drawn frame)
static int  g_coopHoldSent = 0;     // ...and the last frame sent meanwhile
#define COOP_HOLD_MAX  30000        // ms: a guest that hasn't loaded the level by then isn't waited for
#define COOP_CMDS 64
static int  g_coopSeq = 0;          // guest: its commands (never reset: the host takes only newer numbers)
static int  g_coopAck = 0;          // guest: the newest of them the host applied (see SP_CoopMakeCmd)
static int  SP_CoopCurrentCmd( void );
// Co-op saves. The .sav stays a plain single-player save (player 2 isn't in it, see the game's
// coop_save.cpp); player 2's health, armor, weapon and ammo go to <name>.coop next to it, which single
// player never reads. Loading the save hands them to the game the way a map change does (coop_p2save,
// read at the level start). A save without player 2 removes an old .coop of the same name.
extern "C" {
    void  FS_WriteFile( const char *qpath, const void *buffer, int size );
    void  FS_HomeRemove( const char *homePath );
    qboolean FS_FileExists( const char *file );
}
static void SP_CoopWriteSideFile( const char *name ){
    char p2[1024], path[96];
    Cvar_VariableStringBuffer( "coop_p2saved", p2, sizeof(p2) );
    snprintf( path, sizeof(path), "saves/%s.coop", name );
    if( p2[0] ){
        FS_WriteFile( path, p2, (int)strlen( p2 ) );
        blog("co-op: wrote %s (player 2: %s)\n", path, p2);
    } else if( FS_FileExists( path ) ){
        FS_HomeRemove( path );
    }
    Cvar_Set( "coop_p2saved", "" );
}
static void SP_CoopReadSideFile( const char *name ){
    char path[96], p2[1024]; void *buf = NULL;
    snprintf( path, sizeof(path), "saves/%s.coop", name );
    long len = FS_ReadFile( path, &buf );
    if( len > 0 && buf ){
        int n = len < (long)sizeof(p2) - 1 ? (int)len : (int)sizeof(p2) - 1;
        memcpy( p2, buf, n ); p2[n] = 0;
        FS_FreeFile( buf );
        Cvar_Set( "coop_p2save", p2 );
        blog("co-op: read %s (player 2: %s)\n", path, p2);
    } else {
        if( buf ) FS_FreeFile( buf );
        char cur[8]; Cvar_VariableStringBuffer( "coop_p2save", cur, sizeof(cur) );
        if( cur[0] ) Cvar_Set( "coop_p2save", "" );   // (not the last level's: this save starts afresh)
    }
}
// host: a level starts with a guest connected: hold its first game frame until the guest has it too
static void SP_CoopHoldStart( void ){
    g_coopHoldLevel = ( g_coopRep && !g_coopReplica && CoopNet_HostHasGuest() ) ? g_coopLevelId : 0;
    g_coopHoldAt = 0; g_coopHoldSent = 0;
}
static qboolean SP_CoopUserCmd( int n, usercmd_t *cmd );
static void SP_CoopGetReplica( void ){
    typedef const coopReplica_t *(*coopGetReplica_t)( void );
    coopGetReplica_t get = (coopGetReplica_t)dlsym( g_lib, "COOP_GetReplica" );
    g_coopRep = get ? get() : NULL;
    if( g_coopRep && g_coopRep->version != COOP_REPLICA_VERSION ) g_coopRep = NULL;
}
#endif
static intptr_t SP_VmMain( int cmd, int a0=0, int a1=0, int a2=0, int a3=0, int a4=0, int a5=0, int a6=0, int a7=0 ){
    return g_vmMain( cmd, a0, a1, a2, a3, a4, a5, a6, a7 );
}
static int g_spActive = 0;                          // cgame entry point
static intptr_t SP_CgameSyscall( intptr_t cmd, ... );    // cgame->engine dispatcher (defined below)

// ---- minimal server world (entities link here so EntitiesInBox/trace can find them) ----
#define SP_MAX_ENT 1024
static gentity_t *g_linked[SP_MAX_ENT];
static int        g_numLinked = 0;
static char       g_curMap[64] = {0};   // current SP map name (for save framing + transitions)
static int        g_entClip[SP_MAX_ENT];   // bmodel clipHandle per entity number (for mover traces)
static int        g_entArea[SP_MAX_ENT][2]; // [0]=areanum [1]=areanum2 (-1 none); per-entity, set at link (retail svEntity_t)
static signed char g_entPortalOpen[SP_MAX_ENT]; // per-door areaportal state: 1 open, 0 closed, -1 unknown (a load
                                                // restored CM's counts from the save's PRTS) -> guard vs CM ref-count drift
static int        *g_loadPortals = NULL;        // a user load's PRTS chunk, applied in SP_FinishTransition
static int         g_loadPortalsSize = 0;
static void SP_DropLoadPortals( void ){ free(g_loadPortals); g_loadPortals = NULL; g_loadPortalsSize = 0; }
static int SP_EntNum( gentity_t *e ){ return (int)(((unsigned char*)e - (unsigned char*)ge->gentities) / ge->gentitySize); }


#define CS_MAX 1024
static char g_configstrings[CS_MAX][1024];

// EF1 SP 1:1 load-from-save background: the loaded savegame's 256x256 RGBA 'SHOT' screenshot, stashed by
// SP_LoadGameFile so the loading screen's cgi_R_DrawScreenShot (CG_DrawInformation's eFULL branch) can blit it
// fullscreen behind the LCARS bar — exactly as retail's engine draws its stored 'SHOT' buffer (unk_575034).
// Bottom-up like the save-menu thumbnail; the cgame passes a negative height so RB_StretchPic flips it upright.
static unsigned char g_loadShot[256*256*4];
static int           g_loadShotValid = 0;

// server->client command channel (subtitles, objectives, centerprints, music...). The game calls
// gi.SendServerCommand; the cgame pulls them by sequence via CG_GETSERVERCOMMAND up to the snapshot's
// serverCommandSequence (cg_servercmds.cpp CG_ExecuteNewServerCommands).
#define SC_RING 256
static char g_svCmds[SC_RING][1024];
static int  g_svCmdSeq = 0;          // latest server command sequence number
static int  g_snapNum   = 1;         // snapshot sequence (declared early: reset in SP_SpawnServer per map)
static int  g_cmdNum    = 1;         // usercmd sequence

// ---- snapshot ring (retail cl.snapshots[PACKET_BACKUP]) + fixed 50ms sim cadence (retail SV_Frame) ----
// Declared early because SP_SpawnServer's per-map reset (below) references them. The ring lets
// CG_GETSNAPSHOT(num) return DISTINCT numbered snapshots so cg.snap (N) and cg.nextSnap (N+1) carry
// serverTimes 50ms apart -> CG_CalcEntityLerpPositions / CG_InterpolatePlayerState interpolate instead of
// teleporting. g_snap (the scratch) is copied into the ring at the end of BuildSnapshot. SNAP_RING power-of-2 >=2.
#define SNAP_RING 4
static snapshot_t g_snapHist[SNAP_RING];
static int        g_snapBaseTime = 0;        // serverTime of snapshot #1 this map (cg.time clamp floor)
#define SP_SV_FRAMEMSEC   50
#define SP_INTERP_DELAY   50                  // trail newest snap by one tick so a nextSnap always exists
#define SP_MAX_CATCHUP    5                   // drain at most 5 ticks (250ms) per render frame (hitch cap)
#define SP_SKIP_MAX_TICKS 200                 // cutscene skip: up to 200 ticks (10 s of game time) per frame...
#define SP_SKIP_BUDGET_MS 12                  // ...or 12 ms of real time, whichever comes first
static int        g_simResidual = 0;          // millisecond accumulator (retail sv.timeResidual)

// ---- savegame: byte-compatible with retail .sav (verified against real PC saves) ----
// chunk on disk = [chid:4 LE][length:4][checksum:4][data:length][magic:4=0x1234abcd]; checksum =
// Com_BlockChecksum (idTech3 MD4-XOR). gi.AppendToSaveGame/ReadFromSaveGame[Optional] operate on the
// one open save file (shared with the engine-side save/load commands via these globals).
static fileHandle_t s_sgFile = 0;
static qboolean     s_sgWriting = qfalse;
#define SG_MAGIC 0x1234abcdu

static gentity_t *SP_GEntity( int num ){ return (gentity_t*)((unsigned char*)ge->gentities + ge->gentitySize*num); }

// ----- world implementations -----
static void W_LinkEntity( gentity_t *ent ){
    // derive absmin/absmax from currentOrigin + mins/maxs. A ROTATED brush model (non-zero
    // currentAngles) must expand to a rotation-safe radius cube, exactly like retail SV_LinkEntity
    // (efcode/server/sv_world.c). Otherwise the link box stays the entity's UNROTATED AABB, and once
    // the piece tilts (e.g. dn5 'fallwalk'/'fallpillar' tipping over into a walkway, or any rotating
    // door/mover) its real rotated extent falls outside that box, so the broad-phase cull in W_Trace /
    // W_EntitiesInBox discards the entity and the player falls straight through the fallen catwalk.
    // The narrow phase (CM_TransformedBoxTrace) already rotates by currentAngles; only this link box
    // was missing the expansion. Applies on every map, so all rotating movers are covered.
    if( ent->bmodel && ( ent->currentAngles[0] || ent->currentAngles[1] || ent->currentAngles[2] ) ){
        // RadiusFromBounds (inlined; it's a C symbol and this is C++ -> avoid the linkage mismatch):
        // the rotation-safe radius is the length of the per-axis max(|min|,|max|) corner.
        vec3_t corner;
        for(int i=0;i<3;i++){
            float a = (ent->mins[i] < 0) ? -ent->mins[i] : ent->mins[i];
            float b = (ent->maxs[i] < 0) ? -ent->maxs[i] : ent->maxs[i];
            corner[i] = (a > b) ? a : b;
        }
        float rad = sqrtf( corner[0]*corner[0] + corner[1]*corner[1] + corner[2]*corner[2] );
        for(int i=0;i<3;i++){
            ent->absmin[i] = ent->currentOrigin[i] - rad - 1;
            ent->absmax[i] = ent->currentOrigin[i] + rad + 1;
        }
    } else {
        for(int i=0;i<3;i++){
            ent->absmin[i] = ent->currentOrigin[i] + ent->mins[i] - 1;
            ent->absmax[i] = ent->currentOrigin[i] + ent->maxs[i] + 1;
        }
    }
    // Retail SV_LinkEntity area assignment (brush models only -- doors/breakables drive areaportals): collect
    // the leafs the entity spans; areanum = first real area, areanum2 = a distinct second area when it straddles
    // one (a closed door in a doorway straddles the two areas its areaportal joins). Consumed by
    // W_AdjustAreaPortalState. Recomputed every link, exactly like retail.
    if( ent->bmodel ){
        int eNum = SP_EntNum(ent);
        if( eNum >= 0 && eNum < SP_MAX_ENT ){
            int leafs[128], lastLeaf=0;
            int num = CM_BoxLeafnums( ent->absmin, ent->absmax, leafs, 128, &lastLeaf );
            int a1=-1, a2=-1;
            for( int i=0; i<num; i++ ){
                int area = CM_LeafArea( leafs[i] );
                if( area == -1 ) continue;
                if( a1 != -1 && a1 != area ) a2 = area;
                else                         a1 = area;
            }
            g_entArea[eNum][0] = a1;
            g_entArea[eNum][1] = a2;
        }
    }
    // Encode size into entityState.solid for client prediction (mirrors SV_LinkEntity sv_world.c:222-310):
    // SOLID_BMODEL for inline brush models (CG_Mover requires this to pick cgs.inlineDrawModel[] over
    // model_draw[] — else every func_* mover resolves hModel=0 and is invisible); else a packed bbox for
    // solid/body boxes so the client predicts collisions; else 0.
    if( ent->bmodel ) {
        ent->s.solid = SOLID_BMODEL;
    } else if ( ent->contents & ( CONTENTS_SOLID | CONTENTS_BODY ) ) {
        int i = (int)ent->maxs[0];        if(i<1)i=1; if(i>255)i=255;   // x/y assumed symmetric
        int j = (int)(-ent->mins[2]);     if(j<1)j=1; if(j>255)j=255;   // z not symmetric
        int k = (int)(ent->maxs[2]+32);   if(k<1)k=1; if(k>255)k=255;   // z maxs can be negative
        ent->s.solid = (k<<16) | (j<<8) | i;
    } else {
        ent->s.solid = 0;
    }
    // Ensure this entity is in g_linked exactly once. Do NOT trust ->linked alone for membership:
    // after a savegame reload (SP_FinishTransition resets g_numLinked=0) the flag can survive as true
    // while g_linked is empty, so the old `if(!linked)` guard skipped re-adding it. MOVING entities
    // self-heal (G_MoverPush unlinks+relinks every frame), but STATIC ones (triggers like the tutorial's
    // continue_15 lava-cross trigger, static brushes) link once and never re-add -> they silently
    // vanished from EntitiesInBox/W_Trace (no trigger touch, no collision). Search-add (list is small).
    ent->linked = qtrue;
    {
        int li; qboolean present = qfalse;
        for(li=0; li<g_numLinked; li++){ if(g_linked[li]==ent){ present = qtrue; break; } }
        if(!present && g_numLinked < SP_MAX_ENT) g_linked[g_numLinked++] = ent;
    }
}
static void W_UnlinkEntity( gentity_t *ent ){
    if(!ent->linked) return;
    ent->linked = qfalse;
    for(int i=0;i<g_numLinked;i++) if(g_linked[i]==ent){ g_linked[i]=g_linked[--g_numLinked]; break; }
}
static void W_Trace( trace_t *results, const vec3_t start, const vec3_t mins, const vec3_t maxs,
                     const vec3_t end, int passEntityNum, int contentmask ){
    // 1) world BSP
    CM_BoxTrace( results, start, end, mins, maxs, 0, contentmask, 0 );
    results->entityNum = (results->fraction != 1.0f) ? ENTITYNUM_WORLD : ENTITYNUM_NONE;
    if(results->fraction == 0) return;

    // 2) brush-model movers only — doors / lifts / func_* / force-fields. Iterate the LIVE g_linked
    // list (kept in sync by W_LinkEntity/W_UnlinkEntity). The bmodel filter is cheap; CM_TransformedBoxTrace
    // only runs for the handful of brush models. NOTE: mins/maxs may be NULL (point/line traces:
    // line-of-sight, bullets, the transporter beam) — substitute a zero box or we segfault.
    static const float vz[3] = {0,0,0};
    const float *lmins = mins ? mins : vz, *lmaxs = maxs ? maxs : vz;
    vec3_t tmins, tmaxs;
    for(int k=0;k<3;k++){
        tmins[k] = (start[k]<end[k]?start[k]:end[k]) + lmins[k] - 1;
        tmaxs[k] = (start[k]>end[k]?start[k]:end[k]) + lmaxs[k] + 1;
    }
    // Owner-based skipping (retail SV_ClipMoveToEntities): a trace must ignore not just the
    // pass entity itself but anything OWNED by it (its own projectiles), its OWNER, and siblings sharing that
    // owner. Without this a thrown/fired projectile collides with its shooter the instant it spawns ("detonates
    // on the muzzle") and a player riding a mover he owns self-blocks. EF gentity has `gentity_t *owner`.
    gentity_t* passEnt   = (passEntityNum>=0 && passEntityNum<ge->num_entities) ? SP_GEntity(passEntityNum) : NULL;
    gentity_t* passOwner = passEnt ? passEnt->owner : NULL;
    for(int i=0;i<g_numLinked;i++){
        gentity_t* e = g_linked[i];
        int eNum = SP_EntNum(e);
        if(eNum == passEntityNum || eNum < 0 || eNum >= SP_MAX_ENT) continue;
        if(passEnt){
            if(e->owner == passEnt)                continue;   // entity owned by the pass entity (own projectile)
            if(e == passOwner)                     continue;   // the pass entity's owner
            if(passOwner && e->owner == passOwner) continue;   // sibling sharing the same owner
        }
        if(!(e->contents & contentmask)) continue;
        if(e->absmin[0]>tmaxs[0]||e->absmax[0]<tmins[0]||e->absmin[1]>tmaxs[1]||
           e->absmax[1]<tmins[1]||e->absmin[2]>tmaxs[2]||e->absmax[2]<tmins[2]) continue;
        int h;
        if( e->bmodel ){
            // Brush-model movers (doors/lifts/func_*/force-fields). Derive the clip handle from the
            // entity's OWN inline-model index (like SV_ClipHandleForEntity), NOT the cached g_entClip[]
            // slot which could be stale after a savegame reload -> broad-phase used correct (mins-derived)
            // bounds while the narrow trace hit a DIFFERENT mover -> player passed through lava stones/doors.
            h = ( e->s.modelindex > 0 ) ? CM_InlineModel( e->s.modelindex ) : g_entClip[eNum];
            if(h <= 0) continue;
        } else {
            // Point/bbox solids: misc_model_*_health terminals, point breakables, item-style usables.
            // Retail SV_ClipHandleForEntity builds a TEMP BOX hull from the entity's own mins/maxs so
            // gi.trace can hit them. The bridge previously skipped all non-bmodels, so the cgame crosshair
            // trace (CG_ScanForCrosshairEntity) AND the player's TryUse trace never saw point solids ->
            // no LCARS "use" hint AND the health terminal could not be activated (it fires part3->teleport).
            h = CM_TempBoxModel( e->mins, e->maxs, 0 );
            if(h <= 0) continue;
        }
        trace_t tr;
        // Rotation applies ONLY to brush models (retail SV_ClipMoveToEntities: non-bmodels pass vec3_origin). A box
        // solid built from mins/maxs is axis-aligned; rotating it by the entity's currentAngles (NPCs face
        // directions) shrinks/rotates the collision box away from its AABB -> bullets/use-traces against a
        // turned NPC or breakable miss or hit at the wrong fraction. Pass angles only for bmodels.
        CM_TransformedBoxTrace(&tr, start, end, lmins, lmaxs, h, contentmask, e->currentOrigin,
                               e->bmodel ? e->currentAngles : vz, 0);
        if(tr.startsolid) results->startsolid = qtrue;
        if(tr.allsolid)   results->allsolid   = qtrue;
        if(tr.fraction < results->fraction){
            qboolean oldStart = results->startsolid;
            tr.entityNum = eNum;
            *results = tr;
            results->startsolid |= oldStart;
        }
    }
}
static int  W_PointContents( const vec3_t p, int passEntityNum ){
    int contents = CM_PointContents(p, 0);   // world model 0
    // Retail SV_PointContents ORs in every overlapping brush-model's real contents. The world-only stub missed
    // water/lava/slime/fog/trigger volumes attached to func_* brush models (moving water, force-fields, content
    // brushes) -> drowning/fog/content-gated triggers on bmodels misbehaved. Walk the linked bmodels.
    for(int i=0;i<g_numLinked;i++){
        gentity_t* e = g_linked[i];
        if(e == NULL || !e->bmodel || e->s.modelindex <= 0) continue;
        if(SP_EntNum(e) == passEntityNum) continue;
        if(p[0]<e->absmin[0]||p[0]>e->absmax[0]||p[1]<e->absmin[1]||p[1]>e->absmax[1]||
           p[2]<e->absmin[2]||p[2]>e->absmax[2]) continue;
        int h = CM_InlineModel(e->s.modelindex);
        if(h<=0) continue;
        contents |= CM_TransformedPointContents(p, h, e->currentOrigin, e->currentAngles);
    }
    return contents;
}
// Cluster-only PVS (retail SV_inPVSIgnorePortals): visibility regardless of door/areaportal state.
static qboolean W_InPVSIgnorePortals( const vec3_t p1, const vec3_t p2 ){
    int l1=CM_PointLeafnum(p1), l2=CM_PointLeafnum(p2);
    unsigned char *vis=CM_ClusterPVS(CM_LeafCluster(l1));
    int c2=CM_LeafCluster(l2);
    if(c2<0) return qfalse;
    return (vis[c2>>3] & (1<<(c2&7))) ? qtrue : qfalse;
}
// Full PVS (retail SV_inPVS): cluster vis AND the two leaf areas connected. The area test is what makes a
// CLOSED door (sealed areaportal) block visibility -- previously absent because W_AdjustAreaPortalState was a
// no-op, so inPVS never closed across a shut door. Consequence: a BS_REMOVE NPC behind a closed door
// (NPC_BSRemove despawns only when !inPVS(self,player)) stayed solid forever and deadlocked scripted walks
// (voy6: Chang parked on foster's navgoal). Snapshot building does NOT use inPVS, so tightening it cannot
// drop entities from the client; only game-side AI/sound/removal become portal-correct, matching retail.
static qboolean W_InPVS( const vec3_t p1, const vec3_t p2 ){
    if( !W_InPVSIgnorePortals(p1,p2) ) return qfalse;
    int l1=CM_PointLeafnum(p1), l2=CM_PointLeafnum(p2);
    if( !CM_AreasConnected( CM_LeafArea(l1), CM_LeafArea(l2) ) ) return qfalse;
    return qtrue;
}
static void W_SetBrushModel( gentity_t *ent, const char *name ){
    if(!name||name[0]!='*'){ ent->bmodel=qfalse; return; }
    // Real engine SV_SetBrushModel sets s.modelindex = the inline model number; the cgame's CG_Mover
    // indexes cgs.inlineDrawModel[s1->modelindex]. Without this it reads inlineDrawModel[0]=0 -> hModel=0
    // -> CG_Mover early-returns -> EVERY func_* brush-model mover (doors, lava stones, lifts) invisible.
    ent->s.modelindex = atoi(name+1);
    int h = CM_InlineModel( atoi(name+1) );
    CM_ModelBounds( h, ent->mins, ent->maxs );
    ent->bmodel = qtrue;
    ent->contents = -1; // CONTENTS_SOLID-ish; refined later
    int eNum = SP_EntNum(ent);
    if(eNum>=0 && eNum<SP_MAX_ENT) g_entClip[eNum] = h;   // remember the clip handle for W_Trace
    W_LinkEntity(ent);
}
static int  W_EntitiesInBox( const vec3_t mins, const vec3_t maxs, gentity_t **list, int maxcount ){
    int n=0;
    for(int i=0;i<g_numLinked && n<maxcount;i++){
        gentity_t *e=g_linked[i];
        if(e->absmin[0]>maxs[0]||e->absmax[0]<mins[0]) continue;
        if(e->absmin[1]>maxs[1]||e->absmax[1]<mins[1]) continue;
        if(e->absmin[2]>maxs[2]||e->absmax[2]<mins[2]) continue;
        list[n++]=e;
    }
    return n;
}
static qboolean W_EntityContact( const vec3_t mins, const vec3_t maxs, const gentity_t *ent ){
    // Broad-phase AABB reject (cheap, identical to the old behaviour for a clean miss).
    if(ent->absmin[0]>maxs[0]||ent->absmax[0]<mins[0]) return qfalse;
    if(ent->absmin[1]>maxs[1]||ent->absmax[1]<mins[1]) return qfalse;
    if(ent->absmin[2]>maxs[2]||ent->absmax[2]<mins[2]) return qfalse;
    // Narrow-phase: retail SV_EntityContact traces the [mins,maxs] box against the entity's REAL clip hull
    // (CM_TransformedBoxTrace, return startsolid) -- the old AABB-only test reported contact for the whole
    // bounding box even when the actual (L-shaped/angled) trigger brush wasn't touched, so triggers fired
    // early, from the wrong side, or through gaps. G_TouchTriggers runs this for the player every frame, so it
    // affects every non-rectangular trigger. mins/maxs are world-abs (caller g_active.cpp:276), so trace from
    // vec3_origin like retail. Clip-handle selection mirrors W_Trace's narrow phase.
    int eNum = SP_EntNum((gentity_t*)ent);
    static const float vz[3] = {0,0,0};
    int h;
    if( ent->bmodel ){
        h = ( ent->s.modelindex > 0 ) ? CM_InlineModel( ent->s.modelindex )
                                      : ((eNum>=0 && eNum<SP_MAX_ENT) ? g_entClip[eNum] : 0);
    } else {
        h = CM_TempBoxModel( ent->mins, ent->maxs, 0 );
    }
    if( h <= 0 ) return qfalse;
    trace_t tr;
    CM_TransformedBoxTrace(&tr, vz, vz, mins, maxs, h, -1,
                           ent->currentOrigin, ent->bmodel ? ent->currentAngles : vz, 0);
    return tr.startsolid ? qtrue : qfalse;
}
// Retail SV_AdjustAreaPortalState: a brush entity (door/breakable) that straddles two areas toggles the
// areaportal between them when it opens/closes, so CM_AreasConnected (and thus inPVS) reflects whether a shut
// door blocks sight. Was a no-op stub -> doors never sealed PVS. The g_entPortalOpen idempotent guard keeps
// CM's ref-count from under/overflowing (ERR_DROP) if the game ever issues an unbalanced open/close across the
// port's respawn/relink paths; with balanced retail calls it never triggers, so behaviour matches retail.
static void W_AdjustAreaPortalState( gentity_t *ent, qboolean open ){
    int eNum = SP_EntNum(ent);
    if( eNum < 0 || eNum >= SP_MAX_ENT ) return;
    if( g_entArea[eNum][1] == -1 ) return;            // doesn't straddle two areas -> no portal to toggle
    signed char was = g_entPortalOpen[eNum];
    if( was == (open ? 1 : 0) ) return;               // already in this state -> avoid CM ref-count drift
    g_entPortalOpen[eNum] = open ? 1 : 0;
    // Unknown (the portal counts came from a save, which doesn't say which entity holds them): a close only
    // releases a portal that is actually held open, so it can't go negative (CM ERR_DROPs on that).
    if( was < 0 && !open ){
        int a1 = g_entArea[eNum][0], a2 = g_entArea[eNum][1];
        int size = CM_PortalStateSize(), n = 0;
        while( (n+1)*(n+1)*(int)sizeof(int) <= size ) n++;
        if( a1 < 0 || a2 < 0 || a1 >= n || a2 >= n || CM_PortalState()[a1*n + a2] <= 0 ) return;
    }
    CM_AdjustAreaPortalState( g_entArea[eNum][0], g_entArea[eNum][1], open );
}
static qboolean W_AreasConnected( int a, int b ){ return CM_AreasConnected(a,b); }

// ----- import wrappers (signature/return-type adapters) -----
static int   I_FS_ReadFile( const char *name, void **buf ){ return (int)FS_ReadFile(name,buf); }
static void *I_Malloc( int bytes ){ return malloc((size_t)bytes); }
static void  I_Free( void *p ){ free(p); }
static int   g_spMovieQueued = 0;   // the game queued "inGameCinematic" this frame (see SP_DrawFrame's skip)
#ifdef EF_COOP
static int   g_coopMovieSeq = 0;    // host: counts the in-game movies the game started (the guest plays them too)
static char  g_coopMovie[64];       // ...and its name
static void  SP_CoopNoteMovie( const char *t );
#endif
static void  I_SendConsole( const char *t ){
    if( t && !strncasecmp( t, "inGameCinematic", 15 ) ){
        g_spMovieQueued = 1;
#ifdef EF_COOP
        SP_CoopNoteMovie( t );
#endif
    }
    Cbuf_AddText(t);
}
static void  I_DropClient( int n, const char *r ){ (void)n;(void)r; }
#ifdef EF_COOP
// Co-op host: who each server command is for. The game addresses story text (centre prints,
// subtitles) to every client (-1), messages for a player to that player's number: player 1's (0)
// show here only, player 2's go to the guest only (here too while no guest is connected).
#define COOP_FOR_ALL    0
#define COOP_FOR_HOST   1
#define COOP_FOR_GUEST  2
static signed char g_svCmdFor[SC_RING];
static int SP_CoopCmdTarget( int n ){
    if( n < 0 || !g_coopRep || g_coopReplica ) return COOP_FOR_ALL;
    int p2 = g_coopRep->ViewEntity();
    if( p2 > 0 && n == p2 ) return CoopNet_HostHasGuest() ? COOP_FOR_GUEST : COOP_FOR_ALL;
    return COOP_FOR_HOST;
}
#endif
static void  I_SendServerCmd( int n, const char *fmt, ... ){
    (void)n;
    g_svCmdSeq++;
    va_list a; va_start(a,fmt);
    vsnprintf( g_svCmds[g_svCmdSeq & (SC_RING-1)], sizeof(g_svCmds[0]), fmt, a );
    va_end(a);
#ifdef EF_COOP
    g_svCmdFor[g_svCmdSeq & (SC_RING-1)] = (signed char)SP_CoopCmdTarget( n );
#endif
}
static void  I_SetCS( int n, const char *s ){
    if(n>=0&&n<CS_MAX){
        strncpy(g_configstrings[n], s?s:"", 1023); g_configstrings[n][1023]=0;
        // Mid-game change (CS_MUSIC, objectives, items, CS_MESSAGE...): push a `cs` server command
        // so the cgame updates incrementally (cg_servercmds.cpp `cs` handler). Only AFTER the client
        // is active — at level init the game sets many configstrings that the initial gamestate
        // already carries (BuildGameState), and emitting them all would overflow the command ring.
        if(g_spActive) I_SendServerCmd(0, "cs %i \"%s\"", n, g_configstrings[n]);
    }
}
static void  I_GetCS( int n, char *b, int sz ){ if(n>=0&&n<CS_MAX&&sz>0){ strncpy(b,g_configstrings[n],sz-1); b[sz-1]=0; } else if(sz>0) b[0]=0; }
static void  I_GetUserinfo( int n, char *b, int sz ){
    (void)n; if(sz<=0) return;
    // EF parses SEPARATE headModel/torsoModel/legsModel keys (g_client.cpp ClientUserinfoChanged), NOT a
    // single \model\ key — so \model\munro left all three empty -> no player model -> no mirror reflection.
    // Default loadout = Munro in the hazard suit (Q3_Interface.cpp:2657, g_shared.h DEFAULT_HEADMODEL).
    // Read the cvars so a future scripted disguise change can override; fall back to the retail defaults.
    char head[64]={0}, torso[64]={0}, legs[64]={0};
    Cvar_VariableStringBuffer("headModel",  head,  sizeof(head));
    Cvar_VariableStringBuffer("torsoModel", torso, sizeof(torso));
    Cvar_VariableStringBuffer("legsModel",  legs,  sizeof(legs));
    if(!head[0])  strcpy(head,  "munro/default");
    if(!torso[0]) strcpy(torso, "hazard/default");
    if(!legs[0])  strcpy(legs,  "hazard/default");
    snprintf(b, sz, "\\name\\Munro\\headModel\\%s\\torsoModel\\%s\\legsModel\\%s\\sex\\m\\hc\\200", head, torso, legs);
    b[sz-1]=0;
}
static void  I_SetUserinfo( int n, const char *b ){ (void)n;(void)b; }
static void  I_GetServerinfo( char *b, int sz ){ if(sz>0) b[0]=0; }
// gi.AppendToSaveGame: write one retail-format chunk [id:4][len:4][cksum:4][data][magic:4].
static qboolean I_Append( unsigned long c, void *d, int l ){
    if(!s_sgFile || !s_sgWriting) return qtrue;
    unsigned id=(unsigned)c, len=(unsigned)l, cks=(unsigned)Com_BlockChecksum(d,l), magic=SG_MAGIC;
    FS_Write(&id,4,s_sgFile); FS_Write(&len,4,s_sgFile); FS_Write(&cks,4,s_sgFile);   // exactly 4 bytes each
    if(l>0) FS_Write(d,l,s_sgFile);
    FS_Write(&magic,4,s_sgFile);
    return qtrue;
}
// gi.ReadFromSaveGame[Optional]: read+verify the next chunk; optional = peek-and-skip on id mismatch.
static int ReadChunk( unsigned long c, void *addr, int len, void **pp, qboolean optional ){
    if(!s_sgFile || s_sgWriting) return 0;
    unsigned id=0, storedLen=0;
    if(FS_Read(&id,4,s_sgFile)!=4 || FS_Read(&storedLen,4,s_sgFile)!=4) return 0;
    if(id != (unsigned)c){
        if(optional){ FS_Seek(s_sgFile, -8, FS_SEEK_CUR); return 0; }   // rewind id+len
        // Mandatory mismatch: CONSUME the rest of this chunk (cksum + data + magic) so the stream stays framed
        // and the following chunks still parse, instead of leaving cksum/data/magic unread and desyncing every
        // subsequent chunk (retail Com_Errors here; we log + resync to stay non-fatal).
        bwarn("savegame: chunk ID mismatch got %08x want %08lx (consuming %u + resyncing)\n", id, c, storedLen);
        FS_Seek(s_sgFile, (int)(4 + storedLen + 4), FS_SEEK_CUR);
        return 0;
    }
    if(len != 0 && (unsigned)len != storedLen) bwarn("savegame: chunk %08x len %u != requested %d\n", id, storedLen, len);
    unsigned cks=0; FS_Read(&cks,4,s_sgFile);
    void *buf = addr;
    if(!buf){ buf = malloc(storedLen ? storedLen : 1); if(pp) *pp = buf; }
    if(storedLen>0) FS_Read(buf, (int)storedLen, s_sgFile);
    unsigned trailer=0; FS_Read(&trailer,4,s_sgFile);
    if((unsigned)Com_BlockChecksum(buf, (int)storedLen) != cks) bwarn("savegame: checksum FAIL chunk %08x\n", id);
    if(trailer != SG_MAGIC) bwarn("savegame: bad magic chunk %08x (%08x)\n", id, trailer);
    if(!addr && !pp) free(buf);
    return (int)storedLen;
}
static int I_ReadSG   ( unsigned long c, void *d, int l, void **p ){ return ReadChunk(c,d,l,p,qfalse); }
static int I_ReadSGOpt( unsigned long c, void *d, int l, void **p ){ return ReadChunk(c,d,l,p,qtrue ); }
// gi.WriteCam: retail's engine appends a camera ref_tag line to "cameras.map" (developer
// camera-path recording, triggered by the "writecam" console command -> CG_WriteCam_f ->
// gi.WriteCam, cg_consolecmds.cpp). The bridge previously left this game_import_t slot NULL
// (memset 0), so invoking "writecam" (via cfg/bind; the SP build has no console) would
// NULL-deref. Implemented faithfully: append the text to cameras.map.
static void I_WriteCam( const char *text ){
    if(!text || !text[0]) return;
    fileHandle_t f = 0;
    FS_FOpenFileByMode("cameras.map", &f, FS_APPEND);
    if(!f) return;
    FS_Write(text, (int)strlen(text), f);
    FS_FCloseFile(f);
}
// Sized to MAX_GENTITIES (1024): the game reads gi.S_Override[ent->s.number] for ANY entity, and
// EF missions have many voiced NPCs at high entity numbers (borg1 alone spawns 561) — a [256] array
// read out of bounds and could stall a character's dialog. Lots of voices/characters need the full range.
static int   g_sOverride[MAX_GENTITIES];
// S_Override voice tracking: the retail engine flags gi.S_Override[ent]=1 while a CHAN_VOICE
// sound plays on that entity, and the game's G_CheckTasksCompleted (g_main.cpp:471) holds the
// TID_CHAN_VOICE task (dialog wait) until it clears. Our engine doesn't, so g_sOverride was
// stuck at 0 and every voice line completed in one frame -> dialog lines fired all at once.
// We approximate it: when a CHAN_VOICE sound starts we record when it should finish, and refresh
// g_sOverride[] from that each frame before ge->RunFrame.
static int   g_voiceEnd[MAX_GENTITIES];
extern "C" int S_StopAllSoundsCount( void );   // snd_main.c: bumps on every S_StopAllSounds / S_StopGameSounds
extern "C" qboolean S_VoiceLipSync( int *levels, int numEntities );   // snd_main.c: lip sync levels
static void SP_ClearVoiceTimers(void){
    memset(g_voiceEnd, 0, sizeof(g_voiceEnd)); memset(g_sOverride, 0, sizeof(g_sOverride));
}
// Pause / cutscene-skip gates (SP_DrawFrame). While the sim is paused (console, menu, in-game movie) or a
// camera scene is being skipped, the cgame's sound starts are dropped (the paused frame still runs the
// cgame, and its ambient-set timers run on real time), and while skipping its renderer calls are dropped
// too (retail stops drawing the 3D view during a skip).
static int   s_spSoundsBlocked = 0;
static int   s_spSkipDraw = 0;
static int   s_spWasPaused = 0;     // SP_DrawFrame edge state: the last frame was paused / skipping
static int   s_spWasSkipping = 0;
// The gates are set per drawn frame, so the last frame's values outlive the level: quitting from the
// pause menu left s_spSoundsBlocked at 1, and the next game's level-start frames (run before its first
// drawn frame) dropped their sounds -- borg1's opening captain's log was lost and its dialog wait ended
// at once. A new game, load or abort starts with every gate open (SP_UnloadGame).
static void SP_ResetFrameGates(void){
    s_spSoundsBlocked = 0; s_spSkipDraw = 0; s_spWasPaused = 0; s_spWasSkipping = 0;
}
// Retail cutscene skip: Use Item in a camera scene sets skippingCinematic 1 + timescale 100 and the game
// clears both when the camera ends (g_active.cpp ClientThink_real); an in-game movie clears them too
// (cl_cin.c). This ends a skip wherever the level ends without the game seeing the camera end.
static void SP_EndCutsceneSkip(void){
    if( Cvar_VariableIntegerValue("skippingCinematic") ){
        Cvar_Set("skippingCinematic", "0");
        Cvar_Set("timescale", "1");
    }
}
// gi.S_Override[ent] is the face, not a flag: the cgame draws head->customSkin = ci->headSkin + S_Override.
// The head skins are -1 blink, -2 frown, -3 frown+blink, -4 neutral, -5..-8 mouth shapes. The sound
// code sets it from the voice's own volume: -1 while talking but quiet (eyes open, mouth at rest), 5..8 by
// loudness, 0 when not talking (the cgame then blinks and frowns on its own). The mixer computes the same
// thing now (S_VoiceLipSync, snd_dma.c). The value also gates the dialog `wait` (nonzero keeps the script's
// TID_CHAN_VOICE task pending), which g_voiceEnd keeps timing, so a talker is never 0 before its line ends.
// Called every sim tick and every render frame (the cgame reads the array while drawing).
// With the regression harness on, the mixer's level is not used: it follows the sound playing on the wall
// clock, and the cgame's head bob (CG_AddHeadBob) draws random numbers when it jumps, so game state
// would vary from run to run. The fixed mouth cycle below keeps runs identical.
static void SP_UpdateVisemes(void){
    static int levels[MAX_GENTITIES];
    qboolean mixer = S_VoiceLipSync( levels, MAX_GENTITIES ) && !Regress_Active();
    for(int i=0;i<MAX_GENTITIES;i++){
        if(g_voiceEnd[i] > g_levelTime){
            if( mixer ){
                g_sOverride[i] = levels[i] ? levels[i] : -1;
            } else {
                // no lip sync data (OpenAL backend): cycle the mouth shapes at a talking cadence
                unsigned h = (unsigned)(g_levelTime/80 + i*131);
                h = h*1103515245u + 12345u;
                g_sOverride[i] = 5 + (int)((h>>16) % 4);
            }
        } else {
            g_sOverride[i] = 0;
        }
    }
}
static void SP_UpdateVoiceOverride(void){
    // The voice timers count level time, so they must go when the clock jumps (a load sets the saved
    // time, an autosave load or a transition resets it to 1000) or the voices they time are stopped
    // (S_StopAllSounds). Otherwise a stale end time from before keeps a mouth moving with no voice and
    // holds the script's TID_CHAN_VOICE wait (Munro's lost first line after a load). The sim advances
    // 50 or 100 ms between calls; anything else is a jump. A pause holds both the voice and the clock.
    { static int s_lastTime = 0, s_lastStops = -1;
      int stops = S_StopAllSoundsCount();
      if( stops != s_lastStops || g_levelTime < s_lastTime || g_levelTime > s_lastTime + 1000 ){
          int talking = 0;
          for(int i=0;i<MAX_GENTITIES;i++) if(g_voiceEnd[i] > g_levelTime) talking++;
          if( talking ) blog("voice: dropped %d voice timer(s) (%s, levelTime %d -> %d)\n", talking,
                             stops != s_lastStops ? "sounds stopped" : "clock jump", s_lastTime, g_levelTime);
          SP_ClearVoiceTimers();
      }
      s_lastStops = stops; s_lastTime = g_levelTime; }
    SP_UpdateVisemes();
}

// One game frame at g_levelTime. With the regression harness on (qcommon/regress.c), rand() is
// reseeded first and the game's state is logged after; otherwise this is just the voice update +
// ge->RunFrame it always was.
static void SP_GameRunFrame(void){
    Regress_SeedRand( g_levelTime );
    SP_UpdateVoiceOverride(); ge->RunFrame(g_levelTime);
    if( Regress_Active() && g_regressDump ){
        Regress_BeginFrame( g_curMap, g_levelTime );
        g_regressDump( Regress_State );
        Regress_EndFrame();
    }
}

// Fully UNLOAD libefgame so the next dlopen returns an OS-zeroed module. Retail reloads the game+cgame DLL
// on every map (FreeLibrary+LoadLibrary), which is what produced "fresh per-map state" — code never bulk-
// memsets cg/cgs/level. This port previously REUSED the .so, so every global the authors assumed the loader
// would zero leaked across missions (player_locked, in_camera, numKnownAnimFileSets, numBoltOns, iCGResetCount,
// ...). libefgame is dlopen'd RTLD_NOW WITHOUT RTLD_GLOBAL, so dlclose truly unloads -> next dlopen = fresh BSS.
// Forward decls for the New-Game-respawn hunk/GPU reclaim (C1) — same proven teardown SP_FinishTransition uses.
extern "C" void SPR_ShutdownRenderer(void);
extern "C" void SPR_InitRenderer(void);
static void     SP_FillGlConfig(void);
// True once an SP world's models/textures occupy the permanent hunk + GPU (set by SP_SpawnServer /
// SP_FinishTransition). SP_SpawnServer consults it to decide whether a New Game must reclaim first (C1).
static qboolean s_worldResident = qfalse;

extern "C" void SP_UnloadGame( void ){
    if( g_lib ){
        if( g_spActive && g_vmMain )            SP_VmMain(CG_SHUTDOWN, 0,0,0);   // cgame teardown
        if( g_spActive && ge && ge->Shutdown )  ge->Shutdown();                // game/level teardown
        dlclose( g_lib );
    }
    g_lib = NULL; ge = NULL; g_vmMain = NULL; g_camPos = NULL; g_spActive = 0;
#ifdef EF_COOP
    g_coopRep = NULL; g_coopReplica = 0;
#endif
    SP_EndCutsceneSkip();   // a load / new game / abort ends any camera-scene skip (timescale back to 1)
    SP_ResetFrameGates();   // ... and the pause/skip sound and draw gates
    // H3: g_linked[] holds raw gentity_t* into the module we just dlclose'd. Leaving them dangling is a
    // use-after-free waiting for one missing g_spActive gate. The next spawn resets g_numLinked anyway;
    // zero them now so nothing can walk a freed pointer in between.
    g_numLinked = 0; memset(g_linked, 0, sizeof(g_linked));
}

extern "C" qboolean SP_LoadGame( const char *solib ){
    if( g_lib ) SP_UnloadGame();   // drop the stale module first -> every (re)load lands on a fresh, zeroed .so
    g_lib = dlopen( SP_ModulePath( solib ), RTLD_NOW );
    if(!g_lib){ bwarn("SP_LoadGame: dlopen failed: %s\n", dlerror()); return qfalse; }
    GetGameAPI_t GetGameAPI = (GetGameAPI_t)dlsym(g_lib, "_Z10GetGameAPIP13game_import_t");
    if(!GetGameAPI){ bwarn("SP_LoadGame: GetGameAPI not found\n"); return qfalse; }

    static game_import_t gi; memset(&gi,0,sizeof(gi));
    gi.Printf=SP_ComPrintf(); gi.Error=SP_ComError; gi.Milliseconds=Sys_Milliseconds;
    gi.cvar=Cvar_Get; gi.cvar_set=Cvar_Set;
    gi.Cvar_VariableIntegerValue=Cvar_VariableIntegerValue; gi.Cvar_VariableStringBuffer=Cvar_VariableStringBuffer;
    gi.argc=Cmd_Argc; gi.argv=Cmd_Argv;
    gi.FS_FOpenFile=FS_FOpenFileByMode; gi.FS_Read=FS_Read; gi.FS_Write=FS_Write; gi.FS_FCloseFile=FS_FCloseFile;
    gi.FS_ReadFile=I_FS_ReadFile; gi.FS_FreeFile=FS_FreeFile; gi.FS_GetFileList=FS_GetFileList;
    gi.AppendToSaveGame=I_Append; gi.ReadFromSaveGame=I_ReadSG; gi.ReadFromSaveGameOptional=I_ReadSGOpt;
    gi.WriteCam=I_WriteCam;
    gi.SendConsoleCommand=I_SendConsole; gi.DropClient=I_DropClient; gi.SendServerCommand=I_SendServerCmd;
    gi.SetConfigstring=I_SetCS; gi.GetConfigstring=I_GetCS;
    gi.GetUserinfo=I_GetUserinfo; gi.SetUserinfo=I_SetUserinfo; gi.GetServerinfo=I_GetServerinfo;
    gi.SetBrushModel=W_SetBrushModel; gi.trace=W_Trace; gi.pointcontents=W_PointContents;
    gi.inPVS=W_InPVS; gi.inPVSIgnorePortals=W_InPVSIgnorePortals;
    gi.AdjustAreaPortalState=W_AdjustAreaPortalState; gi.AreasConnected=W_AreasConnected;
    gi.linkentity=W_LinkEntity; gi.unlinkentity=W_UnlinkEntity;
    gi.EntitiesInBox=W_EntitiesInBox; gi.EntityContact=W_EntityContact;
    gi.S_Override=g_sOverride; gi.Malloc=I_Malloc; gi.Free=I_Free;

    ge = GetGameAPI(&gi);
    if(!ge){ bwarn("SP_LoadGame: GetGameAPI returned NULL\n"); return qfalse; }
    // wire the cgame syscall dispatcher (no-op) so server-side FX calls don't hit a null pointer
    dllEntry_t dllEntry = (dllEntry_t)dlsym(g_lib, "_Z8dllEntryPFiizE");
    if(dllEntry) dllEntry(SP_CgameSyscall);
    g_vmMain = (vmMain_t)dlsym(g_lib, "_Z6vmMainiiiiiiiii");
    g_camPos = (camPos_t)dlsym(g_lib, "_Z15CG_GetCameraPosPf");   // optional (snapshot loop-sound culling)
    g_regressDump = (regressDump_t)dlsym(g_lib, "G_RegressDump");  // optional (the regression harness)
#ifdef EF_COOP
    SP_CoopSetBridge();   // before the level's InitGame
    SP_CoopGetReplica();
#endif
    blog("SP_LoadGame: ge=%p apiversion=%d gentitySize=%d\n", (void*)ge, ge->apiversion, ge->gentitySize);
    return (ge->apiversion==GAME_API_VERSION) ? qtrue : qfalse;
}

extern "C" void SP_RunFrames( int n );   // level-start settle frames (defined below)
#define SP_SETTLE_FRAMES        3   // retail SV_SpawnServer: 3 x 100 ms before the client connects
#define SP_SETTLE_CLIENT_TICKS  4   // 4 x 50 ms after ClientBegin, before the first snapshot (see SP_SettleClient)

extern "C" qboolean SP_SpawnServer( const char *mapname ){
    // If a level is already running (New Game / map after a prior map), tear it down FIRST. Re-calling
    // ge->Init without ge->Shutdown leaves stale game state (corrupted level/entity/ICARUS state ->
    // e.g. tutorial scripts/target_scriptrunner failing to spawn). Reuses the loaded libefgame.so.
    // The prior module (if any) was already dlclose'd+reloaded by SP_LoadGame (always called before
    // SP_SpawnServer), so all game/cgame globals are freshly OS-zeroed — no in-place teardown needed.
    char bsp[256]; snprintf(bsp,sizeof(bsp),"maps/%s.bsp", mapname);
    // C1 (unbounded-leak fix): retail SV_SpawnServer reclaims the permanent hunk + collision map on EVERY
    // spawn (Hunk_Clear+CM_ClearMap). The Route-b New-Game path (SP_LoadGame->SP_SpawnServer->SP_StartClient)
    // bypasses SV_SpawnServer and never reclaimed, so starting a New Game while a PRIOR map's models / world /
    // VkImages / descriptor sets were still resident (a replayed New Game, or New Game after playing then
    // returning to the menu) leaked them -> eventual "Hunk_Alloc failed" / VK_ERROR_OUT_OF_POOL_MEMORY.
    // Mirror SP_FinishTransition's proven ordering: GPU teardown FIRST (re.Shutdown walks tr.images[] living
    // in this same perm hunk), THEN Hunk_Clear + CM_ClearMap, THEN re-init the renderer at the end of this
    // function (before SP_StartClient's CG_INIT loads the new world). GATED on s_worldResident: the cold-boot
    // FIRST spawn must NOT do this — there is no prior SP world to reclaim, and an extra R_Init would
    // double-init the descriptor pool on first load (the exact crash documented at SP_StartClient:1048).
    // The prior cgame was already CG_SHUTDOWN'd by SP_LoadGame->SP_UnloadGame before we got here.
    if( s_worldResident ){
        blog("SP_SpawnServer: prior world resident -> reclaiming hunk/GPU before respawn (C1)\n");
        SPR_ShutdownRenderer();   // destroy old textures/imageviews + reset descriptor pool (hunk still valid)
        Hunk_Clear();             // reclaim models/world/backEndData now the GPU side is down
        CM_ClearMap();            // reset cm.name so CM_LoadMap actually reloads
    }
    int checksum=0;
    blog("SP_SpawnServer: CM_LoadMap(%s)\n", bsp);
    CM_LoadMap( bsp, qfalse, &checksum );
    char *entstring = CM_EntityString();
    blog("SP_SpawnServer: %d inline models, entity string %d bytes\n",
               CM_NumInlineModels(), entstring?(int)strlen(entstring):0);
    // EF1 SP 1:1: retail SV_SpawnServer starts the level clock at sv.time = 1000,
    // NOT 0. Starting at 0 made the opening target_scriptrunner hit the g_target.cpp:707 `level.time<1000`
    // warm-up gate and reschedule to 1000ms, so the cinematic camera (CGCam_Enable -> in_camera) only
    // activated ~1s after the cgame went live -> ~1s of first-person renders first = the Munro/phaser flash
    // at New-Game start. Starting at 1000 fires the opening script/camera BEFORE the first rendered frame,
    // exactly like retail. The +1000ms offset is uniform across the snapshot ring / g_snapBaseTime / cg.time
    // clamps (all relative), so the 20Hz cadence + interpolation are preserved. New-Game-scoped: a LOAD /
    // transition takes g_levelTime from the save (SP_LoadGameFile/SP_FinishTransition), unaffected.
    g_numLinked = 0; g_levelTime = 1000;
    memset(g_entPortalOpen, 0, sizeof(g_entPortalOpen));  // New Game: CM (re)loaded -> all areaportals start sealed
    SP_DropLoadPortals();                                 // a stale, never-applied load's portal state
    Cvar_Set( "cl_paused", "0" );   // EF1 SP 1:1: retail SV_SpawnServer unpauses every spawn (sv_init.c:464)
    memset(g_voiceEnd, 0, sizeof(g_voiceEnd)); memset(g_sOverride, 0, sizeof(g_sOverride)); // fresh map: no stale voices
    // BRIDGE-side statics dlclose does NOT touch (these live in libmain, not libefgame) — reset per map:
    memset(g_entClip, 0, sizeof(g_entClip)); g_svCmdSeq = 0; g_snapNum = 1; g_cmdNum = 1;
    // Reset the snapshot ring + cadence accumulator so no stale snap from the previous map is served
    // (a stale high-numbered snap would spuriously trip the cgame's CG_RestartLevel / replay old events).
    memset(g_snapHist, 0, sizeof(g_snapHist)); g_snapBaseTime = 0; g_simResidual = 0;
    strncpy(g_curMap, mapname, sizeof(g_curMap)-1); g_curMap[sizeof(g_curMap)-1]=0;
    // Per-map clear of the configstring table (retail SV_SpawnServer memsets the whole table each spawn).
    // g_configstrings lives in libmain and is NOT zeroed by the per-map libefgame dlclose, so without this it
    // accumulates across maps -> model/sound indices drift map-to-map (the borg2 axis-prop class of bug) and
    // dataCount climbs toward MAX_GAMESTATE_CHARS (observed 3133->6656 over borg2->holodeck->voy1). ge->Init
    // below re-registers THIS map's models/sounds fresh. (The LOAD path never comes through SP_SpawnServer —
    // SP_LoadGameFile restores the saved table instead; the transition path clears in SP_FinishTransition.)
    memset(g_configstrings, 0, sizeof(g_configstrings));
    // Populate CS_SERVERINFO so the cgame builds cgs.mapname = "maps/<map>.bsp" (cg_servercmds.cpp:25)
    // — needed for the renderer world load (Route b) AND the cgame levelshot / mission-info text.
    snprintf(g_configstrings[CS_SERVERINFO], sizeof(g_configstrings[0]), "\\mapname\\%s\\sv_maxclients\\1", mapname);
    if(!ge){ bwarn("no game loaded\n"); return qfalse; }
    ge->Init( mapname, "", checksum, entstring, g_levelTime, Regress_Active() ? Regress_Seed() : Sys_Milliseconds(), Sys_Milliseconds(),
              (SavedGameJustLoaded_e)0, qfalse );
    blog("SP_SpawnServer: ge->Init done. num_entities=%d linked=%d\n", ge->num_entities, g_numLinked);
#ifdef EF_COOP
    g_coopLevelId++;
    g_coopApplied = 0; g_coopCmdSeen = 0;
    // a guest's replica level (sp_coop 2, applied by InitGame): no game frames, the host's come instead
    g_coopReplica = ( g_coopRep && Cvar_VariableIntegerValue( "sp_coop" ) == 2 ) ? 1 : 0;
    SP_CoopHoldStart();
    if( !g_coopReplica )
#endif
    SP_RunFrames( SP_SETTLE_FRAMES );   // retail SV_SpawnServer settle frames, before SP_StartClient connects the player
    // C1 (reclaim path only): bring the renderer we tore down above back up BEFORE SP_StartClient's CG_INIT
    // loads the new world. Mirrors SP_FinishTransition's SP_FillGlConfig()+SPR_InitRenderer() ordering. On the
    // first spawn the boot renderer is already up, so skip (CG_INIT loads the world as before).
    if( s_worldResident ){
        SP_FillGlConfig();
        SPR_InitRenderer();
    }
    s_worldResident = qtrue;
    return qtrue;
}

// Level-start settle frames, part 1 (fresh spawns only: New Game, forward transitions, eAUTO autosave loads).
// Retail SV_SpawnServer ran "a few frames to allow everything to settle" right after the game's Init and
// BEFORE any client connected: 3 x ge->RunFrame(sv.time); sv.time += 100 (the stock Q3 loop in
// efcode/server/sv_init.c, and the comment in g_main.cpp InitGame). Map-spawned NPCs run their deferred
// NPC_Begin here (with its beam-in event), movers and spawn scripts start, and the player isn't in the level
// yet. Same order as retail: RunFrame at the current time, then advance by 100.
extern "C" void SP_RunFrames( int n ){
    for(int i=0;i<n;i++){ SP_GameRunFrame(); g_levelTime += 100; }
    blog("SP_RunFrames: settled %d frames, levelTime now %d, num_entities=%d\n", n, g_levelTime, ge->num_entities);
}

// =====================================================================================
//  CLIENT / CGAME side — loopback driver + cgame->engine syscall dispatcher.
//  Renderer/sound are stubbed in this increment (validates cgame loop + struct reconcile
//  headlessly). Real renderervk/sound get wired in the APK phase.
// =====================================================================================
static glconfig_t g_glconfig;
static void SP_FillGlConfig(void);   // fwd: defined near SP_StartClient; called from SP_FinishTransition too
static gameState_t g_gameState;
static snapshot_t  g_snap;
static usercmd_t   g_lastcmd;
static int         g_weaponSelect = 0;

// Level-start settle frames, part 2: after ClientBegin, before the first snapshot and CG_INIT (so the load
// screen is still up). In retail the client entered the world with its first usercmd only after loading its
// cgame, and the listen server kept running frames meanwhile (Com_ModifyMsec caps a frame at 200 ms), so the
// first snapshot the player saw was a few frames past their spawn: they had already dropped onto the floor,
// the spawn-point scripts had started (camera, locks) and spawn events (EV_PLAYER_TELEPORT_IN, 300 ms) from
// the settle frames had expired. The port sent the very first frame instead: the player hanging a few units
// up and falling, and every bridge crew member beaming in on voy1. Same 50 ms ticks as SP_DrawFrame.
// Before CG_INIT the cgame hasn't reported a weapon, so the usercmd carries the player's current one (cmd
// weapon 0 would start lowering the weapon); g_weaponSelect gets it too for the first real frame.
static void SP_SettleClient( int ticks ){
    gentity_t* p0 = SP_GEntity(0);
    for(int i=0;i<ticks;i++){
        if(p0->client) g_lastcmd.weapon = (byte)((playerState_t*)p0->client)->weapon;
        g_levelTime += SP_SV_FRAMEMSEC;
        g_cmdNum++;
        g_lastcmd.serverTime = g_levelTime;
        Regress_SeedRand( g_levelTime );
        ge->ClientThink(0, &g_lastcmd);
        SP_GameRunFrame();
    }
    if(p0->client) g_weaponSelect = ((playerState_t*)p0->client)->weapon;
    blog("SP_SettleClient: %d ticks, levelTime now %d\n", ticks, g_levelTime);
}

static void BuildGameState(void){
    memset(&g_gameState, 0, sizeof(g_gameState));
    g_gameState.dataCount = 1;   // index 0 reserved (offset 0 = empty)
    for(int i=0;i<CS_MAX;i++){
        const char* s = g_configstrings[i];
        if(!s[0]) continue;
        int len = (int)strlen(s)+1;
        if(g_gameState.dataCount + len >= MAX_GAMESTATE_CHARS) break;
        g_gameState.stringOffsets[i] = g_gameState.dataCount;
        memcpy(g_gameState.stringData + g_gameState.dataCount, s, len);
        g_gameState.dataCount += len;
    }
}

// ---- looping sounds of entities the retail server wouldn't have sent ----
// Retail SV_BuildClientSnapshot sent an entity only if it touched a cluster in the PVS of the view (and its area
// was connected to the view's area); the view was the player's eye, or the cinematic camera (the cgame's
// CG_CAMERA_POS export, which exists for this). The cgame re-adds every snapshot entity's s.loopSound each
// frame (cg_ents.cpp CG_EntityEffects), so with the port sending every entity, a looping speaker kept playing
// wherever the player went: the prologue's Borg forcefield hum (three LOOPED_ON target_speakers playing
// borgfieldhum.wav in holodeck.bsp; g_target.cpp links them "so the server can determine who to send updates
// to") ran on through Tuvok's speech and into the turbolift, quieter only with distance.
// The bridge still sends every entity, so nothing the cgame draws or predicts changes; it only drops the loop of
// an entity outside the view's PVS, or in an area that a closed door (areaportal) cuts off from the view's area,
// (a hum behind a shut door is silent). The area test needs the real portal state, which loads
// restore since 1.4.0 (the save's PRTS chunk). An entity counts as connected if ANY area it touches is,
// so this never drops a loop that should be heard.
// No culling when the view is outside the map (no cluster), for unlinked or broadcast entities, or for an
// entity spanning more leafs than we look at. No area test when the view leaf has no area.
struct SP_View { const unsigned char *pvs; int area; };
static void SP_GetView( SP_View *v ){
    v->pvs = NULL; v->area = -1;
    vec3_t org;
    if( !g_camPos || !g_camPos( org ) ){
        VectorCopy( g_snap.ps.origin, org );
        org[2] += g_snap.ps.viewheight;
    }
    int leaf = CM_PointLeafnum( org );
    int cluster = CM_LeafCluster( leaf );
    if( cluster < 0 ) return;
    v->pvs = CM_ClusterPVS( cluster );
    v->area = CM_LeafArea( leaf );
}
static qboolean SP_EntInView( const SP_View *v, gentity_t *e ){
    if( !v->pvs || !e->linked || (e->svFlags & SVF_BROADCAST) ) return qtrue;
    int leafs[128], lastLeaf = 0;
    int num = CM_BoxLeafnums( e->absmin, e->absmax, leafs, 128, &lastLeaf );
    if( num >= 128 ) return qtrue;
    qboolean inPVS = qfalse, anyArea = qfalse, connected = ( v->area < 0 ) ? qtrue : qfalse;
    for( int i = 0; i < num && !(inPVS && connected); i++ ){
        int c = CM_LeafCluster( leafs[i] );
        if( c >= 0 && (v->pvs[c >> 3] & (1 << (c & 7))) ) inPVS = qtrue;
        if( !connected ){
            int a = CM_LeafArea( leafs[i] );
            if( a >= 0 ){ anyArea = qtrue; if( CM_AreasConnected( v->area, a ) ) connected = qtrue; }
        }
    }
    return ( inPVS && ( connected || !anyArea ) ) ? qtrue : qfalse;
}

static void BuildSnapshot(void){
    memset(&g_snap, 0, sizeof(g_snap));
    g_snap.serverTime = g_levelTime;
    g_snap.serverCommandSequence = g_svCmdSeq;   // tell the cgame how many server commands exist
    g_snap.cmdNum = g_cmdNum;   // cmd this snapshot reflects. Retail SV stamps it; if left 0, cg_predict.cpp:413
                                // (current - snap.cmdNum >= CMD_BACKUP) trips once g_cmdNum passes 64 and FREEZES
                                // the predicted view (visible during timescale<1 slow-mo). Stamping = prediction
                                // tracks the snapshot (predicted_player_state = snap.ps), no spurious replay.
    gentity_t* p0 = SP_GEntity(0);
#ifdef EF_COOP
    if( g_coopReplica && g_coopRep && g_coopRep->ViewEntity() > 0 ) p0 = SP_GEntity( g_coopRep->ViewEntity() );
#endif
    if(p0->client) g_snap.ps = *(playerState_t*)p0->client;
#ifdef EF_COOP
    if( g_coopReplica ){
        // the guest's prediction: the host's player 2 as of the guest command it applied last;
        // command times count in the guest's commands (50 ms each, see SP_CoopUserCmd)
        g_snap.cmdNum = g_coopAck;
        g_snap.ps.commandTime = g_coopAck * SP_SV_FRAMEMSEC;
    }
#endif
    int n=0;
    SP_View view; SP_GetView( &view );
    // Start at 0 to INCLUDE the local player's own entity (entity 0). Q3 ships the client's own entity in
    // the snapshot so the cgame can draw the player BODY in mirrors/portals/3rd-person (CG_Player skips it
    // in first-person via clientNum==ps.clientNum). Skipping it (old i=1) meant no Munro reflection at all.
    for(int i=0; i<ge->num_entities && n<MAX_ENTITIES_IN_SNAPSHOT; i++){
        gentity_t* e = SP_GEntity(i);
        if(!e->inuse) continue;
        if(e->svFlags & SVF_NOCLIENT) continue;   // mirror SV_BuildClientSnapshot: don't ship server-only
                                                  // entities; frees slots in the 256 cap so visible movers
                                                  // on dense maps (e.g. borg1 ~561 ents) aren't dropped.
        g_snap.entities[n] = e->s;
        if( i != 0 && e->s.loopSound && !SP_EntInView( &view, e ) ){
            g_snap.entities[n].loopSound = 0;   // out of view: no loop (see SP_EntInView)
        }
        n++;
    }
    g_snap.numEntities = n;
    // Publish this snapshot under its number into the ring (retail CL_ParseSnapshot:
    // cl.snapshots[messageNum & PACKET_MASK]). g_snapNum is the number being built NOW.
    g_snapHist[g_snapNum & (SNAP_RING-1)] = g_snap;
    if(g_snapNum == 1) g_snapBaseTime = g_snap.serverTime;   // floor for the cgame's trailing cg.time
    g_snapNum++;
}

#include "sp_render.h"
#define A(n) (args[n])
#define P(n) ((void*)args[n])
#define VMF(n) (*(float*)&args[n])
// Dispatcher for cgame-registered console commands. The SP cgame registers its commands
// (+info/-info mission objectives, give/god/noclip/notarget/undying, setobjective/viewobjective,
// setviewpos, kill, ...) via cgi_AddCommand -> CG_ADDCOMMAND. Each must become a REAL engine
// command that routes back into the cgame's CG_CONSOLE_COMMAND handler (cg_main.cpp) — exactly
// what the stock engine's CL_GameCommand does (cl_cgame.c:1216 VM_Call(cgvm,CG_CONSOLE_COMMAND)).
// The bridge previously stubbed CG_ADDCOMMAND (return 0), so every cgame command was "Unknown
// command" (e.g. pressing +info errored). The engine has already tokenized argv when it runs the
// command, so CG_ConsoleCommand reads it via the bridge's CG_ARGC/CG_ARGV.
static void SP_CgameConsoleCmd(void){
    // CG_CONSOLE_COMMAND returns qtrue if the cgame's local table claimed it (+info, weapnext, zoom...).
    // The SERVER commands the cgame also registers (give/god/noclip/notarget/undying/kill/setviewpos/
    // setobjective/viewobjective, cg_main.cpp:1398-1406) are NOT in that table -> CG_ConsoleCommand returns
    // qfalse, and they must be forwarded to the game's ClientCommand (mirrors retail CL_GameCommand ->
    // CL_ForwardCommandToServer). argv is already tokenized; gi.argv==Cmd_Argv so ClientCommand reads it.
    if(g_vmMain && SP_VmMain(CG_CONSOLE_COMMAND, 0,0,0)) return;
    if(ge && g_spActive) ge->ClientCommand(0);
}

// CG_ADDCOMMAND: the cgame re-registers its whole command list on every CG_INIT (each map load /
// savegame load), but the engine commands it created stay registered (they all route through
// SP_CgameConsoleCmd, so the old registration is still correct). Register each name once, which
// also stops ~40 "Cmd_AddCommand: X already defined" lines per load. A real clash with an engine
// command (messagemode2) is still reported, once.
static void SP_CgameAddCommand(const char *name){
    static char names[256][32]; static int count = 0;
    if(!name || !name[0]) return;
    for(int i = 0; i < count; i++) if(!strcasecmp(names[i], name)) return;
    if(count < 256 && strlen(name) < sizeof(names[0])){ strcpy(names[count], name); count++; }
    Cmd_AddCommand(name, SP_CgameConsoleCmd);
}

// ---------------------------------------------------------------------------
// Ambient sound sets (sound/sound.txt). The cgame reaches them through engine traps (CG_AS_PARSESETS,
// CG_AS_ADDENTRY, CG_S_UPDATEAMBIENTSET, CG_S_ADDLOCALSET, CG_AS_GETBMODELSOUND); the SP port has no
// separate sound module for them, so they live here.
// The file is line based. A header (type, amsdir, outdir, basedir: settings of the tool that wrote it,
// ignored here), `;` comment lines, then the sets: a "generalSet|localSet|bmodelSet NAME" line followed by
// that set's keyword lines, up to the next set or the end of the file. A { } block around the keywords
// is accepted too.
//   loopedWave <f>                sound/<f>.wav, the set's looping sound
//   subWaves <dir> <n1> <n2> ...  sound/<dir>/<n>.wav: random one-shots for general and local sets; for a
//                                 bmodel set the mover's start, moving (looped) and stop sounds, in order
//   timeBetweenWaves <min> <max>  seconds between one-shots (none when max is 0)
//   volRange <min> <max>          one-shot volume, 0..255
//   radius <n>                    kept but unused: the mixer already fades a sound by distance
// The three kinds are looked up separately (dntractor is both a localSet and a bmodelSet):
//   general  the map's ambience: worldspawn's soundSet, switched by triggers through CS_AMBIENT_SET. Its loop
//            plays at the listener on ENTITYNUM_NONE (MAX_GENTITIES-1, never a real entity's loop slot),
//            re-added every frame like every loop; its one-shots play on the player.
//   local    an entity's own loop and one-shots at its origin (target_speaker, fx_bolt, func_usable ...),
//            silent while the entity is out of view, like the snapshot loops (SP_EntInView).
//   bmodel   door, plat and lift sounds (g_mover.cpp G_PlayDoorSound / G_PlayDoorLoopSound).
// A set's sounds are registered when the cgame loads the sets if the map names it (CG_AS_ADDENTRY), and on
// first use otherwise (the world's own set, which the precache list leaves out).
// ---------------------------------------------------------------------------
#define AS_MAX_SETS      256
#define AS_MAX_SUBWAVES  16
#define AS_MAX_PRECACHE  128
enum { AS_GENERAL, AS_LOCAL, AS_BMODEL };
struct as_set_t {
    char name[64];
    int  type;                       // AS_GENERAL, AS_LOCAL, AS_BMODEL
    int  tbwMin, tbwMax;             // seconds
    int  volMin, volMax;             // 0..255
    int  radius;
    char loopPath[MAX_QPATH];
    char subPaths[AS_MAX_SUBWAVES][MAX_QPATH];
    int  numSubWaves;
    bool registered;                 // looped/subWaves hold sound handles
    int  looped;                     // sfxHandle, 0 = none
    int  subWaves[AS_MAX_SUBWAVES];
    int  nextWave;                   // Sys_Milliseconds of a general set's next one-shot
};
static as_set_t s_asSets[AS_MAX_SETS];
static int      s_asNumSets = 0;
static bool     s_asParsed = false;
static char     s_asPrecache[AS_MAX_PRECACHE][64];
static int      s_asNumPrecache = 0;
static const float s_asZero[3] = {0,0,0};

// Local string helpers — the engine's q_shared funcs are C-linkage and not visible to this C++ TU.
static int  AS_lc(int c){ return (c>='A'&&c<='Z') ? c+32 : c; }
static int  AS_stricmp(const char* a, const char* b){
    for(;;){ int ca=AS_lc((unsigned char)*a), cb=AS_lc((unsigned char)*b); if(ca!=cb) return ca-cb; if(!ca) return 0; a++; b++; }
}
static void AS_strcpy(char* d, const char* s, int n){ if(n<=0) return; int i=0; for(; i<n-1 && s[i]; i++) d[i]=s[i]; d[i]=0; }
// Self-contained tokenizer (avoids C/C++ linkage mismatch vs the engine's C COM_Parse).
// allowNL=false returns "" at the end of the line (every keyword takes its values from its own line).
// Handles ; and // line comments, /* */ comments, "quoted" names and standalone { } tokens.
static char* AS_Tok(char** dp, bool allowNL){
    static char tok[256];
    char* p = *dp; tok[0] = 0;
    if(!p){ return tok; }
    for(;;){
        while(*p && (unsigned char)*p <= ' '){
            if(*p=='\n' && !allowNL){ *dp=p; return tok; }
            p++;
        }
        if(!*p){ *dp=p; return tok; }
        if(*p==';' || (p[0]=='/' && p[1]=='/')){ while(*p && *p!='\n') p++; continue; }
        if(p[0]=='/' && p[1]=='*'){ p+=2; while(*p && !(p[0]=='*'&&p[1]=='/')) p++; if(*p) p+=2; continue; }
        break;
    }
    int n=0;
    if(*p=='"'){
        p++;
        while(*p && *p!='"'){ if(n<255) tok[n++]=*p; p++; }
        if(*p=='"') p++;
    } else if(*p=='{' || *p=='}'){
        tok[n++]=*p++;
    } else {
        while(*p && (unsigned char)*p > ' ' && *p!='{' && *p!='}' && *p!='"' && *p!=';'){ if(n<255) tok[n++]=*p; p++; }
    }
    tok[n]=0; *dp=p; return tok;
}
static void AS_SkipLine(char** dp){ while(AS_Tok(dp,false)[0]){} }
static void AS_Range(char** dp, int* lo, int* hi){
    *lo = atoi(AS_Tok(dp,false)); *hi = atoi(AS_Tok(dp,false));
    if(*lo > *hi){ int t=*lo; *lo=*hi; *hi=t; }
}
static as_set_t* AS_Lookup(const char* name, int type){
    for(int i=0;i<s_asNumSets;i++)
        if(s_asSets[i].type==type && !AS_stricmp(s_asSets[i].name, name)) return &s_asSets[i];
    return NULL;
}
static void AS_Register(as_set_t* s){
    if(s->registered) return;
    s->registered = true;
    if(s->loopPath[0]){
        s->looped = SPR_S_RegisterSound(s->loopPath);
        if(s->looped < 1) bwarn("AS_ParseSets: ^3Unable to load ambient sound \"%s\"\n", s->loopPath);
    }
    for(int i=0;i<s->numSubWaves;i++){
        s->subWaves[i] = SPR_S_RegisterSound(s->subPaths[i]);
        if(s->subWaves[i] < 1) bwarn("AS_ParseSets: ^3Unable to load ambient sound \"%s\"\n", s->subPaths[i]);
    }
}
static void SP_AS_ParseSets(void);
static as_set_t* AS_FindSet(const char* name, int type){
    if(!name || !name[0]) return NULL;
    if(!s_asParsed) SP_AS_ParseSets();       // the game can ask for a mover's sounds before the cgame loads the sets
    as_set_t* s = AS_Lookup(name, type);
    if(s) AS_Register(s);
    return s;
}
static int AS_RandInterval(const as_set_t* s){
    int span = s->tbwMax - s->tbwMin;
    int secs = s->tbwMin + (span>0 ? (rand()%(span+1)) : 0);
    return secs*1000;
}
static int AS_RandVolume(const as_set_t* s){   // 0-255, a random one-shot volume in [volMin,volMax]
    int span = s->volMax - s->volMin;
    return s->volMin + (span>0 ? (rand()%(span+1)) : 0);
}
static bool AS_HasWaves(const as_set_t* s){ return s->numSubWaves > 0 && s->tbwMax > 0; }
static void SP_AS_AddPrecache(const char* name){
    if(!name || !name[0] || s_asNumPrecache >= AS_MAX_PRECACHE) return;
    AS_strcpy(s_asPrecache[s_asNumPrecache++], name, sizeof(s_asPrecache[0]));
}
static void SP_AS_ParseSets(void){
    s_asNumSets = 0;
    s_asParsed = true;
    void* buf = NULL;
    int len = (int)FS_ReadFile("sound/sound.txt", &buf);
    if(len <= 0 || !buf){
        bwarn("AS_ParseSets: ^1Couldn't load ambient sound sets from sound/sound.txt\n");
        s_asNumPrecache = 0;
        return;
    }
    char* p = (char*)buf;
    as_set_t* s = NULL;                                  // the set the keyword lines belong to
    for(;;){
        const char* tok = AS_Tok(&p,true);
        if(!tok[0]) break;                               // EOF
        int type = -1;
        if(!AS_stricmp(tok,"generalSet"))     type = AS_GENERAL;
        else if(!AS_stricmp(tok,"localSet"))  type = AS_LOCAL;
        else if(!AS_stricmp(tok,"bmodelSet")) type = AS_BMODEL;
        if(type >= 0){
            char setName[64];
            AS_strcpy(setName, AS_Tok(&p,false), sizeof(setName));
            AS_SkipLine(&p);
            s = NULL;                                    // a skipped set's keywords are skipped too
            if(!setName[0] || !AS_stricmp(setName,"{")) continue;
            if(AS_Lookup(setName, type)){ bwarn("AS_ParseSets: ^3Duplicate ambient set \"%s\"\n", setName); continue; }
            if(s_asNumSets >= AS_MAX_SETS){ bwarn("AS_ParseSets: ^3Too many ambient sets, \"%s\" skipped\n", setName); continue; }
            s = &s_asSets[s_asNumSets++];
            memset(s, 0, sizeof(*s));
            AS_strcpy(s->name, setName, sizeof(s->name));
            s->type=type; s->tbwMin=10; s->tbwMax=25; s->volMin=255; s->volMax=255; s->radius=250;
            continue;
        }
        if(tok[0]=='{' || tok[0]=='}') continue;         // the { } form: the block only groups the keywords
        if(!s){ AS_SkipLine(&p); continue; }            // header lines
        if(!AS_stricmp(tok,"loopedWave")){
            const char* f = AS_Tok(&p,false);
            if(f[0]) snprintf(s->loopPath,sizeof(s->loopPath),"sound/%s.wav",f);
        } else if(!AS_stricmp(tok,"subWaves")){
            char dir[MAX_QPATH]; AS_strcpy(dir, AS_Tok(&p,false), sizeof(dir));
            const char* wn;
            while((wn=AS_Tok(&p,false))[0]){
                if(s->numSubWaves >= AS_MAX_SUBWAVES){ bwarn("AS_ParseSets: ^3Too many subwaves on set \"%s\"\n", s->name); break; }
                snprintf(s->subPaths[s->numSubWaves++],sizeof(s->subPaths[0]),"sound/%s/%s.wav",dir,wn);
            }
        } else if(!AS_stricmp(tok,"volRange")){
            AS_Range(&p, &s->volMin, &s->volMax);
            if(s->volMin < 0) s->volMin = 0;
            if(s->volMax > 255) s->volMax = 255;
        } else if(!AS_stricmp(tok,"timeBetweenWaves")){
            AS_Range(&p, &s->tbwMin, &s->tbwMax);
            if(s->tbwMin < 0) s->tbwMin = 0;
        } else if(!AS_stricmp(tok,"radius")){
            s->radius = atoi(AS_Tok(&p,false));
        } else {
            bwarn("AS_ParseSets: ^3Unknown ambient set keyword \"%s\"\n", tok);
        }
        AS_SkipLine(&p);
    }
    FS_FreeFile(buf);
    for(int i=0;i<s_asNumPrecache;i++)                   // the sets this map names
        for(int j=0;j<s_asNumSets;j++)
            if(!AS_stricmp(s_asSets[j].name, s_asPrecache[i])) AS_Register(&s_asSets[j]);
    blog("AS_ParseFile: Parsed %d ambient set(s), %d named by the map\n", s_asNumSets, s_asNumPrecache);
    s_asNumPrecache = 0;
}
static void SP_AS_UpdateAmbientSet(const char* name, const float* origin){
    as_set_t* s = AS_FindSet(name, AS_GENERAL);
    if(!s) return;
    if(s->looped > 0 && !s_spSoundsBlocked)
        SPR_S_AddLoopingSound(MAX_GENTITIES-1, origin, s_asZero, s->looped);   // the loop on ENTITYNUM_NONE
    if(AS_HasWaves(s)){
        int now = Sys_Milliseconds();
        if(s->nextWave == 0)        s->nextWave = now + AS_RandInterval(s);     // delay the first one-shot
        else if(now >= s->nextWave){
            int h = s->subWaves[rand()%s->numSubWaves];
            if(h > 0 && !s_spSoundsBlocked) SPR_S_StartSoundVol(NULL, 0, 0 /*CHAN_AUTO*/, h, AS_RandVolume(s));   // on the player
            s->nextWave = now + AS_RandInterval(s);
        }
    }
}
// Returns the entity's next one-shot time (the cgame keeps it in gent->setTime).
static int SP_AS_AddLocalSet(const char* name, const float* origin, int entID, int setTime){
    as_set_t* s = AS_FindSet(name, AS_LOCAL);
    if(!s || entID < 0 || entID >= MAX_GENTITIES) return setTime;
    if(entID != 0 && entID < ge->num_entities){
        SP_View view; SP_GetView( &view );
        if( !SP_EntInView( &view, SP_GEntity(entID) ) ) return setTime;           // behind a closed door / out of the PVS
    }
    if(s->looped > 0 && !s_spSoundsBlocked)
        SPR_S_AddLoopingSound(entID, origin, s_asZero, s->looped);
    if(AS_HasWaves(s)){
        int now = Sys_Milliseconds();
        // first time in view, or a time from another run (a loaded save): schedule the first one-shot
        if(setTime <= 0 || setTime > now + s->tbwMax*1000) return now + AS_RandInterval(s);
        if(now >= setTime){
            int h = s->subWaves[rand()%s->numSubWaves];
            if(h > 0 && !s_spSoundsBlocked) SPR_S_StartSoundVol(origin, entID, 0 /*CHAN_AUTO*/, h, AS_RandVolume(s));
            return now + AS_RandInterval(s);
        }
    }
    return setTime;
}
// The mover sound for stage 0 start, 1 moving (looped), 2 stop, or -1 for none: G_PlayDoorSound only
// skips on -1, so a 0 would send EV_BMODEL_SOUND with an empty sound on every door.
static int SP_AS_GetBModelSound(const char* name, int stage){
    as_set_t* s = AS_FindSet(name, AS_BMODEL);
    if(s && stage>=0 && stage < s->numSubWaves && s->subWaves[stage] > 0) return s->subWaves[stage];
    return -1;
}

static intptr_t SP_CgameSyscall( intptr_t cmd, ... ){
    intptr_t args[16]; va_list ap; va_start(ap,cmd);
    for(int i=0;i<16;i++) args[i]=va_arg(ap,intptr_t);
    va_end(ap);
    if( s_spSkipDraw ){    // skipping a camera scene: the cgame runs (commands, camera) but draws nothing
        switch(cmd){
        case CG_R_ADDREFENTITYTOSCENE: case CG_R_ADDPOLYTOSCENE: case CG_R_ADDLIGHTTOSCENE:
        case CG_R_RENDERSCENE: case CG_R_DRAWSTRETCHPIC: case CG_R_DRAWROTATEPIC: case CG_R_DRAWSCREENSHOT:
            return 0;
        default: break;
        }
    }
    if( s_spSoundsBlocked ){   // paused or skipping: no new game sounds (the music still plays)
        switch(cmd){
        case CG_S_STARTSOUND:
            if( (int)A(2) == 3 || (int)A(2) == 4 ) blog("voice: dropped start on ent %d (sounds blocked)\n", (int)A(1));
            return 0;
        case CG_S_STARTLOCALSOUND: case CG_S_ADDLOOPINGSOUND:
            return 0;
        default: break;
        }
    }
    switch(cmd){
#ifdef __ANDROID__
    case CG_PRINT:        blog("%s",(const char*)P(0)); return 0;
#else
    case CG_PRINT:        SP_ComPrintf()("%s",(const char*)P(0)); return 0;   // cgame text -> engine console as-is
#endif
    case CG_ERROR:        SP_ComError(0,"%s",(const char*)P(0)); return 0;
    case CG_MILLISECONDS: return Sys_Milliseconds();
    case CG_CVAR_REGISTER: Cvar_Register((vmCvar_t*)P(0),(const char*)P(1),(const char*)P(2),(int)A(3)); return 0;
    case CG_CVAR_UPDATE:   Cvar_Update((vmCvar_t*)P(0)); return 0;
    case CG_CVAR_SET:      Cvar_Set((const char*)P(0),(const char*)P(1)); return 0;
    case CG_ARGC:          return Cmd_Argc();
    case CG_ARGV:          { char*b=(char*)P(1); int sz=(int)A(2); const char*v=Cmd_Argv((int)A(0)); if(sz>0){strncpy(b,v,sz-1);b[sz-1]=0;} } return 0;
    case CG_ARGS:          Cmd_ArgsBuffer((char*)P(0),(int)A(1)); return 0;
    case CG_FS_FOPENFILE:  return FS_FOpenFileByMode((const char*)P(0),(fileHandle_t*)P(1),(fsMode_t)A(2));
    case CG_FS_READ:       return FS_Read(P(0),(int)A(1),(fileHandle_t)A(2));
    case CG_FS_WRITE:      return FS_Write(P(0),(int)A(1),(fileHandle_t)A(2));
    case CG_FS_FCLOSEFILE: FS_FCloseFile((fileHandle_t)A(0)); return 0;
    case CG_SENDCONSOLECOMMAND: Cbuf_AddText((const char*)P(0)); return 0;
    case CG_ADDCOMMAND:    SP_CgameAddCommand((const char*)P(0)); return 0;
    case CG_SENDCLIENTCOMMAND:
        // The cgame sends SERVER-handled commands (give/god/noclip/notarget/setobjective/...) via
        // trap_SendClientCommand. In our loopback bridge there's no netchan: tokenize the command and
        // hand it straight to the game's ClientCommand (which reads args via gi.argc/argv = Cmd_*),
        // exactly as SP_ClientCommand does. Was stubbed -> these commands silently did nothing.
        if(ge && g_spActive){ Cmd_TokenizeString((const char*)P(0)); ge->ClientCommand(0); }
        return 0;
    case CG_UPDATESCREEN:
        // EF1 SP 1:1 loading screen: during CG_INIT the cgame calls cgi_UpdateScreen at every load stage
        // (CG_LoadingString, which bumps cg.loadLCARSStage). Retail routes that through SCR_UpdateScreen back
        // into the cgame's own CG_DrawInformation -> animated LCARS load bar + levelshot + quoted level name.
        // Drive that same path here so the load screen actually animates, instead of dropping the redraw (this
        // was a silent no-op -> the port showed no loading art between levels).
        SP_PumpLoadScreen();
        return 0;
    // collision -> reuse engine CM (world)
    case CG_CM_LOADMAP:    return 0;   // already loaded server-side
    case CG_CM_NUMINLINEMODELS: return CM_NumInlineModels();
    case CG_CM_INLINEMODEL: return CM_InlineModel((int)A(0));
    case CG_CM_POINTCONTENTS: return CM_PointContents((float*)P(0),(int)A(1));
    case CG_CM_BOXTRACE:   CM_BoxTrace((trace_t*)P(0),(float*)P(1),(float*)P(2),(float*)P(3),(float*)P(4),(int)A(5),(int)A(6),0); return 0;
    case CG_CM_TEMPBOXMODEL: return CM_TempBoxModel((float*)P(0),(float*)P(1),0);
    case CG_CM_TRANSFORMEDPOINTCONTENTS: return CM_TransformedPointContents((float*)P(0),(int)A(1),(float*)P(2),(float*)P(3));
    case CG_CM_TRANSFORMEDBOXTRACE: CM_TransformedBoxTrace((trace_t*)P(0),(float*)P(1),(float*)P(2),(float*)P(3),(float*)P(4),(int)A(5),(int)A(6),(float*)P(7),(float*)P(8),0); return 0;
    case CG_CM_MARKFRAGMENTS: return SPR_MarkFragments((int)A(0),(const void*)P(1),(const float*)P(2),(int)A(3),(float*)P(4),(int)A(5),(void*)P(6));
    case CG_R_DRAWROTATEPIC:  SPR_DrawStretchPic(VMF(0),VMF(1),VMF(2),VMF(3),VMF(4),VMF(5),VMF(6),VMF(7),(int)A(9)); return 0; // rotation VMF(8) dropped
    case CG_R_SCISSOR:        return 0;     // no clip-rect support in re; HUD draws unclipped
    case CG_S_GETSAMPLELENGTH: return SPR_S_GetSampleLength((int)A(0));   // real sound duration ms (subtitle timing)
    // client state (the critical path)
    case CG_GETGLCONFIG: {   // live size: the desktop window can be resized in place (Alt+Enter)
        int rw=0, rh=0; SPR_GetVidSize(&rw,&rh);
        if(rw>0 && rh>0){ g_glconfig.vidWidth=rw; g_glconfig.vidHeight=rh; g_glconfig.windowAspect=(float)rw/(float)rh; }
        *(glconfig_t*)P(0) = g_glconfig; return 0; }
    case CG_GETGAMESTATE:  *(gameState_t*)P(0) = g_gameState; return 0;
    // Retail CL_GetCurrentSnapshotNumber: return the LATEST built snap's number + its serverTime.
    case CG_GETCURRENTSNAPSHOTNUMBER: {
        int latest = g_snapNum - 1;
        *(int*)P(0) = latest;
        *(int*)P(1) = (latest >= 1) ? g_snapHist[latest & (SNAP_RING-1)].serverTime : g_levelTime;
        return 0;
    }
    // Retail CL_GetSnapshot(num,out): return the ring slot for THAT number; qfalse if too old / future /
    // not yet built (cgame treats qfalse as "extrapolate", NOT an error). Distinct numbered snaps are what
    // give cg.snap != cg.nextSnap (serverTimes 50ms apart) and thus interpolation.
    case CG_GETSNAPSHOT: {
        int num    = (int)A(0);
        int latest = g_snapNum - 1;
        if( num < 1 || num > latest || (latest - num) >= SNAP_RING ) return qfalse;
        *(snapshot_t*)P(1) = g_snapHist[num & (SNAP_RING-1)];
        return qtrue;
    }
    case CG_GETSERVERCOMMAND: {
        int seq = (int)A(0);
        if( seq <= 0 || seq > g_svCmdSeq || seq <= g_svCmdSeq - SC_RING ) return qfalse;
#ifdef EF_COOP
        if( g_svCmdFor[seq & (SC_RING-1)] == COOP_FOR_GUEST ) return qfalse;   // player 2's: the guest shows it
#endif
        Cmd_TokenizeString( g_svCmds[seq & (SC_RING-1)] );   // cgame then reads via CG_ARGC/CG_ARGV
        return qtrue;
    }
#ifdef EF_COOP
    case CG_GETCURRENTCMDNUMBER: if( g_coopReplica ) return SP_CoopCurrentCmd(); return g_cmdNum;
    case CG_GETUSERCMD:    if( g_coopReplica ) return SP_CoopUserCmd( (int)A(0), (usercmd_t*)P(1) );
                           *(usercmd_t*)P(1) = g_lastcmd; return qtrue;
#else
    case CG_GETCURRENTCMDNUMBER: return g_cmdNum;
    case CG_GETUSERCMD:    *(usercmd_t*)P(1) = g_lastcmd; return qtrue;
#endif
    // Weapon select AND the cgame mouse-look sensitivity scale. The 2nd arg (VMF(1)) feeds
    // cl.cgameSensitivity, which CL_MouseMove multiplies the mouse delta by — if left 0 (the
    // CL_ClearState default), touch/mouse look is dead (x0) while key/joystick look still works.
    case CG_SETUSERCMDVALUE: g_weaponSelect=(int)A(0); CL_SetUserCmdValue((int)A(0), VMF(1)); return 0;
    case CG_MEMORY_REMAINING: return 0x4000000;
    // renderer -> real HM renderer via SPR_* wrappers
    case CG_R_REGISTERMODEL:      return SPR_RegisterModel((const char*)P(0));
    case CG_R_REGISTERSKIN:       return SPR_RegisterSkin((const char*)P(0));
    case CG_R_REGISTERSHADER:     return SPR_RegisterShader((const char*)P(0));
    case CG_R_REGISTERSHADERNOMIP:return SPR_RegisterShaderNoMip((const char*)P(0));
    case CG_R_LOADWORLDMAP: {  // Route b: SP loads world; legacy: SPR_LoadWorld no-ops (HM spmap already did)
        const char* wm = (const char*)P(0);
        char fb[128];
        if( !wm || !wm[0] || !strcmp(wm,"maps/.bsp") ){ snprintf(fb,sizeof(fb),"maps/%s.bsp", g_curMap); wm = fb; }  // cgame mapname empty -> use bridge's
        SPR_LoadWorld(wm); blog("CG_R_LOADWORLDMAP %s\n", wm);
        return 0;
    }
    case CG_R_CLEARSCENE:         SPR_ClearScene(); return 0;
    case CG_R_ADDREFENTITYTOSCENE:SPR_AddRefEntity(P(0), (int)sizeof(refEntity_t)); return 0;
    case CG_R_ADDPOLYTOSCENE:     SPR_AddPoly((int)A(0),(int)A(1),P(2)); return 0;
    case CG_R_ADDLIGHTTOSCENE:    SPR_AddLight((const float*)P(0), VMF(1), VMF(2), VMF(3), VMF(4)); return 0;
    case CG_R_RENDERSCENE:        SPR_RenderScene(P(0), (int)sizeof(refdef_t)); return 0;
    case CG_R_SETCOLOR:           SPR_SetColor((const float*)P(0)); return 0;
    case CG_R_DRAWSTRETCHPIC:     SPR_DrawStretchPic(VMF(0),VMF(1),VMF(2),VMF(3),VMF(4),VMF(5),VMF(6),VMF(7),(int)A(8)); return 0;
    case CG_R_MODELBOUNDS:        SPR_ModelBounds((int)A(0),(float*)P(1),(float*)P(2)); return 0;
    case CG_R_LERPTAG:            return SPR_LerpTag(P(0),(int)A(1),(int)A(2),(int)A(3),VMF(4),(const char*)P(5));
    case CG_R_GETLIGHTING:        SPR_GetLighting((const float*)P(0),(float*)P(1),(float*)P(2),(float*)P(3)); return 0;
    case CG_S_REGISTERSOUND:      return SPR_S_RegisterSound((const char*)P(0));
    case CG_S_STARTSOUND: {
        int sEnt=(int)A(1), sChan=(int)A(2), sSfx=(int)A(3);
        SPR_S_StartSound((const float*)P(0), sEnt, sChan, sSfx);
        // CHAN_VOICE=3 / CHAN_VOICE_ATTEN=4 (game channels.h): flag the voice as playing so the
        // dialog script's TID_CHAN_VOICE wait holds until the line finishes (see g_voiceEnd above).
        if((sChan==3 || sChan==4) && sEnt>=0 && sEnt<MAX_GENTITIES){
            int dur = SPR_S_GetSampleLength(sSfx);
            g_voiceEnd[sEnt] = g_levelTime + (dur>0 ? dur : 2500);
            blog("voice: ent %d talks %d ms from levelTime %d\n", sEnt, dur>0 ? dur : 2500, g_levelTime);
            g_sOverride[sEnt] = -1;   // talking, mouth at rest until the mixer has a level
        }
        return 0; }
    case CG_S_STARTLOCALSOUND:    SPR_S_StartLocalSound((int)A(0),(int)A(1)); return 0;
    case CG_S_CLEARLOOPINGSOUNDS: SPR_S_ClearLoopingSounds(); return 0;
    case CG_S_ADDLOOPINGSOUND:    SPR_S_AddLoopingSound((int)A(0),(const float*)P(1),(const float*)P(2),(int)A(3)); return 0;
    case CG_S_UPDATEENTITYPOSITION: SPR_S_UpdateEntityPosition((int)A(0),(const float*)P(1)); return 0;
    case CG_S_RESPATIALIZE:       SPR_S_Respatialize((int)A(0),(const float*)P(1),(float(*)[3])P(2),(int)A(3)); return 0;
    case CG_S_STARTBACKGROUNDTRACK: SPR_S_StartBackgroundTrack((const char*)P(0),(const char*)P(1)); return 0;
    // force-feedback (DirectInput joystick rumble; irrelevant on Android) -> no-op
    case CG_FF_STARTFX: case CG_FF_ENSUREFX: case CG_FF_STOPFX: case CG_FF_STOPALLFX: return 0;
    // ambient sound sets (sound/sound.txt, see SP_AS_ParseSets above)
    case CG_AS_PARSESETS:         SP_AS_ParseSets(); return 0;
    case CG_AS_ADDENTRY:          SP_AS_AddPrecache((const char*)P(0)); return 0;   // a set this map uses
    case CG_S_UPDATEAMBIENTSET:   SP_AS_UpdateAmbientSet((const char*)P(0),(const float*)P(1)); return 0;
    // args: 0=name 1=listener_origin 2=origin 3=entID 4=time (listener unused; engine spatializes by origin)
    case CG_S_ADDLOCALSET:        return SP_AS_AddLocalSet((const char*)P(0),(const float*)P(2),(int)A(3),(int)A(4));
    // the bmodel set's sound for a mover stage, or -1 for none (see SP_AS_GetBModelSound)
    case CG_AS_GETBMODELSOUND: return SP_AS_GetBModelSound((const char*)P(0),(int)A(1));
    case CG_R_DRAWSCREENSHOT:
        // EF1 SP 1:1: on a full-save load (g_eSavedGameJustLoaded==eFULL) CG_DrawInformation draws the saved
        // screenshot behind the LCARS load bar via this trap. Retail keeps the save's 'SHOT' chunk as a 256x256
        // RGBA texture and blits it fullscreen, opaque, vertically flipped. The cgame passes (0, vidHeight,
        // vidWidth, -vidHeight): the negative height makes RB_StretchPic flip our bottom-up 'SHOT' upright (the
        // exact path the save menu uses for its thumbnail). g_loadShot is populated on load (SP_LoadGameFile).
        if( g_loadShotValid )
            SPR_DrawStretchRaw((int)VMF(0),(int)VMF(1),(int)VMF(2),(int)VMF(3), 256, 256, g_loadShot);
        return 0;
    default: return 0;
    }
}

// ---- savegame file open/close (shared with gi.AppendToSaveGame/ReadFromSaveGame via s_sgFile) ----
static qboolean SG_OpenWrite(const char* path){ s_sgFile = FS_FOpenFileWrite(path); s_sgWriting = qtrue;  return s_sgFile != 0; }
static qboolean SG_OpenRead (const char* path){ fileHandle_t f=0; FS_FOpenFileRead(path,&f,qtrue); s_sgFile=f; s_sgWriting=qfalse; return s_sgFile != 0; }
static void     SG_Close(void){ if(s_sgFile) FS_FCloseFile(s_sgFile); s_sgFile=0; s_sgWriting=qfalse; }

// ---- level transitions (Phase 3): borg1->borg2, turbolift/hub. G_ChangeMap emits maptransition/
// loadtransition; the player's gclient/objectives/ICARUS vars carry via a WriteLevel/ReadLevel save. ----
static char g_transMap[64] = {0}, g_transSpawn[64] = {0};
static int  g_transHub = 0;
// Retail derives the SavedGameJustLoaded flag as (GAME_chunk != 0) + 1: a normal user save writes
// GAME=0 -> eFULL on load; a forward maptransition autosave writes GAME!=0 -> eAUTO on load (eAUTO is
// what makes ClientSpawn run Player_RestoreFromPrevLevel to carry the loadout). We hardcoded eFULL for
// both. Track which path we're on: 1 = forward transition (eAUTO), else the loaded GAME flag.
static int  g_transGameFlag = 0;
// Distinguishes the two eAUTO (g_transGameFlag!=0) paths into SP_FinishTransition, which need OPPOSITE level
// handling: an eAUTO autosave LOAD (death-respawn / `load auto`, set by SP_LoadGameFile) must ge->ReadLevel(qtrue)
// the saved entity-0/objectives/ICARUS; a FORWARD map transition (borg1->borg2, set by SP_MapTransition) must
// DISCARD the snapshot (its __trans.sav holds the OLD map's full table, whose inline-model *N indices would
// crash the new BSP) and carry the player via the playersave/* cvars. 1 = autosave load, 0 = forward transition.
static int  g_transIsAutoLoad = 0;
#define TRANS_SAV "saves/__trans.sav"

// SP loading-screen gate (defined in sp_integration.c): hold the per-map levelshot over the
// whole spmap->efsptrans reload window so the HM loading splash never shows during a transition.
extern "C" void SP_BeginLoad(const char* map);
extern "C" void SP_EndLoad(void);
extern "C" void SPR_LoadWorld(const char* name);   // Route b: SP cgame loads the renderer world (gated on sp_dropHM)
extern "C" void SPR_BeginRegistration(void);       // per-map renderer reset (R_Init) before a reload's CG_INIT
static void SP_WriteEntryAutosave(void);           // level-entry autosave (fresh spawn only) -> death-respawn point
extern "C" void SPR_ShutdownRenderer(void);        // GPU teardown half — MUST run before Hunk_Clear (see below)
extern "C" void SPR_InitRenderer(void);            // R_Init half — runs after Hunk_Clear, before CG_INIT
extern "C" int  SP_DropHM(void);                   // 1 = drop the HM spmap/qagame/cgame (single SP cgame)

// Called from the maptransition/loadtransition command handlers. Saves the live level then reloads
// the world (HM spmap) and finishes the SP re-init (efsptrans) once the new BSP is up.
extern "C" void SP_MapTransition(const char* map, const char* spawn, int isHub){
    if(!ge || !map || !map[0]) return;
    // P1-1: a load/transition is consume-once (g_transMap is cleared only when SP_FinishTransition runs). If
    // one is already pending, a SECOND destination queued in the same Cbuf drain (e.g. a turbolift `use` plus a
    // script-driven changelevel, or a `load` racing a `maptransition`) would silently overwrite g_transMap and
    // drop the first -> wrong-map desync. Ignore the overlapping request; the first transition still completes.
    if( g_transMap[0] ){ bwarn("SP_MapTransition: '%s' already pending -> ignoring overlapping '%s'\n", g_transMap, map); return; }
    SP_BeginLoad(map);
    blog("SP_MapTransition: -> %s (spawn '%s' hub %d)\n", map, spawn?spawn:"", isHub);
    if(SG_OpenWrite(TRANS_SAV)){ ge->WriteLevel(qfalse); SG_Close(); }   // snapshot carried state
    strncpy(g_transMap, map, sizeof(g_transMap)-1);   g_transMap[sizeof(g_transMap)-1]=0;
    strncpy(g_transSpawn, spawn?spawn:"", sizeof(g_transSpawn)-1); g_transSpawn[sizeof(g_transSpawn)-1]=0;
    g_transHub = isHub;
    g_transGameFlag = 1;   // forward transition -> eAUTO (carry player loadout via Player_RestoreFromPrevLevel)
    g_transIsAutoLoad = 0; // forward transition (NOT an autosave load) -> discard the old-map snapshot below
    char buf[160];
    if( SP_DropHM() ) snprintf(buf,sizeof(buf),"efsptrans\n");                      // Route b: SP_FinishTransition loads world+CM itself
    else              snprintf(buf,sizeof(buf),"spmap %s\nwait 250\nefsptrans\n", map);
    Cbuf_AddText(buf);                 // reload renderer world + CM, then SP_FinishTransition
}

// Called by the 'efsptrans' command after spmap has loaded the new world. Tears down the old SP
// level (reusing the loaded libefgame.so), re-inits on the new map with the carried state.
extern "C" qboolean SP_FinishTransition(void){
    if(!g_transMap[0]){ bwarn("SP_FinishTransition: no pending map (g_transMap empty) -> abort\n"); return qfalse; }
    // P1-2: silence in-flight one-shots + background music BEFORE the libefgame reload so they don't bleed
    // across the transition. Looping sounds self-heal on the new cgame's first frame (S_ClearLoopingSounds),
    // but one-shots/music have no such reset; without this a VO line or ambient cue carries into the next map.
    { extern void S_StopAllSounds(void); S_StopAllSounds(); }
    // COLD LOAD: if the game module isn't loaded yet, load it now. This happens when a save is loaded
    // straight from the boot main menu with no New Game first — the autoexec no longer auto-loads a map,
    // so libefgame.so was never dlopen'd. Previously ge==NULL made this bail silently, then
    // SP_DropHM_Activate set clc.state=CA_ACTIVE with no game -> client spun (MAX_PACKET_USERCMDS) -> hang.
    SP_LoadGame("libefgame.so");        // UNCONDITIONAL reload: dlclose+dlopen -> OS re-zeroes ALL game+cgame
                                        // globals (retail's per-map DLL reload). s_sgFile is a bridge FS handle,
                                        // so the open save survives -> ge->ReadLevel below restores the saved
                                        // state into the fresh module. SP_UnloadGame already did the teardown.
    if(!ge || !g_vmMain){ bwarn("SP_FinishTransition: game module failed to load -> abort\n"); return qfalse; }
    blog("SP_FinishTransition: %s spawn=%s\n", g_transMap, g_transSpawn);
    g_spActive = 0;
    memset(g_entClip, 0, sizeof(g_entClip)); g_svCmdSeq = 0; g_snapNum = 1; g_cmdNum = 1;   // bridge statics
    memset(g_snapHist, 0, sizeof(g_snapHist)); g_snapBaseTime = 0; g_simResidual = 0;        // ring + cadence

    // EF1 SP 1:1: retail clears the permanent hunk + collision map on EVERY spawn in SV_SpawnServer
    // (sv_init.c:419-422 — Hunk_Clear() then CM_ClearMap(), before CM_LoadMap). The SP bridge runs the
    // Route-b transition WITHOUT SV_SpawnServer, so nothing reclaimed the hunk: CG_INIT below re-registers
    // every model of the new map into the low hunk (renderervk/tr_model.c R_LoadMD3/MDR -> Hunk_Alloc h_low),
    // and R_Init re-allocs backEndData, on top of every PRIOR map's allocations. After a few transitions the
    // 128MB hunk fills -> "Hunk_Alloc failed" -> recursive error -> crash (observed loading voy3.bsp:651).
    // CM_ClearMap() is required alongside the wipe so CM_LoadMap actually reloads (it resets cm.name, else the
    // same-map fast-path could hand back a map whose data was just cleared).
    //
    // ORDER IS CRITICAL: the renderer's image_t structs (textures of the OLD map — or, on a cold load straight
    // from the boot menu, the MENU's textures) live in this same permanent hunk, and re.Shutdown(qfalse) walks
    // tr.images[] to destroy each VkImageView. So we must tear the GPU resources down FIRST, while that hunk
    // memory is still valid; if Hunk_Clear (and CM_LoadMap reusing the freed space) ran first, re.Shutdown would
    // destroy garbage handles -> vkDestroyImageView SIGSEGV (the load-game crash). R_Init is therefore deferred
    // to SPR_InitRenderer() further below, AFTER the clear, so it re-allocates into the fresh hunk. Nothing
    // renders between here and there, so the renderer sitting shut-down across this span is safe.
    SPR_ShutdownRenderer();   // re.Shutdown(qfalse): destroy old textures/imageviews + reset descriptor pool
    Hunk_Clear();             // reclaim the permanent hunk (models/world/backEndData) now that the GPU side is torn down
    CM_ClearMap();
    char bsp[256]; snprintf(bsp,sizeof(bsp),"maps/%s.bsp", g_transMap);
    int checksum=0; CM_LoadMap(bsp, qfalse, &checksum);
    // Forward transition (no save-table restore): clear the configstring table fresh like retail's per-map
    // spawn, so the new map's model/sound indices are deterministic and the table doesn't grow unbounded across
    // transitions. A LOAD (g_transGameFlag==0) must NOT clear here — SP_LoadGameFile already restored the saved
    // table into g_configstrings (clearing it would drop the restore and reintroduce the index mismatch).
    if( g_transGameFlag != 0 ){
        memset(g_configstrings, 0, sizeof(g_configstrings));
    }
    snprintf(g_configstrings[CS_SERVERINFO], sizeof(g_configstrings[0]), "\\mapname\\%s\\sv_maxclients\\1", g_transMap);  // cgame mapname for Route-b world load + info
    char* entstring = CM_EntityString();
    g_numLinked = 0;
    memset(g_entPortalOpen, 0, sizeof(g_entPortalOpen));  // CM reloaded this map -> all areaportals start sealed
    // eAUTO for forward transitions (so ClientSpawn restores the carried loadout); eFULL for a normal
    // user save-load (unchanged behavior). Mirrors retail SV_ReadGame's just-loaded flag = (GAME!=0)+1.
    SavedGameJustLoaded_e sg = (g_transGameFlag != 0) ? eAUTO : eFULL;
    blog("SP_FinishTransition: sg=%d (transGameFlag=%d hub=%d)\n", (int)sg, g_transGameFlag, g_transHub);
    // RETAIL PARITY: reset the level clock to 1000 on every eAUTO spawn. Retail SV_LoadGame runs SV_SpawnServer
    // (which sets the level clock to 1000) and reads the saved 'TIME' chunk back into it
    // ONLY for eFULL user saves (`if (GAME==0)`); an eAUTO load keeps SV_SpawnServer's 1000. The port had
    // diverged: a forward transition carried the monotonic session g_levelTime forward (never reset), and an
    // eAUTO autosave LOAD set g_levelTime=tnow in SP_LoadGameFile -- so a mid-campaign map's opening script
    // ran at a multi-million-ms clock instead of ~1000. EF1 SP openings are timing-sensitive (the warm-up
    // gate g_target.cpp:707 `level.time<1000`, and NPC-nav-gated player unlocks): voy6's intro nests its ONLY
    // SET_PLAYER_LOCKED false inside affect("introfoster") behind a scripted walk, so at the high clock that
    // block never completes and the player is locked forever (can look, can't move). eAUTO respawns every
    // entity FRESH from the BSP here (their think/nav timers re-derive against the new clock), so 1000 is the
    // correct, retail-matching origin. eFULL is deliberately untouched: its ReadLevel restores absolute
    // entity timers, so it MUST keep the saved TIME (g_levelTime=tnow set in SP_LoadGameFile).
    int savedTime = g_levelTime;   // an eAUTO autosave LOAD: the save's TIME (SP_LoadGameFile), see ReadLevel below
    if( sg == eAUTO ){
        blog("SP_FinishTransition: eAUTO -> reset g_levelTime %d -> 1000 (retail SV_SpawnServer parity)\n", g_levelTime);
        g_levelTime = 1000;
    }
    ge->Init(g_transMap, g_transSpawn, checksum, entstring, g_levelTime,
             Regress_Active() ? Regress_Seed() : Sys_Milliseconds(), Sys_Milliseconds(), sg, g_transHub ? qtrue : qfalse);
#ifdef EF_COOP
    g_coopLevelId++;
    g_coopReplica = 0;   // a replica level starts only through SP_SpawnServer (the guest's map load)
    SP_CoopHoldStart();
#endif
    // Fresh spawn (eAUTO: forward transition or autosave load): retail SV_SpawnServer's settle frames, before the
    // player connects. Not for an eFULL user load: ReadLevel below replaces every entity with the saved, already
    // settled ones, so frames run on the fresh spawn would only start scripts that the load then throws away.
    if( sg == eAUTO ) SP_RunFrames( SP_SETTLE_FRAMES );
    BuildGameState();
    char* denied = ge->ClientConnect(0, qfalse, sg);
    if(denied) bwarn("SP_FinishTransition: ClientConnect denied: %s\n", denied);
    // Restore carried state BEFORE ClientBegin (retail order: Init -> ClientConnect -> ReadLevel ->
    // ClientBegin). ReadLevel byte-restores every gentity incl. NPC ps.weapon/torsoAnim and reattaches
    // ICARUS; doing it AFTER ClientBegin let the first frame assert PM_TorsoAnimation's armed default
    // (WP_PHASER->TORSO_WEAPONREADY) before the restored calm NPC state was in place -> Tuvok stuck in
    // the phaser-aim pose after a load/transition. For a LOAD, s_sgFile is already open + positioned at
    // the game chunks; for a transition, open __trans.sav now.
    // Forward map transition (eAUTO, g_transGameFlag!=0, e.g. borg1->borg2): borg2's entities were just
    // spawned FRESH by ge->Init above. Do NOT ReadLevel the previous map's saved entity table here — it holds
    // the OLD map's inline brush-model (*N) indices, which are out of range for the new (often smaller) BSP, so
    // the cgame's prediction clip hits CM_InlineModel "bad number" a few frames in -> ERR_DROP -> back to menu.
    // The player's loadout is carried instead via the playersave/playerammo/borgadapt cvars (recorded by the
    // prior level's G_ChangeMap -> Player_CacheForNextLevel) and restored by Player_RestoreFromPrevLevel inside
    // ge->ClientBegin(...,eAUTO) below. (Retail forward transitions also spawn fresh + carry the player via
    // those cvars; they do not restore the old map's entities.) A full USER LOAD (g_transGameFlag==0, same map)
    // still restores the saved entities exactly as before.
    if( g_transGameFlag == 0 ){
        if(s_sgFile || SG_OpenRead(TRANS_SAV)){ s_sgWriting=qfalse; ge->ReadLevel(qfalse, g_transHub ? qtrue : qfalse); SG_Close(); }
    } else if( g_transIsAutoLoad && s_sgFile ){
        // eAUTO autosave LOAD (death-respawn `load *respawn`->auto, or loading a level-entry / genuine retail
        // eAUTO save). The save holds ONLY entity 0 + objectives + ICARUS vars (WriteLevel(qtrue)), so
        // ReadLevel(qtrue) restores those while EVERY other entity respawns FRESH from the BSP (ge->Init above)
        // -> the spent opening script_runner is re-created and the opening cinematic re-fires (re-locking the
        // player), fixing the eFULL free-walk bug (the spent script was restored, so the opening never re-fired).
        // Reading only entity 0 also avoids the forward-transition inline-model crash -- an autosave has no
        // old-map entity table. The
        // player loadout was carried via the CVSV/AMMO/ADPT cvars (restored in SP_LoadGameFile) and is applied
        // by Player_RestoreFromPrevLevel inside ge->ClientBegin(...,eAUTO) below (g_client.cpp:891-896).
        s_sgWriting=qfalse; ge->ReadLevel(qtrue, qfalse); SG_Close();
        blog("SP_FinishTransition: eAUTO autosave load -> ReadLevel(qtrue), entities respawn fresh\n");
        // ReadLevel restored level_locals, level.time included, from the save. If that's later than the bridge
        // clock after the settle frames (an entry autosave is written after the level-start ticks), move the
        // clock up to it so ClientBegin and the following ticks agree with the game's level.time.
        if( savedTime > g_levelTime ){
            blog("SP_FinishTransition: autosave level time %d > %d -> clock follows the save\n", savedTime, g_levelTime);
            g_levelTime = savedTime;
        }
    } else if( s_sgFile ){
        SG_Close();   // forward transition (eAUTO): discard the level snapshot; the player carries via cvars
    }
    // A user (eFULL) load: put back which areaportals were open,
    // after the spawn. Without it every door read as closed after a load: an open door blocked sight, and the
    // CM counts no longer matched the doors. The save doesn't record which entity holds which portal, so the
    // per-entity guard goes to "unknown" (see W_AdjustAreaPortalState). An autosave (eAUTO) respawns every entity
    // fresh, with its doors closed, so its counts are not applied.
    if( g_transGameFlag == 0 && g_loadPortals ){
        if( CM_SetPortalState( g_loadPortals, g_loadPortalsSize ) ){
            memset(g_entPortalOpen, -1, sizeof(g_entPortalOpen));
            int open=0; for(int i=0;i<g_loadPortalsSize/(int)sizeof(int);i++) if(g_loadPortals[i]>0) open++;
            blog("SP_FinishTransition: restored areaportal state (%d bytes, %d open entries)\n", g_loadPortalsSize, open);
        } else {
            blog("SP_FinishTransition: save's areaportal state is %d bytes, map has %d -> ignored\n", g_loadPortalsSize, CM_PortalStateSize());
        }
    }
    SP_DropLoadPortals();
    memset(&g_lastcmd,0,sizeof(g_lastcmd)); g_lastcmd.serverTime = g_levelTime;
    SP_ResetViewAngles();   // the usercmd angles restart at 0, matching g_lastcmd (see SP_ResetViewAngles)
    ge->ClientBegin(0, &g_lastcmd, sg);
    // Robustly re-link EVERY in-use brush-model entity after a load. The game's ReadLevel only relinks
    // entities whose SAVED 'linked' flag was true (g_savegame.cpp:790-795); a save made on an older build
    // can bake in linked=false for static brush models, leaving them out of g_linked -> invisible to
    // trigger-touch AND collision (e.g. the tutorial's continue_15 lava-cross trigger + bigforcefield).
    // Moving brush models self-heal (per-frame relink) but static ones never do. Force them all in.
    { int relinked=0;
      for(int i=1;i<ge->num_entities;i++){
          gentity_t* e = SP_GEntity(i);
          if(!e || !e->inuse) continue;
          // Brush-model entities that need to be in g_linked: solid brushes (doors/forcefields/movers,
          // s.solid==SOLID_BMODEL) AND triggers (contents&CONTENTS_TRIGGER 0x40000000 — NOT solid, so they
          // have s.solid!=SOLID_BMODEL and the earlier solid-only pass missed them: that's why the
          // continue_15 trigger stayed unlinked). Add to g_linked using the restored absmin/absmax/contents
          // (don't recompute — ReadLevel already restored them; this is just g_linked membership).
          if(e->s.solid != SOLID_BMODEL && !(e->contents & 0x40000000)) continue;
          e->linked = qtrue;
          { qboolean present=qfalse; int li;
            for(li=0; li<g_numLinked; li++){ if(g_linked[li]==e){ present=qtrue; break; } }
            if(!present && g_numLinked<SP_MAX_ENT){ g_linked[g_numLinked++]=e; relinked++; }
          }
      }
      blog("SP_FinishTransition: re-linked %d brush/trigger entities after ReadLevel (g_numLinked=%d)\n", relinked, g_numLinked);
    }
    // Fresh spawn: let the player land and the spawn scripts/events play out before the first snapshot (the load
    // screen stays up, see SP_SettleClient). An eFULL load shows the saved moment as it was, with one frame.
    if( sg == eAUTO ) SP_SettleClient( SP_SETTLE_CLIENT_TICKS );
    else { SP_GameRunFrame(); }
    BuildSnapshot();
    BuildGameState();                   // rebuild AFTER ClientBegin so CS_PLAYERS+0 (player model) is in the
                                        // gamestate the cgame reads at CG_INIT (no live CG_CONFIGSTRING path)
    SP_FillGlConfig();                  // MUST run before CG_INIT: without it g_glconfig.vidWidth=0 on the
                                        // LOAD/transition path -> cgame refdef 0x0 -> black 3D world. See helper.
    SPR_InitRenderer();                 // R_Init (the GPU teardown half, SPR_ShutdownRenderer, already ran ABOVE
                                        // before Hunk_Clear). Clears tr.worldMapLoaded so the new world loads
                                        // (fixes "redundantly load world map" on Load Game / level transition).
    SP_VmMain(CG_INIT, 0,0,0);           // new cgame (re-registers media + CG_R_LOADWORLDMAP)
    g_spActive = 1;
    s_worldResident = qtrue;            // C1: a world now occupies the hunk/GPU -> a later New Game must reclaim
    // EF1 SP 1:1: retail SV_SpawnServer always unpauses on spawn ("make sure we are not paused",
    // sv_init.c:464 Cvar_Set("cl_paused","0")). The bridge bypasses SV_SpawnServer, so without this a
    // load reached via the in-game ESC menu (UIMENU_INGAME sets cl_paused=1, ui_atoms.c:832) stays
    // paused: SP_MenuPaused() now honors cl_paused (for the inGameCinematic overlay), so the freshly
    // loaded level never advances -> no snapshots -> MAX_PACKET_USERCMDS spin -> hang. Unpause here.
    Cvar_Set( "cl_paused", "0" );
    SP_EndLoad();   // new SP level is live -> stop drawing the transition load screen
    strncpy(g_curMap, g_transMap, sizeof(g_curMap)-1); g_curMap[sizeof(g_curMap)-1]=0;
    g_transMap[0] = 0;
    // EF1 SP 1:1: a FORWARD transition (g_transGameFlag!=0, g_transIsAutoLoad==0, fresh spawn) is a new level
    // entry -> write the level-start eAUTO autosave so death-respawn has a checkpoint. Mirror retail's
    // just-loaded==0 guard (the entry autosave is written only when NO save was just loaded): an eAUTO autosave
    // LOAD (death-respawn, g_transIsAutoLoad==1) must NOT rewrite `auto` -- that would overwrite the player's own
    // checkpoint with the just-reloaded state. A user eFULL LOAD (g_transGameFlag==0) is already excluded.
    if( g_transGameFlag != 0 && !g_transIsAutoLoad ) SP_WriteEntryAutosave();
    blog("SP_FinishTransition: done numEnt=%d ps.origin=(%.0f %.0f %.0f)\n",
         ge->num_entities, g_snap.ps.origin[0], g_snap.ps.origin[1], g_snap.ps.origin[2]);
    return qtrue;
}

// 'use <targetname>' and other game console commands -> the game's ClientCommand (turbolift, triggers).
extern "C" void SP_ClientCommand(void){ if(ge && g_spActive) ge->ClientCommand(0); }

// ---- user save / load (Phase 2). Retail-byte-compatible framing chunks around ge->WriteLevel/ReadLevel.
// Retail's save gate: SV_SaveGame calls SV_CanSave, which
// delegates the cutscene check to the game DLL's GameAllowedToSaveHere() (game_export +0x14 == !in_camera).
// The port already ships that export (g_savegame.cpp:999) but never consulted it. Expose it for the UI
// bridge (sp_ui_bridge.cpp) and the console-save backstop below. Returns true if no game is loaded.
extern "C" qboolean SP_GameAllowedToSaveHere(void){
    return (ge && ge->GameAllowedToSaveHere) ? ge->GameAllowedToSaveHere() : qtrue;
}

// Set when an AUTOSAVE was requested during a cutscene (in_camera): SP_DrawFrame writes it on the first
// non-cinematic frame. See SP_SaveGame / the death-respawn-into-cutscene fix below.
static int g_pendingAutosave = 0;

// Internal save writer. bEAUTO selects retail SV_SaveGame's two flavours:
//  - bEAUTO=qfalse -> eFULL: GAME=0 + ge->WriteLevel(qfalse) (full gclient + entity table). User saves and the
//    mid-level target_autosave checkpoint (`save auto*`, retail gameFlag 0). On load the exact level is restored.
//  - bEAUTO=qtrue  -> eAUTO: GAME=1 + ge->WriteLevel(qtrue) (entity 0 + objectives + ICARUS only) + the player
//    carry chunks CVSV/AMMO/ADPT. The level-entry autosave (retail writes it with gameFlag 1).
//    On load the level respawns FRESH from the BSP (the spent opening script_runner re-fires -> the cinematic
//    re-locks the player) and the loadout is restored via Player_RestoreFromPrevLevel from the carry cvars.
static void SP_SaveGameInternal(const char* name, qboolean bEAUTO){
    if(!ge || !g_spActive || !name || !name[0]){ bwarn("save: not in a game\n"); return; }
#ifdef EF_COOP
    if( g_coopReplica ){ SP_ComPrintf()("co-op: only the host saves the game\n"); return; }
#endif
    // Cutscene gate (retail SV_CanSave -> GameAllowedToSaveHere == !in_camera). Retail SV_SaveGame only checks
    // it for gameFlag 0: `if ((param_2==0) && !SV_CanSave) return;`. So an eAUTO autosave (bEAUTO) is NEVER
    // gated -- it writes immediately even during the opening cinematic, which is SAFE now that it is eAUTO:
    // WriteLevel(qtrue) records no spent-script/in_camera entity state, and the load respawns fresh (the
    // cutscene replays). Only eFULL saves honour the gate:
    //  - MANUAL eFULL save during a cutscene: REFUSE (matches retail).
    //  - eFULL "auto" (target_autosave checkpoint) during a cutscene: DEFER to the first non-cinematic frame.
    qboolean isAuto = (strcmp(name,"auto") == 0) ? qtrue : qfalse;
    if( !bEAUTO && ge->GameAllowedToSaveHere && !ge->GameAllowedToSaveHere() ){
        if( isAuto ){
            g_pendingAutosave = 1;
            blog("save: eFULL autosave checkpoint DEFERRED (in cutscene) -> will write when in_camera clears\n");
        } else {
            SP_ComPrintf()("^1Cannot save during a cutscene.\n");   // engine console (Com_Printf is C-linkage; route via the shim)
            bwarn("save: manual save blocked during cutscene -- matches retail SV_CanSave\n");
        }
        return;
    }
    if( isAuto ) g_pendingAutosave = 0;   // writing an autosave now satisfies any pending deferral
    if(!SG_OpenWrite("saves/current.sav")){ bwarn("save: open failed\n"); return; }
    static unsigned char shot[256*256*4];      // SHOT thumbnail: 256x256 RGBA, bottom-up (retail 0x40000-byte chunk)
    char comm[128], mpcm[1024]; int zero=0, tnow=g_levelTime;
    memset(comm,0,sizeof(comm)); memset(mpcm,0,sizeof(mpcm));
    // EFSP: embed a real screenshot. SPR_GetSaveThumb copies the latest framebuffer capture (armed ~1x/sec on
    // un-paused gameplay frames in SP_DrawFrame, so it's the clean 3D+HUD with NO menu overlay — the ESC/Save
    // menu pauses, freezing further captures, so the last clean game frame is what a manual save embeds).
    // Falls back to black if nothing has been captured yet (e.g. a save before the first gameplay frame).
    if( !SPR_GetSaveThumb(shot) ) memset(shot,0,sizeof(shot));
    // COMM (128) = the save comment the Load/Save menu parses (ui_game.cpp:617-643, iSG_COMMENT_SIZE):
    //   [0..62] description, [64..71] time, [73..82] date, [84..] map name. Each null-terminated.
    {
        const char* desc = g_spSaveComment[0] ? g_spSaveComment : g_curMap;
        // Autosaves funnel through SP_SaveGame("auto") (level-entry, target_autosave's `save auto*`,
        // and forward-transition autosave). Label them so the Load menu reads "Autosave - <map>"
        // instead of a bare mapname; ignore any stale user comment for the auto slot.
        char autobuf[64];
        if( !strcmp(name, "auto") ){          // name is the literal lowercase "auto" for every autosave path
            snprintf(autobuf, sizeof(autobuf), "Autosave - %s", g_curMap);
            desc = autobuf;
        }
        int i; for(i=0;i<63 && desc[i];i++) comm[i]=desc[i];
        time_t tt = time(NULL); struct tm* lt = localtime(&tt);
        if(lt){ strftime(comm+64, 9, "%H:%M:%S", lt); strftime(comm+73, 9, "%m/%d/%y", lt); }   // retail uses 2-digit year "MM/DD/YY"
        for(i=0;i<43 && g_curMap[i];i++) comm[84+i]=g_curMap[i];
    }
    snprintf(mpcm,sizeof(mpcm),"%s", g_curMap);
    I_Append('COMM', comm, 128); I_Append('SHOT', shot, 256*256*4); I_Append('MPCM', mpcm, 1024);
    // CVCN: persist the savegame cvars so a load restores the exact save-time state, byte-matching retail.
    // RETAIL FILTER IS CVAR_USERINFO (bit 0x2), NOT CVAR_ARCHIVE: retail SV_WriteGame walks the
    // cvar registry and writes a 'CVCN' count then ('CVAR' name)+('VALU' value) pairs ONLY for cvars whose
    // flags have bit 2 set -> a retail auto.sav carries EXACTLY 9: g_gravity/legsModel/torsoModel/headModel/
    // handicap/sex/snaps/name/g_spskill. The game deliberately tags these CVAR_USERINFO ("using userinfo as
    // savegame flag", g_main.cpp:152/154). Using CVAR_ARCHIVE here was WRONG -- in this ioq3-derived engine
    // hundreds of cvars are CVAR_ARCHIVE (all r_*/cg_*/cl_*/s_*/net_*), so the save wrote ~240 cvars and the
    // load (SP_LoadGameFile) Cvar_Set them all back, CLOBBERING the player's live video/audio/control settings
    // to save-time values (only cg_forceAspect was band-aided). CVAR_USERINFO restores the retail 9 and matches
    // the PC save byte-for-byte. g_spskill (ARCHIVE|USERINFO) and g_gravity (USERINFO) are both still captured.
    // Old 240-cvar saves still load fine (the read loop is generic on the CVCN count).
    {
        char info[8192]; { const char* src=Cvar_InfoString_Big(CVAR_USERINFO); int i=0; for(; src[i] && i<8191; i++) info[i]=src[i]; info[i]=0; }
        int seps=0; for(const char* c=info; *c; c++) if(*c=='\\') seps++;
        int nc = seps/2;                                     // infostring "\k\v\k\v" => 2 backslashes per cvar
        I_Append('CVCN', &nc, 4);
        const char* p = info;
        for(int ci=0; ci<nc; ci++){
            char nm[64]={0}, vl[256]={0}; int n;
            if(*p=='\\') p++;  n=0; while(*p && *p!='\\' && n<63)  nm[n++]=*p++; nm[n]=0;   // key
            if(*p=='\\') p++;  n=0; while(*p && *p!='\\' && n<255) vl[n++]=*p++; vl[n]=0;   // value
            I_Append('CVAR', nm, (int)strlen(nm)+1);
            I_Append('VALU', vl, (int)strlen(vl)+1);
        }
    }
    int gameFlag = bEAUTO ? 1 : 0;
    I_Append('GAME', &gameFlag, 4);
    // eAUTO carry (retail SV_SaveGame eAUTO path, written ONLY when gameFlag!=0): persist the engine carry-cvars the
    // game's Player_RestoreFromPrevLevel reads on the fresh respawn. The cvar names MUST match the game exactly
    // -- "playersave"/"playerammo%d"/"borgadapt%d", NO underscore (g_client.cpp:698/712/719). Written right
    // after GAME so SP_LoadGameFile's game!=0 branch consumes them in the SAME order (the stream stays framed).
    // eFULL saves write none of these -> their load (game==0) reads none -> existing saves are unaffected.
    if( bEAUTO ){
        char cv[1024];
        cv[0]=0; Cvar_VariableStringBuffer("playersave", cv, sizeof(cv)); I_Append('CVSV', cv, (int)strlen(cv)+1);
        for(int i=0;i<4;i++){  char nm[32]; snprintf(nm,sizeof(nm),"playerammo%d",i); cv[0]=0; Cvar_VariableStringBuffer(nm,cv,sizeof(cv)); I_Append('AMMO', cv, (int)strlen(cv)+1); }
        for(int i=0;i<32;i++){ char nm[32]; snprintf(nm,sizeof(nm),"borgadapt%d",i); cv[0]=0; Cvar_VariableStringBuffer(nm,cv,sizeof(cv)); I_Append('ADPT', cv, (int)strlen(cv)+1); }
    }
    I_Append('TIME', &tnow, 4);
    I_Append('TIMR', &zero, 4);
    // PRTS: the areaportal reference counts (numAreas^2 ints). Which doors
    // are open, so sight, PVS and sound culling stay right after a load. (1.3.0 and older wrote 1296 zero bytes.)
    I_Append('PRTS', (void*)CM_PortalState(), CM_PortalStateSize());
    // CSCN/CSIN/CSDA: persist the configstring table so a load can rebuild the EXACT index->string map that
    // every entity's s.modelindex (and the cgame's model_draw[]/sound indices) were assigned against. The
    // bridge's g_configstrings lives in libmain and is NEVER cleared per-map, so a load that doesn't restore
    // it leaves the cgame's rebuilt table mismatched against the ReadLevel'd entity indices -> wrong/zero
    // model handles (borg2 props drew the RGB null-model axis because a saved s.modelindex landed on an empty
    // _c1 chunk slot in the now-stale table). Mirrors retail (skip index 1 = systeminfo).
    { int csn=0;
      for(int i=0;i<CS_MAX;i++){ if(i!=1 && g_configstrings[i][0]) csn++; }
      I_Append('CSCN', &csn, 4);
      for(int i=0;i<CS_MAX;i++){
          if(i==1 || !g_configstrings[i][0]) continue;
          I_Append('CSIN', &i, 4);
          I_Append('CSDA', g_configstrings[i], (int)strlen(g_configstrings[i])+1);
      } }
#ifdef EF_COOP
    if( g_coopRep ) Cvar_Set( "coop_p2saved", "" );   // the co-op game fills it in WriteLevel (player 2's state)
#endif
    ge->WriteLevel(bEAUTO);                     // eFULL(qfalse): gclient + full entity table; eAUTO(qtrue): entity 0 + objectives/ICARUS only
    SG_Close();
    char to[96]; snprintf(to,sizeof(to),"saves/%s.sav", name);
    FS_Rename("saves/current.sav", to);
    binfo("save: wrote %s (map %s, t=%d, %s)\n", to, g_curMap, tnow, bEAUTO ? "eAUTO" : "eFULL");
#ifdef EF_COOP
    if( g_coopRep ) SP_CoopWriteSideFile( name );
#endif
}

// Public save entry (the `save` console command, deferred-autosave writer, quick-save): always eFULL, exactly
// like retail's `save`/`wipe` command path (gameFlag 0). The eAUTO level-entry autosave goes through
// SP_SaveGameInternal(...,qtrue) from SP_WriteEntryAutosave only.
extern "C" void SP_SaveGame(const char* name){ SP_SaveGameInternal(name, qfalse); }

// EF1 SP 1:1: retail writes an `auto` autosave on every FRESH (non-loaded) level entry, right after the
// first ClientBegin (retail SV spawn path), so the death-screen respawn (`load *respawn` -> load `auto`,
// see SP_Load_f) ALWAYS has a level-start checkpoint to reload -- even before the level's first
// target_autosave. Holodeck/_brig (`_`-prefixed) maps are skipped, exactly like retail (respawn() spawns
// in place there). Called from the fresh-spawn paths only (New Game, forward transition) -- NEVER on a user
// load, which would clobber the player's own checkpoint with the just-loaded state.
static void SP_SaveGameInternal(const char* name, qboolean bEAUTO);   // fwd
static void SP_WriteEntryAutosave(void){
    if( !g_spActive ) return;
    if( g_curMap[0] == '_' ) return;        // _holo*/_brig: no entry autosave (respawn() spawns in place)
    // Retail writes the level-entry "auto" with gameFlag 1 = eAUTO (SV_SaveGame("auto",1)
    // on a fresh non-'_' map entry, when nothing was just loaded). eAUTO is what makes the death-respawn `load *respawn`
    // re-spawn the level FRESH (opening cinematic re-fires + re-locks the player) instead of restoring the
    // spent opening script_runner (the eFULL free-walk bug, where the cinematic resumed but the player was free).
    SP_SaveGameInternal("auto", qtrue);
    blog("SP_WriteEntryAutosave: wrote eAUTO level-entry autosave for %s\n", g_curMap);
}

extern "C" void SP_LoadGameFile(const char* name){
    if(!name || !name[0]) return;
#ifdef EF_COOP
    if( g_coopReplica ){ SP_ComPrintf()("co-op: player 2 can't load a game (coop_disconnect leaves the host first)\n"); return; }
#endif
    // P1-1: don't clobber an already-pending load/transition (consume-once g_transMap). A load requested while
    // another is mid-flight would overwrite the pending map + reuse __trans.sav -> wrong-map desync.
    if( g_transMap[0] ){ bwarn("SP_LoadGameFile: '%s' already pending -> ignoring overlapping load '%s'\n", g_transMap, name); return; }
    char path[96]; snprintf(path,sizeof(path),"saves/%s.sav", name);
    if(!SG_OpenRead(path)){ bwarn("load: open failed %s\n", path); return; }
#ifdef EF_COOP
    SP_CoopReadSideFile( name );
#endif
    static unsigned char shot[256*256*4];
    char comm[128], mpcm[1024]; int ncvc=0, game=0, tnow=0, timr=0, cscn=0;
    // EFSP 4:3: cg_forceAspect is a DISPLAY preference (the video-menu Aspect toggle), not gameplay state. The
    // CVCN bag below restores EVERY saved CVAR_ARCHIVE cvar verbatim (retail SV_ReadGame — left byte-identical),
    // and old saves baked in whatever aspect was set when they were written. We do NOT special-case the retail
    // restore loop; instead we snapshot the user's CURRENT aspect here and re-assert it AFTER the loop, so a
    // load never changes what's on screen (matches "persist unless manually changed"). cg_forceAspect doesn't
    // exist in retail saves anyway, so this is purely a port display-layer guard around an unmodified save read.
    char aspectPref[16]; aspectPref[0]=0; Cvar_VariableStringBuffer("cg_forceAspect", aspectPref, sizeof(aspectPref));
    I_ReadSG('COMM', comm,128,0); I_ReadSG('SHOT', shot,256*256*4,0); I_ReadSG('MPCM', mpcm,1024,0);
    // EF1 SP 1:1 load-from-save background: stash the save's screenshot so the loading screen can blit it behind
    // the LCARS bar (CG_DrawInformation's eFULL branch -> CG_R_DRAWSCREENSHOT). Retail's engine likewise copies
    // the 'SHOT' chunk into its persistent draw buffer on every load. Bottom-up 256x256 RGBA; drawn flipped.
    memcpy(g_loadShot, shot, sizeof(g_loadShot)); g_loadShotValid = 1;
    I_ReadSG('CVCN', &ncvc,4,0);                              // M-1: retail's archived-cvar count FourCC (was 'NCVC')
#ifndef CVAR_NONEXISTENT
#define CVAR_NONEXISTENT 0x80000000   // engine Cvar_Flags() sentinel; absent from the 2000-era game q_shared.h
#endif
    for(int i=0;i<ncvc;i++){
        char cv[64], vl[256]; cv[0]=0; vl[0]=0;
        I_ReadSG('CVAR',cv,0,0); I_ReadSG('VALU',vl,0,0);   // always consume the pair to keep the stream aligned
        cv[sizeof(cv)-1]=0; vl[sizeof(vl)-1]=0;
        // Restore each saved cvar (g_spskill/difficulty, model/name/handicap) BEFORE the deferred efsptrans
        // spawns the map, so the game reads save-time state at ge->Init, exactly like retail SV_ReadGame.
        // BUT filter the read side symmetrically with the write side (SP_SaveGameInternal now persists only
        // CVAR_USERINFO -- the retail-9). Old pre-filter saves baked in ~240 CVAR_ARCHIVE cvars (every r_*/cg_*/
        // cl_*/s_*/net_*...); Cvar_Set-ing those back CLOBBERS the device's live video/audio/input config to
        // save-time values -- which then get re-archived into efspconfig.cfg and persist. On some GPUs (e.g.
        // Mali-G52) a bad restored renderer-cvar combo (r_fbo/r_ext_multisample/r_renderScale/...) makes the
        // Vulkan device fault (VK_ERROR_DEVICE_LOST) on the first frame, and it also silently re-overwrites any
        // manual video-menu change the player makes. So restore ONLY true savegame cvars: those currently
        // flagged CVAR_USERINFO (the retail-9, incl. g_spskill which is ARCHIVE|USERINFO), plus gameplay cvars
        // the game DLL hasn't registered yet at this point in the load (nonexistent and not an engine namespace
        // -- e.g. g_* set by ge->Init). Engine-owned display/audio/input/system cvars are skipped. This stays
        // retail-faithful (retail only ever restored the 9) and does NOT change the save format -- old and new
        // saves both load fine; only the no-business-in-a-save values are no longer applied.
        if(cv[0]){
            int fl = Cvar_Flags(cv);
            qboolean exists = (fl != CVAR_NONEXISTENT);
            qboolean engineNS =                                  // engine-owned namespaces (note the underscore)
                !strncmp(cv,"r_",2)  || !strncmp(cv,"cg_",3) || !strncmp(cv,"cl_",3) ||
                !strncmp(cv,"s_",2)  || !strncmp(cv,"snd_",4)|| !strncmp(cv,"com_",4)||
                !strncmp(cv,"net_",4)|| !strncmp(cv,"in_",3) || !strncmp(cv,"cm_",3) ||
                !strncmp(cv,"vid_",4)|| !strncmp(cv,"sv_",3) || !strncmp(cv,"ui_",3) ||
                !strncmp(cv,"con_",4)|| !strncmp(cv,"vm_",3);
            qboolean restore = ( exists && (fl & CVAR_USERINFO) ) || ( !exists && !engineNS );
            if(restore) Cvar_Set(cv, vl);
        }
    }
    // EFSP 4:3: re-assert the user's live aspect preference over whatever the save restored above (display
    // preference, not save state). The retail restore loop ran untouched; this just keeps the screen aspect
    // unchanged across a load. Empty pref (cvar somehow absent) -> leave the save's value alone.
    if(aspectPref[0]) Cvar_Set("cg_forceAspect", aspectPref);
    I_ReadSG('GAME',&game,4,0);
    // L-3: a retail eAUTO save (GAME!=0) carries the cross-level player loadout here — CVSV(playersave, 1024) +
    // 4x AMMO(playerammo_%d) + 32x ADPT(borgadapt_%d) (retail SV_SaveGame eAUTO path). Consume + Cvar_Set so the stream
    // stays framed and the carry state restores. Port-written saves are always eFULL (GAME==0), so this is only
    // exercised when loading a genuine retail transition autosave.
    if(game!=0){
        // Restore the eAUTO carry cvars the game's Player_RestoreFromPrevLevel reads on the fresh respawn.
        // The cvar names MUST match the game EXACTLY -- "playerammo%d"/"borgadapt%d" with NO underscore
        // (g_client.cpp:662/669; retail efgame strings 200ce008/200cdffc). The earlier "playerammo_%d"/
        // "borgadapt_%d" set cvars the game never reads -> ammo/borg-adapt silently never restored. (Only ever
        // reachable for game!=0, i.e. eAUTO saves; eFULL saves skip this whole block, so older saves are
        // unaffected.) CVSV read len 0 = take the stored size (the write side is strlen+1).
        char cvsv[1024]; cvsv[0]=0; I_ReadSG('CVSV', cvsv,0,0); cvsv[sizeof(cvsv)-1]=0; Cvar_Set("playersave", cvsv);
        for(int i=0;i<4;i++){  char a[512]; a[0]=0; I_ReadSG('AMMO', a,0,0); a[sizeof(a)-1]=0; char nm[32]; snprintf(nm,sizeof(nm),"playerammo%d",i); Cvar_Set(nm,a); }
        for(int i=0;i<32;i++){ char d[512]; d[0]=0; I_ReadSG('ADPT', d,0,0); d[sizeof(d)-1]=0; char nm[32]; snprintf(nm,sizeof(nm),"borgadapt%d",i); Cvar_Set(nm,d); }
    }
    I_ReadSG('TIME',&tnow,4,0); I_ReadSG('TIMR',&timr,4,0);
    // PRTS: the areaportal counts. CM isn't loaded for the save's map yet, so keep it
    // for SP_FinishTransition. Old port saves wrote zeros here (all doors closed: what they loaded as before).
    SP_DropLoadPortals();
    { void* pr=0; int n=I_ReadSGOpt('PRTS',0,0,&pr);
      if( n>0 && pr && game==0 ){ g_loadPortals=(int*)pr; g_loadPortalsSize=n; } else free(pr); }
    I_ReadSG('CSCN',&cscn,4,0);
    // Restore the saved configstring table (retail SV_ReadGame: clear the whole table, then write each saved
    // index->string back). The cgame's model_draw[]/sound indices and every entity's s.modelindex were assigned
    // against THIS table at save time; g_configstrings (libmain) is never cleared per-map, so without this the
    // table BuildGameState rebuilds no longer matches the restored entity modelindex -> wrong/zero handles
    // (borg2's missing props / RGB null-model axis: a saved modelindex landed on an empty _c1 chunk slot).
    // Restoring here (before SP_FinishTransition's ge->Init) makes G_ModelIndex re-find the saved indices, so
    // the fresh spawn, the ReadLevel'd entities, and the cgame gamestate all agree. cscn==0 (pre-fix saves)
    // simply clears -> those legacy saves can't be recovered (the table was never written), but new saves are.
    for(int i=0;i<CS_MAX;i++) g_configstrings[i][0]=0;
    for(int i=0;i<cscn;i++){
        int idx=-1; char dat[1024]; dat[0]=0;
        I_ReadSG('CSIN',&idx,4,0);
        if(idx>=0 && idx<CS_MAX){ I_ReadSG('CSDA', g_configstrings[idx], 0, 0); g_configstrings[idx][sizeof(g_configstrings[0])-1]=0; }
        else                     I_ReadSG('CSDA', dat, 0, 0);   // out-of-range index: consume the chunk + discard
    }
    // s_sgFile stays OPEN here, positioned at the WriteLevel game chunks -> SP_FinishTransition ReadLevels it.
    strncpy(g_transMap, mpcm, sizeof(g_transMap)-1); g_transMap[sizeof(g_transMap)-1]=0;
    g_transSpawn[0]=0; g_transHub=0; g_levelTime = tnow;
    g_transGameFlag = game;   // (GAME!=0)+1 -> eAUTO for transition-saves, eFULL for normal user saves (GAME=0)
    g_transIsAutoLoad = (game != 0) ? 1 : 0;   // an eAUTO LOAD (vs forward transition): ReadLevel(qtrue) below
    SP_BeginLoad(mpcm);   // levelshot of the save's map over the reload window
    char buf[160];
    if( SP_DropHM() ) snprintf(buf,sizeof(buf),"efsptrans\n");                       // Route b: no HM spmap
    else              snprintf(buf,sizeof(buf),"spmap %s\nwait 250\nefsptrans\n", mpcm);
    Cbuf_AddText(buf);
    blog("load: %s -> map %s t=%d (deferred reload)\n", path, mpcm, tnow);
}

// Populate the cgame's glconfig (returned to the cgame via CG_GETGLCONFIG). CG_CalcVrect derives
// refdef.width/height from vidWidth/vidHeight, so if these are 0 the 3D viewport is 0x0 and R_RenderView
// early-returns (tr_main.c) -> the world renders BLACK while 2D/HUD/menu (independent of vidWidth) draw
// fine. This MUST run before CG_INIT on EVERY path that brings up a cgame. It used to live only in
// SP_StartClient (New Game); the LOAD / map-transition path (SP_FinishTransition) skipped it, so a
// cold-boot Load Game left g_glconfig.vidWidth=0 -> black 3D.
static void SP_FillGlConfig(void){
    memset(&g_glconfig,0,sizeof(g_glconfig));
    int rw=640, rh=480; SPR_GetVidSize(&rw,&rh); if(rw<=0)rw=640; if(rh<=0)rh=480;
    g_glconfig.vidWidth=rw; g_glconfig.vidHeight=rh; g_glconfig.windowAspect=(float)rw/(float)rh;
    blog("SP_FillGlConfig: vid size %dx%d\n", rw, rh);
    strcpy(g_glconfig.renderer_string,"efsp-headless");
    strcpy(g_glconfig.vendor_string,"efsp");
    strcpy(g_glconfig.version_string,"1.0");
}

extern "C" qboolean SP_StartClient(void){
    if(!ge||!g_vmMain){ bwarn("SP_StartClient: no game/cgame\n"); return qfalse; }
    // New Game is a FRESH start with NO carried-in loadout. Clear the cross-level carry cvars
    // (playersave/playerammo%d/borgadapt%d) that a prior same-session playthrough's forward transitions left set
    // (they are non-archived, so a cold app launch already has them empty -- this covers play->menu->New Game).
    // Otherwise the eAUTO level-entry autosave written below would serialize that STALE loadout into its
    // CVSV/AMMO/ADPT chunks, and a death-respawn on this New Game would restore the previous game's weapons/health.
    // Empty carry -> Player_RestoreFromPrevLevel no-ops (it gates on strlen(playersave)) -> the default ClientSpawn
    // loadout, which is the correct New-Game respawn. Forward transitions/loads set these elsewhere and are unaffected.
    Cvar_Set("playersave", "");
    for(int i=0;i<4;i++){  char nm[32]; snprintf(nm,sizeof(nm),"playerammo%d",i); Cvar_Set(nm,""); }
    for(int i=0;i<32;i++){ char nm[32]; snprintf(nm,sizeof(nm),"borgadapt%d",i); Cvar_Set(nm,""); }
    SP_FillGlConfig();   // glconfig (vidWidth/Height) for the cgame's CG_CalcVrect — see SP_FillGlConfig
    BuildGameState();
    // connect + spawn the single player
    char* denied = ge->ClientConnect(0, qtrue, (SavedGameJustLoaded_e)0);
    if(denied){ bwarn("SP_StartClient: ClientConnect denied: %s\n", denied); return qfalse; }
    memset(&g_lastcmd,0,sizeof(g_lastcmd)); g_lastcmd.serverTime=g_levelTime;
    SP_ResetViewAngles();   // the usercmd angles restart at 0, matching g_lastcmd (see SP_ResetViewAngles)
    ge->ClientBegin(0, &g_lastcmd, (SavedGameJustLoaded_e)0);
    blog("SP_StartClient: client connected+begun\n");
#ifdef EF_COOP
    extern int SP_CoopReplicaApply( void );
    if( g_coopReplica ) SP_CoopReplicaApply();   // the host's newest frame, before the cgame starts
    else
#endif
    SP_SettleClient( SP_SETTLE_CLIENT_TICKS );   // player lands, spawn scripts start, spawn events expire
    BuildSnapshot();
    // Rebuild the gamestate AFTER ClientBegin/RunFrame so it includes configstrings the game set during
    // spawn — notably CS_PLAYERS+0 (the local player's model/skin). The cgame reads configstrings ONLY
    // from CG_GETGAMESTATE (no live CG_CONFIGSTRING), so without this the player has no clientInfo:
    // no Munro model in mirrors/portals and no 3rd-person body. (Was built before ClientConnect -> empty.)
    BuildGameState();
    blog("SP_StartClient: first snapshot numEntities=%d ps.origin=(%.0f %.0f %.0f) ps.weapon=%d\n",
         g_snap.numEntities, g_snap.ps.origin[0], g_snap.ps.origin[1], g_snap.ps.origin[2], g_snap.ps.weapon);
    blog("SP_StartClient: cgame CG_INIT...\n");
    // Do NOT call SPR_BeginRegistration()/CL_InitRenderer here. On the fresh New-Game path there is no prior
    // cgame to CG_SHUTDOWN first, so an extra R_Init re-registers all renderer media WITHOUT releasing the
    // previous descriptor sets -> the Vulkan descriptor pool (vk.storage.descriptor) is exhausted on the
    // FIRST load (VK_ERROR_OUT_OF_POOL_MEMORY). The "redundantly load world map" error only occurs on a
    // *repeated* load of the SAME map, which is now prevented at the source by the SP_MapWrap_f debounce.
    // (SP_FinishTransition resets safely because it CG_SHUTDOWNs the prior cgame first, freeing those sets.)
    SP_VmMain(CG_INIT, 0, 0, 0);
    blog("SP_StartClient: CG_INIT returned OK\n");
    g_spActive = 1;
    SP_EndLoad();   // SP cgame is live now -> SP_DrawFrame owns the screen; stop the load screen
#ifdef EF_COOP
    if( g_coopReplica ) return qtrue;   // only the host saves
#endif
    SP_WriteEntryAutosave();   // New Game = fresh entry -> level-start autosave for death-respawn
    return qtrue;
}

extern "C" void SP_ClientFrames(int n){
    for(int i=0;i<n;i++){
        g_levelTime += 50;
        g_lastcmd.serverTime = g_levelTime;
        g_cmdNum++;
        Regress_SeedRand( g_levelTime );
        ge->ClientThink(0, &g_lastcmd);
        SP_GameRunFrame();
        BuildSnapshot();
        // Trail cg.time by one tick (clamped to snap #1) so priming frames present a valid snap/nextSnap
        // pair, matching SP_DrawFrame. With only one snap so far the clamp pins cg.time to base (nextSnap
        // NULL is fine — nothing flagged interpolate yet).
        int cgTime = g_levelTime - SP_INTERP_DELAY;
        if( cgTime < g_snapBaseTime ) cgTime = g_snapBaseTime;
        SP_VmMain(CG_DRAW_ACTIVE_FRAME, cgTime, 0 /*STEREO_CENTER*/, qfalse);
    }
    blog("SP_ClientFrames: drew %d cgame frames to levelTime=%d (numEntities=%d)\n", n, g_levelTime, g_snap.numEntities);
}

// EF1 SP 1:1 loading screen: present one cgame-drawn load frame. CG_DRAW_ACTIVE_FRAME early-returns into
// CG_DrawInformation (cg_info.cpp) whenever cg.infoScreenText is set (CG_LoadingString sets it at each load
// stage) -> the real levelshot + animated LCARS bar (driven by cg.loadLCARSStage) + the quoted level name
// (CS_MESSAGE). This is exactly what retail's engine drives via trap_UpdateScreen during CG_INIT; the cgame is
// alive here (we are inside the SP_VmMain(CG_INIT) call that requested the redraw via cgi_UpdateScreen), so
// re-entering it to draw is retail-faithful and side-effect free (CG_BuildSolidList/CG_UpdateCvars guard on
// cg.snap, identical to retail's info-screen path). Called only from SCR_DrawScreenField while a load is pumping.
extern "C" void SP_DrawCGameLoad(void){
    if(!g_vmMain) return;
    SP_VmMain(CG_DRAW_ACTIVE_FRAME, g_levelTime, 0 /*STEREO_CENTER*/, qfalse);
}

extern "C" int SP_IsActive(void){ return g_spActive; }
// Route-b: the bridge replaced the engine server, so the engine's SV_GameCommand (gated on
// sv.state==SS_GAME) never fires -> the game's exported ConsoleCommand (g_svcmds.cpp: use/nav/npc/
// Command/Hail/entitylist/ICARUS/...) was UNREACHABLE. That silently dropped every server console
// command issued by the game/UI/scripts -- notably the Virtual Voyager turbolift menu's
// "use tour_turbo_NN" (deck change) and the personal-log/holodeck "use <panel>" activations.
// SV_GameCommand now forwards here when SP is active (mirrors UI_GameCommand -> SPUI_ConsoleCommand).
// Returns 1 only if the game claimed the command, so unclaimed cmds still fall through to UI_GameCommand.
extern "C" int SP_GameConsoleCommand(void){
    return ( ge && g_spActive && ge->ConsoleCommand && ge->ConsoleCommand() ) ? 1 : 0;
}
// True while a save-load/transition is queued (g_transMap set) but the SP cgame hasn't come up yet.
// The load watchdog (sp_integration.c) uses this to retry a starved/dropped efsptrans before it
// gives up and bounces to the main menu.
extern "C" int SP_LoadPending(void){ return g_transMap[0] ? 1 : 0; }
// Bridge-state half of an abort-to-menu (P2-1/P2-2 + memory-audit H2). Called by SP_AbortToMenu when a load
// is abandoned: clear the consume-once pending-load fields so SP_LoadPending() doesn't stay stuck at 1, and
// drop the partial game module (SP_UnloadGame clears g_spActive + zeros the dangling g_linked[]). The engine
// side (clc.state, renderer, SP UI menu) is owned by SP_AbortToMenu. NOTE: if a transition ERR_DROP'd AFTER
// SPR_ShutdownRenderer but before SPR_InitRenderer, the renderer is still down here — that rare mid-transition
// case is handled by SP_AbortToMenu re-bringing the menu up; the common abort (New Game spawn failure) aborts
// before any renderer teardown, so the renderer/menu are live.
extern "C" void SP_AbortState(void){
    g_transMap[0] = 0; g_transSpawn[0] = 0; g_transGameFlag = 0;
    SP_UnloadGame();
}
// The currently-active SP map name (empty if none). Used by SP_MapWrap_f to debounce a spurious
// repeat of 'map <same-map>' that would otherwise reload and hit the renderer's redundant-world guard.
extern "C" const char* SP_CurMap(void){ return g_curMap; }
extern "C" {
#ifdef EF_COOP
    void  SCR_DrawBigString( int x, int y, const char *s, float alpha, qboolean noColorEscape );   // cl_scrn.c
#endif
    void  S_PauseGameSounds( qboolean pause );   // snd_main.c: hold / resume the game's sounds + music
    void  S_StopGameSounds( void );              // snd_main.c: stop effects + loops, keep the music
    void  S_ClearLoopingSounds( qboolean killall );
    float Cvar_VariableValue( const char *name );
}
// The cutscene-skip screen: black, with a row of LCARS-orange dots, one lit and stepping along, in place
// of the 3D view while the scene fast-forwards (retail drew nothing new while skipping).
static void SP_DrawSkipProgress(void){
    int w = g_glconfig.vidWidth, h = g_glconfig.vidHeight;
    if( w <= 0 || h <= 0 ) SPR_GetVidSize(&w, &h);
    if( w <= 0 || h <= 0 ) return;
    int white = SPR_RegisterShaderNoMip("white");
    static const float black[4] = { 0, 0, 0, 1 };
    static const float dim[4]   = { 0.45f, 0.27f, 0.10f, 1 };
    static const float lit[4]   = { 1.00f, 0.60f, 0.20f, 1 };
    SPR_SetColor(black);
    SPR_DrawStretchPic(0, 0, (float)w, (float)h, 0, 0, 1, 1, white);
    const int n = 5;
    float d = h / 60.0f; if( d < 4 ) d = 4;           // dot size scales with the screen
    float gap = d * 1.5f, rowW = n*d + (n-1)*gap;
    float x0 = (w - rowW) * 0.5f, y = h * 0.5f - d * 0.5f;
    int on = ( Sys_Milliseconds() / 200 ) % n;
    for( int i = 0; i < n; i++ ){
        SPR_SetColor( i == on ? lit : dim );
        SPR_DrawStretchPic(x0 + i*(d+gap), y, d, d, 0, 0, 1, 1, white);
    }
    SPR_SetColor(NULL);
}
#ifdef EF_COOP
// ---- co-op replica: the host's side ----
// The host's newest server commands for the guest (not player 1's own, and not the configstring
// updates: those travel as configstrings) as [newest seq][count] then count x ([seq][text\0]), for
// the guest to hand its cgame what's new.
static int SP_CoopCommandsImage( unsigned char *buf, int size ){
    int seqs[32], n = 0;
    for( int seq = g_svCmdSeq; seq > 0 && seq > g_svCmdSeq - SC_RING && n < 32; seq-- ){
        if( !strncmp( g_svCmds[seq & (SC_RING-1)], "cs ", 3 ) ) continue;
        if( g_svCmdFor[seq & (SC_RING-1)] == COOP_FOR_HOST ) continue;
        seqs[n++] = seq;
    }
    int len = 0;
    memcpy( buf + len, &g_svCmdSeq, 4 ); len += 4;
    memcpy( buf + len, &n, 4 ); len += 4;
    int put = 0;
    for( int i = n - 1; i >= 0; i-- ){
        const char *t = g_svCmds[seqs[i] & (SC_RING-1)];
        int tl = (int)strlen( t ) + 1;
        if( len + 4 + tl > size - 88 ) break;
        memcpy( buf + len, &seqs[i], 4 ); len += 4;
        memcpy( buf + len, t, tl ); len += tl;
        put++;
    }
    memcpy( buf + 4, &put, 4 );
    // then whether this level's start is held for the guest, whether a movie plays here now, and the
    // newest in-game movie: [held][playing][seq][name\0] (seq 0: none yet)
    int held = ( g_coopHoldLevel && g_coopHoldLevel == g_coopLevelId ) ? 1 : 0;
    int playing = CL_InGameCinematicActive() ? 1 : 0;
    memcpy( buf + len, &held, 4 ); len += 4;
    memcpy( buf + len, &playing, 4 ); len += 4;
    int ml = (int)strlen( g_coopMovie ) + 1;
    memcpy( buf + len, &g_coopMovieSeq, 4 ); len += 4;
    memcpy( buf + len, g_coopMovie, ml ); len += ml;
    return len;
}
// The game started an in-game movie ("inGameCinematic <name>"): the guest plays it too.
static void SP_CoopNoteMovie( const char *t ){
    char name[64]; int n = 0;
    t += 15;
    while( *t == ' ' || *t == '"' ) t++;
    while( *t && *t != ' ' && *t != '"' && *t != '\n' && *t != ';' && n < (int)sizeof(name) - 1 ){
        char c = *t++;
        if( !isalnum( (unsigned char)c ) && c != '_' && c != '-' && c != '.' && c != '/' ) return;   // not a movie name
        name[n++] = c;
    }
    name[n] = 0;
    if( !n ) return;
    g_coopMovieSeq++;
    memcpy( g_coopMovie, name, n + 1 );
}
// After every game frame while a guest is connected: the world as replica slots.
static void SP_CoopHostFrame( void ){
    static unsigned char buf[512 * 1024];
    static int warned;
    if( !g_coopRep || g_coopReplica || !g_spActive ) return;
    if( !CoopNet_HostFrameBegin( g_coopLevelId, g_levelTime, g_curMap ) ) return;
    static unsigned frameCount = 0;
    frameCount++;
    for( int i = 0; i < ge->num_entities && i < COOP_SLOT_CS; i++ ){
        // one player 2 can't see or hear: its last image again, but a fresh one now and then
        int every = g_coopRep->SendEvery( i );
        if( every > 1 && ( frameCount + i ) % every && CoopNet_HostFrameSlotKeep( i ) ) continue;
        int n = g_coopRep->WriteEntity( i, buf, sizeof(buf) );
        if( n > 0 ) CoopNet_HostFrameSlot( i, buf, n );
        else if( n < 0 && !warned ){ warned = 1; bwarn("co-op: entity %d's image is too big; not sent\n", i); }
    }
    for( int i = 0; i < CS_MAX && i < COOP_SLOT_GLOBALS - COOP_SLOT_CS; i++ ){
        if( g_configstrings[i][0] ) CoopNet_HostFrameSlot( COOP_SLOT_CS + i, g_configstrings[i], (int)strlen(g_configstrings[i]) + 1 );
    }
    int n = g_coopRep->WriteGlobals( buf, sizeof(buf) );
    if( n > 0 ) CoopNet_HostFrameSlot( COOP_SLOT_GLOBALS, buf, n );
    n = SP_CoopCommandsImage( buf, sizeof(buf) );
    CoopNet_HostFrameSlot( COOP_SLOT_COMMANDS, buf, n );
    CoopNet_HostFrameEnd();
}

// ---- co-op replica: the guest's side ----
// In-game movies. The host's game pauses while one plays; the guest plays the same movie (both start
// it in the same host frame), and stops it when the host's game goes on again (the movie ended or the
// host skipped it), so neither waits long for the other. A guest whose copy ends first, or who skips
// it, sees "Host paused" until the host's ends.
static int g_coopMovieSeen = 0;     // guest: the newest host movie seen (played, or left out on joining)
static int g_coopMovieSynced = 0;   // ...decided for this level which one is current (see below)
static int g_coopMovieOn = 0;       // ...one of them is playing here
static int g_coopMovieHeld = 0;     // ...and the host's game has paused for it since
static void SP_CoopGuestMovie( int seq, const char *name, qboolean hostPlaying ){
    if( !g_spActive ) return;   // (decided once the cgame runs)
    if( !g_coopMovieSynced ){
        // the first frame of a level here (joining, or following the host): the host's newest movie
        // only if it plays there now (it may have started while this guest loaded the level)
        g_coopMovieSynced = 1;
        qboolean current = ( seq > 0 && seq != g_coopMovieSeen && hostPlaying ) ? qtrue : qfalse;
        g_coopMovieSeen = seq;
        if( !current ) return;
    } else {
        if( seq <= g_coopMovieSeen ){
            if( seq < g_coopMovieSeen ) g_coopMovieSeen = seq;   // a new host session counts from 0
            return;
        }
        g_coopMovieSeen = seq;
    }
    if( !name[0] ) return;
    blog("co-op: the host plays the movie %s; playing it here too\n", name);
    char cmd[96]; snprintf( cmd, sizeof(cmd), "inGameCinematic %.63s\n", name );
    Cbuf_AddText( cmd );
    g_coopMovieOn = 1; g_coopMovieHeld = 0;
}
// every guest frame: end the movie here once the host's game has gone on after pausing for it
static void SP_CoopGuestMovieFrame( int paused ){
    if( !g_coopMovieOn ) return;
    if( !CL_InGameCinematicActive() ){
        if( g_coopMovieHeld || !paused ) g_coopMovieOn = 0;   // over here (it ended, or the guest skipped it)
        else return;                                        // (queued, not started yet)
    }
    if( paused == 1 ) g_coopMovieHeld = 1;
    else if( g_coopMovieHeld && !paused ){
        blog("co-op: the host's movie is over; stopping it here\n");
        SCR_StopCinematic();
        g_coopMovieOn = 0;
    }
}
static qboolean SP_CoopSlotSame( int a, int b, int slot, const unsigned char **data, int *len ){
    *len = CoopNet_GuestFrameSlot( b, slot, data );
    if( !a ) return qfalse;
    const unsigned char *od; int ol = CoopNet_GuestFrameSlot( a, slot, &od );
    return ( ol == *len && ( !ol || !memcmp( od, *data, ol ) ) ) ? qtrue : qfalse;
}
// One host frame into this level: the slots that differ from the last frame applied (all of them
// when that one is gone or there was none).
static void SP_CoopApplyFrame( int frame, int time ){
    int prev = g_coopApplied, lid, t, ack;
    if( prev && !CoopNet_GuestFrameInfo( prev, &lid, &t, &ack ) ) prev = 0;
    const unsigned char *d; int len;
    if( !SP_CoopSlotSame( prev, frame, COOP_SLOT_GLOBALS, &d, &len ) ) g_coopRep->ApplyGlobals( d, len );
    int top = ge->num_entities;
    for( int i = 0; i < COOP_SLOT_CS; i++ ){
        if( SP_CoopSlotSame( prev, frame, i, &d, &len ) ) continue;
        if( !len && i >= top && i >= ge->num_entities ) continue;   // free and never used here
        g_coopRep->ApplyEntity( i, d, len );
        gentity_t *e = SP_GEntity( i );
        if( len && e->inuse && e->bmodel && e->s.modelindex > 0 && e->s.modelindex < CM_NumInlineModels() )
            g_entClip[i] = CM_InlineModel( e->s.modelindex );   // traces through this brush model (W_SetBrushModel)
    }
    for( int i = 0; i < CS_MAX && i < COOP_SLOT_GLOBALS - COOP_SLOT_CS; i++ ){
        if( SP_CoopSlotSame( prev, frame, COOP_SLOT_CS + i, &d, &len ) ) continue;
        if( i == CS_SERVERINFO ) continue;   // this copy's own (the map name is the same)
        I_SetCS( i, len ? (const char *)d : "" );   // the cgame hears of it when it runs
    }
    len = CoopNet_GuestFrameSlot( frame, COOP_SLOT_COMMANDS, &d );
    if( len >= 8 ){
        int newest, count, pos = 8, held = 0;
        memcpy( &newest, d, 4 ); memcpy( &count, d + 4, 4 );
        // the commands, then [held][movie seq][movie time][movie name]
        const unsigned char *tail = NULL;
        { int p = 8, i;
          for( i = 0; i < count && p + 5 <= len; i++ ){ int tl = (int)strnlen( (const char *)d + p + 4, len - p - 4 ); if( p + 4 + tl >= len ) break; p += 4 + tl + 1; }
          if( i == count && p + 13 <= len && !d[len - 1] ){ tail = d + p; memcpy( &held, tail, 4 ); } }
        // joining: only what comes from now on; but a level the host holds for this guest is new,
        // so all of its story text so far (an opening scroll text, the first subtitles) is shown
        if( !prev ) g_coopCmdSeen = held ? 0 : newest;
        for( int i = 0; i < count && pos + 5 <= len; i++ ){
            int seq; memcpy( &seq, d + pos, 4 ); pos += 4;
            const char *text = (const char *)d + pos;
            int tl = (int)strnlen( text, len - pos );
            if( pos + tl >= len ) break;
            pos += tl + 1;
            if( seq > g_coopCmdSeen && g_spActive ){ I_SendServerCmd( 0, "%s", text ); g_coopCmdSeen = seq; }   // (kept until the cgame runs)
        }
        if( newest < g_coopCmdSeen ) g_coopCmdSeen = newest;   // (the host's count restarts with a level)
        if( tail ){
            int playing, mseq;
            memcpy( &playing, tail + 4, 4 ); memcpy( &mseq, tail + 8, 4 );
            if( !prev ) g_coopMovieSynced = 0;
            SP_CoopGuestMovie( mseq, (const char *)tail + 12, playing ? qtrue : qfalse );
        }
    }
    g_coopRep->ApplyDone( g_spActive ? qtrue : qfalse );
    g_coopApplied = frame;
    g_coopAppliedAt = Sys_Milliseconds();
    g_levelTime = time;
}
// The guest's commands. One is made for every host frame taken in (the host applies one a frame),
// numbered g_coopSeq and kept here; each frame says the newest the host applied (g_coopAck). The
// cgame's prediction (cg_predict.cpp, on in a replica) starts from that frame's player 2 and replays
// the commands after it, then the one being made now. Their times are their numbers * 50 ms, and the
// snapshot's commandTime is the acked number's (BuildSnapshot), so every replayed command moves
// player 2 for one host frame, as the host's ClientThink will.
static int       g_coopBtn = 0;     // buttons pressed since the last command
static int       g_coopSeqAt = 0;   // when the last command was made (Sys_Milliseconds)
static int       g_coopHostPaused = 0;  // the host's game isn't going on (CoopNet_GuestHostPaused)
static int       g_coopPausedSince = 0; // ...and how far into its frame the guest's command was then
static usercmd_t g_coopCmds[COOP_CMDS];
// What the guest's screen showed, in the host's level time, for the host's check of its shots
// (co-op lag compensation): the frame the cgame's predicted shot was aimed in, if that came after
// the last command went; else the newest drawn (a shot of this command that the cgame predicts once
// it has gone is aimed in that one).
static int       g_coopDrawnTime = 0;   // the cgame time of the last drawn frame
static int       g_coopSentView = 0;    // ...as sent with the last command
static void SP_CoopMakeCmd( void ){
    usercmd_t c = g_lastcmd;
    c.buttons |= g_coopBtn;
    int an[3] = { c.angles[0], c.angles[1], c.angles[2] };
    int shotTime = g_coopRep ? g_coopRep->TakeShotTime() : 0;
    int viewTime = ( shotTime > g_coopSentView || ( shotTime && g_coopSentView - shotTime > 1000 ) ) ? shotTime : g_coopDrawnTime;   // (or a new level)
    g_coopSeqAt = Sys_Milliseconds();
    if( !CoopNet_GuestQueueCmd( g_coopSeq + 1, an, c.buttons, c.weapon, c.forwardmove, c.rightmove, c.upmove, viewTime ) ) return;
    g_coopSeq++;
    g_coopBtn = 0;
    g_coopSentView = viewTime;
    c.serverTime = g_coopSeq * SP_SV_FRAMEMSEC;
    g_coopCmds[g_coopSeq & (COOP_CMDS-1)] = c;
}
static int SP_CoopCurrentCmd( void ){
    if( g_coopSeq - g_coopAck >= COOP_CMDS - 4 ) return g_coopAck;   // too far ahead of the host: no replay
    return g_coopSeq + 1;   // + the one being made
}
static qboolean SP_CoopUserCmd( int n, usercmd_t *cmd ){
    if( n == g_coopSeq + 1 ){
        int since = Sys_Milliseconds() - g_coopSeqAt;
        if( since < 0 ) since = 0; if( since > SP_SV_FRAMEMSEC ) since = SP_SV_FRAMEMSEC;
        if( g_coopHostPaused ) since = g_coopPausedSince;   // held where the pause found it
        *cmd = g_lastcmd;
        cmd->serverTime = g_coopSeq * SP_SV_FRAMEMSEC + since;
        return qtrue;
    }
    if( n <= 0 || n > g_coopSeq || n <= g_coopSeq - COOP_CMDS ) return qfalse;
    *cmd = g_coopCmds[n & (COOP_CMDS-1)];
    return qtrue;
}
// The host's frames that came in since the last call (at most 4, oldest first), each into this level
// and a snapshot, and a command for each. Returns how many.
extern "C" int SP_CoopReplicaApply( void ){
    int newest = CoopNet_GuestNewestFrame(), applied = 0;
    if( !g_coopReplica || !g_coopRep || !newest || newest <= g_coopApplied ) return 0;
    int first = ( g_coopApplied && newest - g_coopApplied <= 4 ) ? g_coopApplied + 1 : newest;
    for( int f = first; f <= newest; f++ ){
        int lid, t, ack;
        if( !CoopNet_GuestFrameInfo( f, &lid, &t, &ack ) || lid != CoopNet_GuestLevelId() ) continue;
        int lastTime = g_coopApplied ? g_levelTime : -1;
        SP_CoopApplyFrame( f, t );
        applied++;
        if( ack > g_coopAck && ack <= g_coopSeq ) g_coopAck = ack;
        if( g_spActive ) CoopNet_GuestLevelReady( lid );
        if( t == lastTime ) continue;   // the host holds its level's start for this guest: no game time passed
        if( g_spActive ) SP_CoopMakeCmd();
        if( g_spActive ){
            g_cmdNum++;
            g_lastcmd.serverTime = g_levelTime;
            BuildSnapshot();
        }
    }
    return applied;
}
// The guest's render frame: no game frames here; the host's frames come in instead, and the cgame draws
// one host frame behind the newest (as the host's own view does), moving on with real time between them.
// While the host's game is paused (or its frames stop coming) the guest's world holds too: player 2's
// input is held where it was (so its prediction stays put), the game's sounds are held as in the host's
// own pause, and a line says why.
static void SP_CoopReplicaDrawFrame( int stereo ){
    int paused = CoopNet_GuestHostPaused();
    if( paused && !g_coopHostPaused ){
        int since = Sys_Milliseconds() - g_coopSeqAt;
        if( since < 0 ) since = 0; if( since > SP_SV_FRAMEMSEC ) since = SP_SV_FRAMEMSEC;
        g_coopPausedSince = since;
        blog("co-op: the host's game %s\n", paused == 1 ? "paused" : "stopped sending frames");
    } else if( !paused && g_coopHostPaused ){
        blog("co-op: the host's game goes on\n");
    }
    g_coopHostPaused = paused;
    SP_CoopGuestMovieFrame( paused );
    if( !paused ){
      int an[3]; signed char fwd=0,side=0,up=0; int btn=0;
      SPR_GetLatestCmd(an,&fwd,&side,&up,&btn);
      g_lastcmd.angles[0]=an[0]; g_lastcmd.angles[1]=an[1]; g_lastcmd.angles[2]=an[2];
      g_lastcmd.forwardmove=fwd; g_lastcmd.rightmove=side; g_lastcmd.upmove=up; g_lastcmd.buttons=btn;
      g_lastcmd.weapon = (byte)g_weaponSelect; g_coopBtn |= btn; }
    SP_CoopReplicaApply();
    int since = Sys_Milliseconds() - g_coopAppliedAt;
    if( since < 0 ) since = 0; if( since > SP_SV_FRAMEMSEC - 1 ) since = SP_SV_FRAMEMSEC - 1;
    int cgTime = g_levelTime - SP_SV_FRAMEMSEC + since;
    if( cgTime < g_snapBaseTime ) cgTime = g_snapBaseTime;
    // the host skips a cutscene: the guest's view is the skip screen too, and the game's sounds stop
    int skipping = Cvar_VariableIntegerValue( "skippingCinematic" );
    if( skipping && !s_spWasSkipping ){ S_StopGameSounds(); SP_ClearVoiceTimers(); blog("co-op: the host skips the cutscene\n"); }
    s_spWasSkipping = skipping;
    S_PauseGameSounds( paused ? qtrue : qfalse );   // (nothing to do when they aren't held)
    s_spSoundsBlocked = ( paused || skipping ) ? 1 : 0; s_spSkipDraw = skipping;
    SP_UpdateVisemes();
    SP_VmMain(CG_DRAW_ACTIVE_FRAME, cgTime, stereo, qfalse);
    g_coopDrawnTime = cgTime;
    s_spSkipDraw = 0;
    if( skipping ){
        SP_DrawSkipProgress();
        S_ClearLoopingSounds( qtrue );
    }
    if( paused || skipping ){
        if( paused ) S_ClearLoopingSounds( qtrue );   // the frame above re-added the loops; silent, like the host's pause
        const char *text = paused == 1 ? "Host paused" : paused ? "Waiting for the host" : "Host is skipping the cutscene";
        if( !CL_InGameCinematicActive() )
            SCR_DrawBigString( 320 - (int)strlen( text ) * 8, 232, text, 1.0f, qtrue );   // 16-pixel characters, centred on 640x480
    }
}
// The host's render frame while a new level (a New Game, a map change, a load) waits for the guest to
// load it too, so both start it together: no game frames (the level's time stands still, as in a pause),
// the frozen frame drawn with a line saying so, and a frame for the guest now and then (the first one
// tells it which map to load). Over when the guest has the level up, leaves, or takes too long.
static int SP_CoopHolding( int stereo ){
    if( !g_coopHoldLevel ) return 0;
    int now = Sys_Milliseconds();
    if( !g_coopHoldAt ) g_coopHoldAt = now;
    const char *why = NULL;
    if( g_coopHoldLevel != g_coopLevelId || !CoopNet_HostHasGuest() ) why = "the guest left";
    else if( CoopNet_HostGuestLevel() == g_coopHoldLevel ) why = "player 2 has it";
    else if( now - g_coopHoldAt > COOP_HOLD_MAX ) why = "player 2 is taking too long; going on without waiting";
    if( why ){
        blog("co-op: level start held %d ms for player 2 (%s)\n", now - g_coopHoldAt, why);
        if( now - g_coopHoldAt > COOP_HOLD_MAX ) SP_ComPrintf()("co-op: %s\n", why);
        g_coopHoldLevel = 0;
        SP_CoopHostFrame();   // (the guest's next frame says the level isn't held any more)
        return 0;
    }
    if( !g_coopHoldSent || now - g_coopHoldSent >= 250 ){ g_coopHoldSent = now; SP_CoopHostFrame(); }
    S_PauseGameSounds( qtrue );
    s_spSoundsBlocked = 1;
    int cgTime = g_levelTime - SP_SV_FRAMEMSEC + g_simResidual;
    if( cgTime >= g_levelTime )    cgTime = g_levelTime - 1;
    if( cgTime < g_snapBaseTime )  cgTime = g_snapBaseTime;
    SP_VmMain(CG_DRAW_ACTIVE_FRAME, cgTime, stereo, qfalse);
    S_ClearLoopingSounds( qtrue );
    const char *text = "Waiting for player 2";
    SCR_DrawBigString( 320 - (int)strlen( text ) * 8, 232, text, 1.0f, qtrue );
    return 1;
}
#endif

// Advance the sim by REAL elapsed wall-clock time each rendered frame. This keeps the original's
// smooth per-frame structure (RunFrame every frame -> smooth view) while running at CORRECT speed
// (the prior code added a fixed 50ms/frame -> 3x at 60fps; a 20Hz accumulator was correct-speed but
// choppy/laggy). The delta is clamped so a hitch/pause can't teleport the simulation.
extern "C" void SP_DrawFrame(int serverTime, int stereo){
    (void)serverTime;
    static int fc=0; if(fc<3){ blog("SP_DrawFrame #%d stereo=%d levelTime=%d numEnt=%d\n", fc, stereo, g_levelTime, g_snap.numEntities); } fc++;

    static int lastReal = 0;
    int now = Sys_Milliseconds();
    if(lastReal == 0) lastReal = now;
    int delta = now - lastReal; lastReal = now;
    if(delta < 1) delta = 1; if(delta > 100) delta = 100;   // clamp hitches (no >100ms sim jumps)
    if( Regress_Active() ) delta = SP_SV_FRAMEMSEC;          // regression harness: one game frame per drawn frame
#ifdef EF_COOP
    if( g_coopReplica ){ SP_CoopReplicaDrawFrame( stereo ); return; }
    if( SP_CoopHolding( stereo ) ) return;
#endif

    // PAUSE when the in-game menu (ESC), the console or an in-game movie is up: retail SP pauses the
    // server; our loopback has no server, so freeze the sim here -- do NOT advance g_levelTime or run
    // ClientThink/RunFrame; just re-draw the frozen frame behind the menu. (lastReal is already updated
    // above, so unpausing won't time-jump.)
    { extern int SP_MenuPaused(void);
      if( SP_MenuPaused() ){
        // Hold the game's sounds and the music where they are (S_PauseGameSounds): they used to be
        // stopped with S_StopAllSounds on this edge, which lost the music and the current voice line for
        // good (and left its lip-sync / dialog timer running). Retail didn't stop sounds for the in-game
        // menu either (the UI's "stopsound" there is commented out, ui_menu.cpp). Menu clicks still play,
        // and the voice timers count level time, which is frozen with the voice. New game sounds are
        // dropped while paused (s_spSoundsBlocked).
        S_PauseGameSounds( qtrue );
        s_spSoundsBlocked = 1;
        if( !s_spWasPaused ){ s_spWasPaused = 1;
            // Grab the save-menu thumbnail ONCE here, on the gameplay->menu edge (not every gameplay
            // second). The capture is a full-screen GPU readback that stalls the Adreno pipeline ~100ms;
            // doing it per-second during play was the dominant stutter. You only save from this menu, so a
            // single capture on menu-open is enough and its one-time hitch is hidden by the transition.
            // EF1 SP: capture a CLEAN game frame for the save thumbnail. The capture reads the framebuffer at
            // end-of-frame (after the menu would draw), so arm a one-frame menu suppression: the frozen game is
            // drawn (CG_DRAW_ACTIVE_FRAME below) but the pause menu is skipped for this frame -> the readback is
            // just the game, not the menu over it. Was capturing menu+game; the saved 'SHOT' showed the menu.
            SPR_RequestSaveThumb(); SP_ArmMenuSuppress();
            blog("EFSP: paused: game sounds held, save-thumbnail capture armed\n"); }
        // Freeze the sim but redraw at the SAME trailing cg.time the active path uses below (g_levelTime and
        // g_simResidual are frozen while paused). Passing g_levelTime here instead would jump cg.time FORWARD
        // to the newest snap during the pause, then BACKWARD on unpause -> CG_ProcessSnapshots ERR_DROP
        // ("cg.snap->serverTime > cg.time") -> bounce to menu (the save-then-exit-menu crash).
        int cgTime = g_levelTime - SP_SV_FRAMEMSEC + g_simResidual;
        if( cgTime >= g_levelTime )    cgTime = g_levelTime - 1;
        if( cgTime < g_snapBaseTime )  cgTime = g_snapBaseTime;
        s_spSkipDraw = Cvar_VariableIntegerValue("skippingCinematic");   // paused mid-skip: keep the skip screen
        SP_VmMain(CG_DRAW_ACTIVE_FRAME, cgTime, stereo, qfalse);
        s_spSkipDraw = 0;
        if( Cvar_VariableIntegerValue("skippingCinematic") ) SP_DrawSkipProgress();
        // The paused CG_DRAW_ACTIVE_FRAME above would re-add every entity loopSound, and CL_Frame's
        // per-frame S_Update would mix them -> machinery/ambient loops hum through the pause. The adds are
        // dropped while paused (s_spSoundsBlocked); clear the list too so loops added before the pause stop.
        // The cgame re-adds them on its first frame after the pause. (Diverges from retail, which leaves
        // loops faintly audible; intentional -- a silent pause.)
        S_ClearLoopingSounds(qtrue);
        return;
      }
      if( s_spWasPaused ){ s_spWasPaused = 0; blog("EFSP: unpaused: game sounds resume\n"); }
      S_PauseGameSounds( qfalse );   // resume held sounds where they were (no-op when not held)
    }

    // CUTSCENE SKIP (retail): Use Item in a camera scene sets skippingCinematic 1 + timescale 100 (the
    // game, g_active.cpp); the game ends it when the camera ends, an in-game movie ends it (cl_cin.c),
    // and a load / quit ends it (SP_UnloadGame, CL_Disconnect). While skipping, timescale drives the sim
    // (below), the 3D view isn't drawn (a progress screen instead), and the game's sounds stop -- like
    // retail, which killed every sound but the music when a skip started.
    int skipping = Cvar_VariableIntegerValue("skippingCinematic");
    if( skipping && !s_spWasSkipping ){
        S_StopGameSounds();      // effects, voices, loops; the music plays on
        SP_ClearVoiceTimers();
        blog("EFSP: cutscene skip started (timescale %g)\n", Cvar_VariableValue("timescale"));
    } else if( !skipping && s_spWasSkipping ){
        blog("EFSP: cutscene skip ended at levelTime %d\n", g_levelTime);
    }
    s_spWasSkipping = skipping;
    s_spSoundsBlocked = skipping;
    g_spMovieQueued = 0;

    // (Save-menu thumbnail capture is armed on the gameplay->menu edge in the pause block above, not here
    // every second — the per-second full-screen GPU readback was the dominant stutter. SP_SaveGame embeds
    // the latest capture in the .sav 'SHOT' chunk.)

    // Refresh the usercmd from the latest touch/pad input ONCE per render frame (reused by every 50ms tick
    // drained below). serverTime is stamped per-tick inside the loop so pml.msec == 50.
    { int an[3]; signed char fwd=0,side=0,up=0; int btn=0;
      SPR_GetLatestCmd(an,&fwd,&side,&up,&btn);
      g_lastcmd.angles[0]=an[0]; g_lastcmd.angles[1]=an[1]; g_lastcmd.angles[2]=an[2];
      g_lastcmd.forwardmove=fwd; g_lastcmd.rightmove=side; g_lastcmd.upmove=up; g_lastcmd.buttons=btn;
      // Propagate the cgame's selected weapon into the usercmd. The cgame reports its weapon via
      // CG_SETUSERCMDVALUE (-> g_weaponSelect); pmove (ge->ClientThink) switches ps.weapon to cmd.weapon
      // when the player owns it. Without this, cmd.weapon stays 0 and ps.weapon NEVER changes from WP_NONE
      // -> no view weapon EVER, in any mission. This is the general EF weapon-switch path: scripted
      // SET_WEAPON, pickups, weapnext/weapprev all set cg.weaponSelect -> here -> pmove. (Multi-mission.)
      g_lastcmd.weapon = (byte)g_weaponSelect; }

    // FIXED 50ms/20Hz SIM CADENCE — mirror retail SV_Frame:
    // accumulate real elapsed ms, then drain it in fixed SP_SV_FRAMEMSEC quanta, advancing g_levelTime by
    // exactly 50 each tick and emitting ONE snapshot per tick on the 50ms grid.
    // While skipping a cutscene, timescale (100 from the game) scales the real delta, up to
    // SP_SKIP_MAX_TICKS ticks and SP_SKIP_BUDGET_MS of real time per frame; what doesn't fit carries to
    // the next frames, so a frame never hangs. The fast-forward stops at once when the skip ends (camera
    // over) or the game queues an in-game movie, so the scene after it starts at normal speed.
    int maxTicks = SP_MAX_CATCHUP;
    if( skipping ){
        float ts = Cvar_VariableValue("timescale");
        if( ts < 1.0f ) ts = 1.0f; if( ts > 100.0f ) ts = 100.0f;
        delta = (int)( delta * ts );
        maxTicks = SP_SKIP_MAX_TICKS;
    }
    g_simResidual += delta;
    if( g_simResidual > SP_SV_FRAMEMSEC * maxTicks )   // hitch cap: bound catch-up, no spiral
        g_simResidual = SP_SV_FRAMEMSEC * maxTicks;
    int simStart = Sys_Milliseconds();
#ifdef EF_COOP
    int coopSkipFrames = 0;
#endif
    while( g_simResidual >= SP_SV_FRAMEMSEC ) {
        g_simResidual -= SP_SV_FRAMEMSEC;
        g_levelTime   += SP_SV_FRAMEMSEC;
        g_cmdNum++;
        g_lastcmd.serverTime = g_levelTime;
        Regress_SeedRand( g_levelTime );
        ge->ClientThink(0, &g_lastcmd);
        // The game ends a skip in ClientThink (the camera is over: skippingCinematic 0, timescale 1), and the
        // RunFrame right after it is already normal play: the script's next voice line starts there. Blocking
        // sounds for the whole frame (from `skipping` above) dropped that line, and with no voice timer the
        // dialog wait finished at once, so the line was lost for good.
        if( skipping ) s_spSoundsBlocked = Cvar_VariableIntegerValue("skippingCinematic") ? 1 : 0;
        SP_GameRunFrame();
        BuildSnapshot();
#ifdef EF_COOP
        // (while skipping, many game frames run per drawn frame: the guest gets the last of them, below)
        if( !skipping ) SP_CoopHostFrame();
        else coopSkipFrames++;
#endif
        if( skipping ){
            if( !Cvar_VariableIntegerValue("skippingCinematic") || g_spMovieQueued ){
                g_simResidual %= SP_SV_FRAMEMSEC;   // skip over: back to real time from here
                break;
            }
            if( Sys_Milliseconds() - simStart >= SP_SKIP_BUDGET_MS ) break;
        }
    }

#ifdef EF_COOP
    if( coopSkipFrames ) SP_CoopHostFrame();
#endif
    // Deferred autosave: an autosave requested during a cutscene (in_camera) was held back by SP_SaveGame so it
    // wouldn't capture mid-cinematic state. Write it now that the cutscene has ended, so the death-respawn
    // checkpoint is a clean post-cinematic state instead of one that reloads with the player free-walking the
    // scene. Runs only on active (unpaused) gameplay frames; GameAllowedToSaveHere() == !in_camera.
    if( g_pendingAutosave && ge && g_spActive && (!ge->GameAllowedToSaveHere || ge->GameAllowedToSaveHere()) ){
        blog("save: writing DEFERRED autosave now that the cutscene has ended\n");
        SP_SaveGame("auto");   // clears g_pendingAutosave on the allowed-save path
    }

    // ONE cgame draw per RENDER frame (decoupled from the sim). cg.time advances SMOOTHLY per render frame by
    // adding the real-time residual (the sub-tick fraction toward the next 50ms snap) to the trailing base.
    // So cg.time sweeps (g_levelTime-50 .. g_levelTime) as residual goes 0..49 -> cg.snap (N) and cg.nextSnap
    // (N+1) bracket it and frameInterpolation = residual/50 sweeps (0,1) -> SMOOTH motion. Without the residual
    // cg.time was pinned to the 50ms grid -> frameInterp=0 -> 20Hz stutter. Upper-clamp keeps the newest snap
    // strictly ahead (valid nextSnap); lower-clamp to snap #1 so the first frames never assert.
    int cgTime = g_levelTime - SP_SV_FRAMEMSEC + g_simResidual;
    if( cgTime >= g_levelTime )    cgTime = g_levelTime - 1;
    if( cgTime < g_snapBaseTime )  cgTime = g_snapBaseTime;
    // Still skipping after this frame's ticks: run the cgame (server commands, camera) without drawing.
    int skipNow = Cvar_VariableIntegerValue("skippingCinematic") && !g_spMovieQueued;
    s_spSoundsBlocked = skipNow;
    s_spSkipDraw = skipNow;
    SP_UpdateVisemes();   // mouths follow the voice per render frame, not per sim tick
    SP_VmMain(CG_DRAW_ACTIVE_FRAME, cgTime, stereo, qfalse);
    s_spSkipDraw = 0;
    if( skipNow ){
        SP_DrawSkipProgress();
        S_ClearLoopingSounds(qtrue);
    }
}
