/*
===========================================================================
Windows / Linux: where the game data is.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).

The download ships no game data. At startup, unless the command line sets the
data paths itself (fs_basepath, fs_homepath, com_homepath, ...), this looks
without any UI for <BaseEF>/pak0.pk3 (both names in any case on Linux) in the
same order as the macOS app (sys_osx_data.m):
  a) the game folder remembered in <homepath>/launcher.cfg, a line
         gamefolder "<folder holding BaseEF>"
     (written by the launchers' folder chooser);
  b) <homepath>/baseEF (files copied there; fs_homepath is searched anyway);
  c) a baseEF folder next to the executable (for an AppImage: next to the
     .AppImage file, its own directory being a read-only mount);
  d) well-known installs (GOG, Heroic, Wine prefixes, ...).
The folder found becomes fs_basepath (b: the default is kept). If there is
none, a message box says where to put pak0.pk3 and the game quits (a
dedicated server says it on stderr).

The lookup itself is the launcher core's (efcode/launcher/lc_data.c,
lc_data_find): the launchers and the engine always agree. This file keeps the
engine side: the early log, the not-found message and the Sys_Data* API.

Runs before Com_Init: no Z_Malloc and no Com_Printf here; messages go to
stderr and are replayed into the console by Sys_DataPrintEarlyLog.
===========================================================================
*/

#if !defined( __APPLE__ ) && !defined( __ANDROID__ ) && !defined( __EMSCRIPTEN__ )

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "sys_local.h"
#include "sys_platform.h"
#include "../launcher/lc_data.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#ifndef DEDICATED
#	ifdef USE_LOCAL_HEADERS
#		include "SDL.h"
#	else
#		include <SDL.h>
#	endif
#endif

#define DATA_PATH		1024
#define LAUNCHER_CFG	"launcher.cfg"

// everything logged before the console is up, replayed by Sys_DataPrintEarlyLog
static char *earlyLog;
static size_t earlyLen, earlyCap;
static qboolean logLive;			// the console is up: log there as well

static void AppendEarly( const char *line )
{
	size_t n = strlen( line ) + 1;

	if ( earlyLen + n + 1 > earlyCap ) {
		size_t ncap = earlyCap ? earlyCap * 2 : 8192;
		char *grown;

		while ( ncap < earlyLen + n + 1 )
			ncap *= 2;
		if ( !( grown = realloc( earlyLog, ncap ) ) )
			return;
		earlyLog = grown;
		earlyCap = ncap;
	}
	memcpy( earlyLog + earlyLen, line, n - 1 );
	earlyLog[earlyLen + n - 1] = '\n';
	earlyLen += n;
	earlyLog[earlyLen] = '\0';
}

static lc_mutex *logLock;			// the launcher's worker threads log too

static void DataLogLine( const char *line, void *user )
{
	if ( logLock )
		lc_mutex_lock( logLock );
	fprintf( stderr, "%s\n", line );
	if ( logLive )
		Com_Printf( "%s\n", line );
	else
		AppendEarly( line );
	if ( logLock )
		lc_mutex_unlock( logLock );
}

// the launcher core logs through lc_log: into the early log, then the console
void Sys_DataInstallLog( void )
{
	if ( !logLock )
		logLock = lc_mutex_new( );
	lc_set_log_sink( DataLogLine, NULL );
}

void Sys_DataPrintEarlyLog( void )
{
	if ( logLock )
		lc_mutex_lock( logLock );
	if ( earlyLog && earlyLen )
		Com_Printf( "%s", earlyLog );
	earlyLen = 0;
	if ( earlyLog )
		earlyLog[0] = '\0';
	logLive = qtrue;
	if ( logLock )
		lc_mutex_unlock( logLock );
}

static qboolean TestEnv( const char *name )
{
	const char *v = getenv( name );
	return v && *v && strcmp( v, "0" ) ? qtrue : qfalse;
}

/*
=================
Sys_DataHomePath

The homepath Sys_DefaultHomePath will use once com_homepath is EF_HOMEPATH_NAME
(Sys_PlatformPreInit), known before Com_Init.
=================
*/
const char *Sys_DataHomePath( void )
{
	return lc_homepath( );
}

/*
=================
Sys_DataOverridden

qtrue when the command line sets the data paths itself (no lookup).
=================
*/
qboolean Sys_DataOverridden( int argc, char **argv )
{
	return lc_data_overridden( argc, argv ) ? qtrue : qfalse;
}

static void NotFound( qboolean headless )
{
	char home[DATA_PATH], cfg[DATA_PATH], msg[DATA_PATH * 4];

	Com_sprintf( home, sizeof( home ), "%s%c%s", Sys_DataHomePath( ), PATH_SEP, BASEGAME );
	Com_sprintf( cfg, sizeof( cfg ), "%s%c%s", Sys_DataHomePath( ), PATH_SEP, LAUNCHER_CFG );
	Com_sprintf( msg, sizeof( msg ),
		"The Elite Force game files were not found.\n\n"
		"Copy the .pk3 files (at least pak0.pk3) from the BaseEF folder of an Elite Force "
		"installation (GOG, or the CD with the 1.2 patch) into:\n\n%s\n\n"
		"or put that BaseEF folder next to the game%s, or add a line\n"
		"gamefolder \"<the folder holding BaseEF>\"\nto %s.\n\n"
		"A dedicated server or a shortcut can also pass +set fs_basepath <folder holding BaseEF>.",
		home,
#ifdef _WIN32
		" (next to EliteForce.exe)",
#else
		getenv( "APPIMAGE" ) ? " (next to the .AppImage file)" : "",
#endif
		cfg );

	fprintf( stderr, "ERROR: %s\n", msg );
#ifndef DEDICATED
	if ( !headless && !TestEnv( "VOYAGERSP_TEST_NODIALOG" ) )
		SDL_ShowSimpleMessageBox( SDL_MESSAGEBOX_ERROR, "Elite Force: game files not found", msg, NULL );
#endif
	exit( 1 );
}

/*
=================
Sys_DataResolve

Returns the fs_basepath to pass, or NULL for the default (homepath copy, or
the command line sets the paths). Exits when there is no game data.
=================
*/
const char *Sys_DataResolve( int argc, char **argv, qboolean headless )
{
	static char result[DATA_PATH];
	lc_data d;

	Sys_DataInstallLog( );
	if ( Sys_DataOverridden( argc, argv ) )
		return NULL;
	if ( !*Sys_DataHomePath( ) ) {
		lc_log( "Game data: no home directory, using the defaults" );
		return NULL;
	}
	if ( lc_data_find( argc, argv, &d ) ) {
		if ( !d.basepath[0] )
			return NULL;
		Q_strncpyz( result, d.basepath, sizeof( result ) );
		return result;
	}
	NotFound( headless );
	return NULL;
}

#endif
