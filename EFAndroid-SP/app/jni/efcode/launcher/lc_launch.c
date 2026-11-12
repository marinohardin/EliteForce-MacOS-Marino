/*
===========================================================================
Launcher core: what Play passes to the engine, relaunching, Delete Saves &
Settings (lc_launch.h). Spec: launcher-data-coop.md §3, §5, §10.4, §10.6.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#if !defined( _WIN32 )
#	define _GNU_SOURCE 1
#	if defined( __APPLE__ )
#		define _DARWIN_C_SOURCE 1
#	endif
#endif

#include "lc_launch.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#	ifndef WIN32_LEAN_AND_MEAN
#		define WIN32_LEAN_AND_MEAN
#	endif
#	include <windows.h>
#else
#	include <limits.h>
#	include <sys/stat.h>
#	include <unistd.h>
#endif

#ifndef EF_HOMEPATH_NAME
#	define EF_HOMEPATH_NAME "EliteForce-Marino"
#endif

#define ARGS_MAX 4096

/* ===================================================================== */
/* the arguments (§3.2, §3.3)                                            */

static void Addf( char *buf, size_t size, const char *fmt, ... )
#if defined( __GNUC__ )
	__attribute__ (( format( printf, 3, 4 ) ))
#endif
	;
static void Addf( char *buf, size_t size, const char *fmt, ... )
{
	size_t n = strlen( buf );
	va_list ap;

	if ( n + 1 >= size )
		return;
	va_start( ap, fmt );
	vsnprintf( buf + n, size - n, fmt, ap );
	va_end( ap );
}

/* The §3.2 list as one string (each item followed by a space). used = what the
   command line already holds, size = its whole size: brightness gets what's left. */
static void BuildArgs( const lc_launch *o, const char *homeBase, size_t used, size_t size,
	char *args, size_t argsSize )
{
	int holomatch = o->mode != LC_MODE_SP;
	char name[64], bright[512];
	size_t room;

	args[0] = 0;
	Addf( args, argsSize, "+set com_holomatch %d ", holomatch );
	Addf( args, argsSize, "+set r_fullscreen %d +set r_mode -1 +set r_customwidth %d +set r_customheight %d ",
		o->gfx.fullscreen ? 1 : 0, o->gfx.width, o->gfx.height );
	Addf( args, argsSize, "+set r_highdpi %d +set r_picmip %d +set r_swapInterval %d ",
		o->gfx.highdpi ? 1 : 0, o->gfx.picmip, o->gfx.vsync ? 1 : 0 );
	/* framebuffer objects always: multisampling needs them, and their gamma
	   post-pass is what makes the Brightness sliders apply live */
	Addf( args, argsSize, "+set r_fbo 1 +set r_ext_multisample %d ", o->gfx.msaa );
	if ( holomatch ) {
		lc_clean_name( o->name, name, sizeof( name ) );
		Addf( args, argsSize, "+set name \"%s\" ", name );
		if ( o->cmod.available )
			Addf( args, argsSize, "+set cl_cmodModules %d +set cg_aspectCorrect %d "
				"+set cg_aspectCorrectCenterHud %s +set cg_predictWeapons %d ",
				o->cmod.on ? 1 : 0, o->cmod.hudAspect ? 1 : 0,
				o->cmod.hudCenter[0] ? o->cmod.hudCenter : "0", o->cmod.predictWeapons ? 1 : 0 );
	}
	if ( o->basepath && *o->basepath )
		Addf( args, argsSize, "+set fs_basepath \"%s\" ", o->basepath );
	if ( holomatch && o->mastersCfgWritten )
		Addf( args, argsSize, "+exec vsp_masters.cfg " );
	if ( holomatch && o->loopbackIP && *o->loopbackIP )
		Addf( args, argsSize, "+set cl_loopbackHostIP %s ", o->loopbackIP );
	if ( !holomatch ) {		/* co-op (§6.2) */
		int ok = 1;

		if ( o->coopHost )
			Addf( args, argsSize, "+set sp_coop 1 +set coop_backgroundPlay %d ", o->coopBackground ? 1 : 0 );
		else if ( o->coopConnect && *o->coopConnect ) {
			const char *p;

			for ( p = o->coopConnect; *p; p++ )
				if ( !( ( *p >= '0' && *p <= '9' ) || *p == '.' || *p == ':' ) )
					break;
			if ( !*p )
				Addf( args, argsSize, "+set coop_connect \"%s\" +set coop_sex %s ", o->coopConnect,
					o->coopSex && !strcmp( o->coopSex, "female" ) ? "female" : "male" );
		} else
			ok = 0;
		if ( ok && o->coopName && *o->coopName ) {
			char safe[64];

			lc_coop_safe_name( o->coopName, safe, sizeof( safe ) );
			Addf( args, argsSize, "+set coop_name \"%s\" ", safe );
		}
	}
	if ( holomatch && o->joinAddress && *o->joinAddress ) {
		if ( o->joinPassword && *o->joinPassword )
			Addf( args, argsSize, "+set password \"%s\" ", o->joinPassword );
		Addf( args, argsSize, "+connect %s ", o->joinAddress );
	}
	/* brightness goes last so nothing else can be crowded out */
	used += strlen( args );
	room = used < size ? size - used : 0;
	lc_brightness_args( o->gfx.brightness, room, homeBase, bright, sizeof( bright ) );
	Addf( args, argsSize, "%s", bright );
}

