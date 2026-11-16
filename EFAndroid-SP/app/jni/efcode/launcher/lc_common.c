/*
===========================================================================
Launcher core: the base layer (lc_common.h).

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#if !defined( _WIN32 )
#	define _GNU_SOURCE 1
#	if defined( __APPLE__ )
#		define _DARWIN_C_SOURCE 1
#	endif
#endif

#include "lc_common.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#	ifndef WIN32_LEAN_AND_MEAN
#		define WIN32_LEAN_AND_MEAN
#	endif
#	include <winsock2.h>
#	include <windows.h>
#	include <shellapi.h>
#	include <shlobj.h>
#	include <process.h>
#	define SECURITY_WIN32
#	include <security.h>
#	include <io.h>
#	include <direct.h>
#else
#	include <dirent.h>
#	include <dlfcn.h>
#	include <fcntl.h>
#	include <pthread.h>
#	include <pwd.h>
#	include <signal.h>
#	include <sys/stat.h>
#	include <sys/statvfs.h>
#	include <sys/types.h>
#	include <sys/wait.h>
#	include <unistd.h>
#	if defined( __APPLE__ )
#		include <mach-o/dyld.h>
#	endif
#endif

#ifndef EF_HOMEPATH_NAME
#	define EF_HOMEPATH_NAME "EliteForce-Marino"
#endif

/* ===================================================================== */
/* logging, env                                                          */

static lc_log_sink logSink;
static void *logUser;

void lc_set_log_sink( lc_log_sink sink, void *user )
{
	logSink = sink;
	logUser = user;
}

void lc_log( const char *fmt, ... )
{
	char line[2048];
	va_list ap;

	va_start( ap, fmt );
	vsnprintf( line, sizeof( line ), fmt, ap );
	va_end( ap );
	if ( logSink )
		logSink( line, logUser );
	else
		fprintf( stderr, "%s\n", line );
}

const char *lc_env( const char *name )
{
	const char *v = getenv( name );
	return ( v && *v ) ? v : NULL;
}

int lc_test_env( const char *name )
{
	const char *v = lc_env( name );
	return v && strcmp( v, "0" ) != 0;
}

int lc_coop_ui_enabled( void )
{
	return LC_COOP_BUILT;	/* only when the engine has co-op to start */
}

/* ===================================================================== */
/* strings                                                               */

size_t lc_strlcpy( char *dst, const char *src, size_t size )
{
	size_t n = strlen( src );
	if ( size ) {
		size_t c = n < size - 1 ? n : size - 1;
		memcpy( dst, src, c );
		dst[c] = 0;
	}
	return n;
}

size_t lc_strlcat( char *dst, const char *src, size_t size )
{
	size_t d = strnlen( dst, size );
	if ( d == size )
		return d + strlen( src );
	return d + lc_strlcpy( dst + d, src, size - d );
}

int lc_stricmp( const char *a, const char *b )
{
	for ( ;; a++, b++ ) {
		int ca = tolower( (unsigned char)*a ), cb = tolower( (unsigned char)*b );
		if ( ca != cb )
			return ca - cb;
		if ( !ca )
			return 0;
	}
}

int lc_strnicmp( const char *a, const char *b, size_t n )
{
	for ( ; n; n--, a++, b++ ) {
		int ca = tolower( (unsigned char)*a ), cb = tolower( (unsigned char)*b );
		if ( ca != cb )
			return ca - cb;
		if ( !ca )
			return 0;
	}
	return 0;
}

int lc_snprintf( char *dst, size_t size, const char *fmt, ... )
{
	va_list ap;
	int n;

	if ( !size )
		return 0;
	va_start( ap, fmt );
	n = vsnprintf( dst, size, fmt, ap );
	va_end( ap );
	dst[size - 1] = 0;
	return n;
}

char *lc_strdup( const char *s )
{
	size_t n;
	char *d;

	if ( !s )
		return NULL;
	n = strlen( s ) + 1;
	d = malloc( n );
	if ( d )
		memcpy( d, s, n );
	return d;
}

void lc_trim( char *s )
{
	char *p = s, *e;

	while ( *p && isspace( (unsigned char)*p ) )
		p++;
	if ( p != s )
		memmove( s, p, strlen( p ) + 1 );
	e = s + strlen( s );
	while ( e > s && isspace( (unsigned char)e[-1] ) )
		*--e = 0;
}

int lc_utf8_valid( const char *s )
{
	const unsigned char *p = (const unsigned char *)s;

	while ( *p ) {
		int n;
		if ( *p < 0x80 ) { p++; continue; }
		else if ( ( *p & 0xE0 ) == 0xC0 && *p >= 0xC2 ) n = 1;
		else if ( ( *p & 0xF0 ) == 0xE0 ) n = 2;
		else if ( ( *p & 0xF8 ) == 0xF0 && *p <= 0xF4 ) n = 3;
		else return 0;
		p++;
		while ( n-- ) {
			if ( ( *p & 0xC0 ) != 0x80 )
				return 0;
			p++;
		}
	}
	return 1;
}

void lc_utf8_truncate( char *s, size_t maxBytes )
{
	size_t n = strlen( s );

	if ( n <= maxBytes )
		return;
	n = maxBytes;
	/* back off continuation bytes, then the lead byte of a cut sequence */
	while ( n > 0 && ( (unsigned char)s[n] & 0xC0 ) == 0x80 )
		n--;
	s[n] = 0;
}

/* ===================================================================== */
/* Windows UTF-8 <-> UTF-16                                              */

#ifdef _WIN32
static wchar_t *Wide( const char *s )
{
	int n;
	wchar_t *w;

	if ( !s )
		return NULL;
	n = MultiByteToWideChar( CP_UTF8, 0, s, -1, NULL, 0 );
	if ( n <= 0 )
		return NULL;
	w = malloc( n * sizeof( wchar_t ) );
	if ( w )
		MultiByteToWideChar( CP_UTF8, 0, s, -1, w, n );
	return w;
}

static void Narrow( const wchar_t *w, char *out, size_t size )
{
	if ( !size )
		return;
	if ( !WideCharToMultiByte( CP_UTF8, 0, w, -1, out, (int)size, NULL, NULL ) )
		out[0] = 0;
	out[size - 1] = 0;
}
#endif

/* ===================================================================== */
/* paths and files                                                       */

static int IsSep( char c )
{
#ifdef _WIN32
	return c == '/' || c == '\\';
#else
	return c == '/';
#endif
}

void lc_path_join( char *out, size_t size, const char *a, const char *b )
{
	char tmp[LC_MAX_PATH * 2];
	size_t n;

	lc_strlcpy( tmp, a ? a : "", sizeof( tmp ) );
	n = strlen( tmp );
	if ( n && !IsSep( tmp[n - 1] ) && b && *b ) {
		char sep[2] = { LC_PATH_SEP, 0 };
		lc_strlcat( tmp, sep, sizeof( tmp ) );
	}
	if ( b ) {
		while ( n && IsSep( *b ) )
			b++;
		lc_strlcat( tmp, b, sizeof( tmp ) );
	}
	lc_strlcpy( out, tmp, size );
}

void lc_path_dirname( char *out, size_t size, const char *path )
{
	char tmp[LC_MAX_PATH];
	size_t n;

	lc_strlcpy( tmp, path, sizeof( tmp ) );
	n = strlen( tmp );
	while ( n > 1 && IsSep( tmp[n - 1] ) )
		tmp[--n] = 0;
	while ( n > 0 && !IsSep( tmp[n - 1] ) )
		n--;
	while ( n > 1 && IsSep( tmp[n - 1] ) )
		n--;
	tmp[n] = 0;
	if ( !n )
		lc_strlcpy( tmp, ".", sizeof( tmp ) );
	lc_strlcpy( out, tmp, size );
}

const char *lc_path_basename( const char *path )
{
	const char *p = path + strlen( path );

	while ( p > path && !IsSep( p[-1] ) )
		p--;
	return p;
}

