/* config parsing, graphics model, presets, brightness, cMod values, names */
#include "lc_settings.h"
#include "lc_test.h"
#include "lc_test_util.h"
#include <stdlib.h>

static void TestCfgParsing( void )
{
	const char *dir = tu_dir( "settings-cfg" );
	lc_cfg c;
	char path[LC_MAX_PATH];

	tu_text( dir, "a.cfg",
		"// comment\n"
		"seta r_fullscreen \"1\"\n"
		"set r_customwidth 1600\r\n"
		"seta  R_CustomHeight   \"900\" \n"
		"seta name \"^1Red Shirt\"\n"
		"seta r_gamma \"\"\n"
		"bogus line\n"
		"seta r_picmip \"5\"\n"
		"seta r_picmip \"1\"\n"
		"seta cg_aspectCorrectCenterHud \"16:9*\"\n"
		"seta r_float \"1.25abc\"\n"
		"seta empty\n"
		"SETA upper \"no\"\n" );
	lc_path_join( path, sizeof( path ), dir, "a.cfg" );
	CHECK( lc_cfg_read( path, &c ) );
	CHECK( lc_cfg_int( &c, "r_fullscreen", 7 ) == 1 );
	CHECK( lc_cfg_int( &c, "r_customwidth", 0 ) == 1600 );
	CHECK( lc_cfg_int( &c, "r_customheight", 0 ) == 900 );		/* names are case-insensitive */
	CHECK_STR( lc_cfg_get( &c, "name" ), "^1Red Shirt" );
	CHECK( lc_cfg_int( &c, "r_picmip", 9 ) == 1 );				/* later lines win */
	CHECK_STR( lc_cfg_get( &c, "r_gamma" ), "" );
	CHECK( lc_cfg_float( &c, "r_gamma", 2.5f ) == 2.5f );		/* empty: the default */
	CHECK( lc_cfg_int( &c, "r_gamma", 9 ) == 0 );				/* present but empty: 0 */
	CHECK( lc_cfg_float( &c, "r_float", 0 ) == 1.25f );
	CHECK_STR( lc_cfg_get( &c, "cg_aspectcorrectcenterhud" ), "16:9*" );
	CHECK_STR( lc_cfg_get( &c, "empty" ), "" );
	CHECK( lc_cfg_get( &c, "upper" ) == NULL );					/* only seta / set, lower case */
	CHECK( lc_cfg_get( &c, "bogus" ) == NULL );
	CHECK( lc_cfg_int( &c, "missing", 42 ) == 42 );
	lc_cfg_free( &c );

	/* Latin-1 file */
	tu_write( dir, "l1.cfg", "seta name \"Se\xf1or\"\n", 17 );
	lc_path_join( path, sizeof( path ), dir, "l1.cfg" );
	CHECK( lc_cfg_read( path, &c ) );
	CHECK_STR( lc_cfg_get( &c, "name" ), "Se\xc3\xb1or" );
	lc_cfg_free( &c );

	lc_path_join( path, sizeof( path ), dir, "missing.cfg" );
	CHECK( !lc_cfg_read( path, &c ) );
	CHECK( c.count == 0 );
	lc_cfg_free( &c );

	lc_config_path( "/x/baseEF", 0, path, sizeof( path ) );
	CHECK( strstr( path, "efspconfig.cfg" ) != NULL );
	lc_config_path( "/x/baseEF", 1, path, sizeof( path ) );
	CHECK( strstr( path, "hmconfig.cfg" ) != NULL );
}

