/*
===========================================================================
Launcher core: stvef:// links and their registration (lc_links.h).
Spec: launcher-data-coop.md §7.1, §10.8; servers.md §8.9.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#include "lc_links.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#	ifndef WIN32_LEAN_AND_MEAN
#		define WIN32_LEAN_AND_MEAN
#	endif
#	include <windows.h>
#endif

/* ===================================================================== */
/* parsing                                                               */

static int HexVal( int c )
{
	if ( c >= '0' && c <= '9' )
		return c - '0';
	if ( c >= 'a' && c <= 'f' )
		return c - 'a' + 10;
	if ( c >= 'A' && c <= 'F' )
		return c - 'A' + 10;
	return -1;
}

/* percent-decoding as -stringByRemovingPercentEncoding: 0 on a bad escape or
   when the result isn't valid UTF-8 / doesn't fit */
static int PercentDecode( const char *in, size_t n, char *out, size_t size )
{
	size_t o = 0, i;

	for ( i = 0; i < n; i++ ) {
		int c = (unsigned char)in[i];

		if ( c == '%' ) {
			int h, l;

			if ( i + 2 >= n )
				return 0;
			h = HexVal( in[i + 1] );
			l = HexVal( in[i + 2] );
			if ( h < 0 || l < 0 )
				return 0;
			c = h * 16 + l;
			i += 2;
		}
		if ( o + 1 >= size )
			return 0;
		out[o++] = (char)c;
	}
	out[o] = 0;
	return lc_utf8_valid( out );
}

/* ^([A-Za-z0-9.-]+|\[[0-9A-Fa-f:.]+\])(:[0-9]{1,5})?$ and 1..255 chars */
static int ValidAddress( const char *a )
{
	size_t n = strlen( a ), i = 0;
	int digits;

	if ( !n || n > 255 )
		return 0;
	if ( a[0] == '[' ) {
		i = 1;
		while ( a[i] && a[i] != ']' ) {
			if ( !( isxdigit( (unsigned char)a[i] ) || a[i] == ':' || a[i] == '.' ) )
				return 0;
			i++;
		}
		if ( a[i] != ']' || i == 1 )
			return 0;
		i++;
	} else {
		while ( a[i] && ( ( isalnum( (unsigned char)a[i] ) && (unsigned char)a[i] < 128 ) || a[i] == '.' || a[i] == '-' ) )
			i++;
		if ( i == 0 )
			return 0;
	}
	if ( !a[i] )
		return 1;
	if ( a[i] != ':' )
		return 0;
	i++;
	for ( digits = 0; a[i]; i++, digits++ )
		if ( a[i] < '0' || a[i] > '9' )
			return 0;
	return digits >= 1 && digits <= 5;
}

/* after "stvef:" and the slashes: points at the rest, or NULL */
static const char *LinkRest( const char *link )
{
	const char *rest;

	if ( !link || strlen( link ) < 7 || lc_strnicmp( link, "stvef:", 6 ) )
		return NULL;
	rest = link + 6;
	while ( *rest == '/' )
		rest++;
	return rest;
}

/* text up to the first ? # / decoded; 0 if undecodable */
static int DecodedAddr( const char *s, char *out, size_t size )
{
	size_t n = strcspn( s, "?#/" );
	return PercentDecode( s, n, out, size );
}

/* the "password" (or "pw") parameter of the link's query, kept only if it is safe
   to put in quotes on the command line */