void lc_mask_for_log( const char *args, const char *password, char *out, size_t size )
{
	size_t o = 0, pl = password ? strlen( password ) : 0;
	const char *p = args;

	if ( !size )
		return;
	while ( *p && o + 1 < size ) {
		if ( pl && !strncmp( p, password, pl ) ) {
			const char *dots = "\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2";
			size_t k;

			for ( k = 0; dots[k] && o + 1 < size; k++ )
				out[o++] = dots[k];
			p += pl;
		} else
			out[o++] = *p++;
	}
	out[o] = 0;
}

int lc_launch_append_args( const lc_launch *o, const char *homeBase, char *commandLine, int size )
{
	char args[ARGS_MAX], masked[ARGS_MAX];
	size_t have = strlen( commandLine );

	BuildArgs( o, homeBase, have, (size_t)size, args, sizeof( args ) );
	lc_mask_for_log( args, o->joinPassword, masked, sizeof( masked ) );
	lc_log( "Launcher: %s", masked );
	if ( have + strlen( args ) + 1 > (size_t)size ) {
		lc_log( "Launcher: the command line is full, some arguments were cut off" );
		lc_strlcat( commandLine, args, (size_t)size );
		return 1;
	}
	lc_strlcat( commandLine, args, (size_t)size );
	return 0;
}

/* tokens of a command-line string: split at spaces outside double quotes, quotes dropped */
static char **Tokenize( const char *program, const char *s, int *argc )
{
	int cap = 64, n = 0;
	char **v = calloc( (size_t)cap + 2, sizeof( char * ) );

	if ( !v )
		return NULL;
	v[n++] = lc_strdup( program );
	while ( *s ) {
		char tok[ARGS_MAX];
		size_t t = 0;
		int quoted = 0, started = 0;

		while ( *s == ' ' )
			s++;
		if ( !*s )
			break;
		while ( *s && ( quoted || *s != ' ' ) ) {
			if ( *s == '"' ) {
				quoted = !quoted;
				started = 1;
			} else if ( t + 1 < sizeof( tok ) )
				tok[t++] = *s;
			s++;
		}
		( void )started;
		tok[t] = 0;
		if ( n >= cap ) {
			char **grown;

			cap *= 2;
			grown = realloc( v, ( (size_t)cap + 2 ) * sizeof( char * ) );
			if ( !grown )
				break;
			v = grown;
		}
		v[n++] = lc_strdup( tok );
	}
	v[n] = NULL;
	if ( argc )
		*argc = n;
	return v;
}

char **lc_launch_build_argv( const lc_launch *o, const char *homeBase, const char *program, int *argc )
{
	char args[ARGS_MAX], pre[128];

	/* the engine's command line starts with Sys_PlatformPreInit's com_homepath */
	lc_snprintf( pre, sizeof( pre ), "+set com_homepath \"%s\" ", EF_HOMEPATH_NAME );
	BuildArgs( o, homeBase, strlen( pre ), LC_CMDLINE_SIZE, args, sizeof( args ) );
	{
		char masked[ARGS_MAX];

		lc_mask_for_log( args, o->joinPassword, masked, sizeof( masked ) );
		lc_log( "Launcher: %s", masked );
	}
	return Tokenize( program, args, argc );
}

