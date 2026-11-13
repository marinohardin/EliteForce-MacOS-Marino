/*
===========================================================================
Windows / Linux: the platform hooks (sys_platform.h).

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).

  homepath         the app's own (EF_HOMEPATH_NAME: %APPDATA%\EliteForce-Marino,
                   ~/.local/share/eliteforce-marino)
  launcher         Windows: the in-process Win32 launcher (Sys_WinLauncher,
                   efcode/sys/win32/, when built with EF_WIN_LAUNCHER) before
                   Com_Init, like the macOS one. Linux: a separate Qt program,
                   ef-launcher, next to this one; started with no mode on the
                   command line this program replaces itself with it, and it
                   starts this one again with the mode, graphics and so on as
                   arguments (docs/launcher-spec/ARCHITECTURE.md)
  game data        sys_data.c (the launcher core's lookup)
  quit             a bare "quit" comes back to the launcher when the game was
                   started from it; the File menu's actions (launcher.cfg
                   spec §5.2) as console commands: launcher, switchmode,
                   showsaves, showgamefolder, changegamefolder,
                   deletesettings; the window's close button asks first (§5.3)

The decisions (relaunch, delete) are the launcher core's (lc_launch.c); this
file is the engine side: the dialogs, the console commands, the quit signal.
===========================================================================
*/

#if !defined( __APPLE__ ) && !defined( __ANDROID__ ) && !defined( __EMSCRIPTEN__ ) && !defined( DEDICATED )

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "sys_local.h"
#include "sys_platform.h"
#include "../launcher/lc_common.h"
#include "../launcher/lc_data.h"
#include "../launcher/lc_launch.h"
#ifdef EF_WIN_LAUNCHER
#	include "win32/win_launcher.h"
#endif

#include <signal.h>

#ifdef USE_LOCAL_HEADERS
#	include "SDL.h"
#else
#	include <SDL.h>
#endif

extern volatile sig_atomic_t sys_quitSignal;	// sys_main.c

#define APP_NAME	"Elite Force"

/* ------------------------------------------------------------------------
   Dialogs
   ------------------------------------------------------------------------ */

static qboolean TestEnv( const char *name )
{
	return lc_test_env( name ) ? qtrue : qfalse;
}

// A message box with up to two buttons; the index of the one pressed (0 =
// the first, the default), or -1 for closed. VOYAGERSP_TEST_CONFIRM answers a
// question with its first button, VOYAGERSP_TEST_NODIALOG takes the safe
// choice (the last button of a question, nothing for a notice).
static int Dialog( qboolean warning, const char *title, const char *text, const char *b0, const char *b1 )
{
	SDL_MessageBoxButtonData buttons[2];
	SDL_MessageBoxData data;
	int n = 0, pressed = -1;

	fprintf( stderr, "[dialog] %s \xe2\x80\x94 %s\n", title, text );
	if ( TestEnv( "VOYAGERSP_TEST_CONFIRM" ) )
		return 0;
	if ( TestEnv( "VOYAGERSP_TEST_NODIALOG" ) )
		return b1 ? 1 : 0;

	memset( buttons, 0, sizeof( buttons ) );
	buttons[n].flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
	buttons[n].buttonid = 0;
	buttons[n++].text = b0;
	if ( b1 ) {
		buttons[n].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
		buttons[n].buttonid = 1;
		buttons[n++].text = b1;
	} else
		buttons[0].flags |= SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
	memset( &data, 0, sizeof( data ) );
	data.flags = warning ? SDL_MESSAGEBOX_WARNING : SDL_MESSAGEBOX_INFORMATION;
	data.window = SDL_GetKeyboardFocus( );
	data.title = title;
	data.message = text;
	data.numbuttons = n;
	data.buttons = buttons;
	// the game holds the mouse: let go of it for the box
	SDL_SetRelativeMouseMode( SDL_FALSE );
	SDL_ShowCursor( SDL_ENABLE );
	if ( SDL_ShowMessageBox( &data, &pressed ) < 0 )
		return -1;
	return pressed;
}

static void AlertHook( const char *title, const char *text )
{
	fprintf( stderr, "[dialog] %s \xe2\x80\x94 %s\n", title, text );
	if ( !TestEnv( "VOYAGERSP_TEST_NODIALOG" ) )
		SDL_ShowSimpleMessageBox( SDL_MESSAGEBOX_WARNING, title, text, NULL );
}

static qboolean IsHolomatch( void )
{
	return Cvar_VariableIntegerValue( "com_holomatch" ) != 0 ? qtrue : qfalse;
}

/* ------------------------------------------------------------------------
   Homepath, launcher, game data
   ------------------------------------------------------------------------ */