#ifdef _WIN32
static DWORD Attr( const char *path )
{
	wchar_t *w = Wide( path );
	DWORD a = w ? GetFileAttributesW( w ) : INVALID_FILE_ATTRIBUTES;
	free( w );
	return a;
}

int lc_is_dir( const char *path )
{
	DWORD a = Attr( path );
	return a != INVALID_FILE_ATTRIBUTES && ( a & FILE_ATTRIBUTE_DIRECTORY );
}

int lc_is_file( const char *path )
{
	DWORD a = Attr( path );
	return a != INVALID_FILE_ATTRIBUTES && !( a & FILE_ATTRIBUTE_DIRECTORY );
}

int lc_is_symlink( const char *path )
{
	DWORD a = Attr( path );
	return a != INVALID_FILE_ATTRIBUTES && ( a & FILE_ATTRIBUTE_REPARSE_POINT );
}

static int StatW( const char *path, WIN32_FILE_ATTRIBUTE_DATA *d )
{
	wchar_t *w = Wide( path );
	int ok = w && GetFileAttributesExW( w, GetFileExInfoStandard, d );
	free( w );
	return ok;
}

int64_t lc_file_size( const char *path )
{
	WIN32_FILE_ATTRIBUTE_DATA d;
	if ( !StatW( path, &d ) )
		return -1;
	return ( (int64_t)d.nFileSizeHigh << 32 ) | d.nFileSizeLow;
}

int64_t lc_file_mtime( const char *path )
{
	WIN32_FILE_ATTRIBUTE_DATA d;
	int64_t t;
	if ( !StatW( path, &d ) )
		return -1;
	t = ( (int64_t)d.ftLastWriteTime.dwHighDateTime << 32 ) | d.ftLastWriteTime.dwLowDateTime;
	return t / 10000000 - 11644473600LL;
}

FILE *lc_fopen( const char *path, const char *mode )
{
	wchar_t *w = Wide( path ), *m = Wide( mode );
	FILE *f = ( w && m ) ? _wfopen( w, m ) : NULL;
	free( w );
	free( m );
	return f;
}

int lc_mkdir( const char *path )
{
	wchar_t *w = Wide( path );
	int ok = w && ( CreateDirectoryW( w, NULL ) || GetLastError( ) == ERROR_ALREADY_EXISTS );
	free( w );
	return ok && lc_is_dir( path );
}

int lc_remove( const char *path )
{
	wchar_t *w = Wide( path );
	int ok = 0;
	if ( w )
		ok = lc_is_dir( path ) ? RemoveDirectoryW( w ) : DeleteFileW( w );
	free( w );
	return ok;
}

int lc_rename( const char *from, const char *to )
{
	wchar_t *a = Wide( from ), *b = Wide( to );
	int ok = a && b && MoveFileExW( a, b, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED );
	free( a );
	free( b );
	return ok;
}

int lc_list_dir( const char *dir, int (*cb)( const char *name, int isDir, void *user ), void *user )
{
	char pattern[LC_MAX_PATH], name[LC_MAX_PATH];
	wchar_t *w;
	WIN32_FIND_DATAW fd;
	HANDLE h;
	int stop = 0;

	lc_path_join( pattern, sizeof( pattern ), dir, "*" );
	w = Wide( pattern );
	if ( !w )
		return 0;
	h = FindFirstFileW( w, &fd );
	free( w );
	if ( h == INVALID_HANDLE_VALUE )
		return 0;
	do {
		Narrow( fd.cFileName, name, sizeof( name ) );
		if ( !strcmp( name, "." ) || !strcmp( name, ".." ) )
			continue;
		stop = cb( name, ( fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) != 0, user );
	} while ( !stop && FindNextFileW( h, &fd ) );
	FindClose( h );
	return 1;
}

unsigned long long lc_free_space( const char *dir )
{
	wchar_t *w = Wide( dir );
	ULARGE_INTEGER avail;
	int ok = w && GetDiskFreeSpaceExW( w, &avail, NULL, NULL );
	free( w );
	return ok ? avail.QuadPart : 0;
}

#else /* POSIX */

int lc_is_dir( const char *path )
{
	struct stat st;
	return !stat( path, &st ) && S_ISDIR( st.st_mode );
}

int lc_is_file( const char *path )
{
	struct stat st;
	return !stat( path, &st ) && S_ISREG( st.st_mode );
}

int lc_is_symlink( const char *path )
{
	struct stat st;
	return !lstat( path, &st ) && S_ISLNK( st.st_mode );
}

int64_t lc_file_size( const char *path )
{
	struct stat st;
	return stat( path, &st ) ? -1 : (int64_t)st.st_size;
}

int64_t lc_file_mtime( const char *path )
{
	struct stat st;
	return stat( path, &st ) ? -1 : (int64_t)st.st_mtime;
}

FILE *lc_fopen( const char *path, const char *mode )
{
	return fopen( path, mode );
}

int lc_mkdir( const char *path )
{
	return ( !mkdir( path, 0755 ) || errno == EEXIST ) && lc_is_dir( path );
}

int lc_remove( const char *path )
{
	return !remove( path );
}

int lc_rename( const char *from, const char *to )
{
	return !rename( from, to );
}

int lc_list_dir( const char *dir, int (*cb)( const char *name, int isDir, void *user ), void *user )
{
	DIR *d = opendir( dir );
	struct dirent *e;
	int stop = 0;

	if ( !d )
		return 0;
	while ( !stop && ( e = readdir( d ) ) ) {
		char full[LC_MAX_PATH];
		if ( !strcmp( e->d_name, "." ) || !strcmp( e->d_name, ".." ) )
			continue;
		lc_path_join( full, sizeof( full ), dir, e->d_name );
		stop = cb( e->d_name, lc_is_dir( full ), user );
	}
	closedir( d );
	return 1;
}

unsigned long long lc_free_space( const char *dir )
{
	struct statvfs v;
	return statvfs( dir, &v ) ? 0 : (unsigned long long)v.f_bavail * v.f_frsize;
}

#endif

int lc_mkdir_p( const char *path )
{
	char tmp[LC_MAX_PATH];
	size_t i, n;

	lc_strlcpy( tmp, path, sizeof( tmp ) );
	n = strlen( tmp );
	for ( i = 1; i < n; i++ ) {
		if ( IsSep( tmp[i] ) ) {
			char c = tmp[i];
#ifdef _WIN32
			if ( i == 2 && tmp[1] == ':' )
				continue;
#endif
			tmp[i] = 0;
			if ( !lc_is_dir( tmp ) )
				lc_mkdir( tmp );
			tmp[i] = c;
		}
	}
	return lc_mkdir( tmp );
}

char *lc_read_file( const char *path, size_t *len )
{
	FILE *f = lc_fopen( path, "rb" );
	char *buf = NULL;
	size_t cap = 0, n = 0;

	if ( !f )
		return NULL;
	for ( ;; ) {
		size_t r;
		if ( n + 4096 + 1 > cap ) {
			char *nb;
			cap = cap ? cap * 2 : 8192;
			nb = realloc( buf, cap );
			if ( !nb ) {
				free( buf );
				fclose( f );
				return NULL;
			}
			buf = nb;
		}
		r = fread( buf + n, 1, cap - n - 1, f );
		n += r;
		if ( !r )
			break;
	}
	fclose( f );
	buf[n] = 0;
	if ( len )
		*len = n;
	return buf;
}

int lc_write_file_atomic( const char *path, const void *data, size_t len )
{
	char tmp[LC_MAX_PATH];
	FILE *f;
	int ok;

	lc_snprintf( tmp, sizeof( tmp ), "%s.tmp", path );
	f = lc_fopen( tmp, "wb" );
	if ( !f )
		return 0;
	ok = fwrite( data, 1, len, f ) == len;
	ok = !fflush( f ) && ok;
	ok = !fclose( f ) && ok;
	if ( !ok || !lc_rename( tmp, path ) ) {
		lc_remove( tmp );
		return 0;
	}
	return 1;
}

