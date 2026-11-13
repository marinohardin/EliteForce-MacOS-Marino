/*
===========================================================================
Launcher core: what Play passes to the engine, relaunching, Delete Saves &
Settings. Spec: launcher-data-coop.md §3, §5, §10.4, §10.6. Owner: core-a.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#ifndef LC_LAUNCH_H
#define LC_LAUNCH_H

#include "lc_common.h"
#include "lc_settings.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	int mode;						/* LC_MODE_SP or LC_MODE_HOLOMATCH (Host never gets here) */
	lc_gfx gfx;
	const char *name;				/* raw name field; cleaned inside */
	lc_cmod cmod;
	const char *basepath;			/* NULL: none */
	int mastersCfgWritten;			/* +exec vsp_masters.cfg (lc_masters_write_cfg in lc_server.h) */
	const char *loopbackIP;			/* NULL: none */
	/* co-op (§6.2): set by the co-op sheet */
	int coopHost, coopBackground;
	const char *coopConnect, *coopSex, *coopName;
	/* Browse… / link (§3.5) */
	const char *joinAddress, *joinPassword;
} lc_launch;

/* §3.2 exactly, appended to commandLine (size = whole buffer, LC_CMDLINE_SIZE);
   may write <homeBase>/vsp_brightness.cfg. Logs "Launcher: <args>" with the
   password masked. 0 on success. Windows (in-process launcher) uses this. */
int lc_launch_append_args( const lc_launch *o, const char *homeBase, char *commandLine, int size );
/* the same arguments as an argv vector for exec (Linux ef-launcher → efsp):
   argv[0] = program, then the tokens of §3.2 (quotes removed: one token per value),
   NULL-terminated; *argc set. Free with lc_argv_free. room for brightness as if
   the engine's 1024-byte command line held them (same overflow → cfg rule). */
char **lc_launch_build_argv( const lc_launch *o, const char *homeBase, const char *program, int *argc );
/* §3.3: Play on This PC/Computer from the server window: argv[0] = program, then the Holomatch
   client arguments (graphics, name, brightness with room 600, cMod, fs_basepath, masters cfg,
   loopback), one token per value, NULL-terminated. Uses o->gfx, name, cmod, basepath,
   mastersCfgWritten, loopbackIP. */
char **lc_launch_client_argv( const lc_launch *o, const char *homeBase, const char *program, int *argc );
void lc_argv_free( char **argv );
void lc_mask_for_log( const char *args, const char *password, char *out, size_t size );

/* ---- relaunch (§5.1) — used by the engine side (sys_platform_desktop.c) ---------- */
void lc_set_from_launcher( int on );
int lc_from_launcher( void );			/* set, or env EF_FROM_LAUNCHER=1 */
/* the decision of Sys_PlatformQuitToLauncher; quitSignal = the engine saw SIGTERM/SIGINT */
void lc_quit_to_launcher( int quitSignal );
/* mode: -1 launcher, 0 SP, 1 Holomatch, -2 launcher with the game-folder chooser open.
   Registers an atexit handler (once) that spawns lc_self_command() detached with
   the matching arguments (VOYAGERSP_TEST_NORELAUNCH: log only). */
void lc_relaunch_at_exit( int mode );
int lc_relaunch_pending( void );		/* -3 none, else the mode */

/* ---- Delete Saves & Settings (§5.4) --------------------------------------------- */
/* homepath only if a real dir (not a link) directly in its parent and named
   expectedName case-insensitively; 1 ok */
int lc_safe_home_root( const char *homepath, const char *expectedName, char *out, size_t size );
typedef struct {
	int saves, files, pk3s;
	unsigned long long bytes, pk3Bytes;
	char externalBase[LC_MAX_PATH];		/* the BaseEF in use if outside the homepath, else "" */
} lc_delete_summary;
int lc_delete_summarize( const char *homepath, const char *baseInUse, lc_delete_summary *out );
/* the confirmation text of §5.4 (with LC_TRASH_NAME), message and informative parts */
void lc_delete_texts( const char *homepath, const lc_delete_summary *s,
	char *title, size_t titleSize, char *info, size_t infoSize );
#define LC_DELETE_BUTTON	"Move to " LC_TRASH_NAME " and Quit"
/* a hook for the one alert the exit-time code may need ("Could not delete saves &
   settings"); the engine side installs an SDL message box. May stay unset. */
typedef void (*lc_alert_fn)( const char *title, const char *text );
void lc_set_alert( lc_alert_fn fn );
/* at exit: re-check, move to the trash (VOYAGERSP_TEST_NOTRASH: log only); registers
   atexit; clears gamefolder from the store first */
void lc_delete_at_exit( const char *homepath, const char *expectedName );

#ifdef __cplusplus
}
#endif

#endif