void Sys_PlatformPreInit( int argc, char **argv, char *commandLine, int size )
{
	int i;

	Sys_DataInstallLog( );
	// the app's own homepath, unless the command line names one
	for ( i = 1; i < argc; i++ )
		if ( !Q_stricmp( argv[i], "fs_homepath" ) || !Q_stricmp( argv[i], "com_homepath" ) )
			return;
	Q_strcat( commandLine, size, "+set com_homepath \"" EF_HOMEPATH_NAME "\" " );
}

// The command line names the mode, or the launcher is switched off: no launcher
// (this is how relaunches from the File menu's actions and tests skip it).
static qboolean LauncherSkipped( int argc, char **argv )
{
	int i;

	for ( i = 1; i < argc; i++ )
		if ( !Q_stricmp( argv[i], "com_holomatch" ) )
			return qtrue;
	return TestEnv( "EF_NO_LAUNCHER" );
}

qboolean Sys_PlatformLauncher( int argc, char **argv, char *commandLine, int size )
{
	if ( LauncherSkipped( argc, argv ) )
		return qfalse;
#ifdef _WIN32
#	ifdef EF_WIN_LAUNCHER
	if ( Sys_WinLauncher( argc, argv, commandLine, size ) ) {
		lc_set_from_launcher( 1 );		// a bare "quit" comes back here
		return qtrue;
	}
#	endif
	return qfalse;
#else
	// started by ef-launcher (which names the mode anyway), or asked not to
	char launcher[LC_MAX_PATH];

	if ( TestEnv( "EF_FROM_LAUNCHER" ) )
		return qfalse;
	// the launcher is a separate program, so this one never links Qt: hand over
	if ( lc_sibling_exe( "ef-launcher", launcher, sizeof( launcher ) ) ) {
		const char *args[64];
		int n = 0, i;

		args[n++] = launcher;
		for ( i = 1; i < argc && n < 62; i++ )
			args[n++] = argv[i];
		args[n] = NULL;
		fprintf( stderr, "Starting the launcher: %s\n", launcher );
		lc_exec( args, NULL );
		fprintf( stderr, "Couldn't start %s: running without the launcher\n", launcher );
	}
	return qfalse;
#endif
}

const char *Sys_PlatformResolveGameData( int argc, char **argv, qboolean headless )
{
	return Sys_DataResolve( argc, argv, headless );
}

/* ------------------------------------------------------------------------
   The File menu's actions as console commands (§5.2)
   ------------------------------------------------------------------------ */

static void Quit( void )
{
	Cbuf_ExecuteText( EXEC_APPEND, "quit\n" );
}

static void RelaunchIn( int mode )
{
	lc_relaunch_at_exit( mode );
	Quit( );
}

static void Launcher_f( void )
{
	if ( Dialog( qfalse, "Return to the launcher?",
			IsHolomatch( ) ? "This leaves the current game and restarts Elite Force at the launcher." :
				"This restarts Elite Force at the launcher. Progress since your last save is lost.",
			"Return to Launcher", "Cancel" ) == 0 )
		RelaunchIn( -1 );
}

static void SwitchMode_f( void )
{
	qboolean hm = IsHolomatch( );

	if ( Dialog( qfalse, hm ? "Switch to Single Player?" : "Switch to Holomatch?",
			hm ? "This leaves the current game and restarts Elite Force in single player." :
				"This restarts Elite Force in Holomatch. Progress since your last save is lost.",
			"Switch", "Cancel" ) == 0 )
		RelaunchIn( hm ? 0 : 1 );
}

static void HomeBase( char *out, size_t size )
{
	const char *hp = Cvar_VariableString( "fs_homepath" );

	lc_path_join( out, size, hp && *hp ? hp : lc_homepath( ), LC_BASEGAME );
}

static void ShowSaves_f( void )
{
	char base[LC_MAX_PATH], saves[LC_MAX_PATH];

	HomeBase( base, sizeof( base ) );
	lc_path_join( saves, sizeof( saves ), base, "saves" );
	lc_mkdir_p( saves );
	lc_open_folder( saves );
}

// the BaseEF in use: fs_basepath's if it has pak0, else the homepath's
static qboolean CurrentGameBase( char *out, size_t size )
{
	const char *bp = Cvar_VariableString( "fs_basepath" );
	char base[LC_MAX_PATH], pak[LC_MAX_PATH];

	if ( bp && *bp && lc_child_ci( bp, LC_BASEGAME, base, sizeof( base ) ) && lc_is_dir( base )
		&& lc_child_ci( base, "pak0.pk3", pak, sizeof( pak ) ) ) {
		lc_strlcpy( out, base, size );
		return qtrue;
	}
	HomeBase( base, sizeof( base ) );
	if ( lc_child_ci( base, "pak0.pk3", pak, sizeof( pak ) ) ) {
		lc_strlcpy( out, base, size );
		return qtrue;
	}
	return qfalse;
}

