/* the §3.2 command line, argv builder, relaunch decisions, Delete Saves & Settings */
#include "lc_launch.h"
#include "lc_test.h"
#include "lc_test_util.h"
#include <stdlib.h>
#ifndef _WIN32
#	include <unistd.h>
#endif

static void Defaults( lc_launch *o )
{
	memset( o, 0, sizeof( *o ) );
	o->mode = LC_MODE_SP;
	o->gfx.width = 1920;
	o->gfx.height = 1080;
	o->gfx.highdpi = 1;
	o->gfx.msaa = 4;
	o->gfx.picmip = 0;
	o->gfx.vsync = 1;
	o->gfx.brightness = LC_BRIGHTNESS_CUSTOM;
	o->name = "Janeway";
	strcpy( o->cmod.hudCenter, "0" );
}

static int ArgIndex( char **argv, int argc, const char *s )
{
	int i;
	for ( i = 0; i < argc; i++ )
		if ( !strcmp( argv[i], s ) )
			return i;
	return -1;
}

static void TestSinglePlayer( void )
{
	const char *home = tu_dir( "launch-sp" );
	lc_launch o;
	char cl[LC_CMDLINE_SIZE] = "+set com_homepath \"EliteForce-Marino\" ";

	Defaults( &o );
	o.gfx.brightness = 1;		/* Plain */
	CHECK( lc_launch_append_args( &o, home, cl, sizeof( cl ) ) == 0 );
	CHECK_STR( cl,
		"+set com_homepath \"EliteForce-Marino\" "
		"+set com_holomatch 0 +set r_fullscreen 0 +set r_mode -1 +set r_customwidth 1920 +set r_customheight 1080 "
		"+set r_highdpi 1 +set r_picmip 0 +set r_swapInterval 1 +set r_fbo 1 +set r_ext_multisample 4 "
		"+set r_gamma 1.4 +set r_overBrightFactor 1.5 +set r_mapLightingFactor 2 +set r_mapLightingGamma 1 "
		"+set r_mapLightingClampMin 0 +set r_mapLightingClampMax 1 +set r_intensity 1 +set r_textureGamma 1 " );

	/* single player never gets name, cMod, masters, loopback or a join, whatever is set */
	Defaults( &o );
	o.cmod.available = 1;
	o.mastersCfgWritten = 1;
	o.loopbackIP = "1.2.3.4";
	o.joinAddress = "10.0.0.1:27960";
	o.basepath = "/games/EF";
	cl[0] = 0;
	lc_launch_append_args( &o, home, cl, sizeof( cl ) );
	CHECK_STR( cl,
		"+set com_holomatch 0 +set r_fullscreen 0 +set r_mode -1 +set r_customwidth 1920 +set r_customheight 1080 "
		"+set r_highdpi 1 +set r_picmip 0 +set r_swapInterval 1 +set r_fbo 1 +set r_ext_multisample 4 "
		"+set fs_basepath \"/games/EF\" " );
}

