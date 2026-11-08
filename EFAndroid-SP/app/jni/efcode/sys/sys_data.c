/*
===========================================================================
Windows / Linux: where the game data is (no launcher yet).

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).

The download ships no game data. At startup, unless the command line sets the
data paths itself (fs_basepath, fs_homepath, com_homepath, ...), this looks
without any UI for <BaseEF>/pak0.pk3 (both names in any case on Linux) in the
same order as the macOS app (sys_osx_data.m):
  a) the game folder remembered in <homepath>/launcher.cfg, a line
         gamefolder "<folder holding BaseEF>"
     (written by hand for now; the launchers will write it);
  b) <homepath>/baseEF (files copied there; fs_homepath is searched anyway);
  c) a baseEF folder next to the executable (for an AppImage: next to the
     .AppImage file, its own directory being a read-only mount);
  d) well-known installs:
       Windows: GOG's registry entries (HKLM\SOFTWARE\[WOW6432Node\]GOG.com\
                Games\*, value "path"), uninstall entries named "...Elite
                Force...", C:\GOG Games\*, Program Files (x86)\GOG Galaxy\
                Games\*, Raven\*, Activision\*;
       Linux:   Heroic (~/Games/Heroic/*, also its Flatpak and an SD card on a
                Steam Deck), and the drive_c of Wine/Lutris/Bottles/Heroic/
                Proton prefixes: GOG Games\*, Program Files*\GOG Galaxy\
                Games\*, Raven\*, Activision\*, Program Files*\*.
The folder found becomes fs_basepath (b: the default is kept). If there is
none, a message box says where to put pak0.pk3 and the game quits (a
dedicated server says it on stderr). A folder chooser comes with the launcher.

On Linux the engine looks for exactly "baseEF" under fs_basepath; when the
found folder is named differently (GOG's "BaseEF"), fs_basepath is
<homepath>/.gamedata, holding a symlink baseEF -> the found folder.

Runs before Com_Init: no Z_Malloc and no Com_Printf here; messages go to
stderr and are replayed into the console by Sys_DataPrintEarlyLog.
===========================================================================
*/

#if !defined( __APPLE__ ) && !defined( __ANDROID__ ) && !defined( __EMSCRIPTEN__ )

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "sys_local.h"
#include "sys_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#ifdef _WIN32
#	include <windows.h>
#	include <shlobj.h>
#else
#	include <sys/types.h>
#	include <sys/stat.h>
#	include <dirent.h>
#	include <unistd.h>
#endif

#ifndef DEDICATED
#	ifdef USE_LOCAL_HEADERS
#		include "SDL.h"
#	else
#		include <SDL.h>
#	endif
#endif

#define DATA_PATH		1024
#define LAUNCHER_CFG	"launcher.cfg"
#define MAX_SUBDIRS		512

static char earlyLog[8192];
static char result[DATA_PATH];

static void DataLog( const char *fmt, ... ) __attribute__ ((format (printf, 1, 2)));
static void DataLog( const char *fmt, ... )
{
	char line[DATA_PATH * 2];
	va_list ap;

	va_start( ap, fmt );
	Q_vsnprintf( line, sizeof( line ), fmt, ap );
	va_end( ap );

	fprintf( stderr, "%s\n", line );
	Q_strcat( earlyLog, sizeof( earlyLog ), line );
	Q_strcat( earlyLog, sizeof( earlyLog ), "\n" );
}

void Sys_DataPrintEarlyLog( void )
{
	if ( earlyLog[0] )
		Com_Printf( "%s", earlyLog );
	earlyLog[0] = '\0';
}

static qboolean TestEnv( const char *name )
{
	const char *v = getenv( name );
	return v && *v && strcmp( v, "0" ) ? qtrue : qfalse;
}

static void Join( char *out, size_t size, const char *a, const char *b )
{
	size_t n = strlen( a );

	if ( n && ( a[n - 1] == '/' || a[n - 1] == '\\' ) )
		Com_sprintf( out, size, "%s%s", a, b );
	else
		Com_sprintf( out, size, "%s%c%s", a, PATH_SEP, b );
}