typedef struct { const char *want; char *out; size_t size; int found; const char *dir; } childCI_t;

static int ChildCB( const char *name, int isDir, void *user )
{
	childCI_t *c = user;
	(void)isDir;
	if ( !lc_stricmp( name, c->want ) ) {
		lc_path_join( c->out, c->size, c->dir, name );
		c->found = 1;
		return !strcmp( name, c->want );	/* keep looking for an exact match */
	}
	return 0;
}

int lc_child_ci( const char *dir, const char *name, char *out, size_t size )
{
	childCI_t c = { name, out, size, 0, dir };
	char direct[LC_MAX_PATH];

	lc_path_join( direct, sizeof( direct ), dir, name );
	if ( lc_is_dir( direct ) || lc_is_file( direct ) ) {
#ifdef _WIN32
		lc_strlcpy( out, direct, size );	/* case-insensitive file system */
		return 1;
#endif
	}
	lc_list_dir( dir, ChildCB, &c );
	return c.found;
}

const char *lc_home_dir( void )
{
	static char home[LC_MAX_PATH];

	if ( home[0] )
		return home;
#ifdef _WIN32
	{
		const wchar_t *w = _wgetenv( L"USERPROFILE" );
		if ( w )
			Narrow( w, home, sizeof( home ) );
	}
#else
	{
		const char *h = lc_env( "HOME" );
		if ( !h ) {
			struct passwd *pw = getpwuid( getuid( ) );
			h = pw ? pw->pw_dir : "/tmp";
		}
		lc_strlcpy( home, h, sizeof( home ) );
	}
#endif
	return home;
}

const char *lc_homepath( void )
{
	static char path[LC_MAX_PATH];
	const char *over;

	if ( path[0] )
		return path;
	if ( ( over = lc_env( "LC_HOMEPATH" ) ) ) {	/* unit tests */
		lc_strlcpy( path, over, sizeof( path ) );
		return path;
	}
#ifdef _WIN32
	{
		wchar_t w[MAX_PATH];
		char appdata[LC_MAX_PATH] = "";
		if ( SUCCEEDED( SHGetFolderPathW( NULL, CSIDL_APPDATA, NULL, 0, w ) ) )
			Narrow( w, appdata, sizeof( appdata ) );
		if ( !appdata[0] )
			lc_strlcpy( appdata, lc_home_dir( ), sizeof( appdata ) );
		lc_path_join( path, sizeof( path ), appdata, EF_HOMEPATH_NAME );
	}
#else
	{
		/* as sys_unix.c / sys_data.c: lower case, spaces → '-' */
		char name[256], base[LC_MAX_PATH];
		const char *xdg = lc_env( "XDG_DATA_HOME" );
		size_t i;

		lc_strlcpy( name, EF_HOMEPATH_NAME, sizeof( name ) );
		for ( i = 0; name[i]; i++ )
			name[i] = name[i] == ' ' ? '-' : (char)tolower( (unsigned char)name[i] );
		if ( xdg )
			lc_strlcpy( base, xdg, sizeof( base ) );
		else
			lc_path_join( base, sizeof( base ), lc_home_dir( ), ".local/share" );
		lc_path_join( path, sizeof( path ), base, name );
	}
#endif
	return path;
}

const char *lc_homebase( void )
{
	static char path[LC_MAX_PATH];
	if ( !path[0] )
		lc_path_join( path, sizeof( path ), lc_homepath( ), LC_BASEGAME );
	return path;
}

/* like NSByteCountFormatter's file style: KB without decimals, MB one, GB and
   up two, trailing zeros dropped ("2 KB", "1.5 MB", "600 MB", "1.23 GB") */
void lc_format_bytes( unsigned long long n, char *out, size_t size )
{
	static const char *units[] = { "KB", "MB", "GB", "TB" };
	static const int decimals[] = { 0, 1, 2, 2 };
	double v = (double)n;
	int u = -1;
	char *dot, *e;

	if ( n < 1000 ) {
		lc_snprintf( out, size, n == 1 ? "%llu byte" : "%llu bytes", n );
		return;
	}
	while ( u < 3 && ( u < 0 || v >= 999.5 ) ) {
		v /= 1000;
		u++;
	}
	lc_snprintf( out, size, "%.*f", decimals[u], v );
	if ( ( dot = strchr( out, '.' ) ) ) {
		e = out + strlen( out );
		while ( e > dot && ( e[-1] == '0' || e[-1] == '.' ) ) {
			char c = e[-1];
			*--e = 0;
			if ( c == '.' )
				break;
		}
	}
	lc_strlcat( out, " ", size );
	lc_strlcat( out, units[u], size );
}

/* ===================================================================== */
/* the executable                                                        */

const char *lc_exe_path( void )
{
	static char path[LC_MAX_PATH];

	if ( path[0] )
		return path;
#ifdef _WIN32
	{
		wchar_t w[LC_MAX_PATH];
		DWORD n = GetModuleFileNameW( NULL, w, LC_MAX_PATH );
		if ( n && n < LC_MAX_PATH )
			Narrow( w, path, sizeof( path ) );
	}
#elif defined( __APPLE__ )
	{
		char tmp[LC_MAX_PATH];
		uint32_t n = sizeof( tmp );
		if ( !_NSGetExecutablePath( tmp, &n ) && !realpath( tmp, path ) )
			lc_strlcpy( path, tmp, sizeof( path ) );
	}
#else
	{
		ssize_t n = readlink( "/proc/self/exe", path, sizeof( path ) - 1 );
		path[n > 0 ? n : 0] = 0;
	}
#endif
	return path;
}

const char *lc_exe_dir( void )
{
	static char dir[LC_MAX_PATH];
	if ( !dir[0] )
		lc_path_dirname( dir, sizeof( dir ), lc_exe_path( ) );
	return dir;
}

const char *lc_self_command( void )
{
#ifndef _WIN32
	const char *appimage = lc_env( "APPIMAGE" );
	if ( appimage && lc_is_file( appimage ) )
		return appimage;
#endif
	return lc_exe_path( );
}

int lc_sibling_exe( const char *name, char *out, size_t size )
{
	char file[256];
#ifdef _WIN32
	lc_snprintf( file, sizeof( file ), "%s.exe", name );
#else
	lc_strlcpy( file, name, sizeof( file ) );
#endif
	lc_path_join( out, size, lc_exe_dir( ), file );
	return lc_is_file( out );
}

const char *lc_user_full_name( void )
{
	static char name[256];
	static int done;

	if ( done )
		return name;
	done = 1;
#ifdef _WIN32
	{
		wchar_t w[256];
		ULONG n = 256;
		if ( GetUserNameExW( NameDisplay, w, &n ) && w[0] )
			Narrow( w, name, sizeof( name ) );
		else {
			DWORD m = 256;
			if ( GetUserNameW( w, &m ) )
				Narrow( w, name, sizeof( name ) );
		}
	}
#else
	{
		struct passwd *pw = getpwuid( getuid( ) );
		if ( pw && pw->pw_gecos && pw->pw_gecos[0] ) {
			lc_strlcpy( name, pw->pw_gecos, sizeof( name ) );
			{
				char *comma = strchr( name, ',' );	/* GECOS: full name,room,… */
				if ( comma )
					*comma = 0;
			}
		}
		if ( !name[0] ) {
			const char *u = lc_env( "USER" );
			if ( !u && pw )
				u = pw->pw_name;
			lc_strlcpy( name, u ? u : "", sizeof( name ) );
		}
	}
#endif
	lc_trim( name );
	return name;
}

const char *lc_computer_name( void )
{
	static char name[256];

	if ( name[0] )
		return name;
#ifdef _WIN32
	{
		wchar_t w[256];
		DWORD n = 256;
		if ( GetComputerNameExW( ComputerNameDnsHostname, w, &n ) )
			Narrow( w, name, sizeof( name ) );
	}
#else
	if ( gethostname( name, sizeof( name ) - 1 ) )
		name[0] = 0;
	name[sizeof( name ) - 1] = 0;
	{
		char *dot = strchr( name, '.' );
		if ( dot )
			*dot = 0;
	}
#endif
	if ( !name[0] )
		lc_strlcpy( name, LC_PLATFORM_NOUN, sizeof( name ) );
	return name;
}