static void TestHolomatch( void )
{
	const char *home = tu_dir( "launch-hm" );
	lc_launch o;
	char cl[LC_CMDLINE_SIZE] = "";
	char **argv;
	int argc, i;
	const char *expect =
		"+set com_holomatch 1 +set r_fullscreen 1 +set r_mode -1 +set r_customwidth 1280 +set r_customheight 720 "
		"+set r_highdpi 0 +set r_picmip 1 +set r_swapInterval 0 +set r_fbo 1 +set r_ext_multisample 2 "
		"+set name \"Tom Paris x\" "
		"+set cl_cmodModules 1 +set cg_aspectCorrect 1 +set cg_aspectCorrectCenterHud 0.5 +set cg_predictWeapons 1 "
		"+set fs_basepath \"/games/EF Dir\" +exec vsp_masters.cfg +set cl_loopbackHostIP 203.0.113.5 "
		"+set password \"s3cret\" +connect 10.0.0.1:27960 ";

	Defaults( &o );
	o.mode = LC_MODE_HOLOMATCH;
	o.gfx.fullscreen = 1;
	o.gfx.width = 1280;
	o.gfx.height = 720;
	o.gfx.highdpi = 0;
	o.gfx.msaa = 2;
	o.gfx.picmip = 1;
	o.gfx.vsync = 0;
	o.name = "Tom \"Paris\"; x";
	o.cmod.available = 1;
	o.cmod.on = 1;
	o.cmod.hudAspect = 1;
	strcpy( o.cmod.hudCenter, "0.5" );
	o.cmod.predictWeapons = 1;
	o.basepath = "/games/EF Dir";
	o.mastersCfgWritten = 1;
	o.loopbackIP = "203.0.113.5";
	o.joinAddress = "10.0.0.1:27960";
	o.joinPassword = "s3cret";
	lc_launch_append_args( &o, home, cl, sizeof( cl ) );
	CHECK_STR( cl, expect );

	/* the same as an argv vector: one token per value, quotes gone */
	argv = lc_launch_build_argv( &o, home, "/bin/efsp", &argc );
	CHECK( argv != NULL );
	CHECK_STR( argv[0], "/bin/efsp" );
	CHECK( argv[argc] == NULL );
	i = ArgIndex( argv, argc, "name" );
	CHECK( i > 0 && !strcmp( argv[i - 1], "+set" ) && !strcmp( argv[i + 1], "Tom Paris x" ) );
	i = ArgIndex( argv, argc, "fs_basepath" );
	CHECK( i > 0 && !strcmp( argv[i + 1], "/games/EF Dir" ) );
	i = ArgIndex( argv, argc, "password" );
	CHECK( i > 0 && !strcmp( argv[i + 1], "s3cret" ) );
	CHECK_STR( argv[argc - 1], "10.0.0.1:27960" );
	CHECK_STR( argv[argc - 2], "+connect" );
	CHECK_STR( argv[1], "+set" );
	CHECK_STR( argv[2], "com_holomatch" );
	CHECK_STR( argv[3], "1" );
	/* joined with spaces it is the string above (the name aside) */
	{
		char joined[LC_CMDLINE_SIZE] = "";
		for ( i = 1; i < argc; i++ ) {
			lc_strlcat( joined, argv[i], sizeof( joined ) );
			lc_strlcat( joined, " ", sizeof( joined ) );
		}
		CHECK( strstr( joined, "+exec vsp_masters.cfg +set cl_loopbackHostIP 203.0.113.5 +set password s3cret +connect 10.0.0.1:27960 " ) != NULL );
	}
	lc_argv_free( argv );

	/* no cMod pk3: nothing of cMod's */
	o.cmod.available = 0;
	cl[0] = 0;
	lc_launch_append_args( &o, home, cl, sizeof( cl ) );
	CHECK( strstr( cl, "cl_cmodModules" ) == NULL && strstr( cl, "+set name \"Tom Paris x\" +set fs_basepath" ) != NULL );
	/* a password without an address is not passed; an address without one is */
	o.joinPassword = NULL;
	cl[0] = 0;
	lc_launch_append_args( &o, home, cl, sizeof( cl ) );
	CHECK( strstr( cl, "+set password" ) == NULL && strstr( cl, "+connect 10.0.0.1:27960 " ) != NULL );
}

