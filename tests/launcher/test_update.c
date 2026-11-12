/* version compare, release JSON parsing, the daily throttle, stored state, alert texts */
#include "lc_update.h"
#include "lc_settings.h"
#include "lc_test.h"
#include "lc_test_util.h"
#include <stdlib.h>

#ifdef _WIN32
#	define P	"Windows"
#	define TAG	"windows-v"
#else
#	define P	"Linux"
#	define TAG	"linux-v"
#endif

static const char *sample =
	"[\n"
	"  {\"url\": \"https://api.github.com/repos/x/y/releases/1\", \"tag_name\": \"macos-v1.3.0\", \"draft\": false, \"prerelease\": false,\n"
	"   \"html_url\": \"https://github.com/x/y/releases/tag/macos-v1.3.0\", \"assets\": [{\"name\": \"a.zip\", \"download_count\": 12, \"uploader\": {\"login\": \"m\", \"site_admin\": false}}]},\n"
	"  {\"tag_name\": \"" TAG "1.4.0\", \"draft\": false, \"prerelease\": false, \"html_url\": \"https://github.com/x/y/releases/tag/" TAG "1.4.0\",\n"
	"   \"body\": \"Notes with \\\"quotes\\\", a \\\\ backslash, \\u00e9 and a } brace\", \"author\": {\"id\": 5, \"type\": \"User\"}},\n"
	"  {\"tag_name\": \"" TAG "1.10.0\", \"draft\": false, \"prerelease\": false, \"html_url\": \"https://github.com/x/y/releases/tag/" TAG "1.10.0\", \"id\": 9.5e3},\n"
	"  {\"tag_name\": \"" TAG "1.9.9\", \"draft\": false, \"prerelease\": false, \"html_url\": \"https://github.com/x/y/releases/tag/" TAG "1.9.9\"},\n"
	"  {\"tag_name\": \"" TAG "2.0.0\", \"draft\": false, \"prerelease\": true, \"html_url\": \"https://github.com/x/y/releases/tag/" TAG "2.0.0\"},\n"
	"  {\"tag_name\": \"" TAG "3.0.0\", \"draft\": true, \"prerelease\": false, \"html_url\": \"https://github.com/x/y/releases/tag/" TAG "3.0.0\"},\n"
	"  {\"tag_name\": null, \"draft\": false, \"prerelease\": false},\n"
	"  {\"tag_name\": \"v1.99\", \"draft\": false, \"prerelease\": false, \"html_url\": \"https://example.com/v\"}\n"
	"]";

static void TestCompare( void )
{
	CHECK( lc_version_compare( "1.2", "1.10" ) < 0 );			/* numeric, not lexicographic */
	CHECK( lc_version_compare( "1.10", "1.9.9" ) > 0 );
	CHECK( lc_version_compare( "1.3", "1.3.0" ) < 0 );			/* a prefix is smaller */
	CHECK( lc_version_compare( "1.3.0", "1.3" ) > 0 );
	CHECK( lc_version_compare( "1.3.0", "1.3.0" ) == 0 );
	CHECK( lc_version_compare( "", "" ) == 0 );
	CHECK( lc_version_compare( "", "1" ) < 0 );
	CHECK( lc_version_compare( "007", "7" ) == 0 );
	CHECK( lc_version_compare( "1.4.0-beta", "1.4.0" ) > 0 );
	CHECK( lc_version_compare( "1a", "1b" ) < 0 );
	CHECK( lc_version_compare( "2", "10" ) < 0 );
	CHECK( lc_version_compare( "99999999999999999999", "100000000000000000000" ) < 0 );

	CHECK( lc_version_is_newer( "1.4.0", "1.3.0" ) );
	CHECK( !lc_version_is_newer( "1.3.0", "1.3.0" ) );
	CHECK( !lc_version_is_newer( "1.2.0", "1.3.0" ) );
	CHECK( !lc_version_is_newer( "", "1.3.0" ) );
	CHECK( !lc_version_is_newer( "1.4", "" ) );
	CHECK( !lc_version_is_newer( NULL, "1.3.0" ) );
}

