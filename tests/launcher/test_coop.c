/* lc_coop.c: infostrings, the sheet's description lines, the invite link, the choice and its launch
   fields, and the query client against a fake co-op host on loopback (launcher-data-coop.md 6.1, 6.2, 6.4) */
#include "lc_coop.h"
#include "lc_data.h"
#include "lc_launch.h"
#include "lc_sock.h"
#include "lc_test.h"
#include <stdlib.h>
#include <string.h>

#define OOB "\xff\xff\xff\xff"
#define MAC_APOS "\xe2\x80\x99"

static void test_info( void )
{
	char v[64];
	const char *info = "\\coopProtocol\\6\\version\\1.3.0\\hostname\\Bob-PC\\empty\\\\map\\sp_01";

	lc_coop_info_value( info, "coopProtocol", v, sizeof( v ) );
	CHECK_STR( v, "6" );
	lc_coop_info_value( info, "hostname", v, sizeof( v ) );
	CHECK_STR( v, "Bob-PC" );
	lc_coop_info_value( info, "empty", v, sizeof( v ) );
	CHECK_STR( v, "" );
	lc_coop_info_value( info, "map", v, sizeof( v ) );
	CHECK_STR( v, "sp_01" );
	lc_coop_info_value( info, "missing", v, sizeof( v ) );
	CHECK_STR( v, "" );
	lc_coop_info_value( info, "Map", v, sizeof( v ) );			/* keys are case-sensitive */
	CHECK_STR( v, "" );
	lc_coop_info_value( info, "hostname", v, 4 );				/* truncated, terminated */
	CHECK_STR( v, "Bob" );
	lc_coop_info_value( "not an infostring", "x", v, sizeof( v ) );
	CHECK_STR( v, "" );
}

static void test_describe( void )
{
	char out[1024];

	lc_coop_describe( "\\coopProtocol\\6\\version\\1.3.0\\hostname\\Bob-PC\\player\\B\"ob;\\state\\playing\\map\\sp_01\\mission\\Chapter 1\\time\\125\\skill\\1\\players\\1\\maxPlayers\\2\\needPassword\\0\\guest\\0",
		out, sizeof( out ) );
	CHECK_STR( out, "Mission: \xe2\x80\x9c" "Chapter 1\xe2\x80\x9d (map sp_01)\n"
		"Difficulty: Medium \xc2\xb7 on this level for 2 min\n"
		"Players: 1 of 2 (the host)\n"
		"Host: Bob\n"
		"Host" MAC_APOS "s app: Elite Force 1.3.0 \xc2\xb7 co-op version 6" );

	lc_coop_describe( "\\coopProtocol\\6\\version\\1.3.0\\state\\menu\\skill\\0\\players\\2\\maxPlayers\\2\\needPassword\\1\\guest\\0\\time\\30",
		out, sizeof( out ) );
	CHECK_STR( out, "The host is in the menus: no level is loaded yet.\n"
		"Difficulty: Easy \xc2\xb7 on this level for under a minute\n"
		"Players: 2 of 2 (the host, and player 2 as a stand-in on the host" MAC_APOS "s " LC_PLATFORM_NOUN_LC " until a friend joins)\n"
		"The host set a password.\n"
		"Host" MAC_APOS "s app: Elite Force 1.3.0 \xc2\xb7 co-op version 6" );

	lc_coop_describe( "\\state\\playing\\map\\sp_02\\skill\\7\\players\\0\\guest\\1", out, sizeof( out ) );
	CHECK_STR( out, "Map: sp_02\n"
		"Difficulty: Skill 7\n"
		"Players: 1 of 2 (the host, and a friend as player 2: the game is full)\n"
		"Host" MAC_APOS "s app: Elite Force ? \xc2\xb7 co-op version ?" );

	lc_coop_describe( "", out, 20 );					/* never overflows */
	CHECK( strlen( out ) < 20 );
}