// the directory part of path (path itself if it has no separator)
static void Dirname( char *out, size_t size, const char *path )
{
	char *s;

	Q_strncpyz( out, path, size );
	s = strrchr( out, '/' );
#ifdef _WIN32
	{
		char *b = strrchr( out, '\\' );
		if ( b > s )
			s = b;
	}
#endif
	if ( s && s != out )
		*s = '\0';
}

static const char *Basename( const char *path )
{
	const char *s = strrchr( path, '/' );
#ifdef _WIN32
	const char *b = strrchr( path, '\\' );
	if ( b > s )
		s = b;
#endif
	return s ? s + 1 : path;
}

/* ------------------------------------------------------------------------
   File system, UTF-8 paths
   ------------------------------------------------------------------------ */

#ifdef _WIN32
static qboolean ToWide( WCHAR *out, int count, const char *in )
{
	return MultiByteToWideChar( CP_UTF8, 0, in, -1, out, count ) > 0 ? qtrue : qfalse;
}

static qboolean ToUTF8( char *out, int size, const WCHAR *in )
{
	return WideCharToMultiByte( CP_UTF8, 0, in, -1, out, size, NULL, NULL ) > 0 ? qtrue : qfalse;
}

static DWORD Attributes( const char *path )
{
	WCHAR w[DATA_PATH];
	return ToWide( w, ARRAY_LEN( w ), path ) ? GetFileAttributesW( w ) : INVALID_FILE_ATTRIBUTES;
}

static qboolean IsDir( const char *path )
{
	DWORD a = Attributes( path );
	return a != INVALID_FILE_ATTRIBUTES && ( a & FILE_ATTRIBUTE_DIRECTORY ) ? qtrue : qfalse;
}

static qboolean IsFile( const char *path )
{
	DWORD a = Attributes( path );
	return a != INVALID_FILE_ATTRIBUTES && !( a & FILE_ATTRIBUTE_DIRECTORY ) ? qtrue : qfalse;
}

static FILE *DataFOpen( const char *path, const char *mode )
{
	WCHAR w[DATA_PATH], m[8];
	if ( !ToWide( w, ARRAY_LEN( w ), path ) || !ToWide( m, ARRAY_LEN( m ), mode ) )
		return NULL;
	return _wfopen( w, m );
}

// names of the subdirectories of dir, each NUL-terminated, then an empty one;
// malloc'd (NULL if none or unreadable)
static char *Subdirs( const char *dir, int *count )
{
	WCHAR w[DATA_PATH];
	char pattern[DATA_PATH];
	WIN32_FIND_DATAW fd;
	HANDLE h;
	char *list = NULL;
	size_t len = 0;

	*count = 0;
	Join( pattern, sizeof( pattern ), dir, "*" );
	if ( !ToWide( w, ARRAY_LEN( w ), pattern ) )
		return NULL;
	h = FindFirstFileW( w, &fd );
	if ( h == INVALID_HANDLE_VALUE )
		return NULL;
	do {
		char name[DATA_PATH];
		size_t n;
		char *grown;

		if ( !( fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) || !ToUTF8( name, sizeof( name ), fd.cFileName ) )
			continue;
		if ( !strcmp( name, "." ) || !strcmp( name, ".." ) )
			continue;
		n = strlen( name ) + 1;
		if ( !( grown = realloc( list, len + n + 1 ) ) )
			break;
		list = grown;
		memcpy( list + len, name, n );
		len += n;
		list[len] = '\0';
		(*count)++;
	} while ( *count < MAX_SUBDIRS && FindNextFileW( h, &fd ) );
	FindClose( h );
	return list;
}

static qboolean MakeDir( const char *path )
{
	WCHAR w[DATA_PATH];
	return ToWide( w, ARRAY_LEN( w ), path ) && ( CreateDirectoryW( w, NULL ) || GetLastError( ) == ERROR_ALREADY_EXISTS );
}

#else	// POSIX