char **lc_launch_client_argv( const lc_launch *o, const char *homeBase, const char *program, int *argc )
{
	char args[ARGS_MAX], name[64], bright[512], cmod[256];
	size_t room = 600;
	char **v;

	/* built as one string of unquoted, single-word items; the name may hold spaces, so it is
	   appended to the vector afterwards in the right place */
	lc_clean_name( o->name, name, sizeof( name ) );
	args[0] = 0;
	Addf( args, sizeof( args ), "+set com_holomatch 1 +set r_fullscreen %d +set r_mode -1 +set r_customwidth %d "
		"+set r_customheight %d +set r_highdpi %d +set r_picmip %d +set r_swapInterval %d "
		"+set r_fbo 1 +set r_ext_multisample %d +set name \"%s\" ",
		o->gfx.fullscreen ? 1 : 0, o->gfx.width, o->gfx.height, o->gfx.highdpi ? 1 : 0, o->gfx.picmip,
		o->gfx.vsync ? 1 : 0, o->gfx.msaa, name );
	lc_brightness_args( o->gfx.brightness, room, homeBase, bright, sizeof( bright ) );
	Addf( args, sizeof( args ), "%s", bright );
	cmod[0] = 0;
	if ( o->cmod.available )
		Addf( cmod, sizeof( cmod ), "+set cl_cmodModules %d +set cg_aspectCorrect %d "
			"+set cg_aspectCorrectCenterHud %s +set cg_predictWeapons %d ",
			o->cmod.on ? 1 : 0, o->cmod.hudAspect ? 1 : 0,
			o->cmod.hudCenter[0] ? o->cmod.hudCenter : "0", o->cmod.predictWeapons ? 1 : 0 );
	Addf( args, sizeof( args ), "%s", cmod );
	if ( o->basepath && *o->basepath )
		Addf( args, sizeof( args ), "+set fs_basepath \"%s\" ", o->basepath );
	if ( o->mastersCfgWritten )
		Addf( args, sizeof( args ), "+exec vsp_masters.cfg " );
	if ( o->loopbackIP && *o->loopbackIP )
		Addf( args, sizeof( args ), "+set cl_loopbackHostIP %s ", o->loopbackIP );
	v = Tokenize( program, args, argc );
	return v;
}

void lc_argv_free( char **argv )
{
	int i;

	if ( !argv )
		return;
	for ( i = 0; argv[i]; i++ )
		free( argv[i] );
	free( argv );
}

/* ===================================================================== */
/* relaunch (§5.1)                                                       */

static int fromLauncher;
static int pendingRelaunch = -3;
static int pendingDelete;
static int relaunchRegistered, deleteRegistered;
static char deleteHome[LC_MAX_PATH], deleteExpected[128];
static lc_alert_fn alertFn;

void lc_set_alert( lc_alert_fn fn )
{
	alertFn = fn;
}

void lc_set_from_launcher( int on )
{
	fromLauncher = on ? 1 : 0;
}

int lc_from_launcher( void )
{
	return fromLauncher || lc_test_env( "EF_FROM_LAUNCHER" );
}

int lc_relaunch_pending( void )
{
	return pendingRelaunch;
}

static void DoRelaunch( void )
{
	const char *argv[8];
	char err[256], cmdline[512];
	int n = 0, i;
	long pid;

	if ( pendingRelaunch == -3 || pendingDelete )
		return;
	argv[n++] = lc_self_command( );
	if ( pendingRelaunch == -2 )
		argv[n++] = LC_CHOOSE_FOLDER_ARG;
	else if ( pendingRelaunch == 0 || pendingRelaunch == 1 ) {
		argv[n++] = "+set";
		argv[n++] = "com_holomatch";
		argv[n++] = pendingRelaunch ? "1" : "0";
	}
	argv[n] = NULL;
	cmdline[0] = 0;
	for ( i = 0; i < n; i++ ) {
		lc_strlcat( cmdline, i ? " " : "\"", sizeof( cmdline ) );
		lc_strlcat( cmdline, argv[i], sizeof( cmdline ) );
		if ( !i )
			lc_strlcat( cmdline, "\"", sizeof( cmdline ) );
	}
	fprintf( stderr, "Relaunching: %s\n", cmdline );
	pendingRelaunch = -3;
	if ( lc_test_env( "VOYAGERSP_TEST_NORELAUNCH" ) )
		return;
	/* the same environment, but a new instance doesn't inherit "started from the launcher" */
	pid = lc_spawn_detached( argv, NULL, "EF_FROM_LAUNCHER", err, sizeof( err ) );
	if ( pid < 0 )
		fprintf( stderr, "Relaunching failed: %s\n", err );
}

void lc_relaunch_at_exit( int mode )
{
	pendingRelaunch = mode;
	if ( !relaunchRegistered ) {
		relaunchRegistered = 1;
		atexit( DoRelaunch );
	}
}