static void TestCoop( void )
{
	const char *home = tu_dir( "launch-coop" );
	lc_launch o;
	char cl[LC_CMDLINE_SIZE];
	const char *prefix =
		"+set com_holomatch 0 +set r_fullscreen 0 +set r_mode -1 +set r_customwidth 1920 +set r_customheight 1080 "
		"+set r_highdpi 1 +set r_picmip 0 +set r_swapInterval 1 +set r_fbo 1 +set r_ext_multisample 4 ";
	char want[LC_CMDLINE_SIZE];

	Defaults( &o );
	o.coopHost = 1;
	o.coopBackground = 0;
	o.coopName = "Jos\xc3\xa9";
	cl[0] = 0;
	lc_launch_append_args( &o, home, cl, sizeof( cl ) );
	lc_snprintf( want, sizeof( want ), "%s+set sp_coop 1 +set coop_backgroundPlay 0 +set coop_name \"Jose\" ", prefix );
	CHECK_STR( cl, want );

	Defaults( &o );
	o.coopConnect = "192.168.1.5:29760";
	o.coopSex = "female";
	o.coopName = "Neelix";
	cl[0] = 0;
	lc_launch_append_args( &o, home, cl, sizeof( cl ) );
	lc_snprintf( want, sizeof( want ), "%s+set coop_connect \"192.168.1.5:29760\" +set coop_sex female +set coop_name \"Neelix\" ", prefix );
	CHECK_STR( cl, want );

	o.coopSex = "anything";
	cl[0] = 0;
	lc_launch_append_args( &o, home, cl, sizeof( cl ) );
	CHECK( strstr( cl, "+set coop_sex male " ) != NULL );

	/* an address with other characters is not passed, the name still is (as the Mac code) */
	o.coopConnect = "host.example:1";
	cl[0] = 0;
	lc_launch_append_args( &o, home, cl, sizeof( cl ) );
	lc_snprintf( want, sizeof( want ), "%s+set coop_name \"Neelix\" ", prefix );
	CHECK_STR( cl, want );

	/* neither host nor connect: nothing */
	o.coopConnect = NULL;
	cl[0] = 0;
	lc_launch_append_args( &o, home, cl, sizeof( cl ) );
	CHECK_STR( cl, prefix );

	/* Holomatch ignores co-op */
	Defaults( &o );
	o.mode = LC_MODE_HOLOMATCH;
	o.coopHost = 1;
	cl[0] = 0;
	lc_launch_append_args( &o, home, cl, sizeof( cl ) );
	CHECK( strstr( cl, "sp_coop" ) == NULL );
}

static void TestBrightnessOverflow( void )
{
	const char *home = tu_dir( "launch-bright" );
	lc_launch o;
	char cl[LC_CMDLINE_SIZE] = "", path[LC_MAX_PATH], base[800], **argv;
	int argc;
	char *text;

	memset( base, 'a', 700 );
	base[0] = '/';
	base[700] = 0;
	Defaults( &o );
	o.gfx.brightness = 1;
	o.basepath = base;
	CHECK( lc_launch_append_args( &o, home, cl, sizeof( cl ) ) == 0 );
	CHECK( strlen( cl ) < sizeof( cl ) );
	CHECK( strcmp( cl + strlen( cl ) - 25, "+exec vsp_brightness.cfg " ) == 0 );
	CHECK( strstr( cl, "r_gamma" ) == NULL );
	lc_path_join( path, sizeof( path ), home, "vsp_brightness.cfg" );
	text = lc_read_file( path, NULL );
	CHECK( text && strstr( text, "set r_gamma \"1.4\"\n" ) );
	free( text );
	lc_remove( path );

	argv = lc_launch_build_argv( &o, home, "efsp", &argc );
	CHECK_STR( argv[argc - 2], "+exec" );
	CHECK_STR( argv[argc - 1], "vsp_brightness.cfg" );
	CHECK( lc_is_file( path ) );
	lc_argv_free( argv );

	/* a short path leaves the room: inline */
	o.basepath = NULL;
	cl[0] = 0;
	lc_launch_append_args( &o, home, cl, sizeof( cl ) );
	CHECK( strstr( cl, "+set r_gamma 1.4 " ) != NULL && strstr( cl, "+exec" ) == NULL );
}

