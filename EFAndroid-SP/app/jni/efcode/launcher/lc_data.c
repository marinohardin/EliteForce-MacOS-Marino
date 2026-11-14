/*
===========================================================================
Launcher core: finding, choosing, copying and unpacking the game data
(lc_data.h). Spec: launcher-data-coop.md §2.9, §4, §10.5.

The lookup is the one the engine uses (sys_data.c's Sys_DataResolve calls
lc_data_find), so the launcher and the engine always agree.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#if !defined( _WIN32 )
#	define _GNU_SOURCE 1
#	if defined( __APPLE__ )
#		define _DARWIN_C_SOURCE 1
#	endif
#endif

#include "lc_data.h"
#include "lc_settings.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#	ifndef WIN32_LEAN_AND_MEAN
#		define WIN32_LEAN_AND_MEAN
#	endif
#	include <windows.h>
#	include <sys/utime.h>
#else
#	include <unistd.h>
#	include <utime.h>
#endif

#ifndef LC_HAVE_GOG
#	define LC_HAVE_GOG 0
#endif
#if LC_HAVE_GOG
#	include "../sys/sys_osx_gog.h"
#endif

#define MAX_SUBDIRS 512

/* ===================================================================== */
/* lookup (§4.1, sys_data.c)                                             */

int lc_data_overridden( int argc, char **argv )
{
	static const char *overrides[] = { "fs_basepath", "fs_homepath", "com_homepath",
		"fs_cdpath", "fs_steampath", "fs_gogpath", "com_basegame", NULL };
	int i, j;

	for ( i = 1; i < argc; i++ )
		for ( j = 0; overrides[j]; j++ )
			if ( !lc_stricmp( argv[i], overrides[j] ) )
				return 1;
	return 0;
}

typedef struct {
	char folder[LC_MAX_PATH];		/* the folder holding BaseEF */
	char base[LC_MAX_PATH];			/* BaseEF */
} found_t;

/* qtrue when folder (an install, or its BaseEF folder itself) has <BaseEF>/pak0.pk3 */
static int GameFolder( const char *folder, found_t *out )
{
	char base[LC_MAX_PATH], pak0[LC_MAX_PATH], f[LC_MAX_PATH];
	size_t n;

	if ( !folder || !*folder )
		return 0;
	lc_strlcpy( f, folder, sizeof( f ) );
	n = strlen( f );
	while ( n > 1 && ( f[n - 1] == '/' || f[n - 1] == '\\' ) && !( n == 3 && f[1] == ':' ) )
		f[--n] = 0;
	if ( !lc_is_dir( f ) )
		return 0;
	if ( lc_child_ci( f, LC_BASEGAME, base, sizeof( base ) ) && lc_is_dir( base )
		&& lc_child_ci( base, "pak0.pk3", pak0, sizeof( pak0 ) ) ) {
		lc_strlcpy( out->folder, f, sizeof( out->folder ) );
		lc_strlcpy( out->base, base, sizeof( out->base ) );
		return 1;
	}
	/* the BaseEF folder itself */
	if ( !lc_stricmp( lc_path_basename( f ), LC_BASEGAME ) && lc_child_ci( f, "pak0.pk3", pak0, sizeof( pak0 ) ) ) {
		lc_path_dirname( out->folder, sizeof( out->folder ), f );
		lc_strlcpy( out->base, f, sizeof( out->base ) );
		return 1;
	}
	return 0;
}

static int Candidate( const char *folder, const char *what, found_t *f, char *how )
{
	if ( !GameFolder( folder, f ) )
		return 0;
	lc_log( "Game data: %s, \"%s\"", what, f->base );
	lc_strlcpy( how, what, 64 );
	return 1;
}

typedef struct { char *list; size_t len; int count; } dirlist_t;

static int DirsCB( const char *name, int isDir, void *user )
{
	dirlist_t *l = user;
	size_t n = strlen( name ) + 1;
	char *grown;

	if ( !isDir )
		return 0;
	if ( !( grown = realloc( l->list, l->len + n + 1 ) ) )
		return 1;
	l->list = grown;
	memcpy( l->list + l->len, name, n );
	l->len += n;
	l->list[l->len] = 0;
	l->count++;
	return l->count >= MAX_SUBDIRS;
}

/* root/pattern: '/'-separated components, names (case-insensitive) or "*" (every subdirectory) */
static int Glob( const char *root, const char *pattern, const char *what, found_t *f, char *how )
{
	char comp[256], path[LC_MAX_PATH];
	const char *slash, *rest;
	size_t cl;

	if ( !root || !*root )
		return 0;
	if ( !*pattern )
		return Candidate( root, what, f, how );
	slash = strchr( pattern, '/' );
	rest = slash ? slash + 1 : "";
	cl = slash ? (size_t)( slash - pattern ) : strlen( pattern );
	if ( cl >= sizeof( comp ) )
		cl = sizeof( comp ) - 1;
	memcpy( comp, pattern, cl );
	comp[cl] = 0;

	if ( !strcmp( comp, "*" ) ) {
		dirlist_t l = { NULL, 0, 0 };
		char *name;
		int i, found = 0;

		lc_list_dir( root, DirsCB, &l );
		name = l.list;
		for ( i = 0; i < l.count && !found; i++, name += strlen( name ) + 1 ) {
			lc_path_join( path, sizeof( path ), root, name );
			found = Glob( path, rest, what, f, how );
		}
		free( l.list );
		return found;
	}
	if ( !lc_child_ci( root, comp, path, sizeof( path ) ) || !lc_is_dir( path ) )
		return 0;
	return Glob( path, rest, what, f, how );
}

/* the gamefolder line of <homepath>/launcher.cfg, or "" (read as sys_data.c always did) */
static void RememberedFolder( char *out, size_t size )
{
	lc_store *s = lc_store_load( NULL );
	const char *v = lc_store_get( s, LC_KEY_GAMEFOLDER );

	lc_strlcpy( out, v ? v : "", size );
	lc_store_free( s );
}

#ifdef _WIN32

static int ToUTF8( char *out, int size, const WCHAR *in )
{
	return WideCharToMultiByte( CP_UTF8, 0, in, -1, out, size, NULL, NULL ) > 0;
}

static int RegString( HKEY key, const WCHAR *value, char *out, int size )
{
	WCHAR w[LC_MAX_PATH];
	DWORD type, bytes = sizeof( w ) - sizeof( WCHAR );

	if ( RegQueryValueExW( key, value, NULL, &type, (LPBYTE)w, &bytes ) != ERROR_SUCCESS
		|| ( type != REG_SZ && type != REG_EXPAND_SZ ) )
		return 0;
	w[bytes / sizeof( WCHAR )] = 0;
	return ToUTF8( out, size, w ) && *out;
}

/* every subkey of root\path: valueName is a game folder (and, if nameValue, that
   subkey's nameValue contains nameMatch) */