static void TestGraphics( void )
{
	const char *base = tu_dir( "settings-gfx" );
	lc_gfx g;
	int fromConfig = 7;

	/* first run: Best, 1920x1080, Standard brightness */
	lc_gfx_load( base, 0, 0, 0, &g, &fromConfig );
	CHECK( fromConfig == 0 );
	CHECK( g.width == 1920 && g.height == 1080 && !g.fullscreen );
	CHECK( g.highdpi == 1 && g.msaa == 4 && g.picmip == 0 && g.vsync == 1 );
	CHECK( g.brightness == 0 );
	CHECK( lc_gfx_match_preset( &g ) == LC_PRESET_BEST );

	/* a small screen: the default size drops to one that fits */
	lc_gfx_load( base, 0, 1366, 768, &g, &fromConfig );
	CHECK( g.width == 1280 && g.height == 720 );

	/* from the SP config */
	tu_text( base, "efspconfig.cfg",
		"seta r_fullscreen \"1\"\nseta r_mode \"-1\"\nseta r_customwidth \"1440\"\nseta r_customheight \"900\"\n"
		"seta r_highdpi \"0\"\nseta r_fbo \"1\"\nseta r_ext_multisample \"6\"\nseta r_picmip \"7\"\nseta r_swapinterval \"1\"\n" );
	lc_gfx_load( base, 0, 0, 0, &g, &fromConfig );
	CHECK( fromConfig == 1 );
	CHECK( g.fullscreen == 1 && g.width == 1440 && g.height == 900 );
	CHECK( g.highdpi == 0 && g.msaa == 8 && g.picmip == 2 && g.vsync == 1 );	/* 6 → 8, picmip clamped */
	CHECK( lc_gfx_match_preset( &g ) == LC_PRESET_CUSTOM );
	/* Holomatch falls back to the SP file when it has no hmconfig */
	lc_gfx_load( base, 1, 0, 0, &g, &fromConfig );
	CHECK( fromConfig == 1 && g.width == 1440 );
	/* r_fbo 0: no multisampling; r_mode set: the custom size is ignored */
	tu_text( base, "hmconfig.cfg", "seta r_mode \"6\"\nseta r_customwidth \"1000\"\nseta r_customheight \"700\"\n"
		"seta r_fbo \"0\"\nseta r_ext_multisample \"4\"\nseta r_highdpi \"1\"\n" );
	lc_gfx_load( base, 1, 0, 0, &g, &fromConfig );
	CHECK( g.msaa == 0 && g.width == 1920 && g.height == 1080 && g.highdpi == 1 );
	tu_text( base, "hmconfig.cfg", "seta r_fbo \"1\"\nseta r_ext_multisample \"3\"\n" );
	lc_gfx_load( base, 1, 0, 0, &g, &fromConfig );
	CHECK( g.msaa == 4 );		/* 3 → 4 */

	/* presets */
	lc_gfx_apply_preset( &g, LC_PRESET_PERFORMANCE );
	CHECK( g.highdpi == 0 && g.msaa == 0 && g.picmip == 1 && g.vsync == 0 );
	CHECK( lc_gfx_match_preset( &g ) == LC_PRESET_PERFORMANCE );
	lc_gfx_apply_preset( &g, LC_PRESET_BALANCED );
	CHECK( g.highdpi == 0 && g.msaa == 2 && g.picmip == 0 && g.vsync == 1 );
	CHECK( lc_gfx_match_preset( &g ) == LC_PRESET_BALANCED );
	lc_gfx_apply_preset( &g, LC_PRESET_BEST );
	CHECK( g.highdpi == 1 && g.msaa == 4 && g.picmip == 0 && g.vsync == 1 );
	CHECK( lc_gfx_match_preset( &g ) == LC_PRESET_BEST );
	CHECK_STR( lc_preset_title( LC_PRESET_PERFORMANCE ), "Performance" );
	CHECK_STR( lc_preset_title( LC_PRESET_CUSTOM ), "Custom" );
}

static void TestWindowSizes( void )
{
	int sizes[16][2], n;

	n = lc_window_sizes( 1920, 1000, 1920, 1080, sizes, 16 );
	/* 1920x1080 is listed as the current size although it doesn't fit */
	CHECK( n == 6 );
	CHECK( sizes[0][0] == 1024 && sizes[0][1] == 768 );
	CHECK( sizes[n - 1][0] == 1920 && sizes[n - 1][1] == 1080 );
	n = lc_window_sizes( 1366, 740, 1366, 700, sizes, 16 );
	CHECK( sizes[n - 1][0] == 1366 && sizes[n - 1][1] == 700 );			/* appended, not a candidate */
	CHECK( n == 2 && sizes[0][0] == 1280 && sizes[0][1] == 720 );
}

