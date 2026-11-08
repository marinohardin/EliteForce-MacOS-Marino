/*
===========================================================================
Windows / Linux: the platform hooks (sys_platform.h) without a launcher.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).

Milestone 1 of the Windows/Linux builds: the app's own homepath
(EF_HOMEPATH_NAME: %APPDATA%\EliteForce-Marino, ~/.local/share/
eliteforce-marino), the game data found by sys_data.c, the mode from the
command line (+set com_holomatch 1 for Holomatch). The native launchers
(Win32, Qt) will replace Sys_PlatformLauncher and Sys_PlatformQuitToLauncher.
===========================================================================
*/

#if !defined( __APPLE__ ) && !defined( __ANDROID__ ) && !defined( __EMSCRIPTEN__ ) && !defined( DEDICATED )

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "sys_local.h"
#include "sys_platform.h"

void Sys_PlatformPreInit( int argc, char **argv, char *commandLine, int size )
{
	int i;

	// the app's own homepath, unless the command line names one
	for ( i = 1; i < argc; i++ )
		if ( !Q_stricmp( argv[i], "fs_homepath" ) || !Q_stricmp( argv[i], "com_homepath" ) )
			return;
	Q_strcat( commandLine, size, "+set com_homepath \"" EF_HOMEPATH_NAME "\" " );
}

qboolean Sys_PlatformLauncher( int argc, char **argv, char *commandLine, int size )
{
	return qfalse;	// no launcher yet
}

const char *Sys_PlatformResolveGameData( int argc, char **argv, qboolean headless )
{
	return Sys_DataResolve( argc, argv, headless );
}

void Sys_PlatformPostInit( void )
{
	Sys_DataPrintEarlyLog( );
}

void Sys_PlatformQuitSignals( void )
{
}

void Sys_PlatformFrame( void )
{
}

void Sys_PlatformQuitToLauncher( void )
{
}

#endif