static void TestClientArgs( void )
{
	const char *home = tu_dir( "launch-client" );
	lc_launch o;
	char **argv;
	int argc;
	char joined[1024] = "";
	int i;

	Defaults( &o );
	o.mode = LC_MODE_HOST;
	o.gfx.brightness = 0;
	o.name = "Chell";
	o.cmod.available = 1;
	o.cmod.on = 0;
	o.basepath = "/g";
	o.mastersCfgWritten = 1;
	o.loopbackIP = "9.9.9.9";
	argv = lc_launch_client_argv( &o, home, "efsp", &argc );
	for ( i = 1; i < argc; i++ ) {
		lc_strlcat( joined, argv[i], sizeof( joined ) );
		lc_strlcat( joined, " ", sizeof( joined ) );
	}
	CHECK_STR( joined,
		"+set com_holomatch 1 +set r_fullscreen 0 +set r_mode -1 +set r_customwidth 1920 +set r_customheight 1080 "
		"+set r_highdpi 1 +set r_picmip 0 +set r_swapInterval 1 +set r_fbo 1 +set r_ext_multisample 4 +set name Chell "
		"+set r_gamma 1 +set r_overBrightFactor 0 +set r_mapLightingFactor 0 +set r_mapLightingGamma 1 "
		"+set r_mapLightingClampMin 0 +set r_mapLightingClampMax 1 +set r_intensity 1 +set r_textureGamma 1 "
		"+set r_overBrightBits 0 +set r_mapOverBrightBits 0 "
		"+set cl_cmodModules 0 +set cg_aspectCorrect 0 +set cg_aspectCorrectCenterHud 0 +set cg_predictWeapons 0 "
		"+set fs_basepath /g +exec vsp_masters.cfg +set cl_loopbackHostIP 9.9.9.9 " );
	lc_argv_free( argv );
}

static void TestMask( void )
{
	char out[128];

	lc_mask_for_log( "+set password \"s3cret\" +connect x ", "s3cret", out, sizeof( out ) );
	CHECK_STR( out, "+set password \"\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\" +connect x " );
	lc_mask_for_log( "abc", NULL, out, sizeof( out ) );
	CHECK_STR( out, "abc" );
}

static void TestRelaunchDecision( void )
{
	/* (VOYAGERSP_TEST_NORELAUNCH is set in main: the exit handler only logs) */
	CHECK( lc_relaunch_pending( ) == -3 );
	CHECK( !lc_from_launcher( ) );
	lc_quit_to_launcher( 0 );
	CHECK( lc_relaunch_pending( ) == -3 );			/* not started from the launcher: Quit quits */
	lc_set_from_launcher( 1 );
	CHECK( lc_from_launcher( ) );
	lc_quit_to_launcher( 1 );
	CHECK( lc_relaunch_pending( ) == -3 );			/* SIGTERM / SIGINT: no relaunch */
	lc_quit_to_launcher( 0 );
	CHECK( lc_relaunch_pending( ) == -1 );			/* to the launcher */
	lc_relaunch_at_exit( 1 );
	CHECK( lc_relaunch_pending( ) == 1 );			/* the File menu's choices replace it */
	lc_quit_to_launcher( 0 );
	CHECK( lc_relaunch_pending( ) == 1 );			/* and a pending one isn't overridden */
	lc_relaunch_at_exit( -2 );
	CHECK( lc_relaunch_pending( ) == -2 );
	lc_relaunch_at_exit( 0 );
	CHECK( lc_relaunch_pending( ) == 0 );
	lc_relaunch_at_exit( -1 );						/* leave a launcher relaunch for the exit handler (log only) */
}