static void TestBrightness( void )
{
	lc_cfg c;
	const char *dir = tu_dir( "settings-bright" );
	char path[LC_MAX_PATH], out[1024];
	int i;

	CHECK( lc_brightness_count( ) == 5 );
	CHECK_STR( lc_brightness_title( 0 ), "Standard" );
	CHECK_STR( lc_brightness_title( 4 ), "Full Bright" );
	CHECK_STR( lc_brightness_title( LC_BRIGHTNESS_CUSTOM ), "Custom" );
	CHECK_STR( lc_brightness_test_id( 2 ), "contrast" );
	CHECK_STR( lc_brightness_help( 1 ), "From cMod: safe values that work well with almost all maps." );
	CHECK_STR( lc_brightness_help( LC_BRIGHTNESS_CUSTOM ), "Set in the game (its Brightness slider, say); Play leaves it as it is." );

	/* an empty config is Standard */
	memset( &c, 0, sizeof( c ) );
	CHECK( lc_brightness_match( &c ) == 0 );
	lc_path_join( path, sizeof( path ), dir, "x.cfg" );

	/* every preset's own args, written as a config, match that preset */
	for ( i = 0; i < 5; i++ ) {
		/* brightness_args with a tiny room writes vsp_brightness.cfg in dir: parse that */
		CHECK( lc_brightness_args( i, 10, dir, out, sizeof( out ) ) == 0 );
		CHECK_STR( out, "+exec vsp_brightness.cfg " );
		lc_path_join( path, sizeof( path ), dir, "vsp_brightness.cfg" );
		CHECK( lc_cfg_read( path, &c ) );
		CHECK( lc_brightness_match( &c ) == i );
		lc_cfg_free( &c );
	}

	/* a changed gamma is Custom; so is a clamp max / texture gamma change */
	tu_text( dir, "c1.cfg", "seta r_gamma \"1.2\"\n" );
	lc_path_join( path, sizeof( path ), dir, "c1.cfg" );
	lc_cfg_read( path, &c );
	CHECK( lc_brightness_match( &c ) == LC_BRIGHTNESS_CUSTOM );
	lc_cfg_free( &c );
	tu_text( dir, "c2.cfg", "seta r_textureGamma \"1.1\"\n" );
	lc_path_join( path, sizeof( path ), dir, "c2.cfg" );
	lc_cfg_read( path, &c );
	CHECK( lc_brightness_match( &c ) == LC_BRIGHTNESS_CUSTOM );
	lc_cfg_free( &c );
	/* Standard needs the overbright bits at 0 */
	tu_text( dir, "c3.cfg", "seta r_overBrightBits \"1\"\n" );
	lc_path_join( path, sizeof( path ), dir, "c3.cfg" );
	lc_cfg_read( path, &c );
	CHECK( lc_brightness_match( &c ) == LC_BRIGHTNESS_CUSTOM );
	lc_cfg_free( &c );
	/* tolerance 0.005 */
	tu_text( dir, "c4.cfg", "seta r_gamma \"1.003\"\nseta r_intensity \"1:0.5\"\n" );
	lc_path_join( path, sizeof( path ), dir, "c4.cfg" );
	lc_cfg_read( path, &c );
	CHECK( lc_brightness_match( &c ) == 0 );
	lc_cfg_free( &c );
	/* Extra Bright: intensity with its fraction */
	tu_text( dir, "c5.cfg", "seta r_gamma 1.4\nseta r_overBrightFactor 1.5\nseta r_mapLightingFactor 2\n"
		"seta r_mapLightingGamma 1.75\nseta r_intensity \"1.1:0.5\"\n" );
	lc_path_join( path, sizeof( path ), dir, "c5.cfg" );
	lc_cfg_read( path, &c );
	CHECK( lc_brightness_match( &c ) == 3 );
	lc_cfg_free( &c );
	tu_text( dir, "c6.cfg", "seta r_gamma 1.4\nseta r_overBrightFactor 1.5\nseta r_mapLightingFactor 2\n"
		"seta r_mapLightingGamma 1.75\nseta r_intensity \"1.1:0.9\"\n" );
	lc_path_join( path, sizeof( path ), dir, "c6.cfg" );
	lc_cfg_read( path, &c );
	CHECK( lc_brightness_match( &c ) == LC_BRIGHTNESS_CUSTOM );
	lc_cfg_free( &c );

	/* inline args, with enough room: §2.6's order and %g numbers */
	CHECK( lc_brightness_args( 0, 1000, dir, out, sizeof( out ) ) == 0 );
	CHECK_STR( out, "+set r_gamma 1 +set r_overBrightFactor 0 +set r_mapLightingFactor 0 +set r_mapLightingGamma 1 "
		"+set r_mapLightingClampMin 0 +set r_mapLightingClampMax 1 +set r_intensity 1 +set r_textureGamma 1 "
		"+set r_overBrightBits 0 +set r_mapOverBrightBits 0 " );
	CHECK( lc_brightness_args( 3, 1000, dir, out, sizeof( out ) ) == 0 );
	CHECK_STR( out, "+set r_gamma 1.4 +set r_overBrightFactor 1.5 +set r_mapLightingFactor 2 +set r_mapLightingGamma 1.75 "
		"+set r_mapLightingClampMin 0 +set r_mapLightingClampMax 1 +set r_intensity 1.1:0.5 +set r_textureGamma 1 " );
	CHECK( lc_brightness_args( 4, 1000, dir, out, sizeof( out ) ) == 0 );
	CHECK( strstr( out, "+set r_mapLightingGamma 10 " ) && strstr( out, "+set r_intensity 1.4:0.5 " ) );
	/* Custom: nothing */
	CHECK( lc_brightness_args( LC_BRIGHTNESS_CUSTOM, 1000, dir, out, sizeof( out ) ) == 0 );
	CHECK_STR( out, "" );

	/* the room rule: len(joined) + 2 < room */
	{
		size_t joined;
		lc_brightness_args( 1, 100000, dir, out, sizeof( out ) );
		joined = strlen( out ) - 1;				/* without the trailing space */
		CHECK( lc_brightness_args( 1, joined + 3, dir, out, sizeof( out ) ) == 0 && strncmp( out, "+set", 4 ) == 0 );
		CHECK( lc_brightness_args( 1, joined + 2, dir, out, sizeof( out ) ) == 0 && strncmp( out, "+exec", 5 ) == 0 );
	}
	/* the cfg file's content */
	{
		char *text;
		lc_brightness_args( 3, 0, dir, out, sizeof( out ) );
		lc_path_join( path, sizeof( path ), dir, "vsp_brightness.cfg" );
		text = lc_read_file( path, NULL );
		CHECK( text != NULL );
		if ( text ) {
			CHECK( strncmp( text, "// Written by the Elite Force launcher: its Brightness preset. Edits here are overwritten.\n"
				"set r_gamma \"1.4\"\nset r_overBrightFactor \"1.5\"\n", 120 ) == 0 );
			CHECK( strstr( text, "set r_intensity \"1.1:0.5\"\n" ) != NULL );
			CHECK( strstr( text, "+set" ) == NULL );
			free( text );
		}
	}
}