static qboolean IsDir( const char *path )
{
	struct stat st;
	return !stat( path, &st ) && S_ISDIR( st.st_mode ) ? qtrue : qfalse;
}

static qboolean IsFile( const char *path )
{
	struct stat st;
	return !stat( path, &st ) && S_ISREG( st.st_mode ) ? qtrue : qfalse;
}

static FILE *DataFOpen( const char *path, const char *mode )
{
	return fopen( path, mode );
}

static char *Subdirs( const char *dir, int *count )
{
	DIR *d = opendir( dir );
	struct dirent *e;
	char *list = NULL;
	size_t len = 0;

	*count = 0;
	if ( !d )
		return NULL;
	while ( *count < MAX_SUBDIRS && ( e = readdir( d ) ) != NULL ) {
		char path[DATA_PATH];
		size_t n;
		char *grown;

		if ( !strcmp( e->d_name, "." ) || !strcmp( e->d_name, ".." ) )
			continue;
		Join( path, sizeof( path ), dir, e->d_name );
		if ( !IsDir( path ) )
			continue;
		n = strlen( e->d_name ) + 1;
		if ( !( grown = realloc( list, len + n + 1 ) ) )
			break;
		list = grown;
		memcpy( list + len, e->d_name, n );
		len += n;
		list[len] = '\0';
		(*count)++;
	}
	closedir( d );
	return list;
}

static qboolean MakeDir( const char *path )
{
	return !mkdir( path, 0755 ) || errno == EEXIST ? qtrue : qfalse;
}
#endif

// mkdir -p
static qboolean MakePath( const char *path )
{
	char p[DATA_PATH];
	char *s;

	Q_strncpyz( p, path, sizeof( p ) );
	for ( s = p + 1; *s; s++ ) {
		if ( *s == '/' || *s == '\\' ) {
			char c = *s;
			*s = '\0';
#ifdef _WIN32
			if ( !( s - p == 2 && p[1] == ':' ) )	// not the drive
#endif
				MakeDir( p );
			*s = c;
		}
	}
	return MakeDir( p );
}

// dir/name if it exists, matching name case-insensitively where the file
// system doesn't (Linux)
static qboolean ChildCI( const char *dir, const char *name, char *out, size_t size )
{
	Join( out, size, dir, name );
	if ( IsFile( out ) || IsDir( out ) )
		return qtrue;
#ifndef _WIN32
	{
		DIR *d = opendir( dir );
		struct dirent *e;
		qboolean found = qfalse;

		if ( !d )
			return qfalse;
		while ( !found && ( e = readdir( d ) ) != NULL ) {
			if ( !Q_stricmp( e->d_name, name ) ) {
				Join( out, size, dir, e->d_name );
				found = qtrue;
			}
		}
		closedir( d );
		return found;
	}
#else
	return qfalse;
#endif
}

/* ------------------------------------------------------------------------
   Homepath
   ------------------------------------------------------------------------ */

/*
=================
Sys_DataHomePath

The homepath Sys_DefaultHomePath will use once com_homepath is EF_HOMEPATH_NAME
(Sys_PlatformPreInit), known before Com_Init.
=================
*/
const char *Sys_DataHomePath( void )
{
	static char home[DATA_PATH];

	if ( home[0] )
		return home;
#ifdef _WIN32
	{
		WCHAR w[MAX_PATH];
		char appData[DATA_PATH];

		if ( SUCCEEDED( SHGetFolderPathW( NULL, CSIDL_APPDATA, NULL, 0, w ) ) && ToUTF8( appData, sizeof( appData ), w ) )
			Join( home, sizeof( home ), appData, EF_HOMEPATH_NAME );
	}
#else
	{
		char name[MAX_OSPATH];
		const char *p;
		char *s;

		// lower case, spaces as hyphens, as Sys_DefaultHomePath (sys_unix.c)
		Q_strncpyz( name, EF_HOMEPATH_NAME, sizeof( name ) );
		for ( s = name; *s; s++ )
			*s = ( *s == ' ' ) ? '-' : tolower( *s );
		if ( ( p = getenv( "XDG_DATA_HOME" ) ) != NULL && *p )
			Join( home, sizeof( home ), p, name );
		else if ( ( p = getenv( "HOME" ) ) != NULL && *p )
			Com_sprintf( home, sizeof( home ), "%s/.local/share/%s", p, name );
	}
#endif
	return home;
}