static void TestDelete( void )
{
	const char *root = tu_dir( "launch-delete" );
	char home[LC_MAX_PATH], base[LC_MAX_PATH], safe[LC_MAX_PATH], title[256], info[2048], want[2048];
	lc_delete_summary s;
	char pk0[100], pk1[50], sv[10], sv2[5], cfg[20];

	memset( pk0, 'a', sizeof( pk0 ) );
	memset( pk1, 'b', sizeof( pk1 ) );
	memset( sv, 'c', sizeof( sv ) );
	memset( sv2, 'd', sizeof( sv2 ) );
	memset( cfg, 'e', sizeof( cfg ) );
	lc_path_join( home, sizeof( home ), root, "EliteForce-Marino" );
	lc_path_join( base, sizeof( base ), home, "baseEF" );
	tu_write( base, "pak0.pk3", pk0, sizeof( pk0 ) );
	tu_write( base, "pak1.pk3", pk1, sizeof( pk1 ) );
	tu_write( base, "saves/a.sav", sv, sizeof( sv ) );
	tu_write( base, "saves/sub/b.sav", sv2, sizeof( sv2 ) );
	tu_write( base, "hmconfig.cfg", cfg, sizeof( cfg ) );
	tu_write( home, "launcher.cfg", "", 0 );

	/* the summary */
	CHECK( lc_delete_summarize( home, base, &s ) );
	CHECK( s.files == 6 && s.saves == 2 && s.pk3s == 2 );
	CHECK( s.bytes == 185 && s.pk3Bytes == 150 );
	CHECK_STR( s.externalBase, "" );				/* inside the homepath */
	CHECK( lc_delete_summarize( home, "/elsewhere/BaseEF", &s ) );
	CHECK_STR( s.externalBase, "/elsewhere/BaseEF" );

	lc_delete_texts( home, &s, title, sizeof( title ), info, sizeof( info ) );
	CHECK_STR( title, "Delete saves & settings?" );
	lc_snprintf( want, sizeof( want ),
		"This quits the game and moves this folder to the " LC_TRASH_NAME ":\n%s\n\n"
		"\xe2\x80\xa2 saved games (2 files)\n"
		"\xe2\x80\xa2 settings and key bindings (efspconfig.cfg, hmconfig.cfg, keys, console history)\n"
		"\xe2\x80\xa2 game files copied by the app (2 .pk3, 150 bytes)\n"
		"\nTotal: 6 files, 185 bytes.\n\n"
		"Your game folder at /elsewhere/BaseEF will not be touched.", home );
	CHECK_STR( info, want );
	/* no pk3: no line; inside: the other closing sentence */
	s.pk3s = 0;
	s.externalBase[0] = 0;
	lc_delete_texts( home, &s, title, sizeof( title ), info, sizeof( info ) );
	CHECK( strstr( info, "game files copied" ) == NULL );
	CHECK( strstr( info, "console history)\n\nTotal: 6 files, 185 bytes.\n\nNothing outside this folder is touched." ) != NULL );

	/* the safe root */
	CHECK( lc_safe_home_root( home, "EliteForce-Marino", safe, sizeof( safe ) ) );
	CHECK( lc_safe_home_root( home, "eliteforce-marino", safe, sizeof( safe ) ) );		/* case-insensitive */
	CHECK( !lc_safe_home_root( home, "SomethingElse", safe, sizeof( safe ) ) );
	CHECK( !lc_safe_home_root( tu_path( root, "missing" ), "missing", safe, sizeof( safe ) ) );
	CHECK( !lc_safe_home_root( tu_path( home, "launcher.cfg" ), "launcher.cfg", safe, sizeof( safe ) ) );	/* a file */
#ifndef _WIN32
	{
		const char *link = tu_path( root, "EliteForce-Marino-link" );
		CHECK( symlink( home, link ) == 0 );
		CHECK( !lc_safe_home_root( link, "EliteForce-Marino-link", safe, sizeof( safe ) ) );
	}
#endif

	/* a pending delete blocks the quit-to-launcher relaunch; at exit it only logs (NOTRASH) */
	lc_delete_at_exit( home, "EliteForce-Marino" );
}

int main( void )
{
	putenv( (char *)"VOYAGERSP_TEST_NORELAUNCH=1" );
	putenv( (char *)"VOYAGERSP_TEST_NOTRASH=1" );
	TestSinglePlayer( );
	TestHolomatch( );
	TestCoop( );
	TestBrightnessOverflow( );
	TestClientArgs( );
	TestMask( );
	TestRelaunchDecision( );
	TestDelete( );
	return TEST_DONE( );
}
