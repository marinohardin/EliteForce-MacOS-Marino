/*
===========================================================================
Launcher core: engine config reading, the graphics model, quality and
brightness presets, cMod values, player names (lc_settings.h).
Spec: launcher-data-coop.md §2.4-2.10, §10.2-10.3.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#include "lc_settings.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ===================================================================== */
/* config files (§2.5)                                                   */

static void CfgSet( lc_cfg *c, const char *name, const char *value )
{
	int i;
	char lower[64];
	size_t k;

	for ( k = 0; name[k] && k < sizeof( lower ) - 1; k++ )
		lower[k] = (char)tolower( (unsigned char)name[k] );
	lower[k] = 0;
	for ( i = 0; i < c->count; i++ ) {
		if ( !strcmp( c->names[i], lower ) ) {		/* later lines win */
			lc_strlcpy( c->values[i], value, sizeof( c->values[i] ) );
			return;
		}
	}
	if ( c->count == c->cap ) {
		int ncap = c->cap ? c->cap * 2 : 64;
		char (*nn)[64] = realloc( c->names, (size_t)ncap * sizeof( *nn ) );
		char (*nv)[256];

		if ( !nn )
			return;
		c->names = nn;
		nv = realloc( c->values, (size_t)ncap * sizeof( *nv ) );
		if ( !nv )
			return;
		c->values = nv;
		c->cap = ncap;
	}
	lc_strlcpy( c->names[c->count], lower, sizeof( c->names[0] ) );
	lc_strlcpy( c->values[c->count], value, sizeof( c->values[0] ) );
	c->count++;
}

static int IsWs( char ch )
{
	return ch == ' ' || ch == '\t' || ch == '\v' || ch == '\f';
}

/* one line of a config file (NSScanner as L:198-222) */
static void CfgLine( lc_cfg *c, char *line )
{
	char *p = line, *cmd, *name, *value = (char *)"";

	while ( IsWs( *p ) )
		p++;
	cmd = p;
	while ( *p && !IsWs( *p ) )
		p++;
	if ( p == cmd )
		return;
	if ( *p )
		*p++ = 0;
	if ( strcmp( cmd, "seta" ) && strcmp( cmd, "set" ) )
		return;
	while ( IsWs( *p ) )
		p++;
	name = p;
	while ( *p && !IsWs( *p ) )
		p++;
	if ( p == name )
		return;
	if ( *p )
		*p++ = 0;
	while ( IsWs( *p ) )
		p++;
	if ( *p == '"' ) {
		char *q;

		p++;
		q = strchr( p, '"' );
		if ( q )
			*q = 0;
		value = p;
	} else {
		value = p;
		while ( *p && !IsWs( *p ) )
			p++;
		*p = 0;
	}
	CfgSet( c, name, value );
}

int lc_cfg_read( const char *path, lc_cfg *out )
{
	size_t len = 0, i;
	char *text = lc_read_file( path, &len ), *conv = NULL, *s, *line;

	memset( out, 0, sizeof( *out ) );
	if ( !text )
		return 0;
	if ( !lc_utf8_valid( text ) ) {			/* Latin-1 */
		conv = malloc( len * 2 + 1 );
		if ( conv ) {
			size_t o = 0;
			for ( i = 0; i < len; i++ ) {
				unsigned char ch = (unsigned char)text[i];
				if ( ch < 0x80 )
					conv[o++] = (char)ch;
				else {
					conv[o++] = (char)( 0xC0 | ( ch >> 6 ) );
					conv[o++] = (char)( 0x80 | ( ch & 0x3F ) );
				}
			}
			conv[o] = 0;
			free( text );
			text = conv;
		}
	}
	for ( s = text; *s; ) {
		line = s;
		while ( *s && *s != '\n' && *s != '\r' )
			s++;
		if ( *s )
			*s++ = 0;
		CfgLine( out, line );
	}
	free( text );
	return 1;
}