int lc_is_steam_deck( void )
{
	const char *d = lc_env( "SteamDeck" );
	if ( d && !strcmp( d, "1" ) )
		return 1;
	return lc_env( "GAMESCOPE_WAYLAND_DISPLAY" ) != NULL || lc_test_env( "EF_DECK_UI" );
}

/* ===================================================================== */
/* processes                                                             */

#ifdef _WIN32
/* CommandLineToArgvW-compatible quoting */
static void AppendArgW( wchar_t **buf, size_t *len, size_t *cap, const wchar_t *a )
{
	size_t need = wcslen( a ) * 2 + 4;
	int quote = !*a || wcspbrk( a, L" \t\n\v\"" ) != NULL;
	const wchar_t *p;

	if ( *len + need + 1 > *cap ) {
		*cap = ( *len + need + 1 ) * 2;
		*buf = realloc( *buf, *cap * sizeof( wchar_t ) );
	}
	if ( *len )
		( *buf )[( *len )++] = L' ';
	if ( !quote ) {
		wcscpy( *buf + *len, a );
		*len += wcslen( a );
		return;
	}
	( *buf )[( *len )++] = L'"';
	for ( p = a; ; p++ ) {
		size_t bs = 0;
		while ( *p == L'\\' ) {
			p++;
			bs++;
		}
		if ( !*p ) {
			while ( bs-- ) { ( *buf )[( *len )++] = L'\\'; ( *buf )[( *len )++] = L'\\'; }
			break;
		}
		if ( *p == L'"' ) {
			bs = bs * 2 + 1;
			while ( bs-- ) ( *buf )[( *len )++] = L'\\';
		} else {
			while ( bs-- ) ( *buf )[( *len )++] = L'\\';
		}
		( *buf )[( *len )++] = *p;
	}
	( *buf )[( *len )++] = L'"';
	( *buf )[*len] = 0;
}

static wchar_t *EnvBlock( const char *const *envAdd, const char *removePrefix )
{
	wchar_t *cur = GetEnvironmentStringsW( ), *p, *out;
	size_t total = 0, n = 0, i;
	wchar_t *wprefix = removePrefix ? Wide( removePrefix ) : NULL;
	wchar_t **adds = NULL;
	int nAdd = 0;

	for ( ; envAdd && envAdd[nAdd]; nAdd++ )
		;
	adds = calloc( nAdd + 1, sizeof( *adds ) );
	for ( i = 0; i < (size_t)nAdd; i++ ) {
		adds[i] = Wide( envAdd[i] );
		total += wcslen( adds[i] ) + 1;
	}
	for ( p = cur; *p; p += wcslen( p ) + 1 )
		total += wcslen( p ) + 1;
	out = malloc( ( total + 2 ) * sizeof( wchar_t ) );
	for ( p = cur; *p; p += wcslen( p ) + 1 ) {
		int skip = 0;
		if ( wprefix && !_wcsnicmp( p, wprefix, wcslen( wprefix ) ) )
			skip = 1;
		for ( i = 0; !skip && i < (size_t)nAdd; i++ ) {
			const wchar_t *eq = wcschr( adds[i], L'=' );
			size_t k = eq ? (size_t)( eq - adds[i] ) + 1 : 0;
			if ( k && !_wcsnicmp( p, adds[i], k ) )
				skip = 1;
		}
		if ( !skip ) {
			wcscpy( out + n, p );
			n += wcslen( p ) + 1;
		}
	}
	for ( i = 0; i < (size_t)nAdd; i++ ) {
		wcscpy( out + n, adds[i] );
		n += wcslen( adds[i] ) + 1;
		free( adds[i] );
	}
	out[n++] = 0;
	out[n] = 0;
	free( adds );
	free( wprefix );
	FreeEnvironmentStringsW( cur );
	return out;
}

static void LastError( char *err, size_t errSize )
{
	wchar_t w[512];
	if ( !err || !errSize )
		return;
	if ( FormatMessageW( FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
			GetLastError( ), 0, w, 512, NULL ) ) {
		Narrow( w, err, errSize );
		lc_trim( err );
	} else
		lc_snprintf( err, errSize, "error %lu", (unsigned long)GetLastError( ) );
}

long lc_spawn_detached( const char *const *argv, const char *const *envAdd,
	const char *envRemovePrefix, char *err, size_t errSize )
{
	wchar_t *cmd = NULL, *exe, *env;
	size_t len = 0, cap = 0;
	STARTUPINFOW si;
	PROCESS_INFORMATION pi;
	int i;
	BOOL ok;

	for ( i = 0; argv[i]; i++ ) {
		wchar_t *w = Wide( argv[i] );
		AppendArgW( &cmd, &len, &cap, w ? w : L"" );
		free( w );
	}
	exe = Wide( argv[0] );
	env = EnvBlock( envAdd, envRemovePrefix );
	memset( &si, 0, sizeof( si ) );
	si.cb = sizeof( si );
	ok = CreateProcessW( exe, cmd, NULL, NULL, FALSE,
		CREATE_UNICODE_ENVIRONMENT | DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP,
		env, NULL, &si, &pi );
	if ( !ok )
		LastError( err, errSize );
	free( cmd );
	free( exe );
	free( env );
	if ( !ok )
		return -1;
	AllowSetForegroundWindow( pi.dwProcessId );
	CloseHandle( pi.hThread );
	CloseHandle( pi.hProcess );
	return (long)pi.dwProcessId;
}

int lc_exec( const char *const *argv, const char *const *envAdd )
{
	char err[256];
	if ( lc_spawn_detached( argv, envAdd, NULL, err, sizeof( err ) ) < 0 ) {
		lc_log( "Launcher: couldn't start %s (%s)", argv[0], err );
		return -1;
	}
	exit( 0 );
}

int lc_pid_alive( long pid )
{
	HANDLE h = OpenProcess( PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid );
	DWORD code = 0;
	int alive;
	if ( !h )
		return 0;
	alive = GetExitCodeProcess( h, &code ) && code == STILL_ACTIVE;
	CloseHandle( h );
	return alive;
}

long lc_getpid( void )
{
	return (long)GetCurrentProcessId( );
}

#else /* POSIX */

extern char **environ;

static char **BuildEnv( const char *const *envAdd, const char *removePrefix )
{
	int n = 0, a = 0, i, j, k = 0;
	char **out;

	while ( environ[n] )
		n++;
	while ( envAdd && envAdd[a] )
		a++;
	out = calloc( n + a + 1, sizeof( *out ) );
	for ( i = 0; i < n; i++ ) {
		int skip = removePrefix && !strncmp( environ[i], removePrefix, strlen( removePrefix ) );
		for ( j = 0; !skip && j < a; j++ ) {
			const char *eq = strchr( envAdd[j], '=' );
			size_t l = eq ? (size_t)( eq - envAdd[j] ) + 1 : 0;
			if ( l && !strncmp( environ[i], envAdd[j], l ) )
				skip = 1;
		}
		if ( !skip )
			out[k++] = environ[i];
	}
	for ( j = 0; j < a; j++ )
		out[k++] = (char *)envAdd[j];
	out[k] = NULL;
	return out;
}