static void test_choice( void )
{
	struct lc_launch_coop_fields f;

	lc_coop_choose_none( );
	CHECK( lc_coop_choice( ) == 0 );
	lc_coop_launch_fields( &f );
	CHECK( !f.coopHost && !f.coopConnect && !f.coopName && !f.coopSex );
	CHECK_STR( lc_coop_password( ), "" );

	lc_coop_choose_host( 1, "  Capt\xc3\xa9n \"Pat\"; 50%  ", "secret" );
	CHECK( lc_coop_choice( ) == 1 );
	lc_coop_launch_fields( &f );
	CHECK( f.coopHost == 1 && f.coopBackground == 1 && !f.coopConnect );
	CHECK_STR( f.coopName, "Captn Pat 50" );				/* safe name: ASCII, no quotes ; % */
	CHECK_STR( lc_coop_password( ), "secret" );
	lc_coop_choose_host( 0, "", "" );
	lc_coop_launch_fields( &f );
	CHECK( f.coopHost == 1 && f.coopBackground == 0 && !f.coopName );		/* empty name: none on the command line */

	lc_coop_choose_join( "192.168.1.20:29760", "Female", "seven_hazard", "Zed", "pw" );
	CHECK( lc_coop_choice( ) == 2 );
	lc_coop_launch_fields( &f );
	CHECK( !f.coopHost );
	CHECK_STR( f.coopConnect, "192.168.1.20:29760" );
	CHECK_STR( f.coopSex, "female" );
	CHECK_STR( f.coopName, "Zed" );
	CHECK_STR( f.coopLook, "seven_hazard" );
	CHECK_STR( lc_coop_password( ), "pw" );
	lc_coop_choose_join( "host.example.com", "male", "janeway", "Zed", "" );		/* only 0-9 . : on the command line */
	lc_coop_launch_fields( &f );
	CHECK( !f.coopConnect && !f.coopSex && !f.coopLook );
	lc_coop_choose_join( "10.0.0.1", "weird", "borgqueen", "A name that is far too long to be used as a name 123456789", "" );
	lc_coop_launch_fields( &f );
	CHECK_STR( f.coopSex, "male" );
	CHECK( !f.coopLook );							/* not one of coop_characters.h: Munro */
	CHECK( strlen( f.coopName ) <= 31 );
	lc_coop_choose_none( );
	CHECK_STR( lc_coop_password( ), "" );
}

static void test_invite( void )
{
	char out[128];
	lc_coop_invite_link( "203.0.113.5:29760", out, sizeof( out ) );
	CHECK_STR( out, "stvef://coop/203.0.113.5:29760" );
	lc_coop_invite_link( NULL, out, sizeof( out ) );
	CHECK( out[0] == 0 || !strncmp( out, "stvef://coop/", 13 ) );
	if ( out[0] )
		CHECK( strstr( out, ":29760" ) != NULL );
}

/* ---- the fake host ---- */
typedef struct {
	lc_sock s;
	int port;
	volatile int stop;
	lc_thread *thr;
	char protocol[8], version[32], paks[128];
	int ignoreChallenge, badChallenge, silent;
	volatile int queries;
} host_t;

static void host_run( void *u )
{
	host_t *h = (host_t *)u;
	char buf[256];

	while ( !h->stop ) {
		struct sockaddr_in from;
		socklen_t fl = sizeof( from );
		int n;
		char reply[1024], chal[64] = "";
		if ( lc_sock_wait_readable( h->s, 50 ) <= 0 )
			continue;
		n = (int)recvfrom( h->s, buf, sizeof( buf ) - 1, 0, (struct sockaddr *)&from, &fl );
		if ( n < 16 )
			continue;
		buf[n] = 0;
		if ( memcmp( buf, OOB "getcoopinfo", 15 ) )
			continue;
		h->queries++;
		if ( h->silent )
			continue;
		if ( buf[15] == ' ' )
			lc_strlcpy( chal, buf + 16, sizeof( chal ) );
		lc_snprintf( reply, sizeof( reply ), OOB "coopInfoResponse\n\\coopProtocol\\%s\\version\\%s\\hostname\\Fake\\paks\\%s\\state\\menu\\skill\\1\\players\\1\\maxPlayers\\2\\needPassword\\0\\guest\\0%s%s",
			h->protocol, h->version, h->paks, h->ignoreChallenge ? "" : "\\challenge\\", h->ignoreChallenge ? "" : h->badChallenge ? "wrong" : chal );
		sendto( h->s, reply, (int)strlen( reply ), 0, (struct sockaddr *)&from, fl );
	}
}

static host_t *host_start( void )
{
	host_t *h = (host_t *)calloc( 1, sizeof( *h ) );
	struct sockaddr_in a;
	socklen_t al = sizeof( a );

	lc_snprintf( h->protocol, sizeof( h->protocol ), "%d", LC_COOP_PROTOCOL );
	lc_strlcpy( h->version, lc_coop_app_version( ), sizeof( h->version ) );
	h->s = socket( AF_INET, SOCK_DGRAM, 0 );
	memset( &a, 0, sizeof( a ) );
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl( 0x7f000001u );
	bind( h->s, (struct sockaddr *)&a, sizeof( a ) );
	getsockname( h->s, (struct sockaddr *)&a, &al );
	h->port = ntohs( a.sin_port );
	h->thr = lc_thread_start( host_run, h );
	return h;
}

