/* stvef:// links */
#include "lc_links.h"
#include "lc_test.h"
#include "lc_test_util.h"
#include <stdlib.h>

static int Addr( const char *link, char *out, char *pw )
{
	return lc_link_address( link, out, 256, pw, 64 );
}

static void TestServerLinks( void )
{
	char a[256], pw[64];

	CHECK( Addr( "stvef://connect/1.2.3.4:27960", a, pw ) == 1 );
	CHECK_STR( a, "1.2.3.4:27960" );
	CHECK_STR( pw, "" );
	CHECK( Addr( "STVEF:connect/example.com", a, pw ) == 1 );
	CHECK_STR( a, "example.com" );
	CHECK( Addr( "stvef:////connect/play-efservers.example.org:27961/?x=1#frag", a, pw ) == 1 );
	CHECK_STR( a, "play-efservers.example.org:27961" );
	CHECK( Addr( "stvef://CONNECT/host:1?a=b", a, pw ) == 1 );
	CHECK_STR( a, "host:1" );
	CHECK( Addr( "stvef://connect/%5B::1%5D:27960", a, pw ) == 1 );		/* percent-decoded IPv6 */
	CHECK_STR( a, "[::1]:27960" );
	CHECK( Addr( "stvef://connect/%31%2E2.3.4", a, pw ) == 1 );
	CHECK_STR( a, "1.2.3.4" );

	/* the legacy form */
	CHECK( Addr( "stvef:10.0.0.7;27960;My Server;map", a, pw ) == 1 );
	CHECK_STR( a, "10.0.0.7:27960" );
	CHECK( Addr( "stvef://10.0.0.7;27961", a, pw ) == 1 );
	CHECK_STR( a, "10.0.0.7:27961" );
	CHECK( Addr( "stvef:10.0.0.7", a, pw ) == 0 );					/* needs two fields */

	/* a password parameter, when it is safe on a command line */
	CHECK( Addr( "stvef://connect/1.2.3.4:27960?password=s3cret&x=1", a, pw ) == 1 );
	CHECK_STR( pw, "s3cret" );
	CHECK( Addr( "stvef://connect/1.2.3.4:27960?x=1&pw=a%2Db#frag", a, pw ) == 1 );
	CHECK_STR( pw, "a-b" );
	CHECK( Addr( "stvef://connect/1.2.3.4:27960?password=a%22b", a, pw ) == 1 );	/* a quote: dropped */
	CHECK_STR( pw, "" );

	/* refused: nothing that could end up as more than an address on a command line */
	CHECK( Addr( "stvef://connect/", a, pw ) == 0 );
	CHECK( Addr( "stvef://connect/1.2.3.4:123456", a, pw ) == 0 );
	CHECK( Addr( "stvef://connect/1.2.3.4:", a, pw ) == 0 );
	CHECK( Addr( "stvef://connect/a b", a, pw ) == 0 );
	CHECK( Addr( "stvef://connect/a%20b", a, pw ) == 0 );
	CHECK( Addr( "stvef://connect/a%22+exec", a, pw ) == 0 );
	CHECK( Addr( "stvef://connect/a%zz", a, pw ) == 0 );
	CHECK( Addr( "stvef://connect/a%2", a, pw ) == 0 );
	CHECK( Addr( "stvef://connect/a;b", a, pw ) == 0 );
	CHECK( Addr( "stvef://connect/%3Bquit", a, pw ) == 0 );
	CHECK( Addr( "stvef://connect/[::1", a, pw ) == 0 );
	CHECK( Addr( "stvef://connect/[xyz]", a, pw ) == 0 );
	CHECK( Addr( "stvef://connect/\xc3\xa9.example", a, pw ) == 0 );
	CHECK( Addr( "http://connect/1.2.3.4", a, pw ) == 0 );
	CHECK( Addr( "stvef:", a, pw ) == 0 );
	CHECK( Addr( "", a, pw ) == 0 );
	CHECK( Addr( NULL, a, pw ) == 0 );
	{
		char longLink[400] = "stvef://connect/";
		memset( longLink + 16, 'a', 300 );
		longLink[316] = 0;
		CHECK( Addr( longLink, a, pw ) == 0 );		/* over 255 characters */
		memset( longLink + 16, 'a', 255 );
		longLink[16 + 255] = 0;
		CHECK( Addr( longLink, a, pw ) == 1 );
	}
}