long lc_spawn_detached( const char *const *argv, const char *const *envAdd,
	const char *envRemovePrefix, char *err, size_t errSize )
{
	char **env = BuildEnv( envAdd, envRemovePrefix );
	int fds[2];
	pid_t pid, grandchild = -1;
	int status;

	/* double fork: the new process is not our child (no zombie), and a pipe
	   carries the grandchild's pid or its exec error back */
	if ( pipe( fds ) ) {
		lc_snprintf( err, errSize, "%s", strerror( errno ) );
		free( env );
		return -1;
	}
	pid = fork( );
	if ( pid < 0 ) {
		lc_snprintf( err, errSize, "%s", strerror( errno ) );
		close( fds[0] );
		close( fds[1] );
		free( env );
		return -1;
	}
	if ( pid == 0 ) {
		pid_t g;
		close( fds[0] );
		setsid( );
		g = fork( );
		if ( g == 0 ) {
			int e, devnull;
			fcntl( fds[1], F_SETFD, FD_CLOEXEC );
			devnull = open( "/dev/null", O_RDWR );
			if ( devnull >= 0 ) {
				dup2( devnull, 0 );
				if ( devnull > 2 )
					close( devnull );
			}
			signal( SIGPIPE, SIG_DFL );
			execve( argv[0], (char *const *)argv, env );
			e = errno;
			{
				long msg[2] = { -1, e };
				if ( write( fds[1], msg, sizeof( msg ) ) < 0 ) { }
			}
			_exit( 127 );
		}
		{
			long msg[2] = { (long)g, 0 };
			if ( write( fds[1], msg, sizeof( msg ) ) < 0 ) { }
		}
		_exit( g < 0 ? 1 : 0 );
	}
	close( fds[1] );
	waitpid( pid, &status, 0 );
	{
		long msg[2];
		ssize_t r = read( fds[0], msg, sizeof( msg ) );
		if ( r == (ssize_t)sizeof( msg ) )
			grandchild = msg[0];
		/* an exec error follows the pid message */
		r = read( fds[0], msg, sizeof( msg ) );
		if ( r == (ssize_t)sizeof( msg ) && msg[0] == -1 ) {
			lc_snprintf( err, errSize, "%s", strerror( (int)msg[1] ) );
			grandchild = -1;
		}
	}
	close( fds[0] );
	free( env );
	if ( grandchild < 0 && err && errSize && !err[0] )
		lc_snprintf( err, errSize, "couldn't start %s", argv[0] );
	return grandchild;
}

int lc_exec( const char *const *argv, const char *const *envAdd )
{
	char **env = BuildEnv( envAdd, NULL );
	execve( argv[0], (char *const *)argv, env );
	lc_log( "Launcher: couldn't start %s (%s)", argv[0], strerror( errno ) );
	free( env );
	return -1;
}

int lc_pid_alive( long pid )
{
	return pid > 0 && ( !kill( (pid_t)pid, 0 ) || errno == EPERM );
}

long lc_getpid( void )
{
	return (long)getpid( );
}

#if !defined( __APPLE__ )
/* run argv and wait; exit code or -1 */
static int RunWait( const char *const *argv )
{
	pid_t pid = fork( );
	int status;

	if ( pid < 0 )
		return -1;
	if ( pid == 0 ) {
		int devnull = open( "/dev/null", O_RDWR );
		if ( devnull >= 0 ) {
			dup2( devnull, 0 );
			dup2( devnull, 1 );
			dup2( devnull, 2 );
		}
		execvp( argv[0], (char *const *)argv );
		_exit( 127 );
	}
	if ( waitpid( pid, &status, 0 ) < 0 )
		return -1;
	return WIFEXITED( status ) ? WEXITSTATUS( status ) : -1;
}
#endif

#endif

/* ===================================================================== */
/* desktop integration                                                   */

#ifdef _WIN32
static int ShellOpen( const char *what )
{
	wchar_t *w = Wide( what );
	INT_PTR r = w ? (INT_PTR)ShellExecuteW( NULL, L"open", w, NULL, NULL, SW_SHOWNORMAL ) : 0;
	free( w );
	return r > 32;
}

int lc_open_url( const char *url )
{
	return ShellOpen( url );
}

int lc_open_folder( const char *path )
{
	return ShellOpen( path );
}

int lc_move_to_trash( const char *path, char *err, size_t errSize )
{
	wchar_t *w = Wide( path ), *from;
	SHFILEOPSTRUCTW op;
	size_t n;
	int r;

	if ( !w ) {
		lc_snprintf( err, errSize, "bad path" );
		return 0;
	}
	n = wcslen( w );
	from = calloc( n + 2, sizeof( wchar_t ) );	/* double-NUL terminated */
	wcscpy( from, w );
	free( w );
	memset( &op, 0, sizeof( op ) );
	op.wFunc = FO_DELETE;
	op.pFrom = from;
	op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
	r = SHFileOperationW( &op );
	free( from );
	if ( r || op.fAnyOperationsAborted ) {
		lc_snprintf( err, errSize, "the %s refused it (error %d)", LC_TRASH_NAME, r );
		return 0;
	}
	return 1;
}

#else

static int Opener( const char *what )
{
#if defined( __APPLE__ )
	const char *argv[] = { "/usr/bin/open", what, NULL };
#else
	const char *argv[] = { "/usr/bin/xdg-open", what, NULL };
	char xdg[LC_MAX_PATH];
	/* outside the AppImage's own environment: xdg-open must not see our LD_* */
	if ( !lc_is_file( argv[0] ) && lc_is_file( "/bin/xdg-open" ) )
		argv[0] = "/bin/xdg-open";
	(void)xdg;
#endif
	{
		const char *clean[] = { "LD_LIBRARY_PATH=", "QT_PLUGIN_PATH=", NULL };
		char err[128];
		return lc_spawn_detached( argv, clean, NULL, err, sizeof( err ) ) > 0;
	}
}

int lc_open_url( const char *url )
{
	return Opener( url );
}

int lc_open_folder( const char *path )
{
	return Opener( path );
}

/* freedesktop.org Trash spec, home trash only (the homepath lives in the home
   file system): $XDG_DATA_HOME/Trash/{files,info} */
int lc_move_to_trash( const char *path, char *err, size_t errSize )
{
#if defined( __APPLE__ )
	char dest[LC_MAX_PATH];
	lc_path_join( dest, sizeof( dest ), lc_home_dir( ), ".Trash" );
	lc_path_join( dest, sizeof( dest ), dest, lc_path_basename( path ) );
	if ( !lc_rename( path, dest ) ) {
		lc_snprintf( err, errSize, "%s", strerror( errno ) );
		return 0;
	}
	return 1;
#else
	const char *gio[] = { "gio", "trash", path, NULL };
	char trash[LC_MAX_PATH], files[LC_MAX_PATH], info[LC_MAX_PATH], name[256], target[LC_MAX_PATH], infoFile[LC_MAX_PATH];
	const char *xdg = lc_env( "XDG_DATA_HOME" );
	char abs[LC_MAX_PATH];
	int i;
	FILE *f;
	time_t now = time( NULL );
	struct tm tmv;

	if ( RunWait( gio ) == 0 && !lc_is_dir( path ) && !lc_is_file( path ) )
		return 1;
	if ( xdg )
		lc_path_join( trash, sizeof( trash ), xdg, "Trash" );
	else
		lc_path_join( trash, sizeof( trash ), lc_home_dir( ), ".local/share/Trash" );
	lc_path_join( files, sizeof( files ), trash, "files" );
	lc_path_join( info, sizeof( info ), trash, "info" );
	lc_mkdir_p( files );
	lc_mkdir_p( info );
	if ( !realpath( path, abs ) )
		lc_strlcpy( abs, path, sizeof( abs ) );
	for ( i = 0; i < 1000; i++ ) {
		if ( i )
			lc_snprintf( name, sizeof( name ), "%s.%d", lc_path_basename( path ), i + 1 );
		else
			lc_strlcpy( name, lc_path_basename( path ), sizeof( name ) );
		lc_path_join( target, sizeof( target ), files, name );
		lc_snprintf( infoFile, sizeof( infoFile ), "%s/%s.trashinfo", info, name );
		if ( !lc_is_dir( target ) && !lc_is_file( target ) && !lc_is_file( infoFile ) )
			break;
	}
	f = fopen( infoFile, "w" );
	if ( !f ) {
		lc_snprintf( err, errSize, "%s", strerror( errno ) );
		return 0;
	}
	localtime_r( &now, &tmv );
	fprintf( f, "[Trash Info]\nPath=" );
	{
		const unsigned char *p;
		for ( p = (const unsigned char *)abs; *p; p++ ) {
			if ( isalnum( *p ) || strchr( "/-_.~", *p ) )
				fputc( *p, f );
			else
				fprintf( f, "%%%02X", *p );
		}
	}
	fprintf( f, "\nDeletionDate=%04d-%02d-%02dT%02d:%02d:%02d\n", tmv.tm_year + 1900, tmv.tm_mon + 1,
		tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec );
	fclose( f );
	if ( !lc_rename( path, target ) ) {
		lc_snprintf( err, errSize, "%s", strerror( errno ) );
		lc_remove( infoFile );
		return 0;
	}
	return 1;
#endif
}