const char *lc_cfg_get( const lc_cfg *c, const char *name )
{
	int i;
	char lower[64];
	size_t k;

	if ( !c )
		return NULL;
	for ( k = 0; name[k] && k < sizeof( lower ) - 1; k++ )
		lower[k] = (char)tolower( (unsigned char)name[k] );
	lower[k] = 0;
	for ( i = 0; i < c->count; i++ )
		if ( !strcmp( c->names[i], lower ) )
			return c->values[i];
	return NULL;
}

/* NSString intValue: leading whitespace, sign, digits, saturating */
static int LeadingInt( const char *s )
{
	long long v = 0;
	int neg = 0;

	while ( isspace( (unsigned char)*s ) )
		s++;
	if ( *s == '+' || *s == '-' )
		neg = *s++ == '-';
	while ( *s >= '0' && *s <= '9' ) {
		v = v * 10 + ( *s++ - '0' );
		if ( v > 2147483648LL )
			v = 2147483648LL;
	}
	if ( neg )
		v = -v;
	if ( v > 2147483647LL )
		v = 2147483647LL;
	if ( v < -2147483647LL - 1 )
		v = -2147483647LL - 1;
	return (int)v;
}

int lc_cfg_int( const lc_cfg *c, const char *name, int def )
{
	const char *v = lc_cfg_get( c, name );
	return v ? LeadingInt( v ) : def;
}

static float LeadingFloat( const char *s )
{
	char *end;
	double d;

	while ( isspace( (unsigned char)*s ) )
		s++;
	if ( !( ( *s >= '0' && *s <= '9' ) || *s == '.' || *s == '-' || *s == '+' ) )
		return 0;
	d = strtod( s, &end );
	return end == s ? 0.0f : (float)d;
}

float lc_cfg_float( const lc_cfg *c, const char *name, float def )
{
	const char *v = lc_cfg_get( c, name );
	return v && *v ? LeadingFloat( v ) : def;
}

void lc_cfg_free( lc_cfg *c )
{
	if ( !c )
		return;
	free( c->names );
	free( c->values );
	memset( c, 0, sizeof( *c ) );
}

void lc_config_path( const char *homeBase, int holomatch, char *out, size_t size )
{
	lc_path_join( out, size, homeBase, holomatch ? "hmconfig.cfg" : "efspconfig.cfg" );
}

/* ===================================================================== */
/* brightness (§2.6)                                                     */

static const struct {
	const char *title, *help, *test;
	double gamma, overbright, mapLighting, mapGamma, clampMin;
	const char *intensity;
} brightnessPresets[] = {
	{ "Standard", "This version's own look: the original lighting, without overbright.", "standard",
		1.0, 0.0, 0.0, 1.0, 0.0, "1" },
	{ "Plain", "From cMod: safe values that work well with almost all maps.", "plain",
		1.4, 1.5, 2.0, 1.0, 0.0, "1" },
	{ "Full Contrast", "From cMod: the original Elite Force look. Good, but parts of some maps can be too bright.", "contrast",
		1.3, 2.0, 2.0, 1.0, 0.0, "1" },
	{ "Extra Bright", "From cMod: for better visibility in dark places.", "bright",
		1.4, 1.5, 2.0, 1.75, 0.0, "1.1:0.5" },
	{ "Full Bright", "From cMod: no shadows or lighting effects. Results vary between maps.", "fullbright",
		1.1, 1.0, 2.0, 10.0, 1.0, "1.4:0.5" },
};
#define NUM_BRIGHTNESS ( (int)( sizeof( brightnessPresets ) / sizeof( brightnessPresets[0] ) ) )

int lc_brightness_count( void )
{
	return NUM_BRIGHTNESS;
}

const char *lc_brightness_title( int b )
{
	return b >= 0 && b < NUM_BRIGHTNESS ? brightnessPresets[b].title : "Custom";
}

const char *lc_brightness_help( int b )
{
	return b >= 0 && b < NUM_BRIGHTNESS ? brightnessPresets[b].help :
		"Set in the game (its Brightness slider, say); Play leaves it as it is.";
}

const char *lc_brightness_test_id( int b )
{
	return b >= 0 && b < NUM_BRIGHTNESS ? brightnessPresets[b].test : "custom";
}

static int Near( float a, float b )
{
	return fabsf( a - b ) < 0.005f;
}