static const char *HomeDir( void )
{
#ifdef _WIN32
	static char profile[DATA_PATH];
	WCHAR w[MAX_PATH];

	if ( !profile[0] && SUCCEEDED( SHGetFolderPathW( NULL, CSIDL_PROFILE, NULL, 0, w ) ) )
		ToUTF8( profile, sizeof( profile ), w );
	return profile;
#else
	const char *h = getenv( "HOME" );
	return h ? h : "";
#endif
}

/* ------------------------------------------------------------------------
   Candidates
   ------------------------------------------------------------------------ */

/*
=================
GameFolder

qtrue when folder (an install, or its BaseEF folder itself) has
<BaseEF>/pak0.pk3. *outFolder is the folder holding BaseEF, *outBase BaseEF.
=================
*/
static qboolean GameFolder( const char *folder, char *outFolder, char *outBase )
{
	char base[DATA_PATH], pak0[DATA_PATH];

	if ( !folder || !*folder || !IsDir( folder ) )
		return qfalse;
	if ( ChildCI( folder, BASEGAME, base, sizeof( base ) ) && IsDir( base )
		&& ChildCI( base, "pak0.pk3", pak0, sizeof( pak0 ) ) ) {
		Q_strncpyz( outFolder, folder, DATA_PATH );
		Q_strncpyz( outBase, base, DATA_PATH );
		return qtrue;
	}
	// the BaseEF folder itself
	if ( !Q_stricmp( Basename( folder ), BASEGAME ) && ChildCI( folder, "pak0.pk3", pak0, sizeof( pak0 ) ) ) {
		Dirname( outFolder, DATA_PATH, folder );
		Q_strncpyz( outBase, folder, DATA_PATH );
		return qtrue;
	}
	return qfalse;
}

static char foundFolder[DATA_PATH], foundBase[DATA_PATH];

static qboolean Candidate( const char *folder, const char *what )
{
	if ( !GameFolder( folder, foundFolder, foundBase ) )
		return qfalse;
	DataLog( "Game data: %s, \"%s\"", what, foundBase );
	return qtrue;
}

/*
=================
Glob

Tries root/pattern, where pattern's '/'-separated components are names
(matched case-insensitively on Linux) or "*" (every subdirectory). Stops at
the first game folder.
=================
*/
static qboolean Glob( const char *root, const char *pattern, const char *what )
{
	char comp[MAX_OSPATH], path[DATA_PATH];
	const char *slash, *rest;

	if ( !root || !*root )
		return qfalse;
	if ( !*pattern )
		return Candidate( root, what );

	slash = strchr( pattern, '/' );
	rest = slash ? slash + 1 : "";
	Q_strncpyz( comp, pattern, slash && slash - pattern + 1 < (int)sizeof( comp ) ? (int)( slash - pattern + 1 ) : (int)sizeof( comp ) );

	if ( !strcmp( comp, "*" ) ) {
		int count, i;
		char *list = Subdirs( root, &count ), *name = list;
		qboolean found = qfalse;

		for ( i = 0; i < count && !found; i++, name += strlen( name ) + 1 ) {
			Join( path, sizeof( path ), root, name );
			found = Glob( path, rest, what );
		}
		free( list );
		return found;
	}
	if ( !ChildCI( root, comp, path, sizeof( path ) ) || !IsDir( path ) )
		return qfalse;
	return Glob( path, rest, what );
}

/* ------------------------------------------------------------------------
   a) launcher.cfg
   ------------------------------------------------------------------------ */