#endif

/* ===================================================================== */
/* threads and time                                                      */

struct lc_thread_s {
#ifdef _WIN32
	HANDLE h;
#else
	pthread_t t;
#endif
	void (*fn)( void *user );
	void *user;
};

#ifdef _WIN32
static unsigned __stdcall ThreadMain( void *p )
{
	lc_thread *t = p;
	t->fn( t->user );
	return 0;
}
#else
static void *ThreadMain( void *p )
{
	lc_thread *t = p;
	t->fn( t->user );
	return NULL;
}
#endif

lc_thread *lc_thread_start( void (*fn)( void *user ), void *user )
{
	lc_thread *t = calloc( 1, sizeof( *t ) );
	if ( !t )
		return NULL;
	t->fn = fn;
	t->user = user;
#ifdef _WIN32
	t->h = (HANDLE)_beginthreadex( NULL, 0, ThreadMain, t, 0, NULL );
	if ( !t->h ) {
		free( t );
		return NULL;
	}
#else
	if ( pthread_create( &t->t, NULL, ThreadMain, t ) ) {
		free( t );
		return NULL;
	}
#endif
	return t;
}

/* the struct must outlive a detached thread that is still reading fn/user: it
   only reads them before calling fn, so detaching after start is safe once
   ThreadMain has run; to be safe the detached struct is leaked (a few bytes). */
void lc_thread_join( lc_thread *t )
{
	if ( !t )
		return;
#ifdef _WIN32
	WaitForSingleObject( t->h, INFINITE );
	CloseHandle( t->h );
#else
	pthread_join( t->t, NULL );
#endif
	free( t );
}

void lc_thread_detach( lc_thread *t )
{
	if ( !t )
		return;
#ifdef _WIN32
	CloseHandle( t->h );
#else
	pthread_detach( t->t );
#endif
}

struct lc_mutex_s {
#ifdef _WIN32
	CRITICAL_SECTION cs;
#else
	pthread_mutex_t m;
#endif
};

lc_mutex *lc_mutex_new( void )
{
	lc_mutex *m = calloc( 1, sizeof( *m ) );
	if ( !m )
		return NULL;
#ifdef _WIN32
	InitializeCriticalSection( &m->cs );
#else
	{
		pthread_mutexattr_t a;
		pthread_mutexattr_init( &a );
		pthread_mutexattr_settype( &a, PTHREAD_MUTEX_RECURSIVE );
		pthread_mutex_init( &m->m, &a );
		pthread_mutexattr_destroy( &a );
	}
#endif
	return m;
}

void lc_mutex_lock( lc_mutex *m )
{
#ifdef _WIN32
	EnterCriticalSection( &m->cs );
#else
	pthread_mutex_lock( &m->m );
#endif
}

void lc_mutex_unlock( lc_mutex *m )
{
#ifdef _WIN32
	LeaveCriticalSection( &m->cs );
#else
	pthread_mutex_unlock( &m->m );
#endif
}

void lc_mutex_free( lc_mutex *m )
{
	if ( !m )
		return;
#ifdef _WIN32
	DeleteCriticalSection( &m->cs );
#else
	pthread_mutex_destroy( &m->m );
#endif
	free( m );
}

void lc_sleep_ms( int ms )
{
#ifdef _WIN32
	Sleep( (DWORD)ms );
#else
	struct timespec ts = { ms / 1000, ( ms % 1000 ) * 1000000L };
	while ( nanosleep( &ts, &ts ) && errno == EINTR )
		;
#endif
}

double lc_now( void )
{
#ifdef _WIN32
	static LARGE_INTEGER freq;
	LARGE_INTEGER c;
	if ( !freq.QuadPart )
		QueryPerformanceFrequency( &freq );
	QueryPerformanceCounter( &c );
	return (double)c.QuadPart / (double)freq.QuadPart;
#else
	struct timespec ts;
	clock_gettime( CLOCK_MONOTONIC, &ts );
	return ts.tv_sec + ts.tv_nsec / 1e9;
#endif
}

int64_t lc_time( void )
{
	return (int64_t)time( NULL );
}

/* ===================================================================== */
/* sockets, random                                                       */

int lc_net_init( void )
{
#ifdef _WIN32
	static int done, ok;
	if ( !done ) {
		WSADATA wsa;
		ok = !WSAStartup( MAKEWORD( 2, 2 ), &wsa );
		done = 1;
	}
	return ok;
#else
	signal( SIGPIPE, SIG_IGN );
	return 1;
#endif
}

#ifdef _WIN32
typedef BOOLEAN (WINAPI *RtlGenRandom_t)( PVOID, ULONG );
#endif

void lc_random_bytes( void *out, size_t n )
{
#ifdef _WIN32
	static RtlGenRandom_t gen;
	if ( !gen ) {
		HMODULE adv = LoadLibraryW( L"advapi32.dll" );
		if ( adv )
			gen = (RtlGenRandom_t)(void *)GetProcAddress( adv, "SystemFunction036" );
	}
	if ( gen && gen( out, (ULONG)n ) )
		return;
#elif defined( __APPLE__ )
	arc4random_buf( out, n );
	return;
#else
	{
		int fd = open( "/dev/urandom", O_RDONLY | O_CLOEXEC );
		if ( fd >= 0 ) {
			ssize_t r = read( fd, out, n );
			close( fd );
			if ( r == (ssize_t)n )
				return;
		}
	}
#endif
	{
		/* last resort, not cryptographic */
		unsigned char *p = out;
		size_t i;
		unsigned x = (unsigned)time( NULL ) ^ (unsigned)lc_getpid( ) ^ (unsigned)( lc_now( ) * 1e6 );
		for ( i = 0; i < n; i++ ) {
			x = x * 1103515245u + 12345u;
			p[i] = (unsigned char)( x >> 16 );
		}
	}
}

/* ===================================================================== */
/* settings store                                                        */

typedef struct {
	char *key;		/* NULL: a raw line (comment / unparsable) kept as is */
	char *value;	/* raw line when key is NULL */
} storeLine_t;

struct lc_store_s {
	char path[LC_MAX_PATH];
	storeLine_t *lines;
	int count, cap;
};

static void StoreAppend( lc_store *s, char *key, char *value )
{
	if ( s->count == s->cap ) {
		s->cap = s->cap ? s->cap * 2 : 32;
		s->lines = realloc( s->lines, s->cap * sizeof( *s->lines ) );
	}
	s->lines[s->count].key = key;
	s->lines[s->count].value = value;
	s->count++;
}

/* `key "value"` or `key value`, read exactly as sys_data.c reads gamefolder: no
   escapes, the value is everything between the first and the last quote (so
   Windows paths and values containing quotes survive) */
static int ParseStoreLine( const char *line, char **key, char **value )
{
	const char *p = line, *k;
	char *v, *e;
	size_t kl;

	while ( *p == ' ' || *p == '\t' )
		p++;
	if ( !*p || *p == '#' || ( p[0] == '/' && p[1] == '/' ) )
		return 0;
	k = p;
	while ( *p && *p != ' ' && *p != '\t' )
		p++;
	kl = (size_t)( p - k );
	while ( *p == ' ' || *p == '\t' )
		p++;
	v = lc_strdup( p );
	e = v + strlen( v );
	while ( e > v && ( e[-1] == ' ' || e[-1] == '\t' ) )
		*--e = 0;
	if ( *v == '"' ) {
		memmove( v, v + 1, strlen( v ) );
		e = v + strlen( v );
		if ( e > v && e[-1] == '"' )
			*--e = 0;
	}
	*key = malloc( kl + 1 );
	memcpy( *key, k, kl );
	( *key )[kl] = 0;
	*value = v;
	return 1;
}