/* r_intensity's value and cMod's optional ":fraction" */
static void SplitIntensity( const char *s, float *value, float *fraction )
{
	const char *colon = strchr( s, ':' );

	*value = ( colon ? colon != s : *s ) ? LeadingFloat( s ) : 1.0f;
	*fraction = colon ? LeadingFloat( colon + 1 ) : 1.0f;
}

int lc_brightness_match( const lc_cfg *cfg )
{
	float obf = lc_cfg_float( cfg, "r_overbrightfactor", 0 ), mlf = lc_cfg_float( cfg, "r_maplightingfactor", 0 );
	float in, inFrac;
	const char *inten = lc_cfg_get( cfg, "r_intensity" );
	int i;

	SplitIntensity( inten ? inten : "1", &in, &inFrac );
	if ( !Near( lc_cfg_float( cfg, "r_maplightingclampmax", 1 ), 1 ) || !Near( lc_cfg_float( cfg, "r_texturegamma", 1 ), 1 ) )
		return LC_BRIGHTNESS_CUSTOM;
	for ( i = 0; i < NUM_BRIGHTNESS; i++ ) {
		float pin, pinFrac;

		SplitIntensity( brightnessPresets[i].intensity, &pin, &pinFrac );
		if ( !Near( lc_cfg_float( cfg, "r_gamma", 1 ), (float)brightnessPresets[i].gamma ) ||
			!Near( obf > 0 ? obf : 0, (float)brightnessPresets[i].overbright ) ||
			!Near( mlf > 0 ? mlf : 0, (float)brightnessPresets[i].mapLighting ) ||
			!Near( lc_cfg_float( cfg, "r_maplightinggamma", 1 ), (float)brightnessPresets[i].mapGamma ) ||
			!Near( lc_cfg_float( cfg, "r_maplightingclampmin", 0 ), (float)brightnessPresets[i].clampMin ) ||
			!Near( in, pin ) || ( pin != 1.0f && !Near( inFrac, pinFrac ) ) )
			continue;
		/* with a factor unset, its overbright bits decide; Standard has them at 0 */
		if ( ( brightnessPresets[i].overbright == 0 && lc_cfg_int( cfg, "r_overbrightbits", 0 ) != 0 ) ||
			( brightnessPresets[i].mapLighting == 0 && lc_cfg_int( cfg, "r_mapoverbrightbits", 0 ) != 0 ) )
			continue;
		return i;
	}
	return LC_BRIGHTNESS_CUSTOM;
}

/* the name/value pairs of preset b (BrightnessSets): up to 10; returns the count */
static int BrightnessSets( int b, char names[10][32], char values[10][16] )
{
	int n = 0;

#define SET_S( nm, v ) do { lc_strlcpy( names[n], nm, 32 ); lc_strlcpy( values[n], v, 16 ); n++; } while ( 0 )
#define SET_G( nm, v ) do { lc_strlcpy( names[n], nm, 32 ); lc_snprintf( values[n], 16, "%g", (double)( v ) ); n++; } while ( 0 )
	if ( b < 0 || b >= NUM_BRIGHTNESS )
		return 0;
	SET_G( "r_gamma", brightnessPresets[b].gamma );
	SET_G( "r_overBrightFactor", brightnessPresets[b].overbright );
	SET_G( "r_mapLightingFactor", brightnessPresets[b].mapLighting );
	SET_G( "r_mapLightingGamma", brightnessPresets[b].mapGamma );
	SET_G( "r_mapLightingClampMin", brightnessPresets[b].clampMin );
	SET_S( "r_mapLightingClampMax", "1" );
	SET_S( "r_intensity", brightnessPresets[b].intensity );
	SET_S( "r_textureGamma", "1" );
	if ( brightnessPresets[b].overbright == 0 )
		SET_S( "r_overBrightBits", "0" );
	if ( brightnessPresets[b].mapLighting == 0 )
		SET_S( "r_mapOverBrightBits", "0" );
#undef SET_S
#undef SET_G
	return n;
}