static void TestParse( void )
{
	char v[64], u[512];

	CHECK( lc_update_parse( sample, strlen( sample ), TAG, "x/y", v, sizeof( v ), u, sizeof( u ) ) == 1 );
	CHECK_STR( v, "1.10.0" );
	CHECK_STR( u, "https://github.com/x/y/releases/tag/" TAG "1.10.0" );

	CHECK( lc_update_parse( sample, strlen( sample ), "macos-v", "x/y", v, sizeof( v ), u, sizeof( u ) ) == 1 );
	CHECK_STR( v, "1.3.0" );

	/* no release with that prefix: none, and the releases page */
	CHECK( lc_update_parse( sample, strlen( sample ), "nope-v", "x/y", v, sizeof( v ), u, sizeof( u ) ) == 0 );
	CHECK_STR( v, "" );
	CHECK_STR( u, "https://github.com/x/y/releases" );
	CHECK( lc_update_parse( "[]", 2, TAG, "x/y", v, sizeof( v ), u, sizeof( u ) ) == 0 );
	CHECK_STR( u, "https://github.com/x/y/releases" );
	CHECK( lc_update_parse( " [ ] ", 5, TAG, "x/y", v, sizeof( v ), u, sizeof( u ) ) == 0 );

	/* not an array, or damaged */
	CHECK( lc_update_parse( "{\"message\": \"Not Found\"}", 24, TAG, "x/y", v, sizeof( v ), u, sizeof( u ) ) == -1 );
	CHECK( lc_update_parse( "", 0, TAG, "x/y", v, sizeof( v ), u, sizeof( u ) ) == -1 );
	{
		const char *bad = "[{\"tag_name\": \"" TAG "1.0\"";
		CHECK( lc_update_parse( bad, strlen( bad ), TAG, "x/y", v, sizeof( v ), u, sizeof( u ) ) == -1 );
	}

	/* a missing html_url falls back to the releases page */
	{
		const char *j = "[{\"tag_name\":\"" TAG "2.5\",\"draft\":false,\"prerelease\":false}]";
		CHECK( lc_update_parse( j, strlen( j ), TAG, "x/y", v, sizeof( v ), u, sizeof( u ) ) == 1 );
		CHECK_STR( v, "2.5" );
		CHECK_STR( u, "https://github.com/x/y/releases" );
	}
	/* the first of equal versions wins */
	{
		const char *j = "[{\"tag_name\":\"" TAG "2.5\",\"html_url\":\"A\"},{\"tag_name\":\"" TAG "2.5\",\"html_url\":\"B\"}]";
		lc_update_parse( j, strlen( j ), TAG, "x/y", v, sizeof( v ), u, sizeof( u ) );
		CHECK_STR( u, "A" );
	}
}

static void TestDue( void )
{
	CHECK( lc_update_due( 0, 1000000 ) );
	CHECK( !lc_update_due( 1000, 1000 ) );
	CHECK( !lc_update_due( 1000, 1000 + 86399 ) );
	CHECK( lc_update_due( 1000, 1000 + 86400 ) );
	CHECK( lc_update_due( 5000, 1000 ) );			/* a date in the future re-checks */
}