lc_store *lc_store_load( const char *path )
{
	lc_store *s = calloc( 1, sizeof( *s ) );
	char *text, *line, *next;

	if ( path )
		lc_strlcpy( s->path, path, sizeof( s->path ) );
	else
		lc_path_join( s->path, sizeof( s->path ), lc_homepath( ), "launcher.cfg" );
	text = lc_read_file( s->path, NULL );
	if ( !text )
		return s;
	for ( line = text; line && *line; line = next ) {
		char *key, *value;
		next = strchr( line, '\n' );
		if ( next )
			*next++ = 0;
		if ( *line && line[strlen( line ) - 1] == '\r' )
			line[strlen( line ) - 1] = 0;
		if ( ParseStoreLine( line, &key, &value ) )
			StoreAppend( s, key, value );
		else if ( *line )
			StoreAppend( s, NULL, lc_strdup( line ) );
	}
	free( text );
	return s;
}

static int StoreFind( lc_store *s, const char *key )
{
	int i;
	for ( i = s->count - 1; i >= 0; i-- )	/* last wins */
		if ( s->lines[i].key && !lc_stricmp( s->lines[i].key, key ) )
			return i;
	return -1;
}

const char *lc_store_get( lc_store *s, const char *key )
{
	int i = StoreFind( s, key );
	return i < 0 ? NULL : s->lines[i].value;
}

int lc_store_has( lc_store *s, const char *key )
{
	return StoreFind( s, key ) >= 0;
}

int lc_store_get_int( lc_store *s, const char *key, int def )
{
	const char *v = lc_store_get( s, key );
	char *end;
	long n;
	if ( !v || !*v )
		return def;
	n = strtol( v, &end, 10 );
	return *end ? def : (int)n;
}

int lc_store_get_bool( lc_store *s, const char *key, int def )
{
	const char *v = lc_store_get( s, key );
	if ( !v || !*v )
		return def;
	if ( !lc_stricmp( v, "1" ) || !lc_stricmp( v, "true" ) || !lc_stricmp( v, "yes" ) )
		return 1;
	if ( !lc_stricmp( v, "0" ) || !lc_stricmp( v, "false" ) || !lc_stricmp( v, "no" ) )
		return 0;
	return def;
}

long long lc_store_get_int64( lc_store *s, const char *key, long long def )
{
	const char *v = lc_store_get( s, key );
	char *end;
	long long n;
	if ( !v || !*v )
		return def;
	n = strtoll( v, &end, 10 );
	return *end ? def : n;
}

void lc_store_remove( lc_store *s, const char *key )
{
	int i, j = 0;
	for ( i = 0; i < s->count; i++ ) {
		if ( s->lines[i].key && !lc_stricmp( s->lines[i].key, key ) ) {
			free( s->lines[i].key );
			free( s->lines[i].value );
			continue;
		}
		s->lines[j++] = s->lines[i];
	}
	s->count = j;
}

void lc_store_set( lc_store *s, const char *key, const char *value )
{
	int i;
	if ( !value ) {
		lc_store_remove( s, key );
		return;
	}
	i = StoreFind( s, key );
	if ( i >= 0 ) {
		int j, k = 0;
		/* drop earlier duplicates so the file has one line per key */
		for ( j = 0; j < s->count; j++ ) {
			if ( j != i && s->lines[j].key && !lc_stricmp( s->lines[j].key, key ) ) {
				free( s->lines[j].key );
				free( s->lines[j].value );
				continue;
			}
			s->lines[k++] = s->lines[j];
		}
		s->count = k;
		i = StoreFind( s, key );
		free( s->lines[i].value );
		s->lines[i].value = lc_strdup( value );
		return;
	}
	StoreAppend( s, lc_strdup( key ), lc_strdup( value ) );
}

void lc_store_set_int( lc_store *s, const char *key, int v )
{
	char buf[32];
	lc_snprintf( buf, sizeof( buf ), "%d", v );
	lc_store_set( s, key, buf );
}

void lc_store_set_int64( lc_store *s, const char *key, long long v )
{
	char buf[32];
	lc_snprintf( buf, sizeof( buf ), "%lld", v );
	lc_store_set( s, key, buf );
}

int lc_store_has_list( lc_store *s, const char *key )
{
	char k[256];
	lc_snprintf( k, sizeof( k ), "%s.n", key );
	return lc_store_has( s, k );
}

int lc_store_get_list( lc_store *s, const char *key, char *out, size_t itemSize, int max )
{
	char k[256];
	int n, i, got = 0;

	lc_snprintf( k, sizeof( k ), "%s.n", key );
	n = lc_store_get_int( s, k, 0 );
	for ( i = 0; i < n; i++ ) {
		const char *v;
		lc_snprintf( k, sizeof( k ), "%s.%d", key, i );
		v = lc_store_get( s, k );
		if ( !v )
			continue;
		if ( out ) {
			if ( got >= max )
				break;
			lc_strlcpy( out + (size_t)got * itemSize, v, itemSize );
		}
		got++;
	}
	return got;
}

void lc_store_remove_list( lc_store *s, const char *key )
{
	char k[256];
	int n, i;

	lc_snprintf( k, sizeof( k ), "%s.n", key );
	n = lc_store_get_int( s, k, 0 );
	lc_store_remove( s, k );
	for ( i = 0; i < n + 64; i++ ) {
		lc_snprintf( k, sizeof( k ), "%s.%d", key, i );
		lc_store_remove( s, k );
	}
}

void lc_store_set_list( lc_store *s, const char *key, const char *items, size_t itemSize, int n )
{
	char k[256];
	int i;

	lc_store_remove_list( s, key );
	lc_snprintf( k, sizeof( k ), "%s.n", key );
	lc_store_set_int( s, k, n );
	for ( i = 0; i < n; i++ ) {
		lc_snprintf( k, sizeof( k ), "%s.%d", key, i );
		lc_store_set( s, k, items + (size_t)i * itemSize );
	}
}

int lc_store_save( lc_store *s )
{
	size_t cap = 4096, len = 0;
	char *buf = malloc( cap );
	int i, ok;
	char dir[LC_MAX_PATH];

	for ( i = 0; i < s->count; i++ ) {
		const char *v = s->lines[i].value;
		size_t need = strlen( v ) * 2 + ( s->lines[i].key ? strlen( s->lines[i].key ) : 0 ) + 8;
		if ( len + need >= cap ) {
			cap = ( len + need ) * 2;
			buf = realloc( buf, cap );
		}
		if ( !s->lines[i].key ) {
			len += sprintf( buf + len, "%s\n", v );
			continue;
		}
		len += sprintf( buf + len, "%s \"", s->lines[i].key );
		for ( ; *v; v++ ) {
			if ( *v == '\n' || *v == '\r' ) {
				buf[len++] = ' ';
				continue;
			}
			buf[len++] = *v;
		}
		buf[len++] = '"';
		buf[len++] = '\n';
	}
	lc_path_dirname( dir, sizeof( dir ), s->path );
	lc_mkdir_p( dir );
	ok = lc_write_file_atomic( s->path, buf, len );
	free( buf );
	if ( !ok )
		lc_log( "Launcher: couldn't save %s", s->path );
	return ok;
}

void lc_store_free( lc_store *s )
{
	int i;
	if ( !s )
		return;
	for ( i = 0; i < s->count; i++ ) {
		free( s->lines[i].key );
		free( s->lines[i].value );
	}
	free( s->lines );
	free( s );
}

lc_store *lc_store_shared( void )
{
	static lc_store *shared;
	if ( !shared )
		shared = lc_store_load( NULL );
	return shared;
}