static void TestCoopLinks( void )
{
	char a[256];

	CHECK( lc_coop_link_address( "stvef://coop/192.168.1.20:29760", a, sizeof( a ) ) == 1 );
	CHECK_STR( a, "192.168.1.20:29760" );
	CHECK( lc_coop_link_address( "stvef:COOP/host.example?x=y", a, sizeof( a ) ) == 1 );
	CHECK_STR( a, "host.example" );
	CHECK( lc_coop_link_address( "stvef://coop/", a, sizeof( a ) ) == 0 );
	CHECK( lc_coop_link_address( "stvef://coop/bad address", a, sizeof( a ) ) == 0 );
	CHECK( lc_coop_link_address( "stvef://connect/1.2.3.4", a, sizeof( a ) ) == -1 );
	CHECK( lc_coop_link_address( "stvef://coop", a, sizeof( a ) ) == -1 );
	CHECK( lc_coop_link_address( "nope", a, sizeof( a ) ) == -1 );
}

static void TestArgs( void )
{
	char *argv1[] = { (char *)"EliteForce", (char *)"stvef://connect/1.2.3.4" };
	char *argv2[] = { (char *)"EliteForce", (char *)"--uri", (char *)"stvef://connect/1.2.3.4", (char *)"+set" };
	char *argv3[] = { (char *)"EliteForce", (char *)"--uri" };
	char *argv4[] = { (char *)"EliteForce", (char *)"+set", (char *)"a", (char *)"b" };

	CHECK( lc_is_link_arg( "stvef://x" ) && lc_is_link_arg( "STVEF:x" ) );
	CHECK( !lc_is_link_arg( "+set" ) && !lc_is_link_arg( NULL ) && !lc_is_link_arg( "stve" ) );
	CHECK_STR( lc_link_from_args( 2, argv1 ), "stvef://connect/1.2.3.4" );
	CHECK_STR( lc_link_from_args( 4, argv2 ), "stvef://connect/1.2.3.4" );
	CHECK( lc_link_from_args( 2, argv3 ) == NULL );
	CHECK( lc_link_from_args( 4, argv4 ) == NULL );
}

static void TestRegistration( void )
{
#ifndef _WIN32
	/* the .desktop file, written into a private XDG_DATA_HOME; no xdg-mime call */
	const char *dir = tu_dir( "links-xdg" );
	static char env[LC_MAX_PATH + 32];
	char err[256] = "", path[LC_MAX_PATH], *text;

	lc_snprintf( env, sizeof( env ), "XDG_DATA_HOME=%s", dir );
	putenv( env );
	putenv( (char *)"LC_NO_XDG=1" );
	CHECK( lc_link_register( err, sizeof( err ) ) );
	lc_path_join( path, sizeof( path ), dir, "applications/eliteforce-marino-stvef.desktop" );
	text = lc_read_file( path, NULL );
	CHECK( text != NULL );
	if ( text ) {
		CHECK( strstr( text, "[Desktop Entry]\n" ) == text );
		CHECK( strstr( text, "MimeType=x-scheme-handler/stvef;\n" ) != NULL );
		CHECK( strstr( text, "Exec=\"" ) != NULL && strstr( text, "\" %u\n" ) != NULL );
		free( text );
	}
	putenv( (char *)"XDG_DATA_HOME=" );
	putenv( (char *)"LC_NO_XDG=" );
#else
	/* the registry (HKCU\Software\Classes\stvef) is exercised by the Windows smoke test */
#endif
}

int main( void )
{
	TestServerLinks( );
	TestCoopLinks( );
	TestArgs( );
	TestRegistration( );
	return TEST_DONE( );
}