int lc_brightness_args( int b, size_t room, const char *homeBase, char *out, size_t outSize )
{
	char names[10][32], values[10][16];
	int n = BrightnessSets( b, names, values ), i;
	size_t joined = 0;
	char text[1024];

	if ( outSize )
		out[0] = 0;
	if ( !n )
		return 0;
	/* "+set name value" joined by single spaces */
	for ( i = 0; i < n; i++ )
		joined += ( i ? 1 : 0 ) + 5 + strlen( names[i] ) + 1 + strlen( values[i] );	/* "+set name value" */
	if ( joined + 2 < room ) {
		for ( i = 0; i < n; i++ ) {
			char tok[96];

			lc_snprintf( tok, sizeof( tok ), "+set %s %s ", names[i], values[i] );
			if ( lc_strlcat( out, tok, outSize ) >= outSize )
				return 1;
		}
		return 0;
	}
	/* the same settings in a cfg file to +exec */
	{
		char path[LC_MAX_PATH];
		FILE *f;
		int ok = 0;

		lc_strlcpy( text, "// Written by the Elite Force launcher: its Brightness preset. Edits here are overwritten.\n",
			sizeof( text ) );
		for ( i = 0; i < n; i++ ) {
			char ln[96];

			lc_snprintf( ln, sizeof( ln ), "set %s \"%s\"\n", names[i], values[i] );
			lc_strlcat( text, ln, sizeof( text ) );
		}
		lc_path_join( path, sizeof( path ), homeBase, "vsp_brightness.cfg" );
		lc_mkdir_p( homeBase );
		if ( ( f = lc_fopen( path, "wb" ) ) ) {
			fwrite( text, 1, strlen( text ), f );
			ok = fclose( f ) == 0;
		}
		if ( !ok ) {
			lc_log( "Couldn't write %s: brightness left as it is", path );
			return 1;
		}
		lc_log( "Launcher: brightness in vsp_brightness.cfg (the command line is full)" );
		lc_strlcpy( out, "+exec vsp_brightness.cfg ", outSize );
		return 0;
	}
}

/* ===================================================================== */
/* graphics (§2.4, §2.5)                                                 */

static const struct { lc_preset preset; int highdpi, msaa, picmip, vsync; } presets[] = {
	{ LC_PRESET_PERFORMANCE,	0, 0, 1, 0 },
	{ LC_PRESET_BALANCED,		0, 2, 0, 1 },
	{ LC_PRESET_BEST,			1, 4, 0, 1 },
};

static const int windowSizes[][2] = {
	{ 1024, 768 }, { 1280, 720 }, { 1280, 800 }, { 1440, 900 }, { 1600, 900 },
	{ 1680, 1050 }, { 1920, 1080 }, { 1920, 1200 }, { 2560, 1440 }, { 2560, 1600 },
};
#define NUM_SIZES ( (int)( sizeof( windowSizes ) / sizeof( windowSizes[0] ) ) )

