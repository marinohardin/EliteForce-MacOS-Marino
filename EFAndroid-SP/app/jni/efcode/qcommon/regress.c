/*
===========================================================================
regress.c -- the regression harness's engine side (macos/regress.sh runs it).

OFF unless the regress_log cvar is set on the command line; with it empty (the default) every
Regress_* call returns at once and nothing behaves differently.

When on, the engine runs a fixed, repeatable simulation and writes a per-frame fingerprint of
the game state to the regress_log file (an OS path), so two builds can be compared frame by frame:
  - fixed time step: every engine frame advances exactly one 50 ms game frame (Com_ModifyMsec,
    and SP_DrawFrame in single player), and frames don't wait for the wall clock (Com_Frame);
  - fixed seeds: the game's InitGame seed is regress_seed, and libc's rand() (used by the SP game,
    the botlib and the engine) is reseeded from regress_seed + the game time before every game
    frame, so whatever else draws random numbers between frames can't shift them;
  - fixed input: the local player's usercmd is a scripted pattern of the frame number
    (Regress_Usercmd), not the keyboard/mouse;
  - the fingerprint: after each game frame, a "f <frame> <map> <time>" line, then a line per
    entity / player state / level whose fields changed since the previous frame ("e", "p", "l"),
    or "x <kind> <num>" when it went away. "end <frames> <hash>" closes the log.

Cvars (all set with +set on the command line):
  regress_log <path>      the log to write (on when non-empty)
  regress_frames <n>      quit after this many game frames (default 600 = 30 s of game time)
  regress_seed <n>        the seed (default 1)
  regress_cmdframe <n>    at this frame, run regress_cmd (a console command, e.g. a map transition)
  regress_cmd <text>
===========================================================================
*/

#include "q_shared.h"
#include "qcommon.h"

#define REGRESS_SLOTS	1024	// MAX_GENTITIES in both games
#define REGRESS_KINDS	3		// 0 entity, 1 player state, 2 level

static cvar_t	*regress_log, *regress_frames, *regress_seed, *regress_cmdframe, *regress_cmd;
static FILE		*regressFile;
static int		regressFrame;		// game frames logged so far
static qboolean	regressDone;
static unsigned long long regressHash = 1469598103934665603ULL;	// FNV-1a over everything written
static unsigned	lastHash[REGRESS_KINDS][REGRESS_SLOTS];		// 0 = not present
static int		seenFrame[REGRESS_KINDS][REGRESS_SLOTS];

static const char regressKindChar[REGRESS_KINDS] = { 'e', 'p', 'l' };

static unsigned Regress_Fnv( const char *s ) {
	unsigned h = 2166136261u;
	while ( *s ) { h ^= (unsigned char)*s++; h *= 16777619u; }
	return h ? h : 1;
}

static void Regress_Write( const char *line ) {
	const char *s;
	for ( s = line; *s; s++ ) { regressHash ^= (unsigned char)*s; regressHash *= 1099511628211ULL; }
	fputs( line, regressFile );
}

void Regress_Init( void ) {
	regress_log = Cvar_Get( "regress_log", "", CVAR_INIT | CVAR_PROTECTED );
	regress_frames = Cvar_Get( "regress_frames", "600", CVAR_INIT );
	regress_seed = Cvar_Get( "regress_seed", "1", CVAR_INIT );
	regress_cmdframe = Cvar_Get( "regress_cmdframe", "0", CVAR_INIT );
	regress_cmd = Cvar_Get( "regress_cmd", "", CVAR_INIT );
	if ( !regress_log->string[0] ) {
		return;
	}
	regressFile = fopen( regress_log->string, "w" );
	if ( !regressFile ) {
		Com_Printf( S_COLOR_RED "regress: can't write %s, the harness stays off\n", regress_log->string );
		return;
	}
	Com_Printf( "regress: ON, logging %d frames to %s (seed %d)\n", regress_frames->integer,
		regress_log->string, regress_seed->integer );
	fprintf( regressFile, "# regress seed=%d frames=%d\n", regress_seed->integer, regress_frames->integer );
}

qboolean Regress_Active( void ) {
	return regressFile && !regressDone;
}

int Regress_Seed( void ) {
	return regress_seed ? regress_seed->integer : 1;
}

void Regress_SeedRand( int gameTime ) {
	if ( Regress_Active() ) {
		srand( (unsigned)( regress_seed->integer * 7919 + gameTime ) );
	}
}

int Regress_FrameNum( void ) {
	return regressFrame;
}

void Regress_BeginFrame( const char *map, int gameTime ) {
	if ( !Regress_Active() ) return;
	Regress_Write( va( "f %d %s %d\n", regressFrame, map && map[0] ? map : "-", gameTime ) );
}

// one entity's / player state's fields this frame (logged only when they changed)
void Regress_State( int kind, int num, const char *fields ) {
	unsigned h;
	if ( !Regress_Active() || kind < 0 || kind >= REGRESS_KINDS || num < 0 || num >= REGRESS_SLOTS ) return;
	seenFrame[kind][num] = regressFrame + 1;
	h = Regress_Fnv( fields );
	if ( h == lastHash[kind][num] ) return;
	lastHash[kind][num] = h;
	Regress_Write( va( "%c %d %s\n", regressKindChar[kind], num, fields ) );
}

void Regress_EndFrame( void ) {
	int k, i;
	if ( !Regress_Active() ) return;
	for ( k = 0; k < REGRESS_KINDS; k++ ) {
		for ( i = 0; i < REGRESS_SLOTS; i++ ) {
			if ( lastHash[k][i] && seenFrame[k][i] != regressFrame + 1 ) {
				lastHash[k][i] = 0;
				Regress_Write( va( "x %c %d\n", regressKindChar[k], i ) );
			}
		}
	}
	regressFrame++;
	if ( regress_cmd->string[0] && regressFrame == regress_cmdframe->integer ) {
		Com_Printf( "regress: frame %d: %s\n", regressFrame, regress_cmd->string );
		Cbuf_AddText( va( "%s\n", regress_cmd->string ) );
	}
	if ( regressFrame >= regress_frames->integer ) {
		fprintf( regressFile, "end %d %016llx\n", regressFrame, regressHash );
		fclose( regressFile );
		regressFile = NULL;
		regressDone = qtrue;
		Com_Printf( "regress: done, %d frames\n", regressFrame );
		Cbuf_AddText( "quit\n" );
	} else if ( ( regressFrame & 63 ) == 0 ) {
		fflush( regressFile );
	}
}

// The local player's input: a fixed pattern of the frame number (walk, strafe, turn, jump, crouch,
// fire), so the same frames always get the same usercmd. Angles are absolute shorts.
void Regress_Usercmd( int angles[3], signed char *forward, signed char *right, signed char *up, int *buttons ) {
	int f = regressFrame;
	int phase = ( f / 40 ) % 6;
	angles[0] = 0;
	angles[1] = ( f * 91 ) & 0xffff;			// ~0.5 degree a frame
	angles[2] = 0;
	*forward = ( phase == 0 || phase == 1 || phase == 3 ) ? 127 : ( phase == 4 ? -127 : 0 );
	*right = phase == 1 ? 127 : ( phase == 2 ? -127 : 0 );
	*up = ( f % 50 ) == 10 ? 127 : ( phase == 5 ? -127 : 0 );	// a jump now and then; crouch in phase 5
	*buttons = ( f % 60 ) >= 50 ? 1 : 0;		// BUTTON_ATTACK, half a second of every 3 s
}