static void host_stop( host_t *h )
{
	h->stop = 1;
	lc_thread_join( h->thr );
	lc_sock_close( h->s );
	free( h );
}

static void test_query( void )
{
	lc_coop_query q;
	host_t *h = host_start( );
	char addr[64], expect[64];

	lc_snprintf( addr, sizeof( addr ), "127.0.0.1:%d", h->port );
	lc_coop_query_run( addr, 1.5, &q );
	CHECK_STR( q.status, "ok" );
	CHECK_STR( q.address, addr );
	CHECK_STR( q.error, "" );
	lc_coop_info_value( q.info, "hostname", expect, sizeof( expect ) );
	CHECK_STR( expect, "Fake" );
	CHECK( h->queries >= 1 );

	/* an address with spaces around it is trimmed */
	{
		char padded[80];
		lc_snprintf( padded, sizeof( padded ), "  %s \t", addr );
		lc_coop_query_run( padded, 1.5, &q );
		CHECK_STR( q.status, "ok" );
	}
	/* other co-op version: mismatch, with the sentence and the info for the description */
	lc_strlcpy( h->protocol, "5", sizeof( h->protocol ) );
	lc_coop_query_run( addr, 1.5, &q );
	CHECK_STR( q.status, "mismatch" );
	CHECK( strstr( q.error, "The host runs Elite Force " ) == q.error && strstr( q.error, "(co-op version 5)" ) != NULL );
	CHECK( strstr( q.error, "Both need the same version to play together." ) != NULL );
	CHECK( strstr( q.info, "coopProtocol\\5" ) != NULL );
	lc_snprintf( h->protocol, sizeof( h->protocol ), "%d", LC_COOP_PROTOCOL );
	lc_strlcpy( h->version, "0.0.1-other", sizeof( h->version ) );
	lc_coop_query_run( addr, 1.5, &q );
	CHECK_STR( q.status, "mismatch" );
	lc_strlcpy( h->version, lc_coop_app_version( ), sizeof( h->version ) );
	/* a wrong challenge, or a missing challenge on a host that echoes one: answers are ignored / accepted as the Mac does */
	h->badChallenge = 1;
	lc_coop_query_run( addr, 0.9, &q );
	CHECK_STR( q.status, "noanswer" );
	h->badChallenge = 0;
	h->ignoreChallenge = 1;
	lc_coop_query_run( addr, 0.9, &q );
	CHECK_STR( q.status, "ok" );
	h->ignoreChallenge = 0;
	/* the pak compare after a found game (this test's homepath has no paks at all) */
	h->paks[0] = 0;
	lc_coop_query_run( addr, 1.5, &q );
	lc_coop_check_paks( &q, NULL );
	CHECK_STR( q.status, "ok" );
	CHECK_STR( q.warning, "" );
	lc_strlcpy( h->paks, "pak0.pk3:3376297517 pak1.pk3:596947475", sizeof( h->paks ) );
	lc_coop_query_run( addr, 1.5, &q );
	lc_coop_check_paks( &q, NULL );
	CHECK_STR( q.status, "paks" );
	CHECK_STR( q.error, "You" MAC_APOS "re missing pak0.pk3, pak1.pk3 (the host has it). Both need the game" MAC_APOS "s original pak0.pk3, pak1.pk3 and pak2.pk3 (other packs don" MAC_APOS "t matter)." );
	lc_strlcpy( h->paks, "pak3.pk3:1592359207", sizeof( h->paks ) );
	lc_coop_query_run( addr, 1.5, &q );
	lc_coop_check_paks( &q, NULL );
	CHECK_STR( q.status, "ok" );
	CHECK( strstr( q.warning, "You don" MAC_APOS "t have the Expansion Pack (pak3.pk3); the host does." ) == q.warning );
	h->paks[0] = 0;
	/* asked three times over the timeout */
	h->silent = 1;
	h->queries = 0;
	lc_coop_query_run( addr, 0.9, &q );
	CHECK_STR( q.status, "noanswer" );
	CHECK( h->queries == 3 );
	CHECK_STR( q.address, addr );
	host_stop( h );

	/* the bad addresses (6.4), with the exact sentences */
	lc_coop_query_run( "   ", 0.5, &q );
	CHECK( !strcmp( q.status, "bad" ) && !strcmp( q.error, "Type the host" MAC_APOS "s address, such as 192.168.1.20." ) && !q.address[0] );
	lc_coop_query_run( "1.2.3.4:5:6", 0.5, &q );
	CHECK( !strcmp( q.status, "bad" ) && !strcmp( q.error, "IPv6 addresses aren" MAC_APOS "t supported yet. Use the host" MAC_APOS "s IPv4 address, such as 192.168.1.20." ) );
	lc_coop_query_run( "1.2.3.4:abc", 0.5, &q );
	CHECK_STR( q.status, "bad" );
	CHECK_STR( q.error, "\xe2\x80\x9c" "abc\xe2\x80\x9d isn" MAC_APOS "t a port. Leave it out for the co-op port (29760), or give a number from 1 to 65535." );
	lc_coop_query_run( "1.2.3.4:70000", 0.5, &q );
	CHECK_STR( q.status, "bad" );
	lc_coop_query_run( "1.2.3.4:", 0.5, &q );
	CHECK_STR( q.status, "bad" );
	lc_coop_query_run( ":29760", 0.5, &q );
	CHECK( !strcmp( q.status, "bad" ) && !strcmp( q.error, "Type the host" MAC_APOS "s address before the port." ) );
	lc_coop_query_run( "no-such-host.invalid", 0.5, &q );
	CHECK( !strcmp( q.status, "bad" ) && !strcmp( q.error, "\xe2\x80\x9c" "no-such-host.invalid\xe2\x80\x9d couldn" MAC_APOS "t be found. Check the address." ) );
}