void lc_gfx_load( const char *homeBase, int holomatch, int screenW, int screenH,
	lc_gfx *g, int *fromConfig )
{
	char path[LC_MAX_PATH];
	lc_cfg cfg;
	int fc;

	lc_config_path( homeBase, holomatch, path, sizeof( path ) );
	lc_cfg_read( path, &cfg );
	if ( cfg.count == 0 ) {
		lc_cfg_free( &cfg );
		lc_config_path( homeBase, !holomatch, path, sizeof( path ) );
		lc_cfg_read( path, &cfg );
	}
	fc = cfg.count > 0;
	if ( fromConfig )
		*fromConfig = fc;
	memset( g, 0, sizeof( *g ) );
	g->brightness = lc_brightness_match( &cfg );		/* first run: Standard */
	g->fullscreen = lc_cfg_int( &cfg, "r_fullscreen", 0 ) != 0;
	g->width = 1920;
	g->height = 1080;
	if ( lc_cfg_int( &cfg, "r_mode", -1 ) == -1 && lc_cfg_int( &cfg, "r_customwidth", 0 ) >= 640 &&
		lc_cfg_int( &cfg, "r_customheight", 0 ) >= 480 ) {
		g->width = lc_cfg_int( &cfg, "r_customwidth", 1920 );
		g->height = lc_cfg_int( &cfg, "r_customheight", 1080 );
	} else if ( screenW > 0 && screenH > 0 && ( g->width > screenW || g->height > screenH - 28 ) ) {
		/* the default doesn't fit this screen: the biggest listed size that does */
		int i;

		g->width = 1024;
		g->height = 768;
		for ( i = NUM_SIZES - 1; i >= 0; i-- ) {
			if ( windowSizes[i][0] <= screenW && windowSizes[i][1] <= screenH - 28 ) {
				g->width = windowSizes[i][0];
				g->height = windowSizes[i][1];
				break;
			}
		}
	}
	if ( fc ) {
		g->highdpi = lc_cfg_int( &cfg, "r_highdpi", 0 ) != 0;
		g->msaa = lc_cfg_int( &cfg, "r_fbo", 0 ) ? lc_cfg_int( &cfg, "r_ext_multisample", 0 ) : 0;
		g->picmip = lc_cfg_int( &cfg, "r_picmip", 0 );
		if ( g->picmip < 0 )
			g->picmip = 0;
		if ( g->picmip > 2 )
			g->picmip = 2;
		g->vsync = lc_cfg_int( &cfg, "r_swapinterval", 0 ) != 0;
	} else {
		/* first run: the Best preset */
		g->highdpi = presets[2].highdpi;
		g->msaa = presets[2].msaa;
		g->picmip = presets[2].picmip;
		g->vsync = presets[2].vsync;
	}
	if ( g->msaa != 0 && g->msaa != 2 && g->msaa != 4 && g->msaa != 8 )
		g->msaa = g->msaa > 4 ? 8 : g->msaa > 2 ? 4 : 2;
	lc_cfg_free( &cfg );
}

lc_preset lc_gfx_match_preset( const lc_gfx *g )
{
	size_t i;

	for ( i = 0; i < sizeof( presets ) / sizeof( presets[0] ); i++ )
		if ( presets[i].highdpi == g->highdpi && presets[i].msaa == g->msaa &&
			presets[i].picmip == g->picmip && presets[i].vsync == g->vsync )
			return presets[i].preset;
	return LC_PRESET_CUSTOM;
}

void lc_gfx_apply_preset( lc_gfx *g, lc_preset p )
{
	size_t i;

	for ( i = 0; i < sizeof( presets ) / sizeof( presets[0] ); i++ ) {
		if ( presets[i].preset == p ) {
			g->highdpi = presets[i].highdpi;
			g->msaa = presets[i].msaa;
			g->picmip = presets[i].picmip;
			g->vsync = presets[i].vsync;
		}
	}
}

const char *lc_preset_title( lc_preset p )
{
	switch ( p ) {
	case LC_PRESET_PERFORMANCE:	return "Performance";
	case LC_PRESET_BALANCED:	return "Balanced";
	case LC_PRESET_BEST:		return "Best";
	default:					return "Custom";
	}
}

const char *lc_preset_help( lc_preset p )
{
	switch ( p ) {
	case LC_PRESET_PERFORMANCE:	return "Fastest: no anti-aliasing, medium texture detail, no V-Sync.";
	case LC_PRESET_BALANCED:	return "A good middle: 2\xc3\x97 anti-aliasing, high texture detail, V-Sync.";
	case LC_PRESET_BEST:		return "Sharpest: full resolution, 4\xc3\x97 anti-aliasing, high texture detail, V-Sync.";
	default:					return "Your own mix of the settings below.";
	}
}

int lc_window_sizes( int maxW, int maxH, int curW, int curH, int (*out)[2], int max )
{
	int n = 0, i, haveCurrent = 0;

	for ( i = 0; i < NUM_SIZES; i++ ) {
		int w = windowSizes[i][0], h = windowSizes[i][1];

		if ( ( w > maxW || h > maxH ) && !( w == curW && h == curH ) )
			continue;
		if ( n < max ) {
			out[n][0] = w;
			out[n][1] = h;
			n++;
		}
		haveCurrent |= ( w == curW && h == curH );
	}
	if ( !haveCurrent && n < max ) {
		out[n][0] = curW;
		out[n][1] = curH;
		n++;
	}
	return n;
}