static void LinkPassword( const char *link, char *password, size_t pwSize )
{
	const char *q = strchr( link, '?' ), *end;

	if ( !password || !pwSize )
		return;
	password[0] = 0;
	if ( !q )
		return;
	q++;
	end = strchr( q, '#' );
	if ( !end )
		end = q + strlen( q );
	while ( q < end ) {
		const char *amp = memchr( q, '&', (size_t)( end - q ) );
		const char *stop = amp ? amp : end;
		const char *eq = memchr( q, '=', (size_t)( stop - q ) );

		if ( eq && ( ( eq - q == 8 && !lc_strnicmp( q, "password", 8 ) ) || ( eq - q == 2 && !lc_strnicmp( q, "pw", 2 ) ) ) ) {
			char tmp[256];
			size_t i;

			if ( PercentDecode( eq + 1, (size_t)( stop - ( eq + 1 ) ), tmp, sizeof( tmp ) ) && strlen( tmp ) < pwSize && strlen( tmp ) < 64 ) {
				int ok = 1;

				for ( i = 0; tmp[i]; i++ ) {
					unsigned char c = (unsigned char)tmp[i];
					if ( c < 33 || c > 126 || c == '"' || c == ';' || c == '\\' || c == '%' )
						ok = 0;
				}
				if ( ok )
					lc_strlcpy( password, tmp, pwSize );
			}
			return;
		}
		q = stop + 1;
	}
}

int lc_link_address( const char *link, char *out, size_t size, char *password, size_t pwSize )
{
	const char *rest = LinkRest( link );
	char addr[512];

	if ( password && pwSize )
		password[0] = 0;
	if ( !rest )
		return 0;
	if ( !lc_strnicmp( rest, "connect/", 8 ) ) {
		if ( !DecodedAddr( rest + 8, addr, sizeof( addr ) ) )
			return 0;
	} else {
		const char *s1 = strchr( rest, ';' ), *s2, *e;
		char f0[300], f1[300];

		if ( !s1 )
			return 0;
		s2 = strchr( s1 + 1, ';' );
		e = s2 ? s2 : s1 + 1 + strlen( s1 + 1 );
		if ( (size_t)( s1 - rest ) >= sizeof( f0 ) || (size_t)( e - ( s1 + 1 ) ) >= sizeof( f1 ) )
			return 0;
		memcpy( f0, rest, (size_t)( s1 - rest ) );
		f0[s1 - rest] = 0;
		memcpy( f1, s1 + 1, (size_t)( e - ( s1 + 1 ) ) );
		f1[e - ( s1 + 1 )] = 0;
		lc_snprintf( addr, sizeof( addr ), "%s:%s", f0, f1 );
	}
	if ( !ValidAddress( addr ) )
		return 0;
	lc_strlcpy( out, addr, size );
	if ( password )
		LinkPassword( link, password, pwSize );
	return 1;
}

int lc_coop_link_address( const char *link, char *out, size_t size )
{
	const char *rest = LinkRest( link );
	char addr[512];

	if ( out && size )
		out[0] = 0;
	if ( !rest || lc_strnicmp( rest, "coop/", 5 ) )
		return -1;
	if ( !DecodedAddr( rest + 5, addr, sizeof( addr ) ) || !ValidAddress( addr ) )
		return 0;
	lc_strlcpy( out, addr, size );
	return 1;
}

int lc_is_link_arg( const char *arg )
{
	return arg && !lc_strnicmp( arg, "stvef:", 6 );
}

const char *lc_link_from_args( int argc, char **argv )
{
	int i;

	for ( i = 1; i < argc; i++ ) {
		if ( !strcmp( argv[i], "--uri" ) && i + 1 < argc )
			return lc_is_link_arg( argv[i + 1] ) ? argv[i + 1] : NULL;
		if ( lc_is_link_arg( argv[i] ) )
			return argv[i];
	}
	return NULL;
}

/* ===================================================================== */
/* registration                                                          */

#ifdef _WIN32

static void ToWide( const char *s, wchar_t *out, int n )
{
	if ( !MultiByteToWideChar( CP_UTF8, 0, s, -1, out, n ) )
		out[0] = 0;
}

static int SetKeyString( HKEY root, const wchar_t *path, const wchar_t *name, const wchar_t *value )
{
	HKEY k;
	LONG r;

	if ( RegCreateKeyExW( root, path, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL ) != ERROR_SUCCESS )
		return 0;
	r = RegSetValueExW( k, name, 0, REG_SZ, (const BYTE *)value, (DWORD)( ( wcslen( value ) + 1 ) * sizeof( wchar_t ) ) );
	RegCloseKey( k );
	return r == ERROR_SUCCESS;
}