// the gamefolder line of <homepath>/launcher.cfg, or ""
static void RememberedFolder( char *out, size_t size )
{
	char path[DATA_PATH], line[DATA_PATH];
	FILE *f;

	*out = '\0';
	Join( path, sizeof( path ), Sys_DataHomePath( ), LAUNCHER_CFG );
	if ( !( f = DataFOpen( path, "r" ) ) )
		return;
	while ( fgets( line, sizeof( line ), f ) ) {
		char *s = line, *e;

		while ( *s == ' ' || *s == '\t' )
			s++;
		if ( Q_stricmpn( s, "gamefolder", 10 ) || ( s[10] != ' ' && s[10] != '\t' ) )
			continue;
		s += 10;
		while ( *s == ' ' || *s == '\t' )
			s++;
		e = s + strlen( s );
		while ( e > s && ( e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t' ) )
			*--e = '\0';
		if ( *s == '"' ) {
			s++;
			if ( e > s && e[-1] == '"' )
				*--e = '\0';
		}
		Q_strncpyz( out, s, size );
	}
	fclose( f );
}

/* ------------------------------------------------------------------------
   d) well-known installs
   ------------------------------------------------------------------------ */

#ifdef _WIN32
static qboolean RegString( HKEY key, const WCHAR *value, char *out, int size )
{
	WCHAR w[DATA_PATH];
	DWORD type, bytes = sizeof( w ) - sizeof( WCHAR );

	if ( RegQueryValueExW( key, value, NULL, &type, (LPBYTE)w, &bytes ) != ERROR_SUCCESS
		|| ( type != REG_SZ && type != REG_EXPAND_SZ ) )
		return qfalse;
	w[bytes / sizeof( WCHAR )] = 0;
	return ToUTF8( out, size, w ) && *out;
}

// every subkey of root\path: valueName is a game folder (and, if nameValue,
// that subkey's nameValue contains nameMatch)
static qboolean RegScan( HKEY root, const WCHAR *path, const WCHAR *valueName,
	const WCHAR *nameValue, const char *nameMatch, const char *what )
{
	HKEY key;
	DWORD i;
	qboolean found = qfalse;

	if ( RegOpenKeyExW( root, path, 0, KEY_READ, &key ) != ERROR_SUCCESS )
		return qfalse;
	for ( i = 0; !found; i++ ) {
		WCHAR name[256];
		DWORD len = ARRAY_LEN( name );
		HKEY sub;
		char folder[DATA_PATH], display[DATA_PATH];

		if ( RegEnumKeyExW( key, i, name, &len, NULL, NULL, NULL, NULL ) != ERROR_SUCCESS )
			break;
		if ( RegOpenKeyExW( key, name, 0, KEY_READ, &sub ) != ERROR_SUCCESS )
			continue;
		if ( ( !nameValue || ( RegString( sub, nameValue, display, sizeof( display ) ) && Q_stristr( display, nameMatch ) ) )
			&& RegString( sub, valueName, folder, sizeof( folder ) ) )
			found = Candidate( folder, what );
		RegCloseKey( sub );
	}
	RegCloseKey( key );
	return found;
}

static qboolean FindWellKnown( void )
{
	static const char *dirPatterns[] = {
		"GOG Galaxy/Games/*", "GOG Games/*", "Raven/*", "Activision/*", "Raven Software/*",
		"Star Trek Voyager Elite Force", "Star Trek Voyager - Elite Force", "Elite Force", NULL
	};
	const char *roots[4];
	char drive[8];
	const char *sys = getenv( "SystemDrive" );
	int r, p;

	// GOG Galaxy and GOG's offline installers register every game here
	if ( RegScan( HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\GOG.com\\Games", L"path", NULL, NULL, "GOG install" )
		|| RegScan( HKEY_LOCAL_MACHINE, L"SOFTWARE\\GOG.com\\Games", L"path", NULL, NULL, "GOG install" ) )
		return qtrue;
	// retail CD installs and anything else with an uninstall entry
	if ( RegScan( HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
			L"InstallLocation", L"DisplayName", "Elite Force", "installed game" )
		|| RegScan( HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
			L"InstallLocation", L"DisplayName", "Elite Force", "installed game" )
		|| RegScan( HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
			L"InstallLocation", L"DisplayName", "Elite Force", "installed game" ) )
		return qtrue;

	Com_sprintf( drive, sizeof( drive ), "%s\\", sys && *sys ? sys : "C:" );
	roots[0] = drive;
	roots[1] = getenv( "ProgramFiles(x86)" );
	roots[2] = getenv( "ProgramFiles" );
	roots[3] = NULL;
	for ( r = 0; r < 3; r++ )
		for ( p = 0; dirPatterns[p]; p++ )
			if ( Glob( roots[r], dirPatterns[p], "installed game" ) )
				return qtrue;
	return qfalse;
}

#else	// Linux

static qboolean FindWellKnown( void )
{
	// where launchers put Windows games (relative to $HOME, or absolute)
	static const char *installs[] = {
		"Games/Heroic/*",
		".var/app/com.heroicgameslauncher.hgl/Games/Heroic/*",
		"/run/media/*/Heroic/*",			// Steam Deck SD card
		"/run/media/*/*/Heroic/*",
		"/run/media/*/Games/Heroic/*",
		NULL
	};
	// Wine prefixes: Wine, Lutris (~/Games/<game>), Heroic, Bottles, Proton
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
	const char *home = HomeDir( );
	char pattern[DATA_PATH];
	int i, j;

	if ( !*home )
		return qfalse;
	for ( i = 0; installs[i]; i++ ) {
		if ( installs[i][0] == '/' ) {
			if ( Glob( "/", installs[i] + 1, "Heroic install" ) )
				return qtrue;
		} else if ( Glob( home, installs[i], "Heroic install" ) )
			return qtrue;
	}
	for ( i = 0; prefixes[i]; i++ ) {
		for ( j = 0; driveC[j]; j++ ) {
			Com_sprintf( pattern, sizeof( pattern ), "%s/%s", prefixes[i], driveC[j] );
			if ( Glob( home, pattern, "Wine prefix" ) )
				return qtrue;
		}
	}
	return qfalse;
}
#endif

/* ------------------------------------------------------------------------
   Result
   ------------------------------------------------------------------------ */

/*
=================
UseFound

The fs_basepath for foundFolder/foundBase. Linux needs the folder named
exactly BASEGAME: otherwise a symlink to it in <homepath>/.gamedata.
=================
*/
static const char *UseFound( void )
{
#ifndef _WIN32
	if ( strcmp( Basename( foundBase ), BASEGAME ) ) {
		char dir[DATA_PATH], link[DATA_PATH], target[DATA_PATH];
		ssize_t n;

		Join( dir, sizeof( dir ), Sys_DataHomePath( ), ".gamedata" );
		Join( link, sizeof( link ), dir, BASEGAME );
		n = readlink( link, target, sizeof( target ) - 1 );
		if ( n >= 0 )
			target[n] = '\0';
		if ( n < 0 || strcmp( target, foundBase ) ) {
			MakePath( dir );
			unlink( link );		// only ever a symlink this function made
			if ( symlink( foundBase, link ) ) {
				DataLog( "WARNING: could not link \"%s\" to \"%s\": %s", link, foundBase, strerror( errno ) );
				Q_strncpyz( result, foundFolder, sizeof( result ) );
				return result;
			}
		}
		DataLog( "Game data: \"%s\" is linked as \"%s\"", foundBase, link );
		Q_strncpyz( result, dir, sizeof( result ) );
		return result;
	}
#endif
	Q_strncpyz( result, foundFolder, sizeof( result ) );
	return result;
}

/*
=================
Sys_DataOverridden

qtrue when the command line sets the data paths itself (no lookup).
=================
*/
qboolean Sys_DataOverridden( int argc, char **argv )
{
	static const char *overrides[] = { "fs_basepath", "fs_homepath", "com_homepath",
		"fs_cdpath", "fs_steampath", "fs_gogpath", "com_basegame", NULL };
	int i, j;

	for ( i = 1; i < argc; i++ )
		for ( j = 0; overrides[j]; j++ )
			if ( !Q_stricmp( argv[i], overrides[j] ) )
				return qtrue;
	return qfalse;
}

// the directory holding the executable (for an AppImage, the .AppImage file)
static void ExecutableDir( char *out, size_t size, char **argv )
{
	const char *appImage = getenv( "APPIMAGE" );

	*out = '\0';
	if ( appImage && *appImage ) {
		Dirname( out, size, appImage );
		return;
	}
#ifndef DEDICATED
	{
		char *base = SDL_GetBasePath( );
		if ( base ) {
			Q_strncpyz( out, base, size );
			SDL_free( base );
			return;
		}
	}
#endif
	if ( argv && argv[0] )
		Dirname( out, size, argv[0] );
}

static void NotFound( qboolean headless )
{
	char home[DATA_PATH], cfg[DATA_PATH], msg[DATA_PATH * 4];

	Join( home, sizeof( home ), Sys_DataHomePath( ), BASEGAME );
	Join( cfg, sizeof( cfg ), Sys_DataHomePath( ), LAUNCHER_CFG );
	Com_sprintf( msg, sizeof( msg ),
		"The Elite Force game files were not found.\n\n"
		"Copy the .pk3 files (at least pak0.pk3) from the BaseEF folder of an Elite Force "
		"installation (GOG, or the CD with the 1.2 patch) into:\n\n%s\n\n"
		"or put that BaseEF folder next to the game%s, or add a line\n"
		"gamefolder \"<the folder holding BaseEF>\"\nto %s.\n\n"
		"A dedicated server or a shortcut can also pass +set fs_basepath <folder holding BaseEF>.",
		home,
#ifdef _WIN32
		" (next to EliteForce.exe)",
#else
		getenv( "APPIMAGE" ) ? " (next to the .AppImage file)" : "",
#endif
		cfg );

	fprintf( stderr, "ERROR: %s\n", msg );
#ifndef DEDICATED
	if ( !headless && !TestEnv( "VOYAGERSP_TEST_NODIALOG" ) )
		SDL_ShowSimpleMessageBox( SDL_MESSAGEBOX_ERROR, "Elite Force: game files not found", msg, NULL );
#endif
	exit( 1 );
}

/*
=================
Sys_DataResolve

Returns the fs_basepath to pass, or NULL for the default (homepath copy, or
the command line sets the paths). Exits when there is no game data.
=================
*/
const char *Sys_DataResolve( int argc, char **argv, qboolean headless )
{
	char remembered[DATA_PATH], path[DATA_PATH], exeDir[DATA_PATH];

	if ( Sys_DataOverridden( argc, argv ) )
		return NULL;
	if ( !*Sys_DataHomePath( ) ) {
		DataLog( "Game data: no home directory, using the defaults" );
		return NULL;
	}

	// a) remembered game folder
	RememberedFolder( remembered, sizeof( remembered ) );
	if ( *remembered ) {
		if ( Candidate( remembered, "remembered game folder" ) )
			return UseFound( );
		DataLog( "Game data: remembered game folder \"%s\" has no %s/pak0.pk3", remembered, BASEGAME );
	}

	// b) copied into the homepath (searched as fs_homepath; default basepath)
	Join( path, sizeof( path ), Sys_DataHomePath( ), BASEGAME );
	if ( ChildCI( path, "pak0.pk3", remembered, sizeof( remembered ) ) ) {
		DataLog( "Game data: \"%s\"", path );
		return NULL;
	}

	// c) next to the executable
	ExecutableDir( exeDir, sizeof( exeDir ), argv );
	if ( *exeDir && Candidate( exeDir, "next to the game" ) )
		return UseFound( );

	// d) well-known installs
	if ( FindWellKnown( ) )
		return UseFound( );

	DataLog( "Game data not found (no %s/pak0.pk3 in the remembered folder, \"%s\", next to the game "
		"or in a known install)", BASEGAME, path );
	NotFound( headless );
	return NULL;
}

#endif