static void test_paks( void )
{
	char text[1024];
	static const char *ours = "pak0.pk3:3376297517 pak1.pk3:596947475 pak2.pk3:3960871590";

	CHECK( lc_coop_core_pak_index( 3376297517u ) == 0 );
	CHECK( lc_coop_core_pak_index( 1592359207u ) == 3 );
	CHECK( lc_coop_core_pak_index( 12345u ) == -1 );
	CHECK( lc_coop_compare_paks( ours, ours, 0, text, sizeof( text ) ) == LC_COOP_PAKS_SAME && !text[0] );
	/* the same sentences as the engine's CoopNet_ComparePaks (sys_coop_net.c) */
	CHECK( lc_coop_compare_paks( ours, "pak0.pk3:3376297517 pak1.pk3:596947475 pak2.pk3:1", 0, text, sizeof( text ) ) == LC_COOP_PAKS_DIFFER );
	CHECK_STR( text, "Your pak2.pk3 differs from the host's. Both need the game's original pak0.pk3, pak1.pk3 and pak2.pk3 (other packs don't matter)." );
	CHECK( lc_coop_compare_paks( "pak0.pk3:3376297517", ours, 1, text, sizeof( text ) ) == LC_COOP_PAKS_DIFFER );
	CHECK_STR( text, "You're missing pak1.pk3, pak2.pk3 (player 2 has it). Both need the game's original pak0.pk3, pak1.pk3 and pak2.pk3 (other packs don't matter)." );
	CHECK( lc_coop_compare_paks( ours, "pak0.pk3:3376297517 pak1.pk3:596947475 pak2.pk3:3960871590 pak3.pk3:1592359207", 0, text, sizeof( text ) ) == LC_COOP_PAKS_EXPANSION );
	CHECK( strstr( text, "The host doesn't have the Expansion Pack (pak3.pk3); you do. You can still play together" ) == text );
	/* other packs never count */
	CHECK( lc_coop_compare_paks( ours, "zpak.pk3:5 pak0.pk3:3376297517 pak1.pk3:596947475 pak2.pk3:3960871590", 0, text, sizeof( text ) ) == LC_COOP_PAKS_SAME );
	lc_coop_local_paks( NULL, text, sizeof( text ) );
	CHECK_STR( text, "" );							/* this test's homepath has none */
}