static void TestState( void )
{
	const char *dir = tu_dir( "update-state" );
	lc_store *s = lc_store_load( tu_path( dir, "store.cfg" ) );
	char v[64], u[512];
	lc_update_result r;

	putenv( (char *)"VOYAGERSP_TEST_UPDATE=" );
	/* nothing stored: a fetch is due, no badge */
	CHECK( lc_update_auto_state( s, v, sizeof( v ), u, sizeof( u ) ) == 1 );
	CHECK_STR( v, "" );
	/* disabled: nothing at all */
	lc_store_set( s, LC_KEY_CHECK_UPDATES, "0" );
	CHECK( lc_update_auto_state( s, v, sizeof( v ), u, sizeof( u ) ) == 0 );
	lc_store_remove( s, LC_KEY_CHECK_UPDATES );

	/* a successful fetch is saved */
	memset( &r, 0, sizeof( r ) );
	r.ok = 1;
	strcpy( r.version, "1.9.0" );
	strcpy( r.url, "https://example.com/rel" );
	lc_update_save( s, &r, "1.3.0" );
	CHECK( lc_store_get_int64( s, LC_KEY_UPDATE_LAST, 0 ) > 1000000000LL );
	CHECK_STR( lc_store_get( s, LC_KEY_UPDATE_VERSION ), "1.9.0" );
	CHECK( lc_update_auto_state( s, v, sizeof( v ), u, sizeof( u ) ) == 0 );	/* throttled for a day */
	CHECK_STR( v, "1.9.0" );
	CHECK_STR( u, "https://example.com/rel" );
	/* ... a stale timestamp makes it due again, the stored latest is still shown */
	lc_store_set_int64( s, LC_KEY_UPDATE_LAST, lc_time( ) - 90000 );
	CHECK( lc_update_auto_state( s, v, sizeof( v ), u, sizeof( u ) ) == 1 );
	CHECK_STR( v, "1.9.0" );
	/* no release found: the stored latest goes */
	r.version[0] = 0;
	lc_update_save( s, &r, "1.3.0" );
	CHECK( lc_store_get( s, LC_KEY_UPDATE_VERSION ) == NULL );
	/* a failed check saves nothing */
	lc_store_set_int64( s, LC_KEY_UPDATE_LAST, 5 );
	r.ok = 0;
	lc_update_save( s, &r, "1.3.0" );
	CHECK( lc_store_get_int64( s, LC_KEY_UPDATE_LAST, 0 ) == 5 );

	/* the test hook answers at once, bypasses the throttle and saves nothing */
	putenv( (char *)"VOYAGERSP_TEST_UPDATE=9.9" );
	lc_store_set_int64( s, LC_KEY_UPDATE_LAST, lc_time( ) );
	CHECK( lc_update_auto_state( s, v, sizeof( v ), u, sizeof( u ) ) == 1 );
	lc_update_fetch( "1.3.0", &r );
	CHECK( r.ok && !strcmp( r.version, "9.9" ) );
	CHECK_STR( r.url, "https://github.com/" LC_GITHUB_REPO "/releases/tag/" TAG "9.9" );
	lc_store_remove( s, LC_KEY_UPDATE_VERSION );
	lc_store_set_int64( s, LC_KEY_UPDATE_LAST, 7 );
	lc_update_save( s, &r, "1.3.0" );
	CHECK( lc_store_get( s, LC_KEY_UPDATE_VERSION ) == NULL && lc_store_get_int64( s, LC_KEY_UPDATE_LAST, 0 ) == 7 );
	putenv( (char *)"VOYAGERSP_TEST_UPDATE=none" );
	lc_update_fetch( "1.3.0", &r );
	CHECK( r.ok && r.version[0] == 0 );
	CHECK_STR( r.url, "https://github.com/" LC_GITHUB_REPO "/releases" );
	putenv( (char *)"VOYAGERSP_TEST_UPDATE=" );
	lc_store_free( s );
}

static void TestAlerts( void )
{
	lc_update_result r;
	char m[256], i[512];
	int newer = 7;

	memset( &r, 0, sizeof( r ) );
	r.ok = 0;
	strcpy( r.problem, "GitHub answered 403." );
	lc_update_alert_texts( &r, "1.3.0", m, sizeof( m ), i, sizeof( i ), &newer );
	CHECK_STR( m, "Couldn't check for updates." );
	CHECK_STR( i, "GitHub answered 403. Check your internet connection and try again." );
	CHECK( newer == 0 );

	r.ok = 1;
	strcpy( r.version, "1.4.0" );
	lc_update_alert_texts( &r, "1.3.0", m, sizeof( m ), i, sizeof( i ), &newer );
	CHECK_STR( m, "Elite Force for " P " 1.4.0 is available." );
	CHECK( strncmp( i, "You have version 1.3.0. Download the new version from its release page on GitHub, then ", 87 ) == 0 );
	CHECK( strstr( i, "Your settings and saves are kept." ) != NULL );
	CHECK( strstr( i, "Applications" ) == NULL && strstr( i, "Mac" ) == NULL );
	CHECK( newer == 1 );

	strcpy( r.version, "1.3.0" );
	lc_update_alert_texts( &r, "1.3.0", m, sizeof( m ), i, sizeof( i ), &newer );
	CHECK_STR( m, "You're up to date." );
	CHECK_STR( i, "Elite Force for " P " 1.3.0 is the newest version." );
	CHECK( newer == 0 );
	r.version[0] = 0;
	lc_update_alert_texts( &r, "1.3.0", m, sizeof( m ), i, sizeof( i ), &newer );
	CHECK_STR( m, "You're up to date." );
}

int main( void )
{
	TestCompare( );
	TestParse( );
	TestDue( );
	TestState( );
	TestAlerts( );
	return TEST_DONE( );
}