static void TestCmod( void )
{
	lc_cfg c;
	const char *dir = tu_dir( "settings-cmod" );
	lc_store *s;
	lc_cmod m;
	char path[LC_MAX_PATH], pak[LC_MAX_PATH];

	/* the bundled pk3 (LC_CMOD_DIR is the test hook) */
	tu_text( dir, "cmod/pakcmod-release-1.pk3", "x" );
	putenv( (char *)"LC_CMOD_DIR=" );
	{
		static char env[LC_MAX_PATH + 16];
		lc_snprintf( env, sizeof( env ), "LC_CMOD_DIR=%s", tu_path( dir, "cmod" ) );
		putenv( env );
	}
	CHECK( lc_cmod_pak_path( pak, sizeof( pak ) ) );
	CHECK( strstr( pak, "pakcmod-release-1.pk3" ) != NULL );

	tu_text( dir, "hm.cfg", "seta cl_cmodModules \"0\"\nseta cg_aspectCorrect \"0\"\nseta cg_aspectCorrectCenterHud \"16:9*\"\n"
		"seta cg_predictWeapons \"0\"\nseta name \"Chakotay\"\n" );
	lc_path_join( path, sizeof( path ), dir, "hm.cfg" );
	lc_cfg_read( path, &c );
	s = lc_store_load( tu_path( dir, "store.cfg" ) );
	lc_cmod_load( &c, s, &m );
	CHECK( m.available == 1 && m.on == 0 && m.predictWeapons == 0 );
	CHECK( m.hudAspect == 1 );							/* from the store (default 1), not hmconfig */
	CHECK_STR( m.hudCenter, "16:9*" );
	lc_cmod_load( NULL, s, &m );
	CHECK( m.on == 1 && m.predictWeapons == 1 );
	CHECK_STR( m.hudCenter, "0" );
	m.hudAspect = 0;
	lc_cmod_save_store( &m, s );
	CHECK( lc_store_get_int( s, LC_KEY_HUD_WIDESCREEN, 5 ) == 0 );
	lc_cmod_load( &c, s, &m );
	CHECK( m.hudAspect == 0 );
	lc_store_free( s );

	{
		char name[64];
		lc_player_name_from_config( &c, "Kathryn Janeway", name, sizeof( name ) );
		CHECK_STR( name, "Chakotay" );
		lc_cfg_free( &c );
		tu_text( dir, "hm2.cfg", "seta name \"UnnamedPlayer\"\n" );
		lc_path_join( path, sizeof( path ), dir, "hm2.cfg" );
		lc_cfg_read( path, &c );
		lc_player_name_from_config( &c, "Kathryn Janeway", name, sizeof( name ) );
		CHECK_STR( name, "Kathryn" );
		lc_player_name_from_config( NULL, "", name, sizeof( name ) );
		CHECK_STR( name, "Crewman" );
		lc_cfg_free( &c );
	}
	putenv( (char *)"LC_CMOD_DIR=" );
}