static int RegScan( HKEY root, const WCHAR *path, const WCHAR *valueName,
	const WCHAR *nameValue, const char *nameMatch, const char *what, found_t *f, char *how )
{
	HKEY key;
	DWORD i;
	int found = 0;

	if ( RegOpenKeyExW( root, path, 0, KEY_READ, &key ) != ERROR_SUCCESS )
		return 0;
	for ( i = 0; !found; i++ ) {
		WCHAR name[256];
		DWORD len = 256;
		HKEY sub;
		char folder[LC_MAX_PATH], display[LC_MAX_PATH];

		if ( RegEnumKeyExW( key, i, name, &len, NULL, NULL, NULL, NULL ) != ERROR_SUCCESS )
			break;
		if ( RegOpenKeyExW( key, name, 0, KEY_READ, &sub ) != ERROR_SUCCESS )
			continue;
		if ( ( !nameValue || ( RegString( sub, nameValue, display, sizeof( display ) )
				&& strstr( display, nameMatch ) ) )
			&& RegString( sub, valueName, folder, sizeof( folder ) ) )
			found = Candidate( folder, what, f, how );
		RegCloseKey( sub );
	}
	RegCloseKey( key );
	return found;
}

static int FindWellKnown( found_t *f, char *how )
{
	static const char *dirPatterns[] = {
		"GOG Galaxy/Games/*", "GOG Games/*", "Raven/*", "Activision/*", "Raven Software/*",
		"Star Trek Voyager Elite Force", "Star Trek Voyager - Elite Force", "Elite Force", NULL
	};
	const char *roots[4];
	char drive[8];
	const char *sys = getenv( "SystemDrive" );
	int r, p;

	/* GOG Galaxy and GOG's offline installers register every game here */
	if ( RegScan( HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\GOG.com\\Games", L"path", NULL, NULL, "GOG install", f, how )
		|| RegScan( HKEY_LOCAL_MACHINE, L"SOFTWARE\\GOG.com\\Games", L"path", NULL, NULL, "GOG install", f, how ) )
		return 1;
	/* retail CD installs and anything else with an uninstall entry */
	if ( RegScan( HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
			L"InstallLocation", L"DisplayName", "Elite Force", "installed game", f, how )
		|| RegScan( HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
			L"InstallLocation", L"DisplayName", "Elite Force", "installed game", f, how )
		|| RegScan( HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
			L"InstallLocation", L"DisplayName", "Elite Force", "installed game", f, how ) )
		return 1;

	lc_snprintf( drive, sizeof( drive ), "%s\\", sys && *sys ? sys : "C:" );
	roots[0] = drive;
	roots[1] = getenv( "ProgramFiles(x86)" );
	roots[2] = getenv( "ProgramFiles" );
	roots[3] = NULL;
	for ( r = 0; r < 3; r++ )
		for ( p = 0; dirPatterns[p]; p++ )
			if ( Glob( roots[r], dirPatterns[p], "installed game", f, how ) )
				return 1;
	return 0;
}

#else	/* Linux (and the macOS test build) */

static int FindWellKnown( found_t *f, char *how )
{
	/* where launchers put Windows games (relative to $HOME, or absolute) */
	static const char *installs[] = {
		"Games/Heroic/*",
		".var/app/com.heroicgameslauncher.hgl/Games/Heroic/*",
		"/run/media/*/Heroic/*",			/* Steam Deck SD card */
		"/run/media/*/*/Heroic/*",
		"/run/media/*/Games/Heroic/*",
		NULL
	};
	/* Wine prefixes: Wine, Lutris (~/Games/<game>), Heroic, Bottles, Proton */
	static const char *prefixes[] = {
		".wine",
		"Games/*",
		"Games/Heroic/Prefixes/*",
		"Games/Heroic/Prefixes/default/*",
		".local/share/lutris/prefixes/*",
		".local/share/bottles/bottles/*",
		".var/app/com.usebottles.bottles/data/bottles/bottles/*",
		".var/app/net.lutris.Lutris/data/lutris/prefixes/*",
		".steam/steam/steamapps/compatdata/*/pfx",
		".local/share/Steam/steamapps/compatdata/*/pfx",
		".var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/compatdata/*/pfx",
		NULL
	};
	static const char *driveC[] = {
		"drive_c/GOG Games/*",
		"drive_c/Program Files (x86)/GOG Galaxy/Games/*",
		"drive_c/Program Files/GOG Galaxy/Games/*",
		"drive_c/Program Files (x86)/Raven/*",
		"drive_c/Program Files (x86)/Activision/*",
		"drive_c/Program Files/Raven/*",
		"drive_c/Program Files/Activision/*",
		"drive_c/Program Files (x86)/*",
		"drive_c/Program Files/*",
		NULL
	};
	const char *home = lc_home_dir( );
	char pattern[LC_MAX_PATH];
	int i, j;

	if ( !*home )
		return 0;
	for ( i = 0; installs[i]; i++ ) {
		if ( installs[i][0] == '/' ) {
			if ( Glob( "/", installs[i] + 1, "Heroic install", f, how ) )
				return 1;
		} else if ( Glob( home, installs[i], "Heroic install", f, how ) )
			return 1;
	}
	for ( i = 0; prefixes[i]; i++ ) {
		for ( j = 0; driveC[j]; j++ ) {
			lc_snprintf( pattern, sizeof( pattern ), "%s/%s", prefixes[i], driveC[j] );
			if ( Glob( home, pattern, "Wine prefix", f, how ) )
				return 1;
		}
	}
	return 0;
}

#endif

/* the fs_basepath for folder/base. Linux needs the folder named exactly BaseEF's
   spelling "baseEF": otherwise a symlink to it in <homepath>/.gamedata. */
const char *lc_use_found_basepath( const char *folder, const char *base )
{
	static char result[LC_MAX_PATH];

#ifndef _WIN32
	if ( strcmp( lc_path_basename( base ), LC_BASEGAME ) ) {
		char dir[LC_MAX_PATH], link[LC_MAX_PATH], target[LC_MAX_PATH];
		ssize_t n;

		lc_path_join( dir, sizeof( dir ), lc_homepath( ), ".gamedata" );
		lc_path_join( link, sizeof( link ), dir, LC_BASEGAME );
		n = readlink( link, target, sizeof( target ) - 1 );
		if ( n >= 0 )
			target[n] = 0;
		if ( n < 0 || strcmp( target, base ) ) {
			lc_mkdir_p( dir );
			unlink( link );		/* only ever a symlink this function made */
			if ( symlink( base, link ) ) {
				lc_log( "WARNING: could not link \"%s\" to \"%s\": %s", link, base, strerror( errno ) );
				lc_strlcpy( result, folder, sizeof( result ) );
				return result;
			}
		}
		lc_log( "Game data: \"%s\" is linked as \"%s\"", base, link );
		lc_strlcpy( result, dir, sizeof( result ) );
		return result;
	}
#endif
	lc_strlcpy( result, folder, sizeof( result ) );
	return result;
}

static void ExecutableDir( char *out, size_t size )
{
	const char *appImage = lc_env( "APPIMAGE" );

	if ( appImage ) {
		lc_path_dirname( out, size, appImage );
		return;
	}
	lc_strlcpy( out, lc_exe_dir( ), size );
}

int lc_data_find( int argc, char **argv, lc_data *out )
{
	char remembered[LC_MAX_PATH], path[LC_MAX_PATH], exeDir[LC_MAX_PATH], pak[LC_MAX_PATH];
	found_t f;

	memset( out, 0, sizeof( *out ) );
	if ( lc_data_overridden( argc, argv ) ) {
		out->found = 1;
		lc_strlcpy( out->how, "the command line", sizeof( out->how ) );
		return 1;
	}
	if ( !*lc_homepath( ) ) {
		lc_log( "Game data: no home directory, using the defaults" );
		return 0;
	}

	/* a) remembered game folder */
	RememberedFolder( remembered, sizeof( remembered ) );
	if ( *remembered ) {
		if ( Candidate( remembered, "remembered game folder", &f, out->how ) ) {
			out->found = out->inPlace = 1;
			lc_strlcpy( out->base, f.base, sizeof( out->base ) );
			lc_strlcpy( out->basepath, lc_use_found_basepath( f.folder, f.base ), sizeof( out->basepath ) );
			return 1;
		}
		lc_log( "Game data: remembered game folder \"%s\" has no %s/pak0.pk3", remembered, LC_BASEGAME );
	}

	/* b) copied into the homepath (searched as fs_homepath; default basepath) */
	lc_path_join( path, sizeof( path ), lc_homepath( ), LC_BASEGAME );
	if ( lc_child_ci( path, "pak0.pk3", pak, sizeof( pak ) ) ) {
		lc_log( "Game data: \"%s\"", path );
		out->found = 1;
		lc_strlcpy( out->base, path, sizeof( out->base ) );
		lc_strlcpy( out->how, "homepath", sizeof( out->how ) );
		return 1;
	}

	/* c) next to the executable */
	ExecutableDir( exeDir, sizeof( exeDir ) );
	if ( *exeDir && Candidate( exeDir, "next to the game", &f, out->how ) )
		goto inplace;

	/* d) well-known installs */
	if ( FindWellKnown( &f, out->how ) )
		goto inplace;

	lc_log( "Game data not found (no %s/pak0.pk3 in the remembered folder, \"%s\", next to the game "
		"or in a known install)", LC_BASEGAME, path );
	return 0;

inplace:
	out->found = out->inPlace = 1;
	lc_strlcpy( out->base, f.base, sizeof( out->base ) );
	lc_strlcpy( out->basepath, lc_use_found_basepath( f.folder, f.base ), sizeof( out->basepath ) );
	return 1;
}

int lc_data_external_base( char *out, size_t size )
{
	char remembered[LC_MAX_PATH];
	found_t f;

	out[0] = 0;
	RememberedFolder( remembered, sizeof( remembered ) );
	if ( *remembered && GameFolder( remembered, &f ) ) {
		lc_strlcpy( out, f.base, size );
		return 1;
	}
	return 0;
}

int lc_data_pak0_in_homebase( void )
{
	char p[LC_MAX_PATH];
	return lc_child_ci( lc_homebase( ), "pak0.pk3", p, sizeof( p ) );
}

/* ===================================================================== */
/* status, patch and expansion checks (§2.9, §4.5)                       */

int lc_data_missing_patch_paks( const char *homeBase, const char *base, char names[2][16] )
{
	static const char *want[2] = { "pak1.pk3", "pak2.pk3" };
	char p[LC_MAX_PATH];
	int i, n = 0;

	for ( i = 0; i < 2; i++ ) {
		if ( ( homeBase && *homeBase && lc_child_ci( homeBase, want[i], p, sizeof( p ) ) )
			|| ( base && *base && lc_child_ci( base, want[i], p, sizeof( p ) ) ) )
			continue;
		lc_strlcpy( names[n++], want[i], 16 );
	}
	return n;
}

unsigned char *lc_zip_central_directory( const char *pk3, size_t *cdLen )
{
	FILE *f = lc_fopen( pk3, "rb" );
	unsigned char tail[65536 + 22], *cd = NULL;
	long long size;
	size_t tailLen, cdSize;
	long i;
	unsigned cdOffset;

	*cdLen = 0;
	if ( !f )
		return NULL;
	if ( fseek( f, 0, SEEK_END ) ) {
		fclose( f );
		return NULL;
	}
	size = ftell( f );
	if ( size < 22 ) {
		fclose( f );
		return NULL;
	}
	tailLen = size < (long long)sizeof( tail ) ? (size_t)size : sizeof( tail );
	if ( fseek( f, (long)( size - (long long)tailLen ), SEEK_SET ) || fread( tail, 1, tailLen, f ) != tailLen ) {
		fclose( f );
		return NULL;
	}
	for ( i = tailLen - 22; ; i-- ) {
		if ( tail[i] == 'P' && tail[i + 1] == 'K' && tail[i + 2] == 5 && tail[i + 3] == 6 )
			break;
		if ( i == 0 ) {
			fclose( f );
			return NULL;
		}
	}
	cdSize = (size_t)tail[i + 12] | ( (size_t)tail[i + 13] << 8 ) | ( (size_t)tail[i + 14] << 16 ) | ( (size_t)tail[i + 15] << 24 );
	cdOffset = (unsigned)tail[i + 16] | ( (unsigned)tail[i + 17] << 8 ) | ( (unsigned)tail[i + 18] << 16 ) | ( (unsigned)tail[i + 19] << 24 );
	if ( !cdSize || cdSize > ( 64u << 20 ) || (long long)cdOffset + (long long)cdSize > size ) {
		fclose( f );
		return NULL;
	}
	cd = malloc( cdSize );
	if ( !cd || fseek( f, (long)cdOffset, SEEK_SET ) || fread( cd, 1, cdSize, f ) != cdSize ) {
		free( cd );
		fclose( f );
		return NULL;
	}
	fclose( f );
	*cdLen = cdSize;
	return cd;
}

int lc_zip_has_entry( const char *pk3, const char *name )
{
	size_t cdSize;
	unsigned char *cd = lc_zip_central_directory( pk3, &cdSize ), *p;
	int found = 0;
	size_t nameLen = strlen( name );

	if ( !cd )
		return 0;
	p = cd;
	while ( p + 46 <= cd + cdSize && p[0] == 'P' && p[1] == 'K' && p[2] == 1 && p[3] == 2 ) {
		size_t nl = (size_t)p[28] | ( (size_t)p[29] << 8 );
		size_t el = (size_t)p[30] | ( (size_t)p[31] << 8 );
		size_t cl = (size_t)p[32] | ( (size_t)p[33] << 8 );

		if ( p + 46 + nl > cd + cdSize )
			break;
		if ( nl == nameLen && !lc_strnicmp( (const char *)p + 46, name, nl ) ) {
			found = 1;
			break;
		}
		p += 46 + nl + el + cl;
	}
	free( cd );
	return found;
}

/* ---- the engine's pack checksum (qcommon/md4.c Com_BlockChecksum, files.c FS_LoadZipFile) ---- */

typedef struct { uint32_t a, b, c, d; } md4State;

#define MD4_F( x, y, z )	( ( ( x ) & ( y ) ) | ( ~( x ) & ( z ) ) )
#define MD4_G( x, y, z )	( ( ( x ) & ( y ) ) | ( ( x ) & ( z ) ) | ( ( y ) & ( z ) ) )
#define MD4_H( x, y, z )	( ( x ) ^ ( y ) ^ ( z ) )
#define MD4_ROT( x, s )		( ( ( x ) << ( s ) ) | ( ( x ) >> ( 32 - ( s ) ) ) )

static void Md4Block( md4State *m, const unsigned char *in )
{
	static const int r1[4] = { 3, 7, 11, 19 }, r2[4] = { 3, 5, 9, 13 }, r3[4] = { 3, 9, 11, 15 };
	static const int o3[16] = { 0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15 };
	uint32_t x[16], v[4], t;
	int i;

	for ( i = 0; i < 16; i++ )
		x[i] = (uint32_t)in[i * 4] | ( (uint32_t)in[i * 4 + 1] << 8 ) | ( (uint32_t)in[i * 4 + 2] << 16 ) | ( (uint32_t)in[i * 4 + 3] << 24 );
	v[0] = m->a; v[1] = m->b; v[2] = m->c; v[3] = m->d;
	/* each step updates v[(4 - i % 4) % 4] from the other three, in the order a, d, c, b */
	for ( i = 0; i < 16; i++ ) {
		uint32_t *A = &v[( 4 - i % 4 ) % 4], B = v[( 5 - i % 4 ) % 4], C = v[( 6 - i % 4 ) % 4], D = v[( 7 - i % 4 ) % 4];
		t = *A + MD4_F( B, C, D ) + x[i];
		*A = MD4_ROT( t, r1[i % 4] );
	}
	for ( i = 0; i < 16; i++ ) {
		uint32_t *A = &v[( 4 - i % 4 ) % 4], B = v[( 5 - i % 4 ) % 4], C = v[( 6 - i % 4 ) % 4], D = v[( 7 - i % 4 ) % 4];
		t = *A + MD4_G( B, C, D ) + x[( i % 4 ) * 4 + i / 4] + 0x5A827999u;
		*A = MD4_ROT( t, r2[i % 4] );
	}
	for ( i = 0; i < 16; i++ ) {
		uint32_t *A = &v[( 4 - i % 4 ) % 4], B = v[( 5 - i % 4 ) % 4], C = v[( 6 - i % 4 ) % 4], D = v[( 7 - i % 4 ) % 4];
		t = *A + MD4_H( B, C, D ) + x[o3[i]] + 0x6ED9EBA1u;
		*A = MD4_ROT( t, r3[i % 4] );
	}
	m->a += v[0]; m->b += v[1]; m->c += v[2]; m->d += v[3];
}

void lc_md4( const void *data, size_t len, unsigned char out[16] )
{
	md4State m = { 0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u };
	const unsigned char *p = (const unsigned char *)data;
	unsigned char buf[128];
	size_t n = len, i;
	uint32_t bits;

	while ( n >= 64 ) {
		Md4Block( &m, p );
		p += 64;
		n -= 64;
	}
	memset( buf, 0, sizeof( buf ) );
	if ( n )
		memcpy( buf, p, n );
	buf[n] = 0x80;
	bits = (uint32_t)( len * 8 );		/* the low 32 bits of the length, as the engine's mdfour */
	if ( n <= 55 ) {
		for ( i = 0; i < 4; i++ )
			buf[56 + i] = (unsigned char)( bits >> ( 8 * i ) );
		Md4Block( &m, buf );
	} else {
		for ( i = 0; i < 4; i++ )
			buf[120 + i] = (unsigned char)( bits >> ( 8 * i ) );
		Md4Block( &m, buf );
		Md4Block( &m, buf + 64 );
	}
	for ( i = 0; i < 4; i++ ) {
		out[i] = (unsigned char)( m.a >> ( 8 * i ) );
		out[4 + i] = (unsigned char)( m.b >> ( 8 * i ) );
		out[8 + i] = (unsigned char)( m.c >> ( 8 * i ) );
		out[12 + i] = (unsigned char)( m.d >> ( 8 * i ) );
	}
}

int lc_pak_checksum( const char *pk3, unsigned *checksum )
{
	size_t cdSize, n = 0, cap = 256;
	unsigned char *cd = lc_zip_central_directory( pk3, &cdSize ), *p, digest[16];
	unsigned char *crcs;
	uint32_t w[4];
	int i;

	if ( !cd )
		return 0;
	crcs = malloc( cap );
	if ( !crcs ) {
		free( cd );
		return 0;
	}
	p = cd;
	while ( p + 46 <= cd + cdSize && p[0] == 'P' && p[1] == 'K' && p[2] == 1 && p[3] == 2 ) {
		size_t nl = (size_t)p[28] | ( (size_t)p[29] << 8 );
		size_t el = (size_t)p[30] | ( (size_t)p[31] << 8 );
		size_t cl = (size_t)p[32] | ( (size_t)p[33] << 8 );
		unsigned uncompressed = (unsigned)p[24] | ( (unsigned)p[25] << 8 ) | ( (unsigned)p[26] << 16 ) | ( (unsigned)p[27] << 24 );

		if ( uncompressed > 0 ) {		/* the CRC-32 of each non-empty file, little-endian, in order */
			if ( n + 4 > cap ) {
				unsigned char *bigger = realloc( crcs, cap * 2 );
				if ( !bigger ) {
					free( crcs );
					free( cd );
					return 0;
				}
				crcs = bigger;
				cap *= 2;
			}
			memcpy( crcs + n, p + 16, 4 );
			n += 4;
		}
		p += 46 + nl + el + cl;
	}
	free( cd );
	lc_md4( crcs, n, digest );
	free( crcs );
	for ( i = 0; i < 4; i++ )
		w[i] = (uint32_t)digest[i * 4] | ( (uint32_t)digest[i * 4 + 1] << 8 ) | ( (uint32_t)digest[i * 4 + 2] << 16 ) | ( (uint32_t)digest[i * 4 + 3] << 24 );
	*checksum = (unsigned)( w[0] ^ w[1] ^ w[2] ^ w[3] );
	return 1;
}

typedef struct { const char *dir; int found; } expCtx;

static int ExpCB( const char *name, int isDir, void *user )
{
	expCtx *c = user;
	size_t n = strlen( name );

	if ( isDir || name[0] == '.' || n < 5 || lc_stricmp( name + n - 4, ".pk3" ) )
		return 0;
	{
		char full[LC_MAX_PATH];

		lc_path_join( full, sizeof( full ), c->dir, name );
		if ( lc_zip_has_entry( full, "maps/tour/deck02.bsp" ) )
			c->found = 1;
	}
	return c->found;
}

static int HasLooseDeck( const char *dir )
{
	char a[LC_MAX_PATH], b[LC_MAX_PATH], c[LC_MAX_PATH];

	return dir && *dir && lc_child_ci( dir, "maps", a, sizeof( a ) ) && lc_child_ci( a, "tour", b, sizeof( b ) )
		&& lc_child_ci( b, "deck02.bsp", c, sizeof( c ) ) && lc_is_file( c );
}

int lc_data_has_expansion( const char *homeBase, const char *base )
{
	const char *dirs[2];
	int i;

	dirs[0] = homeBase;
	dirs[1] = base;
	for ( i = 0; i < 2; i++ ) {
		expCtx c;

		if ( !dirs[i] || !*dirs[i] )
			continue;
		if ( HasLooseDeck( dirs[i] ) )
			return 1;
		c.dir = dirs[i];
		c.found = 0;
		lc_list_dir( dirs[i], ExpCB, &c );
		if ( c.found )
			return 1;
	}
	return 0;
}

void lc_data_display_path( const char *path, char *out, size_t size )
{
#ifdef _WIN32
	lc_strlcpy( out, path, size );
#else
	const char *home = lc_home_dir( );
	size_t n = strlen( home );

	if ( n > 1 && !strncmp( path, home, n ) && ( path[n] == '/' || !path[n] ) )
		lc_snprintf( out, size, "~%s", path + n );
	else
		lc_strlcpy( out, path, size );
#endif
}

void lc_data_status_text( const lc_data *d, char *line, size_t lineSize, char *detail, size_t detailSize )
{
	char names[2][16];
	int missing;

	if ( detail && detailSize )
		detail[0] = 0;
	if ( !d->found ) {
		lc_strlcpy( line, LC_DATA_NONE_STATUS, lineSize );
		return;
	}
	missing = lc_data_missing_patch_paks( lc_homebase( ), d->base, names );
	if ( missing ) {
		char joined[64];

		if ( missing == 2 )
			lc_snprintf( joined, sizeof( joined ), "%s and %s", names[0], names[1] );
		else
			lc_strlcpy( joined, names[0], sizeof( joined ) );
		lc_snprintf( line, lineSize, "Patch 1.2 missing \xc2\xb7 add %s with Add Game Files\xe2\x80\xa6", joined );
		if ( detail )
			lc_snprintf( detail, detailSize, "These game files are from the original CD (version 1.0). The game also needs %s "
				"from the 1.1 and 1.2 patches: copy them from a patched installation (GOG, Steam, or a PC with patch 1.2) "
				"with Add Game Files\xe2\x80\xa6.", joined );
		return;
	}
	lc_strlcpy( line, d->basepath[0] ? "Ready \xc2\xb7 using the files in this folder" :
		"Ready \xc2\xb7 copied into the app's own folder", lineSize );
}

/* ===================================================================== */
/* the chooser (§4.2)                                                    */

int lc_is_installer( const char *path )
{
	size_t n = path ? strlen( path ) : 0;

	return n > 4 && !lc_is_dir( path ) && !lc_stricmp( path + n - 4, ".exe" );
}

int lc_validate_choice( const char *chosen, int inPlace, char *base, size_t baseSize,
	char *problem, size_t problemSize )
{
	char c[LC_MAX_PATH], child[LC_MAX_PATH], pak[LC_MAX_PATH];
	size_t n;
	const char *msg = NULL;

	if ( baseSize )
		base[0] = 0;
	if ( problemSize )
		problem[0] = 0;
	lc_strlcpy( c, chosen ? chosen : "", sizeof( c ) );
	n = strlen( c );
	while ( n > 1 && ( c[n - 1] == '/' || c[n - 1] == '\\' ) && !( n == 3 && c[1] == ':' ) )
		c[--n] = 0;

	if ( lc_child_ci( c, LC_BASEGAME, child, sizeof( child ) ) && lc_is_dir( child ) ) {
		lc_strlcpy( base, child, baseSize );
		if ( !lc_child_ci( child, "pak0.pk3", pak, sizeof( pak ) ) )
			msg = LC_MSG_PAK0_ON_CD;
	} else if ( lc_is_dir( c ) && ( lc_child_ci( c, "pak0.pk3", pak, sizeof( pak ) ) || !lc_stricmp( lc_path_basename( c ), LC_BASEGAME ) ) ) {
		lc_strlcpy( base, c, baseSize );
		if ( !lc_child_ci( c, "pak0.pk3", pak, sizeof( pak ) ) )
			msg = LC_MSG_PAK0_ON_CD;
	} else
		msg = "This folder has no BaseEF folder. Choose the Elite Force installation folder (the one containing BaseEF), "
			"or the BaseEF folder itself.";
	if ( !msg && inPlace && lc_stricmp( lc_path_basename( base ), LC_BASEGAME ) )
		msg = "To use the files in place the folder must be called BaseEF. Rename it, or untick the checkbox to copy the files instead.";
	if ( msg ) {
		lc_strlcpy( problem, msg, problemSize );
		return 1;
	}
	return 0;
}

int lc_remember_game_folder( const char *folderHoldingBase )
{
	lc_store *s = lc_store_shared( );

	if ( folderHoldingBase && *folderHoldingBase )
		lc_store_set( s, LC_KEY_GAMEFOLDER, folderHoldingBase );
	else
		lc_store_remove( s, LC_KEY_GAMEFOLDER );
	return lc_store_save( s );
}

void lc_choice_alert( const char *chosen, const char *problem, char *title, size_t titleSize, char *text, size_t textSize )
{
	lc_strlcpy( title, "That folder can't be used", titleSize );
	lc_snprintf( text, textSize, "%s\n\n%s", chosen, problem );
}

/* ===================================================================== */
/* copy / unpack jobs (§4.3, §4.4)                                       */

typedef struct {
	char from[LC_MAX_PATH], to[LC_MAX_PATH];
	unsigned long long size;
#if LC_HAVE_GOG
	int isGog;
	gogFile_t gog;
#endif
} item_t;

#define KIND_COPY	0
#define KIND_ADD	1
#define KIND_UNPACK	2

struct lc_copyjob_s {
	item_t *items;
	int n, cap;
	unsigned long long total;
	int kind;
	char installer[LC_MAX_PATH];
	char homeBase[LC_MAX_PATH];
};

static lc_copyjob *JobNew( int kind, const char *homeBase )
{
	lc_copyjob *j = calloc( 1, sizeof( *j ) );

	if ( !j )
		return NULL;
	j->kind = kind;
	lc_strlcpy( j->homeBase, homeBase, sizeof( j->homeBase ) );
	return j;
}

static item_t *JobAdd( lc_copyjob *j )
{
	if ( j->n == j->cap ) {
		int ncap = j->cap ? j->cap * 2 : 16;
		item_t *grown = realloc( j->items, (size_t)ncap * sizeof( item_t ) );

		if ( !grown )
			return NULL;
		j->items = grown;
		j->cap = ncap;
	}
	memset( &j->items[j->n], 0, sizeof( item_t ) );
	return &j->items[j->n++];
}

void lc_copyjob_free( lc_copyjob *j )
{
	int i;

	if ( !j )
		return;
#if LC_HAVE_GOG
	for ( i = 0; i < j->n; i++ )
		if ( j->items[i].isGog )
			free( j->items[i].gog.parts );
#else
	( void )i;
#endif
	free( j->items );
	free( j );
}

unsigned long long lc_copyjob_total( const lc_copyjob *j )
{
	return j ? j->total : 0;
}

int lc_copyjob_count( const lc_copyjob *j )
{
	return j ? j->n : 0;
}

int lc_copyjob_is_unpack( const lc_copyjob *j )
{
	return j && j->kind == KIND_UNPACK;
}

static int HasPk3Ext( const char *name )
{
	size_t n = strlen( name );
	return n > 4 && !lc_stricmp( name + n - 4, ".pk3" );
}

typedef struct { const char *dir; char (*names)[256]; int n, cap; } nameList;

static int NamesCB( const char *name, int isDir, void *user )
{
	nameList *l = user;

	( void )isDir;
	if ( l->n == l->cap ) {
		int ncap = l->cap ? l->cap * 2 : 64;
		char (*grown)[256] = realloc( l->names, (size_t)ncap * sizeof( *grown ) );

		if ( !grown )
			return 1;
		l->names = grown;
		l->cap = ncap;
	}
	lc_strlcpy( l->names[l->n++], name, 256 );
	return 0;
}

typedef struct {
	lc_copyjob *job;
	const char *srcRoot, *dstRoot;
	int saves, kept;
} savesCtx;

static void PlanSaves( savesCtx *c, const char *srcDir, const char *dstDir )
{
	nameList l = { srcDir, NULL, 0, 0 };
	int i;

	lc_list_dir( srcDir, NamesCB, &l );
	for ( i = 0; i < l.n; i++ ) {
		char from[LC_MAX_PATH], to[LC_MAX_PATH];

		lc_path_join( from, sizeof( from ), srcDir, l.names[i] );
		lc_path_join( to, sizeof( to ), dstDir, l.names[i] );
		if ( lc_is_symlink( from ) )
			continue;
		if ( lc_is_dir( from ) ) {
			PlanSaves( c, from, to );
			continue;
		}
		if ( !lc_is_file( from ) || l.names[i][0] == '.' )
			continue;
		if ( lc_is_file( to ) || lc_is_dir( to ) ) {
			c->kept++;
			continue;
		} else {
			item_t *it = JobAdd( c->job );
			int64_t sz = lc_file_size( from );

			if ( !it )
				break;
			lc_strlcpy( it->from, from, sizeof( it->from ) );
			lc_strlcpy( it->to, to, sizeof( it->to ) );
			it->size = sz > 0 ? (unsigned long long)sz : 0;
			c->job->total += it->size;
			c->saves++;
		}
	}
	free( l.names );
}

lc_copyjob *lc_plan_copy_game_files( const char *srcBase, const char *homeBase,
	int *pk3s, int *saves, int *kept )
{
	lc_copyjob *j = JobNew( KIND_COPY, homeBase );
	nameList l = { srcBase, NULL, 0, 0 };
	char savesDir[LC_MAX_PATH], total[64];
	int i, np = 0;
	savesCtx sc;

	if ( !j )
		return NULL;
	lc_list_dir( srcBase, NamesCB, &l );
	for ( i = 0; i < l.n; i++ ) {
		char from[LC_MAX_PATH];
		item_t *it;
		int64_t sz;

		if ( l.names[i][0] == '.' || !HasPk3Ext( l.names[i] ) )
			continue;
		lc_path_join( from, sizeof( from ), srcBase, l.names[i] );
		if ( !lc_is_file( from ) )		/* follows symlinks */
			continue;
		if ( !( it = JobAdd( j ) ) )
			break;
		sz = lc_file_size( from );
		lc_strlcpy( it->from, from, sizeof( it->from ) );
		lc_path_join( it->to, sizeof( it->to ), homeBase, l.names[i] );
		it->size = sz > 0 ? (unsigned long long)sz : 0;
		j->total += it->size;
		np++;
	}
	free( l.names );
	sc.job = j;
	sc.srcRoot = srcBase;
	sc.dstRoot = homeBase;
	sc.saves = sc.kept = 0;
	if ( lc_child_ci( srcBase, "saves", savesDir, sizeof( savesDir ) ) && lc_is_dir( savesDir ) ) {
		char dst[LC_MAX_PATH];

		lc_path_join( dst, sizeof( dst ), homeBase, "saves" );
		PlanSaves( &sc, savesDir, dst );
	}
	lc_format_bytes( j->total, total, sizeof( total ) );
	lc_log( "Copying %d .pk3 files and %d saves (%d existing saves kept) from \"%s\" to \"%s\": %s",
		np, sc.saves, sc.kept, srcBase, homeBase, total );
	if ( pk3s )
		*pk3s = np;
	if ( saves )
		*saves = sc.saves;
	if ( kept )
		*kept = sc.kept;
	return j;
}

/* an existing same-named file (any case) in dir or the external base, with its size */
static int Have( const char *dir, const char *external, const char *name, int64_t *size )
{
	char p[LC_MAX_PATH];

	if ( ( dir && *dir && lc_child_ci( dir, name, p, sizeof( p ) ) ) ||
		( external && *external && lc_child_ci( external, name, p, sizeof( p ) ) ) ) {
		*size = lc_file_size( p );
		return 1;
	}
	return 0;
}

static void DestFor( const char *homeBase, const char *name, char *out, size_t size )
{
	char have[LC_MAX_PATH];

	if ( lc_child_ci( homeBase, name, have, sizeof( have ) ) )
		lc_strlcpy( out, have, size );
	else
		lc_path_join( out, size, homeBase, name );
}

static void NamesOf( const lc_copyjob *j, char *out, size_t size )
{
	int i;

	out[0] = 0;
	for ( i = 0; i < j->n; i++ ) {
		if ( i )
			lc_strlcat( out, ", ", size );
		lc_strlcat( out, lc_path_basename( j->items[i].to ), size );
	}
}

lc_copyjob *lc_plan_add_files( const char *const *picked, int n, const char *homeBase,
	const char *externalBase, int *present )
{
	lc_copyjob *j = JobNew( KIND_ADD, homeBase );
	char (*queued)[256] = NULL;
	int nq = 0, cq = 0, pr = 0, p;
	char total[64];

	if ( !j )
		return NULL;
	for ( p = 0; p < n; p++ ) {
		nameList l = { NULL, NULL, 0, 0 };
		char dir[LC_MAX_PATH], base[LC_MAX_PATH];
		int i, isDir = lc_is_dir( picked[p] );

		if ( isDir ) {
			if ( lc_child_ci( picked[p], LC_BASEGAME, base, sizeof( base ) ) && lc_is_dir( base ) )
				lc_strlcpy( dir, base, sizeof( dir ) );
			else
				lc_strlcpy( dir, picked[p], sizeof( dir ) );
			l.dir = dir;
			lc_list_dir( dir, NamesCB, &l );
		} else {
			char dn[LC_MAX_PATH];

			lc_path_dirname( dn, sizeof( dn ), picked[p] );
			lc_strlcpy( dir, dn, sizeof( dir ) );
			NamesCB( lc_path_basename( picked[p] ), 0, &l );
		}
		for ( i = 0; i < l.n; i++ ) {
			char from[LC_MAX_PATH], lower[256];
			int64_t size, haveSize;
			int k, dup = 0;
			item_t *it;

			if ( l.names[i][0] == '.' || !HasPk3Ext( l.names[i] ) )
				continue;
			lc_path_join( from, sizeof( from ), dir, l.names[i] );
			if ( !isDir )
				lc_strlcpy( from, picked[p], sizeof( from ) );
			if ( !lc_is_file( from ) )
				continue;
			for ( k = 0; l.names[i][k] && k < 255; k++ )
				lower[k] = (char)tolower( (unsigned char)l.names[i][k] );
			lower[k] = 0;
			for ( k = 0; k < nq; k++ )
				if ( !strcmp( queued[k], lower ) )
					dup = 1;
			if ( dup )
				continue;
			size = lc_file_size( from );
			if ( Have( homeBase, externalBase, l.names[i], &haveSize ) && haveSize == size ) {
				pr++;
				continue;
			}
			if ( nq == cq ) {
				int ncap = cq ? cq * 2 : 32;
				char (*grown)[256] = realloc( queued, (size_t)ncap * sizeof( *grown ) );

				if ( !grown )
					break;
				queued = grown;
				cq = ncap;
			}
			lc_strlcpy( queued[nq++], lower, 256 );
			if ( !( it = JobAdd( j ) ) )
				break;
			lc_strlcpy( it->from, from, sizeof( it->from ) );
			DestFor( homeBase, l.names[i], it->to, sizeof( it->to ) );	/* replaces a same-named file in place */
			it->size = size > 0 ? (unsigned long long)size : 0;
			j->total += it->size;
		}
		free( l.names );
	}
	free( queued );
	lc_format_bytes( j->total, total, sizeof( total ) );
	lc_log( "Add game files: %d new .pk3 (%s), %d already present", j->n, total, pr );
	if ( present )
		*present = pr;
	return j;
}

lc_copyjob *lc_plan_unpack_installer( const char *exe, int onlyNew, const char *homeBase,
	const char *externalBase, int *present, char *err, size_t errSize )
{
#if LC_HAVE_GOG
	gogInstaller_t inst;
	lc_copyjob *j;
	char total[64];
	int i, pr = 0;

	if ( present )
		*present = 0;
	if ( err && errSize )
		err[0] = 0;
	lc_log( "GOG installer: reading \"%s\"", exe );
	if ( !GOG_ReadInstaller( exe, &inst, err, errSize ) ) {
		lc_log( "GOG installer: can't be used: %s", err );
		return NULL;
	}
	j = JobNew( KIND_UNPACK, homeBase );
	if ( !j ) {
		GOG_Free( &inst );
		return NULL;
	}
	lc_strlcpy( j->installer, exe, sizeof( j->installer ) );
	for ( i = 0; i < inst.numFiles; i++ ) {
		int64_t haveSize;
		item_t *it;

		if ( onlyNew && Have( homeBase, externalBase, inst.files[i].name, &haveSize ) &&
			(unsigned long long)haveSize == inst.files[i].size ) {
			pr++;
			continue;
		}
		if ( !( it = JobAdd( j ) ) )
			break;
		it->isGog = 1;
		it->gog = inst.files[i];
		it->gog.parts = malloc( sizeof( gogPart_t ) * (size_t)( inst.files[i].numParts > 0 ? inst.files[i].numParts : 1 ) );
		if ( it->gog.parts )
			memcpy( it->gog.parts, inst.files[i].parts, sizeof( gogPart_t ) * (size_t)inst.files[i].numParts );
		lc_strlcpy( it->from, exe, sizeof( it->from ) );
		DestFor( homeBase, inst.files[i].name, it->to, sizeof( it->to ) );
		it->size = inst.files[i].size;
		j->total += it->size;
	}
	lc_format_bytes( j->total, total, sizeof( total ) );
	lc_log( "GOG installer: %d .pk3 files (%s to unpack), %d already present", inst.numFiles, total, pr );
	GOG_Free( &inst );
	if ( present )
		*present = pr;
	if ( !j->n ) {
		lc_copyjob_free( j );
		return NULL;			/* nothing to add: err is empty */
	}
	return j;
#else
	( void )exe; ( void )onlyNew; ( void )homeBase; ( void )externalBase;
	if ( present )
		*present = 0;
	lc_strlcpy( err, "this version was built without GOG installer support", errSize );
	return NULL;
#endif
}

int lc_have_gog( void )
{
	return LC_HAVE_GOG;
}

/* --- running --- */

typedef struct {
	volatile unsigned long long *progress;
	volatile signed char *cancel;
} runCtx;

static void SetTimes( const char *path, int64_t mtime )
{
	if ( mtime < 0 )
		return;
#ifdef _WIN32
	{
		wchar_t w[LC_MAX_PATH];
		struct _utimbuf t;

		t.actime = t.modtime = (time_t)mtime;
		if ( MultiByteToWideChar( CP_UTF8, 0, path, -1, w, LC_MAX_PATH ) )
			_wutime( w, &t );
	}
#else
	{
		struct utimbuf t;

		t.actime = t.modtime = (time_t)mtime;
		utime( path, &t );
	}
#endif
}

#define COPY_BLOCK ( 1 << 20 )

static int CopyOne( runCtx *rc, const char *from, const char *to, char *buf, char *err, size_t errSize )
{
	char part[LC_MAX_PATH];
	FILE *in, *out;
	size_t n;
	int bad = 0, cancelled = 0;
	int64_t mtime = lc_file_mtime( from );

	lc_snprintf( part, sizeof( part ), "%s.part", to );
	if ( !( in = lc_fopen( from, "rb" ) ) ) {
		lc_snprintf( err, errSize, "Cannot read %s: %s", from, strerror( errno ) );
		return 0;
	}
	if ( !( out = lc_fopen( part, "wb" ) ) ) {
		lc_snprintf( err, errSize, "Cannot write %s: %s", part, strerror( errno ) );
		fclose( in );
		return 0;
	}
	while ( ( n = fread( buf, 1, COPY_BLOCK, in ) ) > 0 ) {
		if ( rc->cancel && *rc->cancel ) {
			cancelled = 1;
			break;
		}
		if ( fwrite( buf, 1, n, out ) != n ) {
			lc_snprintf( err, errSize, "Cannot write %s: %s", part, strerror( errno ) );
			bad = 1;
			break;
		}
		if ( rc->progress )
			*rc->progress += n;
	}
	if ( !bad && !cancelled && ferror( in ) ) {
		lc_snprintf( err, errSize, "Cannot read %s: %s", from, strerror( errno ) );
		bad = 1;
	}
	fclose( in );
	if ( fclose( out ) && !bad && !cancelled ) {
		lc_snprintf( err, errSize, "Cannot write %s: %s", part, strerror( errno ) );
		bad = 1;
	}
	if ( rc->cancel && *rc->cancel )
		cancelled = 1;
	if ( bad || cancelled ) {
		lc_remove( part );
		if ( cancelled && !bad )
			lc_strlcpy( err, "Cancelled.", errSize );
		return 0;
	}
	SetTimes( part, mtime );		/* keep the original dates (the save menu shows them) */
	/* rename into place: a running engine keeps reading the old inode */
	if ( !lc_rename( part, to ) ) {
		lc_snprintf( err, errSize, "Cannot rename %s: %s", part, strerror( errno ) );
		lc_remove( part );
		return 0;
	}
	return 1;
}

int lc_copyjob_run( lc_copyjob *j, volatile unsigned long long *progress,
	volatile signed char *cancel, char *err, size_t errSize )
{
	runCtx rc;
	volatile unsigned long long dummyProgress = 0;
	volatile signed char dummyCancel = 0;
	char localErr[LC_MAX_PATH * 2 + 128] = "";
	char (*created)[LC_MAX_PATH] = NULL;
	int nCreated = 0, i, failed = 0, wasCancelled = 0;
	char *buf = NULL;

	rc.progress = progress ? progress : &dummyProgress;
	rc.cancel = cancel ? cancel : &dummyCancel;
	if ( err && errSize )
		err[0] = 0;
	created = calloc( (size_t)( j->n ? j->n : 1 ), sizeof( *created ) );
	buf = malloc( COPY_BLOCK );
	if ( !created || !buf ) {
		free( created );
		free( buf );
		lc_strlcpy( err, "Out of memory.", errSize );
		return 0;
	}
	for ( i = 0; i < j->n && !failed; i++ ) {
		item_t *it = &j->items[i];
		char dir[LC_MAX_PATH];
		int existed = lc_is_file( it->to ), ok;

		if ( *rc.cancel ) {
			lc_strlcpy( localErr, "Cancelled.", sizeof( localErr ) );
			failed = 1;
			break;
		}
		lc_path_dirname( dir, sizeof( dir ), it->to );
		lc_mkdir_p( dir );
#if LC_HAVE_GOG
		if ( it->isGog )
			ok = GOG_ExtractFile( it->from, &it->gog, it->to, rc.progress, rc.cancel, localErr, sizeof( localErr ) );
		else
#endif
			ok = CopyOne( &rc, it->from, it->to, buf, localErr, sizeof( localErr ) );
		if ( !ok ) {
			failed = 1;
			break;
		}
		if ( !existed )
			lc_strlcpy( created[nCreated++], it->to, LC_MAX_PATH );
	}
	free( buf );
	if ( failed ) {
		/* remove what this job created; files that existed before stay */
		for ( i = 0; i < nCreated; i++ )
			lc_remove( created[i] );
		wasCancelled = *rc.cancel || !strcmp( localErr, "Cancelled." );
		lc_strlcpy( err, localErr, errSize );
		lc_log( "Copy failed: %s", localErr );
		free( created );
		return wasCancelled ? -1 : 0;
	}
	free( created );
	{
		char names[1024], size[64], pak[LC_MAX_PATH];

		NamesOf( j, names, sizeof( names ) );
		lc_format_bytes( *rc.progress, size, sizeof( size ) );
		if ( j->kind == KIND_COPY ) {
			if ( !lc_child_ci( j->homeBase, "pak0.pk3", pak, sizeof( pak ) ) ) {
				lc_strlcpy( err, LC_MSG_PAK0_NOT_COPIED, errSize );
				return 0;
			}
			lc_log( "Copied %s of game files to \"%s\"", size, j->homeBase );
		} else if ( j->kind == KIND_ADD )
			lc_log( "Added %s (%s) to \"%s\"", names, size, j->homeBase );
		else
			lc_log( "GOG installer: unpacked %s (%s) to \"%s\"", names, size, j->homeBase );
	}
	return 1;
}

int lc_copyjob_check_space( const lc_copyjob *j, char *title, size_t titleSize, char *text, size_t textSize )
{
	unsigned long long freeBytes, need = j->total + 64ull * 1024 * 1024;
	char needS[64], freeS[64], totalS[64];

	lc_mkdir_p( j->homeBase );
	freeBytes = lc_free_space( j->homeBase );
	if ( !freeBytes || freeBytes >= need )
		return 1;
	lc_format_bytes( need, needS, sizeof( needS ) );
	lc_format_bytes( freeBytes, freeS, sizeof( freeS ) );
	lc_format_bytes( j->total, totalS, sizeof( totalS ) );
	lc_strlcpy( title, "Not enough disk space", titleSize );
	if ( j->kind == KIND_COPY )
		lc_snprintf( text, textSize, "Copying the game files needs %s, but only %s is free on the disk containing\n%s.\n\n"
			"Free up some space, or tick \"Use BaseEF files in place rather than copy\" to use the game folder directly.",
			needS, freeS, j->homeBase );
	else if ( j->kind == KIND_UNPACK )
		lc_snprintf( text, textSize, "Unpacking the game files needs %s, but only %s is free.", needS, freeS );
	else
		lc_snprintf( text, textSize, "Adding the files needs %s, but only %s is free.", totalS, freeS );
	return 0;
}

void lc_copyjob_failure_texts( const lc_copyjob *j, const char *err, char *title, size_t titleSize,
	char *text, size_t textSize )
{
	if ( !strcmp( err, LC_MSG_PAK0_NOT_COPIED ) ) {
		lc_strlcpy( title, "Copying the game files failed", titleSize );
		lc_strlcpy( text, err, textSize );
	} else if ( j->kind == KIND_UNPACK ) {
		lc_strlcpy( title, "Unpacking the GOG installer failed", titleSize );
		lc_snprintf( text, textSize, "%s.\n\nThe partly unpacked files were removed. If the download is damaged, "
			"download it again from GOG; otherwise: " LC_MSG_USE_BASEEF, err );
	} else {
		lc_strlcpy( title, "Copying the game files failed", titleSize );
		lc_snprintf( text, textSize, "%s\n\nThe partial copy was removed.", err );
	}
}

void lc_installer_error_alert( const char *exe, const char *phrase, char *title, size_t titleSize,
	char *text, size_t textSize )
{
	lc_strlcpy( title, "This installer can't be used", titleSize );
	lc_snprintf( text, textSize, "%s\n\nThis app can't unpack it: %s.\n\n" LC_MSG_USE_BASEEF, lc_path_basename( exe ), phrase );
}

void lc_nothing_to_add_alert( int unpackInstaller, int present, char *title, size_t titleSize, char *text, size_t textSize )
{
	lc_strlcpy( title, "Nothing to add", titleSize );
	if ( unpackInstaller )
		lc_strlcpy( text, "The game already has all of this installer's .pk3 files.", textSize );
	else if ( present == 1 )
		lc_strlcpy( text, "The game already has that .pk3 file.", textSize );
	else if ( present > 1 )
		lc_strlcpy( text, "The game already has all of those .pk3 files.", textSize );
	else
		lc_strlcpy( text, "No .pk3 files were found there. Choose .pk3 files, or the BaseEF folder that contains them.", textSize );
}

void lc_copyjob_title( const lc_copyjob *j, char *out, size_t size )
{
	lc_strlcpy( out, j->kind == KIND_UNPACK ? "Unpacking the GOG installer" : "Copying game files", size );
}

void lc_copyjob_progress_text( const lc_copyjob *j, unsigned long long done, char *out, size_t size )
{
	( void )done;			/* the label is fixed; the bar shows the progress */
	lc_snprintf( out, size, j->kind == KIND_UNPACK ? "Unpacking game files to %s \xe2\x80\xa6" :
		"Copying game files to %s \xe2\x80\xa6", j->homeBase );
}