void lc_quit_to_launcher( int quitSignal )
{
	if ( fromLauncher || lc_test_env( "EF_FROM_LAUNCHER" ) ) {
		if ( pendingRelaunch == -3 && !pendingDelete && !quitSignal )
			lc_relaunch_at_exit( -1 );
	}
}

/* ===================================================================== */
/* Delete Saves & Settings (§5.4)                                        */

static void StripTrailingSeps( char *s )
{
	size_t n = strlen( s );

	while ( n > 1 && ( s[n - 1] == '/' || s[n - 1] == '\\' ) && !( n == 3 && s[1] == ':' ) )
		s[--n] = 0;
}

int lc_safe_home_root( const char *homepath, const char *expectedName, char *out, size_t size )
{
	char path[LC_MAX_PATH], parent[LC_MAX_PATH];

	if ( !homepath || !*homepath )
		return 0;
	lc_strlcpy( path, homepath, sizeof( path ) );
	StripTrailingSeps( path );
	if ( lc_is_symlink( path ) || !lc_is_dir( path ) )
		return 0;
	if ( lc_stricmp( lc_path_basename( path ), expectedName ) )
		return 0;
	lc_path_dirname( parent, sizeof( parent ), path );
#ifndef _WIN32
	{
		char parentReal[PATH_MAX], rootReal[PATH_MAX];
		const char *slash;
		size_t plen;

		if ( !realpath( parent, parentReal ) || !realpath( path, rootReal ) )
			return 0;
		plen = strlen( parentReal );
		slash = strrchr( rootReal, '/' );
		if ( !slash || (size_t)( slash - rootReal ) != plen || strncmp( rootReal, parentReal, plen ) )
			return 0;		/* not directly inside its parent: a link on the way */
		if ( lc_stricmp( slash + 1, expectedName ) )
			return 0;
		lc_strlcpy( out, rootReal, size );
		return 1;
	}
#else
	{
		wchar_t w[LC_MAX_PATH], full[LC_MAX_PATH];
		char narrow[LC_MAX_PATH];

		if ( lc_is_symlink( parent ) )
			return 0;
		if ( !MultiByteToWideChar( CP_UTF8, 0, path, -1, w, LC_MAX_PATH ) || !GetFullPathNameW( w, LC_MAX_PATH, full, NULL ) ||
			!WideCharToMultiByte( CP_UTF8, 0, full, -1, narrow, sizeof( narrow ), NULL, NULL ) )
			return 0;
		lc_strlcpy( out, narrow, size );
		return 1;
	}
#endif
}

typedef struct {
	const char *dir;
	unsigned long long bytes;
	int files;
} walkCtx;

static int WalkCB( const char *name, int isDir, void *user )
{
	walkCtx *c = user;
	char full[LC_MAX_PATH];

	lc_path_join( full, sizeof( full ), c->dir, name );
	if ( lc_is_symlink( full ) )
		return 0;
	if ( isDir ) {
		walkCtx sub;

		sub.dir = full;
		sub.bytes = 0;
		sub.files = 0;
		lc_list_dir( full, WalkCB, &sub );
		c->bytes += sub.bytes;
		c->files += sub.files;
	} else if ( lc_is_file( full ) ) {
		int64_t sz = lc_file_size( full );

		c->files++;
		c->bytes += sz > 0 ? (unsigned long long)sz : 0;
	}
	return 0;
}

typedef struct {
	const char *dir;
	int pk3s;
	unsigned long long bytes;
} pk3Ctx;

static int Pk3CB( const char *name, int isDir, void *user )
{
	pk3Ctx *c = user;
	size_t n = strlen( name );

	if ( !isDir && n > 4 && !lc_stricmp( name + n - 4, ".pk3" ) ) {
		char full[LC_MAX_PATH];
		int64_t sz;

		lc_path_join( full, sizeof( full ), c->dir, name );
		sz = lc_file_size( full );
		c->pk3s++;
		c->bytes += sz > 0 ? (unsigned long long)sz : 0;
	}
	return 0;
}

/* is path inside root (case-insensitively, as the Mac check) */
static int InsideDir( const char *path, const char *root )
{
	char r[LC_MAX_PATH];
	size_t n;

	lc_strlcpy( r, root, sizeof( r ) );
	StripTrailingSeps( r );
	n = strlen( r );
	if ( lc_strnicmp( path, r, n ) )
		return 0;
	return path[n] == '/' || path[n] == '\\';
}

