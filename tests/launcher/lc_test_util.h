/* file-system helpers for the launcher core tests (static, per test executable) */
#ifndef LC_TEST_UTIL_H
#define LC_TEST_UTIL_H
#include "lc_common.h"
#include <stdlib.h>
#include <string.h>

static int tu_rm_cb( const char *name, int isDir, void *user );
static void tu_rmtree( const char *path )
{
	if ( lc_is_symlink( path ) || !lc_is_dir( path ) ) {
		lc_remove( path );
		return;
	}
	lc_list_dir( path, tu_rm_cb, (void *)path );
	lc_remove( path );
}
static int tu_rm_cb( const char *name, int isDir, void *user )
{
	char p[LC_MAX_PATH];
	( void )isDir;
	lc_path_join( p, sizeof( p ), (const char *)user, name );
	tu_rmtree( p );
	return 0;
}
/* a fresh empty directory <homepath>/<name> */
static const char *tu_dir( const char *name )
{
	static char path[8][LC_MAX_PATH];
	static int n;
	char *p = path[n++ & 7];
	lc_path_join( p, LC_MAX_PATH, lc_homepath( ), name );
	tu_rmtree( p );
	lc_mkdir_p( p );
	return p;
}
/* write a file (creating its folder) */
static void tu_write( const char *dir, const char *name, const void *data, size_t len )
{
	char p[LC_MAX_PATH], d[LC_MAX_PATH];
	FILE *f;
	lc_path_join( p, sizeof( p ), dir, name );
	lc_path_dirname( d, sizeof( d ), p );
	lc_mkdir_p( d );
	f = lc_fopen( p, "wb" );
	if ( f ) {
		fwrite( data, 1, len, f );
		fclose( f );
	}
}
static void tu_text( const char *dir, const char *name, const char *text )
{
	tu_write( dir, name, text, strlen( text ) );
}
static const char *tu_path( const char *dir, const char *name )
{
	static char p[8][LC_MAX_PATH];
	static int n;
	char *r = p[n++ & 7];
	lc_path_join( r, LC_MAX_PATH, dir, name );
	return r;
}
#endif