/* ===================================================================== */
/* cMod (§2.8, §2.10)                                                    */

typedef struct { char *out; size_t size; const char *dir; int found; } cmodScan;

static int CmodCB( const char *name, int isDir, void *user )
{
	cmodScan *c = user;
	size_t n = strlen( name );

	if ( !isDir && n > 4 && !lc_stricmp( name + n - 4, ".pk3" ) ) {
		if ( c->out )
			lc_path_join( c->out, c->size, c->dir, name );
		c->found = 1;
		return 1;
	}
	return 0;
}

int lc_cmod_pak_path( char *out, size_t size )
{
	char dir[LC_MAX_PATH], up[LC_MAX_PATH];
	cmodScan c;
	const char *over = lc_env( "LC_CMOD_DIR" );		/* tests */

	c.out = out;
	c.size = size;
	c.found = 0;
	if ( out && size )
		out[0] = 0;
	if ( over ) {
		c.dir = over;
		lc_list_dir( over, CmodCB, &c );
		return c.found;
	}
	lc_path_join( dir, sizeof( dir ), lc_exe_dir( ), "cmod" );
	c.dir = dir;
	if ( lc_is_dir( dir ) )
		lc_list_dir( dir, CmodCB, &c );
	if ( c.found )
		return 1;
#ifndef _WIN32
	lc_path_join( up, sizeof( up ), lc_exe_dir( ), "../share/eliteforce-marino/cmod" );
	c.dir = up;
	if ( lc_is_dir( up ) )
		lc_list_dir( up, CmodCB, &c );
#else
	( void )up;
#endif
	return c.found;
}

void lc_cmod_load( const lc_cfg *hm, lc_store *s, lc_cmod *out )
{
	const char *center = lc_cfg_get( hm, "cg_aspectcorrectcenterhud" );

	memset( out, 0, sizeof( *out ) );
	out->available = lc_cmod_pak_path( NULL, 0 );
	out->on = lc_cfg_int( hm, "cl_cmodmodules", 1 ) != 0;
	out->hudAspect = s ? lc_store_get_bool( s, LC_KEY_HUD_WIDESCREEN, 1 ) : 1;
	lc_strlcpy( out->hudCenter, center && *center ? center : "0", sizeof( out->hudCenter ) );
	out->predictWeapons = lc_cfg_int( hm, "cg_predictweapons", 1 ) != 0;
}

void lc_cmod_save_store( const lc_cmod *c, lc_store *s )
{
	lc_store_set_int( s, LC_KEY_HUD_WIDESCREEN, c->hudAspect ? 1 : 0 );
}

/* ===================================================================== */
/* names (§2.8, §6.2)                                                    */

void lc_clean_name( const char *in, char *out, size_t size )
{
	size_t n = 0;
	int blank = 1;
	const unsigned char *p = (const unsigned char *)( in ? in : "" );

	if ( !size )
		return;
	for ( ; *p && n < 31 && n + 1 < size; p++ ) {
		unsigned char c = *p;

		if ( c < 32 || c > 126 || c == '"' || c == '+' || c == ';' || c == '\\' || c == '%' )
			continue;
		out[n++] = (char)c;
		if ( c != ' ' )
			blank = 0;
	}
	out[n] = 0;
	if ( blank )
		lc_strlcpy( out, "UnnamedPlayer", size );
}

void lc_first_name( const char *fullName, char *out, size_t size )
{
	const char *sp;
	size_t n;

	if ( !size )
		return;
	if ( !fullName )
		fullName = "";
	sp = strchr( fullName, ' ' );
	n = sp ? (size_t)( sp - fullName ) : strlen( fullName );
	if ( !n ) {
		lc_strlcpy( out, "Crewman", size );
		return;
	}
	if ( n >= size )
		n = size - 1;
	memcpy( out, fullName, n );
	out[n] = 0;
}