/* ===================================================================== */
/* HTTP via libcurl, loaded at run time                                  */

typedef void CURL;
struct curl_slist;
typedef int (*curl_global_init_t)( long );
typedef CURL *(*curl_easy_init_t)( void );
typedef int (*curl_easy_setopt_t)( CURL *, int, ... );
typedef int (*curl_easy_perform_t)( CURL * );
typedef int (*curl_easy_getinfo_t)( CURL *, int, ... );
typedef void (*curl_easy_cleanup_t)( CURL * );
typedef const char *(*curl_easy_strerror_t)( int );
typedef struct curl_slist *(*curl_slist_append_t)( struct curl_slist *, const char * );
typedef void (*curl_slist_free_all_t)( struct curl_slist * );

static struct {
	int tried, ok;
	curl_global_init_t global_init;
	curl_easy_init_t init;
	curl_easy_setopt_t setopt;
	curl_easy_perform_t perform;
	curl_easy_getinfo_t getinfo;
	curl_easy_cleanup_t cleanup;
	curl_easy_strerror_t strerror_;
	curl_slist_append_t slist_append;
	curl_slist_free_all_t slist_free_all;
} curl;

#define LC_CURLOPT_WRITEDATA		10001
#define LC_CURLOPT_URL				10002
#define LC_CURLOPT_ERRORBUFFER		10010
#define LC_CURLOPT_WRITEFUNCTION	20011
#define LC_CURLOPT_TIMEOUT			13
#define LC_CURLOPT_FOLLOWLOCATION	52
#define LC_CURLOPT_CONNECTTIMEOUT	78
#define LC_CURLOPT_NOSIGNAL			99
#define LC_CURLOPT_HTTPHEADER		10023
#define LC_CURLOPT_ACCEPT_ENCODING	10102
#define LC_CURLINFO_RESPONSE_CODE	0x200002
#define LC_CURL_GLOBAL_DEFAULT		3

static void *LibSym( void *lib, const char *name )
{
#ifdef _WIN32
	return (void *)GetProcAddress( (HMODULE)lib, name );
#else
	return dlsym( lib, name );
#endif
}

static int CurlLoad( void )
{
	static const char *names[] = {
#if defined( _WIN32 )
		"libcurl-4.dll", "libcurl.dll", "libcurl-x64.dll",
#elif defined( __APPLE__ )
		"libcurl.4.dylib", "/usr/lib/libcurl.4.dylib",
#else
		"libcurl.so.4", "libcurl-gnutls.so.4", "libcurl.so",
#endif
		NULL };
	void *lib = NULL;
	int i;

	if ( curl.tried )
		return curl.ok;
	curl.tried = 1;
	for ( i = 0; names[i] && !lib; i++ ) {
#ifdef _WIN32
		char path[LC_MAX_PATH];
		wchar_t *w;
		lc_path_join( path, sizeof( path ), lc_exe_dir( ), names[i] );
		w = Wide( path );
		lib = w ? (void *)LoadLibraryW( w ) : NULL;
		free( w );
		if ( !lib ) {
			w = Wide( names[i] );
			lib = w ? (void *)LoadLibraryW( w ) : NULL;
			free( w );
		}
#else
		lib = dlopen( names[i], RTLD_NOW | RTLD_LOCAL );
#endif
	}
	if ( !lib )
		return 0;
	curl.global_init = (curl_global_init_t)LibSym( lib, "curl_global_init" );
	curl.init = (curl_easy_init_t)LibSym( lib, "curl_easy_init" );
	curl.setopt = (curl_easy_setopt_t)LibSym( lib, "curl_easy_setopt" );
	curl.perform = (curl_easy_perform_t)LibSym( lib, "curl_easy_perform" );
	curl.getinfo = (curl_easy_getinfo_t)LibSym( lib, "curl_easy_getinfo" );
	curl.cleanup = (curl_easy_cleanup_t)LibSym( lib, "curl_easy_cleanup" );
	curl.strerror_ = (curl_easy_strerror_t)LibSym( lib, "curl_easy_strerror" );
	curl.slist_append = (curl_slist_append_t)LibSym( lib, "curl_slist_append" );
	curl.slist_free_all = (curl_slist_free_all_t)LibSym( lib, "curl_slist_free_all" );
	curl.ok = curl.global_init && curl.init && curl.setopt && curl.perform && curl.getinfo &&
		curl.cleanup && curl.strerror_ && curl.slist_append && curl.slist_free_all;
	if ( curl.ok )
		curl.global_init( LC_CURL_GLOBAL_DEFAULT );
	return curl.ok;
}

typedef struct { char *buf; size_t len, cap; } httpBuf_t;

static size_t HttpWrite( char *data, size_t size, size_t nmemb, void *user )
{
	httpBuf_t *b = user;
	size_t n = size * nmemb;
	if ( b->len + n + 1 > b->cap ) {
		size_t cap = ( b->len + n + 1 ) * 2;
		char *nb = realloc( b->buf, cap );
		if ( !nb )
			return 0;
		b->buf = nb;
		b->cap = cap;
	}
	memcpy( b->buf + b->len, data, n );
	b->len += n;
	b->buf[b->len] = 0;
	return n;
}

int lc_http_get( const char *url, const char *const *headers, int timeoutSeconds,
	char **body, size_t *len, long *status, char *err, size_t errSize )
{
	static lc_mutex *lock;
	CURL *c;
	struct curl_slist *list = NULL;
	httpBuf_t b = { NULL, 0, 0 };
	char ebuf[256] = "";
	int rc, i;
	long code = 0;

	if ( body )
		*body = NULL;
	if ( len )
		*len = 0;
	if ( status )
		*status = 0;
	if ( !lock )
		lock = lc_mutex_new( );	/* first call is on one thread; fine for this app */
	lc_mutex_lock( lock );
	rc = CurlLoad( );
	lc_mutex_unlock( lock );
	if ( !rc ) {
		lc_snprintf( err, errSize, "HTTP isn't available (no libcurl)" );
		return LC_HTTP_UNAVAILABLE;
	}
	c = curl.init( );
	if ( !c ) {
		lc_snprintf( err, errSize, "HTTP isn't available" );
		return LC_HTTP_UNAVAILABLE;
	}
	for ( i = 0; headers && headers[i]; i++ )
		list = curl.slist_append( list, headers[i] );
	curl.setopt( c, LC_CURLOPT_URL, url );
	curl.setopt( c, LC_CURLOPT_WRITEFUNCTION, HttpWrite );
	curl.setopt( c, LC_CURLOPT_WRITEDATA, &b );
	curl.setopt( c, LC_CURLOPT_ERRORBUFFER, ebuf );
	curl.setopt( c, LC_CURLOPT_TIMEOUT, (long)timeoutSeconds );
	curl.setopt( c, LC_CURLOPT_CONNECTTIMEOUT, (long)( timeoutSeconds < 10 ? timeoutSeconds : 10 ) );
	curl.setopt( c, LC_CURLOPT_FOLLOWLOCATION, 1L );
	curl.setopt( c, LC_CURLOPT_NOSIGNAL, 1L );
	curl.setopt( c, LC_CURLOPT_ACCEPT_ENCODING, "" );
	if ( list )
		curl.setopt( c, LC_CURLOPT_HTTPHEADER, list );
	rc = curl.perform( c );
	curl.getinfo( c, LC_CURLINFO_RESPONSE_CODE, &code );
	if ( list )
		curl.slist_free_all( list );
	curl.cleanup( c );
	if ( rc ) {
		lc_snprintf( err, errSize, "%s", ebuf[0] ? ebuf : curl.strerror_( rc ) );
		free( b.buf );
		return LC_HTTP_FAILED;
	}
	if ( !b.buf )
		b.buf = calloc( 1, 1 );
	if ( body )
		*body = b.buf;
	else
		free( b.buf );
	if ( len )
		*len = b.len;
	if ( status )
		*status = code;
	return LC_HTTP_OK;
}