/* RFC 1320's test vectors, and a pk3's checksum from its central directory */
static void test_checksum( void )
{
	static const struct { const char *in, *hex; } v[] = {
		{ "", "31d6cfe0d16ae931b73c59d7e0c089c0" },
		{ "abc", "a448017aaf21d8525fc10ae87aa6729d" },
		{ "message digest", "d9130a8164549fe818874806e1c7014b" },
		{ "12345678901234567890123456789012345678901234567890123456789012345678901234567890", "e33b4ddc9c38f2199c3e7b164fcc0536" },
	};
	unsigned char d[16];
	char hex[33];
	int i, j;

	for ( i = 0; i < (int)( sizeof( v ) / sizeof( v[0] ) ); i++ ) {
		lc_md4( v[i].in, strlen( v[i].in ), d );
		for ( j = 0; j < 16; j++ )
			lc_snprintf( hex + j * 2, 3, "%02x", d[j] );
		CHECK_STR( hex, v[i].hex );
	}
	/* a zip with three entries: two files (CRCs 0x11223344 and 0xaabbccdd) and an empty one */
	{
		unsigned char zip[3 * 47 + 22], crcs[8] = { 0x44, 0x33, 0x22, 0x11, 0xdd, 0xcc, 0xbb, 0xaa };
		const unsigned crc[3] = { 0x11223344u, 0, 0xaabbccddu }, usize[3] = { 10, 0, 7 };
		unsigned sum, want;
		char path[LC_MAX_PATH];
		FILE *f;
		size_t o = 0;

		memset( zip, 0, sizeof( zip ) );
		for ( i = 0; i < 3; i++ ) {
			unsigned char *p = zip + o;
			p[0] = 'P'; p[1] = 'K'; p[2] = 1; p[3] = 2;
			for ( j = 0; j < 4; j++ ) {
				p[16 + j] = (unsigned char)( crc[i] >> ( 8 * j ) );
				p[24 + j] = (unsigned char)( usize[i] >> ( 8 * j ) );
			}
			p[28] = 1;						/* name length */
			p[46] = (unsigned char)( 'a' + i );
			o += 47;
		}
		zip[o] = 'P'; zip[o + 1] = 'K'; zip[o + 2] = 5; zip[o + 3] = 6;
		zip[o + 12] = (unsigned char)o;		/* central directory size; offset 0 */
		lc_path_join( path, sizeof( path ), lc_homepath( ), "t.pk3" );
		lc_mkdir_p( lc_homepath( ) );
		f = fopen( path, "wb" );
		CHECK( f != NULL );
		if ( f ) {
			fwrite( zip, 1, sizeof( zip ), f );
			fclose( f );
		}
		lc_md4( crcs, sizeof( crcs ), d );
		want = 0;
		for ( j = 0; j < 4; j++ )
			want ^= (unsigned)d[j * 4] | ( (unsigned)d[j * 4 + 1] << 8 ) | ( (unsigned)d[j * 4 + 2] << 16 ) | ( (unsigned)d[j * 4 + 3] << 24 );
		CHECK( lc_pak_checksum( path, &sum ) && sum == want );
		remove( path );
		CHECK( !lc_pak_checksum( path, &sum ) );
	}
}

static void test_characters( void )
{
	const char *id, *name, *group;
	char args[LC_CMDLINE_SIZE] = "";
	lc_launch o;

	CHECK( lc_coop_character_count( ) > 10 );
	CHECK( lc_coop_character( 0, &id, &name, &group ) && !strcmp( id, "janeway" ) && !strcmp( group, "crew" ) );
	CHECK( !lc_coop_character( lc_coop_character_count( ), &id, &name, &group ) );
	CHECK( lc_coop_character_valid( "telsia" ) && !lc_coop_character_valid( "" ) && !lc_coop_character_valid( NULL ) && !lc_coop_character_valid( "../x" ) );
	/* the launch arguments carry it after the sex, as the Mac's */
	memset( &o, 0, sizeof( o ) );
	o.mode = LC_MODE_SP;
	o.coopConnect = "10.0.0.2:29760";
	o.coopSex = "female";
	o.coopLook = "seven";
	lc_launch_append_args( &o, lc_homebase( ), args, sizeof( args ) );
	CHECK( strstr( args, "+set coop_connect \"10.0.0.2:29760\" +set coop_sex female +set coop_look seven " ) != NULL );
	args[0] = 0;
	o.coopLook = "bad id";
	lc_launch_append_args( &o, lc_homebase( ), args, sizeof( args ) );
	CHECK( strstr( args, "coop_look" ) == NULL );
}

int main( void )
{
	lc_net_init( );
	test_info( );
	test_describe( );
	test_choice( );
	test_invite( );
	test_paks( );
	test_checksum( );
	test_characters( );
	test_query( );
	return TEST_DONE( );
}