static void ShowGameFolder_f( void )
{
	char base[LC_MAX_PATH];

	if ( CurrentGameBase( base, sizeof( base ) ) )
		lc_open_folder( base );
	else
		Dialog( qfalse, "No game folder", "No BaseEF folder with pak0.pk3 is in use.", "OK", NULL );
}

// The chooser is the launcher's: restart there with it open
static void ChangeGameFolder_f( void )
{
	if ( Dialog( qfalse, "Change the game folder?",
			IsHolomatch( ) ? "This leaves the current game and restarts Elite Force at the launcher, with the game folder chooser open." :
				"This restarts Elite Force at the launcher, with the game folder chooser open. Progress since your last save is lost.",
			"Restart", "Cancel" ) == 0 )
		RelaunchIn( -2 );
}

static void DeleteSettings_f( void )
{
	char root[LC_MAX_PATH], base[LC_MAX_PATH], title[128], info[4096], text[LC_MAX_PATH + 256];
	lc_delete_summary s;

	if ( !lc_safe_home_root( lc_homepath( ), EF_HOMEPATH_NAME, root, sizeof( root ) ) ) {
		Com_sprintf( text, sizeof( text ), "%s does not exist, or is a link to another location, so nothing was deleted.",
			lc_homepath( ) );
		Dialog( qtrue, "Nothing to delete", text, "OK", NULL );
		return;
	}
	if ( !CurrentGameBase( base, sizeof( base ) ) )
		base[0] = '\0';
	lc_delete_summarize( root, base, &s );
	lc_delete_texts( root, &s, title, sizeof( title ), info, sizeof( info ) );
	if ( Dialog( qtrue, title, info, LC_DELETE_BUTTON, "Cancel" ) != 0 )
		return;
	lc_delete_at_exit( root, EF_HOMEPATH_NAME );
	Quit( );
}

/* ------------------------------------------------------------------------
   The close button (5.3)

   Closing the window (the close button, Alt+F4) arrives as SDL_QUIT. A filter
   holds it back; the next frame asks "Quit Elite Force?" (a message box: the
   game waits, as single player does on macOS), and Quit does "quit Closed
   window", which doesn't return to the launcher. VOYAGERSP_TEST_NODIALOG quits
   at once; VOYAGERSP_TEST_QUIT_ANSWER=quit|cancel answers without the box.
   ------------------------------------------------------------------------ */

static volatile int closeRequested;

static int SDLCALL CloseFilter( void *user, SDL_Event *e )
{
	if ( e->type == SDL_QUIT ) {
		closeRequested = 1;
		return 0;
	}
	return 1;
}

static void ConfirmQuit( void )
{
	const char *answer = getenv( "VOYAGERSP_TEST_QUIT_ANSWER" );
	int quit;

	if ( answer && *answer )
		quit = !Q_stricmp( answer, "quit" );
	else
		quit = Dialog( qtrue, "Quit " APP_NAME "?", IsHolomatch( ) ? "" :
			"Progress since your last save is lost.", "Quit", "Cancel" ) == 0;
	if ( quit )
		Cbuf_ExecuteText( EXEC_APPEND, "quit Closed window\n" );
}

void Sys_PlatformPostInit( void )
{
	Sys_DataPrintEarlyLog( );
	if ( Cvar_VariableIntegerValue( "dedicated" ) )
		return;
	lc_set_alert( AlertHook );
	if ( !TestEnv( "VOYAGERSP_TEST_NODIALOG" ) )
		SDL_SetEventFilter( CloseFilter, NULL );
	Cmd_AddCommand( "launcher", Launcher_f );
	Cmd_AddCommand( "switchmode", SwitchMode_f );
	Cmd_AddCommand( "showsaves", ShowSaves_f );
	Cmd_AddCommand( "showgamefolder", ShowGameFolder_f );
	Cmd_AddCommand( "changegamefolder", ChangeGameFolder_f );
	Cmd_AddCommand( "deletesettings", DeleteSettings_f );
}

/* ------------------------------------------------------------------------
   Quit
   ------------------------------------------------------------------------ */

void Sys_PlatformQuitSignals( void )
{
	// SIGTERM / SIGINT stay with sys_main.c's handler: it sets sys_quitSignal,
	// which Sys_PlatformQuitToLauncher reads
}

void Sys_PlatformFrame( void )
{
	if ( closeRequested ) {
		closeRequested = 0;
		ConfirmQuit( );
	}
}

// The game's own Quit (a bare "quit"): back at the launcher, unless the game was
// started without one, a File-menu action decided already, or the process was
// asked to end (SIGTERM, SIGINT: a logout or a kill, not the player's choice)
void Sys_PlatformQuitToLauncher( void )
{
	lc_quit_to_launcher( sys_quitSignal != 0 );
}

#endif