static void OurCommand( wchar_t *out, size_t n )
{
	wchar_t exe[LC_MAX_PATH];

	ToWide( lc_self_command( ), exe, LC_MAX_PATH );
	_snwprintf( out, n, L"\"%ls\" --uri \"%%1\"", exe );
	out[n - 1] = 0;
}

int lc_link_handler_state( char *otherApp, size_t size )
{
	HKEY k;
	wchar_t cmd[2048], ours[2048];
	DWORD len = sizeof( cmd ) - sizeof( wchar_t ), type;

	if ( otherApp && size )
		otherApp[0] = 0;
	/* the merged view: a machine-wide registration counts too */
	if ( RegOpenKeyExW( HKEY_CLASSES_ROOT, L"stvef\\shell\\open\\command", 0, KEY_READ, &k ) != ERROR_SUCCESS )
		return 0;
	if ( RegQueryValueExW( k, NULL, NULL, &type, (BYTE *)cmd, &len ) != ERROR_SUCCESS || ( type != REG_SZ && type != REG_EXPAND_SZ ) ) {
		RegCloseKey( k );
		return 0;
	}
	RegCloseKey( k );
	cmd[len / sizeof( wchar_t )] = 0;
	OurCommand( ours, 2048 );
	if ( !_wcsicmp( cmd, ours ) )
		return 1;
	if ( otherApp && size ) {
		/* the program: the quoted path, or the first word */
		wchar_t prog[LC_MAX_PATH];
		const wchar_t *s = cmd, *e, *base;
		char utf8[LC_MAX_PATH];

		if ( *s == L'"' ) {
			s++;
			e = wcschr( s, L'"' );
		} else
			e = wcschr( s, L' ' );
		if ( !e )
			e = s + wcslen( s );
		if ( (size_t)( e - s ) >= LC_MAX_PATH )
			e = s + LC_MAX_PATH - 1;
		wcsncpy( prog, s, (size_t)( e - s ) );
		prog[e - s] = 0;
		base = wcsrchr( prog, L'\\' );
		base = base ? base + 1 : prog;
		if ( WideCharToMultiByte( CP_UTF8, 0, base, -1, utf8, sizeof( utf8 ), NULL, NULL ) )
			lc_strlcpy( otherApp, utf8, size );
	}
	return 2;
}

int lc_link_register( char *err, size_t errSize )
{
	wchar_t cmd[2048], exe[LC_MAX_PATH], icon[LC_MAX_PATH + 8];

	OurCommand( cmd, 2048 );
	ToWide( lc_self_command( ), exe, LC_MAX_PATH );
	_snwprintf( icon, LC_MAX_PATH + 8, L"\"%ls\",0", exe );
	icon[LC_MAX_PATH + 7] = 0;
	if ( !SetKeyString( HKEY_CURRENT_USER, L"Software\\Classes\\stvef", NULL, L"URL:Star Trek Voyager: Elite Force server" )
		|| !SetKeyString( HKEY_CURRENT_USER, L"Software\\Classes\\stvef", L"URL Protocol", L"" )
		|| !SetKeyString( HKEY_CURRENT_USER, L"Software\\Classes\\stvef\\DefaultIcon", NULL, icon )
		|| !SetKeyString( HKEY_CURRENT_USER, L"Software\\Classes\\stvef\\shell\\open\\command", NULL, cmd ) ) {
		if ( err && errSize )
			lc_snprintf( err, errSize, "Couldn't write the registry keys (HKCU\\Software\\Classes\\stvef)." );
		return 0;
	}
	lc_log( "Link: registered stvef: for this app" );
	return 1;
}

#else	/* Linux (and the macOS test build) */

