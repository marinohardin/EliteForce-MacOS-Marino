/*
===========================================================================
Windows: the in-process Win32 launcher (efcode/sys/win32/, built when the
CMake option EF_WIN_LAUNCHER is ON).

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#ifndef WIN_LAUNCHER_H
#define WIN_LAUNCHER_H

#include "../../qcommon/q_shared.h"

// Shows the launcher (or, for --host-server, only the server window and then
// exits), and appends the chosen mode and settings to commandLine
// (lc_launch_append_args). qfalse when it didn't run or the user's choice leaves
// the data to the caller. Called by Sys_PlatformLauncher before Com_Init, only
// when the command line doesn't already name the mode (com_holomatch) and
// EF_NO_LAUNCHER isn't set; the caller then marks the game as started from the
// launcher (lc_set_from_launcher) so a bare "quit" returns here.
qboolean Sys_WinLauncher( int argc, char **argv, char *commandLine, int size );

#endif
