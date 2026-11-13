#include "lc_common.h"
#include "lc_test.h"
#include <stdlib.h>

int main( void )
{
	char buf[256], path[LC_MAX_PATH];
	lc_store *s;
	char list[3][32] = { "one", "two \"quoted\"", "three\\" }, got[4][32];

	/* strings */
	lc_strlcpy( buf, "  hello  ", sizeof( buf ) );
	lc_trim( buf );
	CHECK_STR( buf, "hello" );
	lc_strlcpy( buf, "abc\xc3\xa9", sizeof( buf ) );	/* abcé */
	lc_utf8_truncate( buf, 4 );
	CHECK_STR( buf, "abc" );
	CHECK( lc_utf8_valid( "abc\xc3\xa9" ) );
	CHECK( !lc_utf8_valid( "abc\xc3" ) );
	lc_format_bytes( 600000000ULL, buf, sizeof( buf ) );
	CHECK_STR( buf, "600 MB" );
	lc_format_bytes( 1500, buf, sizeof( buf ) );
	CHECK_STR( buf, "2 KB" );
	lc_format_bytes( 1500000, buf, sizeof( buf ) );
	CHECK_STR( buf, "1.5 MB" );
	lc_format_bytes( 1230000000ULL, buf, sizeof( buf ) );
	CHECK_STR( buf, "1.23 GB" );
	lc_format_bytes( 2000000000ULL, buf, sizeof( buf ) );
	CHECK_STR( buf, "2 GB" );
	lc_path_dirname( buf, sizeof( buf ), "/a/b/c.txt" );
	CHECK_STR( buf, "/a/b" );
	CHECK_STR( lc_path_basename( "/a/b/c.txt" ), "c.txt" );

	/* store round trip */
	CHECK( lc_mkdir_p( lc_homepath( ) ) );
	lc_path_join( path, sizeof( path ), lc_homepath( ), "launcher.cfg" );
	{
		FILE *f = lc_fopen( path, "wb" );
		fprintf( f, "// comment kept\ngamefolder \"C:\\Games\\EF\"\nunknownkey kept value\nlaunchermode \"1\"\nlaunchermode \"2\"\n" );
		fclose( f );
	}
	s = lc_store_load( NULL );
	CHECK_STR( lc_store_get( s, "gamefolder" ), "C:\\Games\\EF" );
	CHECK( lc_store_get_int( s, "LauncherMode", 0 ) == 2 );
	CHECK_STR( lc_store_get( s, "unknownkey" ), "kept value" );
	lc_store_set_int( s, "launchermode", 1 );
	lc_store_set_list( s, "masters", list[0], sizeof( list[0] ), 3 );
	lc_store_set( s, "checkforupdates", "0" );
	CHECK( lc_store_save( s ) );
	lc_store_free( s );
	s = lc_store_load( NULL );
	CHECK( lc_store_get_int( s, "launchermode", 0 ) == 1 );
	CHECK( lc_store_get_bool( s, "checkforupdates", 1 ) == 0 );
	CHECK( lc_store_get_list( s, "masters", got[0], sizeof( got[0] ), 4 ) == 3 );
	CHECK_STR( got[1], "two \"quoted\"" );
	CHECK_STR( got[2], "three\\" );
	CHECK_STR( lc_store_get( s, "gamefolder" ), "C:\\Games\\EF" );
	lc_store_free( s );
	{
		char *text = lc_read_file( path, NULL );
		CHECK( text && strstr( text, "// comment kept" ) );
		free( text );
	}
	return TEST_DONE( );
}
