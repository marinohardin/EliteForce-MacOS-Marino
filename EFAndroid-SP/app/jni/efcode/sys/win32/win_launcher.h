/*
===========================================================================
Windows native launcher (Win32 + Common Controls v6). Spec:
docs/launcher-spec/launcher-data-coop.md and servers.md; checklist in
docs/launcher-spec/win32-checklist.md.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#ifndef WIN_LAUNCHER_H
#define WIN_LAUNCHER_H

#include "../../qcommon/q_shared.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Runs before Com_Init. Normal launcher (modal; Play appends the +set arguments to
   commandLine and returns qtrue, Quit exits the process), --host-server (the server
   window only, then exit), --choose-game-folder, stvef:// links in argv. Returns qfalse
   when no launcher is wanted (the mode is on the command line). */
qboolean Sys_WinLauncher( int argc, char **argv, char *commandLine, int size );

#ifdef __cplusplus
}
#endif

#endif