#define DESKTOP_NAME "eliteforce-marino-stvef.desktop"

static void AppsDir( char *out, size_t size )
{
	const char *xdg = lc_env( "XDG_DATA_HOME" );
	char base[LC_MAX_PATH];

	if ( xdg )
		lc_strlcpy( base, xdg, sizeof( base ) );
	else
		lc_path_join( base, sizeof( base ), lc_home_dir( ), ".local/share" );
	lc_path_join( out, size, base, "applications" );
}

/* the first line of a command's output, "" if it fails */
static void CommandLine1( const char *cmd, char *out, size_t size )
{
	FILE *p = popen( cmd, "r" );

	out[0] = 0;
	if ( !p )
		return;
	if ( fgets( out, (int)size, p ) )
		lc_trim( out );
	else
		out[0] = 0;
	pclose( p );
}

int lc_link_handler_state( char *otherApp, size_t size )
{
	char cur[256];

	if ( otherApp && size )
		otherApp[0] = 0;
	CommandLine1( "xdg-mime query default x-scheme-handler/stvef 2>/dev/null", cur, sizeof( cur ) );
	if ( !cur[0] )
		return 0;
	if ( !strcmp( cur, DESKTOP_NAME ) )
		return 1;
	if ( otherApp && size ) {
		size_t n = strlen( cur );

		if ( n > 8 && !strcmp( cur + n - 8, ".desktop" ) )
			cur[n - 8] = 0;
		lc_strlcpy( otherApp, cur, size );
	}
	return 2;
}

/* an Exec= argument in double quotes (the desktop entry spec's escapes) */
static void ExecQuote( const char *s, char *out, size_t size )
{
	size_t o = 0;

	if ( size < 3 )
		return;
	out[o++] = '"';
	for ( ; *s && o + 5 < size; s++ ) {
		if ( *s == '\\' ) {		/* quoting escape, then the string escape of each backslash */
			out[o++] = '\\'; out[o++] = '\\'; out[o++] = '\\'; out[o++] = '\\';
		} else if ( *s == '"' || *s == '`' || *s == '$' ) {
			out[o++] = '\\'; out[o++] = '\\'; out[o++] = *s;
		} else
			out[o++] = *s;
	}
	out[o++] = '"';
	out[o] = 0;
}

int lc_link_register( char *err, size_t errSize )
{
	char dir[LC_MAX_PATH], path[LC_MAX_PATH], exec[LC_MAX_PATH * 2], text[LC_MAX_PATH * 3];
	char cmd[LC_MAX_PATH * 2];

	AppsDir( dir, sizeof( dir ) );
	lc_path_join( path, sizeof( path ), dir, DESKTOP_NAME );
	ExecQuote( lc_self_command( ), exec, sizeof( exec ) );
	lc_snprintf( text, sizeof( text ),
		"[Desktop Entry]\n"
		"Type=Application\n"
		"Name=Star Trek Voyager: Elite Force (server link)\n"
		"Exec=%s %%u\n"
		"Icon=eliteforce-marino\n"
		"Terminal=false\n"
		"NoDisplay=true\n"
		"MimeType=x-scheme-handler/stvef;\n",
		exec );
	if ( !lc_mkdir_p( dir ) || !lc_write_file_atomic( path, text, strlen( text ) ) ) {
		if ( err && errSize )
			lc_snprintf( err, errSize, "Couldn't write %s.", path );
		return 0;
	}
	/* the mime default (xdg-utils); the file is in place even where this is missing */
	lc_snprintf( cmd, sizeof( cmd ), "xdg-mime default " DESKTOP_NAME " x-scheme-handler/stvef >/dev/null 2>&1" );
	if ( !lc_test_env( "LC_NO_XDG" ) && system( cmd ) != 0 )
		lc_log( "Link: xdg-mime default failed; %s is written but may not be the handler yet", path );
	lc_log( "Link: registered stvef: in %s", path );
	return 1;
}

#endif