int lc_delete_summarize( const char *homepath, const char *baseInUse, lc_delete_summary *out )
{
	walkCtx w;
	char base[LC_MAX_PATH], saves[LC_MAX_PATH];
	pk3Ctx p;

	memset( out, 0, sizeof( *out ) );
	if ( !homepath || !lc_is_dir( homepath ) )
		return 0;
	w.dir = homepath;
	w.bytes = 0;
	w.files = 0;
	lc_list_dir( homepath, WalkCB, &w );
	out->files = w.files;
	out->bytes = w.bytes;
	if ( lc_child_ci( homepath, LC_BASEGAME, base, sizeof( base ) ) && lc_is_dir( base ) ) {
		if ( lc_child_ci( base, "saves", saves, sizeof( saves ) ) && lc_is_dir( saves ) ) {
			w.dir = saves;
			w.bytes = 0;
			w.files = 0;
			lc_list_dir( saves, WalkCB, &w );
			out->saves = w.files;
		}
		p.dir = base;
		p.pk3s = 0;
		p.bytes = 0;
		lc_list_dir( base, Pk3CB, &p );
		out->pk3s = p.pk3s;
		out->pk3Bytes = p.bytes;
	}
	if ( baseInUse && *baseInUse && !InsideDir( baseInUse, homepath ) )
		lc_strlcpy( out->externalBase, baseInUse, sizeof( out->externalBase ) );
	return 1;
}

void lc_delete_texts( const char *homepath, const lc_delete_summary *s,
	char *title, size_t titleSize, char *info, size_t infoSize )
{
	char total[64], pk3[64], pk3Line[160], tail[LC_MAX_PATH + 64];

	lc_strlcpy( title, "Delete saves & settings?", titleSize );
	lc_format_bytes( s->bytes, total, sizeof( total ) );
	pk3Line[0] = 0;
	if ( s->pk3s ) {
		lc_format_bytes( s->pk3Bytes, pk3, sizeof( pk3 ) );
		lc_snprintf( pk3Line, sizeof( pk3Line ), "\xe2\x80\xa2 game files copied by the app (%d .pk3, %s)\n", s->pk3s, pk3 );
	}
	if ( s->externalBase[0] )
		lc_snprintf( tail, sizeof( tail ), "Your game folder at %s will not be touched.", s->externalBase );
	else
		lc_strlcpy( tail, "Nothing outside this folder is touched.", sizeof( tail ) );
	lc_snprintf( info, infoSize,
		"This quits the game and moves this folder to the " LC_TRASH_NAME ":\n%s\n\n"
		"\xe2\x80\xa2 saved games (%d files)\n"
		"\xe2\x80\xa2 settings and key bindings (efspconfig.cfg, hmconfig.cfg, keys, console history)\n"
		"%s"
		"\nTotal: %d files, %s.\n\n%s",
		homepath, s->saves, pk3Line, s->files, total, tail );
}

static void DoPendingDelete( void )
{
	char root[LC_MAX_PATH], err[256];

	if ( !pendingDelete )
		return;
	if ( !lc_safe_home_root( deleteHome, deleteExpected, root, sizeof( root ) ) ) {
		fprintf( stderr, "Delete Saves & Settings: refusing, %s is not a plain directory\n", deleteHome );
		return;
	}
	if ( lc_test_env( "VOYAGERSP_TEST_NOTRASH" ) ) {
		fprintf( stderr, "[test] would move \"%s\" to the " LC_TRASH_NAME "\n", root );
		return;
	}
	if ( lc_move_to_trash( root, err, sizeof( err ) ) )
		fprintf( stderr, "Moved \"%s\" to the " LC_TRASH_NAME "\n", root );
	else {
		char text[LC_MAX_PATH + 512];

		fprintf( stderr, "Could not move \"%s\" to the " LC_TRASH_NAME ": %s\n", root, err );
		lc_snprintf( text, sizeof( text ), "%s could not be moved to the " LC_TRASH_NAME ": %s\n\nYou can delete it in "
			LC_FILE_MANAGER ".", root, err );
		if ( alertFn )
			alertFn( "Could not delete saves & settings", text );
	}
}

void lc_delete_at_exit( const char *homepath, const char *expectedName )
{
	lc_store *s = lc_store_shared( );

	lc_strlcpy( deleteHome, homepath, sizeof( deleteHome ) );
	lc_strlcpy( deleteExpected, expectedName, sizeof( deleteExpected ) );
	lc_store_remove( s, LC_KEY_GAMEFOLDER );
	lc_store_save( s );
	pendingDelete = 1;
	if ( !deleteRegistered ) {
		deleteRegistered = 1;
		atexit( DoPendingDelete );		/* after the engine wrote its config and closed its files */
	}
}