static void TestNames( void )
{
	char out[64];

	lc_clean_name( "Seven of Nine", out, sizeof( out ) );
	CHECK_STR( out, "Seven of Nine" );
	lc_clean_name( "a\"b+c;d\\e%f", out, sizeof( out ) );
	CHECK_STR( out, "abcdef" );
	lc_clean_name( "Se\xc3\xb1or \xe2\x82\xac" "x", out, sizeof( out ) );	/* non-ASCII is dropped */
	CHECK_STR( out, "Seor x" );
	lc_clean_name( "   ", out, sizeof( out ) );
	CHECK_STR( out, "UnnamedPlayer" );
	lc_clean_name( "", out, sizeof( out ) );
	CHECK_STR( out, "UnnamedPlayer" );
	lc_clean_name( "\xc3\xa9\xc3\xa8", out, sizeof( out ) );
	CHECK_STR( out, "UnnamedPlayer" );
	lc_clean_name( "0123456789012345678901234567890123456789", out, sizeof( out ) );
	CHECK( strlen( out ) == 31 );
	lc_clean_name( "^1Red ^7Shirt", out, sizeof( out ) );	/* colour codes survive */
	CHECK_STR( out, "^1Red ^7Shirt" );

	lc_first_name( "Kathryn Janeway", out, sizeof( out ) );
	CHECK_STR( out, "Kathryn" );
	lc_first_name( "Tuvok", out, sizeof( out ) );
	CHECK_STR( out, "Tuvok" );
	lc_first_name( "", out, sizeof( out ) );
	CHECK_STR( out, "Crewman" );
	lc_first_name( " Nobody", out, sizeof( out ) );
	CHECK_STR( out, "Crewman" );

	lc_coop_safe_name( "Se\xc3\xb1or  Gar\xc3\xa7on", out, sizeof( out ) );
	CHECK_STR( out, "Senor Garcon" );
	lc_coop_safe_name( "  \xc3\x85ngstr\xc3\xb6m \"quoted\";\\%  ", out, sizeof( out ) );
	CHECK_STR( out, "Angstrom quoted" );
	lc_coop_safe_name( "\xc5\x81\xc3\xb3" "d\xc5\xba \xc3\x9f \xc3\x86", out, sizeof( out ) );
	CHECK_STR( out, "Lodz ss AE" );
	lc_coop_safe_name( "\xe2\x82\xac\xe2\x82\xac", out, sizeof( out ) );
	CHECK_STR( out, "" );
	lc_coop_safe_name( "abcdefghijklmnopqrstuvwxyz0123456789", out, sizeof( out ) );
	CHECK( strlen( out ) == 31 );
}

int main( void )
{
	TestCfgParsing( );
	TestGraphics( );
	TestWindowSizes( );
	TestBrightness( );
	TestCmod( );
	TestNames( );
	return TEST_DONE( );
}
