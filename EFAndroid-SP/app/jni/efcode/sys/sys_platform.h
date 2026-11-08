/*
===========================================================================
Platform hooks around the engine's startup and main loop (sys_main.c).

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).

main() calls these at fixed points; each desktop platform implements them:
  macOS          sys_osx.m: thin wrappers around the Sys_Mac* functions
                 (bundle environment, homepath migration, Cocoa launcher,
                 game-data chooser, File menu)
  Windows/Linux  sys_platform_stub.c: own homepath name, game-data lookup
                 (sys_data.c), no launcher yet
Android and Emscripten have no implementation: the inline no-ops below.
===========================================================================
*/

#ifndef SYS_PLATFORM_H
#define SYS_PLATFORM_H

#include "../qcommon/q_shared.h"

// The homepath folder (com_homepath) of this app's saves and config:
// ~/Library/Application Support/<name>, %APPDATA%\<name>,
// $XDG_DATA_HOME/<name, lower case> (~/.local/share/...).
#ifndef EF_HOMEPATH_NAME
#	if defined( MACOS_HOMEPATH_NAME )
#		define EF_HOMEPATH_NAME MACOS_HOMEPATH_NAME
#	elif defined( __APPLE__ )
#		define EF_HOMEPATH_NAME "EliteForce-MacOS"
#	else
#		define EF_HOMEPATH_NAME "EliteForce-Marino"
#	endif
#endif

#if defined( EF_DESKTOP ) && !defined( DEDICATED )

// Before the command line is joined: set up the process environment and
// append any "+set" the platform needs first (com_homepath) to commandLine.
void Sys_PlatformPreInit( int argc, char **argv, char *commandLine, int size );

// The launcher, when the platform has one: shows it and appends the chosen
// mode/settings to commandLine. qfalse when it didn't run (no launcher, or the
// command line already names the mode), so the caller resolves the data itself.
qboolean Sys_PlatformLauncher( int argc, char **argv, char *commandLine, int size );

// Finds the game data (headless: without any UI, exits with an error when
// there is none). Returns the fs_basepath to pass, or NULL for the default
// or when the command line sets the data paths itself.
const char *Sys_PlatformResolveGameData( int argc, char **argv, qboolean headless );

// After Com_Init: log what happened before the console was up, add commands.
void Sys_PlatformPostInit( void );

// After the default signal handlers are installed: replace them if needed.
void Sys_PlatformQuitSignals( void );

// Every frame of the main loop (not when headless).
void Sys_PlatformFrame( void );

// The game's own Quit (Com_Quit_f): come back at the launcher, if any.
void Sys_PlatformQuitToLauncher( void );

#else

static ID_INLINE void Sys_PlatformPreInit( int argc, char **argv, char *commandLine, int size ) { }
static ID_INLINE qboolean Sys_PlatformLauncher( int argc, char **argv, char *commandLine, int size ) { return qfalse; }
static ID_INLINE const char *Sys_PlatformResolveGameData( int argc, char **argv, qboolean headless ) { return NULL; }
static ID_INLINE void Sys_PlatformPostInit( void ) { }
static ID_INLINE void Sys_PlatformQuitSignals( void ) { }
static ID_INLINE void Sys_PlatformFrame( void ) { }
static ID_INLINE void Sys_PlatformQuitToLauncher( void ) { }

#endif

#if !defined( __APPLE__ ) && !defined( __ANDROID__ ) && !defined( __EMSCRIPTEN__ )
// sys_data.c (Windows/Linux): game-data lookup, see there
const char *Sys_DataResolve( int argc, char **argv, qboolean headless );
const char *Sys_DataHomePath( void );
void Sys_DataPrintEarlyLog( void );
qboolean Sys_DataOverridden( int argc, char **argv );
#endif

#endif