/* Latin-1 Supplement U+00C0..U+00FF and Latin Extended-A U+0100..U+017F as ASCII */
static const char *const latin1Fold[64] = {
	"A", "A", "A", "A", "A", "A", "AE", "C", "E", "E", "E", "E", "I", "I", "I", "I",
	"D", "N", "O", "O", "O", "O", "O", "x", "O", "U", "U", "U", "U", "Y", "TH", "ss",
	"a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
	"d", "n", "o", "o", "o", "o", "o", "/", "o", "u", "u", "u", "u", "y", "th", "y",
};
static const char *const latinAFold[128] = {
	"A", "a", "A", "a", "A", "a", "C", "c", "C", "c", "C", "c", "C", "c", "D", "d", "D", "d",
	"E", "e", "E", "e", "E", "e", "E", "e", "E", "e",
	"G", "g", "G", "g", "G", "g", "G", "g",
	"H", "h", "H", "h",
	"I", "i", "I", "i", "I", "i", "I", "i", "I", "i",
	"IJ", "ij",
	"J", "j", "K", "k", "k",
	"L", "l", "L", "l", "L", "l", "L", "l", "L", "l",
	"N", "n", "N", "n", "N", "n", "'n", "N", "n",
	"O", "o", "O", "o", "O", "o", "OE", "oe",
	"R", "r", "R", "r", "R", "r",
	"S", "s", "S", "s", "S", "s", "S", "s",
	"T", "t", "T", "t", "T", "t",
	"U", "u", "U", "u", "U", "u", "U", "u", "U", "u", "U", "u",
	"W", "w", "Y", "y", "Y",
	"Z", "z", "Z", "z", "Z", "z", "s",
};

void lc_coop_safe_name( const char *in, char *out, size_t size )
{
	const unsigned char *p = (const unsigned char *)( in ? in : "" );
	size_t n = 0;

	if ( !size )
		return;
	while ( *p && n < 31 && n + 1 < size ) {
		unsigned cp;
		const char *rep, *r;
		char single[2] = { 0, 0 };

		if ( *p < 0x80 ) {
			cp = *p++;
		} else if ( ( *p & 0xE0 ) == 0xC0 && ( p[1] & 0xC0 ) == 0x80 ) {
			cp = ( ( *p & 0x1Fu ) << 6 ) | ( p[1] & 0x3Fu );
			p += 2;
		} else if ( ( *p & 0xF0 ) == 0xE0 && ( p[1] & 0xC0 ) == 0x80 && ( p[2] & 0xC0 ) == 0x80 ) {
			cp = ( ( *p & 0x0Fu ) << 12 ) | ( ( p[1] & 0x3Fu ) << 6 ) | ( p[2] & 0x3Fu );
			p += 3;
		} else if ( ( *p & 0xF8 ) == 0xF0 ) {
			cp = 0x10000;
			p++;
			while ( ( *p & 0xC0 ) == 0x80 )
				p++;
		} else {
			p++;
			continue;
		}
		if ( cp >= 0xC0 && cp <= 0xFF )
			rep = latin1Fold[cp - 0xC0];
		else if ( cp >= 0x100 && cp <= 0x17F )
			rep = latinAFold[cp - 0x100];
		else if ( cp == 0xA0 )
			rep = " ";
		else if ( cp < 0x80 ) {
			single[0] = (char)cp;
			rep = single;
		} else
			continue;
		for ( r = rep; *r && n < 31 && n + 1 < size; r++ ) {
			unsigned char c = (unsigned char)*r;

			if ( c < ' ' || c > '~' || c == '"' || c == '\\' || c == ';' || c == '%' )
				continue;
			if ( c == ' ' && ( n == 0 || out[n - 1] == ' ' ) )
				continue;
			out[n++] = (char)c;
		}
	}
	while ( n && out[n - 1] == ' ' )
		n--;
	out[n] = 0;
}

void lc_player_name_from_config( const lc_cfg *hm, const char *fullUserName, char *out, size_t size )
{
	const char *n = lc_cfg_get( hm, "name" );

	if ( n && strcmp( n, "UnnamedPlayer" ) )
		lc_strlcpy( out, n, size );
	else
		lc_first_name( fullUserName, out, size );
}
