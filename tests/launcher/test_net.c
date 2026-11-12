/* lc_net.c: master list parsing (binary and hex), getinfo / getstatus parsing, colour codes, address
   parsing, efservers helpers, sorting, and a loopback UDP test: fake servers and a fake master on
   127.0.0.1 answering getinfo / getstatus / getservers, queried with lc_query_servers, lc_query_status,
   lc_query_master and the browser model (servers.md 7, 8). */
#include "lc_net.h"
#include "lc_nat.h"
#include "lc_sock.h"
#include "lc_test.h"
#include <stdlib.h>
#include <string.h>

/* ---- parsing ---- */

static int g_entries;
static lc_addr4 g_list[16];

static void list_cb( lc_addr4 a, void *u )
{
	(void)u;
	if ( g_entries < 16 )
		g_list[g_entries] = a;
	g_entries++;
}

static void test_master_lists( void )
{
	/* binary: \ + 4 bytes + 2 bytes, ending in \EOT */
	static const unsigned char bin[] = { 0xff, 0xff, 0xff, 0xff, 'g', 'e', 't', 's', 'e', 'r', 'v', 'e', 'r', 's', 'R', 'e', 's', 'p', 'o', 'n', 's', 'e',
		'\\', 127, 0, 0, 1, 0x6d, 0x38, '\\', 192, 168, 1, 5, 0x6d, 0x39, '\\', 'E', 'O', 'T', 0, 0, 0 };
	static const char hex[] = "\xff\xff\xff\xffgetserversResponse\\7F0000016d38\\C0A801056D39\\EOT";
	static const char hex2[] = "\xff\xff\xff\xffgetserversResponse \\0A0000016D38\\ZZ\\C0A801056D3A\\EOT\\";
	static const unsigned char stray[] = { '\\', 1, 2, 3, 4, 0x6d, 0x38, 'x', 'y', '\\', 5, 6, 7, 8, 0x6d, 0x39, '\\', 'E', 'O', 'T' };

	g_entries = 0;
	CHECK( lc_parse_server_list( bin, sizeof( bin ), list_cb, NULL ) == 2 );
	CHECK( g_entries == 2 );
	CHECK( g_list[0].ip[0] == 127 && g_list[0].ip[3] == 1 && g_list[0].port == 27960 );
	CHECK( g_list[1].ip[0] == 192 && g_list[1].ip[2] == 1 && g_list[1].ip[3] == 5 && g_list[1].port == 27961 );
	g_entries = 0;
	CHECK( lc_parse_server_list( (const unsigned char *)hex, sizeof( hex ) - 1, list_cb, NULL ) == 2 );
	CHECK( g_list[0].ip[0] == 127 && g_list[0].port == 27960 );
	CHECK( g_list[1].ip[0] == 192 && g_list[1].ip[3] == 5 && g_list[1].port == 27961 );
	/* the encoding is decided from the first entry (hex here), a bad hex entry gives zeros, parsing goes on */
	g_entries = 0;
	CHECK( lc_parse_server_list( (const unsigned char *)hex2, sizeof( hex2 ) - 1, list_cb, NULL ) >= 2 );
	CHECK( g_list[0].ip[0] == 10 && g_list[0].port == 27960 );
	/* stray bytes between entries are skipped up to the next backslash */
	g_entries = 0;
	CHECK( lc_parse_server_list( stray, sizeof( stray ), list_cb, NULL ) == 2 );
	CHECK( g_list[1].ip[0] == 5 && g_list[1].port == 27961 );
	/* nothing to parse */
	g_entries = 0;
	CHECK( lc_parse_server_list( (const unsigned char *)"\xff\xff\xff\xff" "x", 5, list_cb, NULL ) == 0 );
	CHECK( lc_parse_server_list( (const unsigned char *)"", 0, list_cb, NULL ) == 0 );
	CHECK( g_entries == 0 );
}

static void test_addresses( void )
{
	struct sockaddr_in a;

	CHECK( lc_parse_address4( "1.2.3.4", 27960, &a ) && ntohs( a.sin_port ) == 27960 && ntohl( a.sin_addr.s_addr ) == 0x01020304u );
	CHECK( lc_parse_address4( "1.2.3.4:27961", 27960, &a ) && ntohs( a.sin_port ) == 27961 );
	CHECK( !lc_parse_address4( "1.2.3.4:0", 27960, &a ) );
	CHECK( !lc_parse_address4( "1.2.3.4:65536", 27960, &a ) );
	CHECK( !lc_parse_address4( "1.2.3.4:", 27960, &a ) );
	CHECK( !lc_parse_address4( "1.2.3.4:x", 27960, &a ) );
	CHECK( !lc_parse_address4( "999.1.1.1", 27960, &a ) );
	CHECK( !lc_parse_address4( "example.com", 27960, &a ) );
	CHECK( !lc_parse_address4( ":27960", 27960, &a ) );
	CHECK( !lc_parse_address4( "", 27960, &a ) );
	CHECK( lc_ipv4_private( 0x0a000001u ) && lc_ipv4_private( 0xac100001u ) && lc_ipv4_private( 0xc0a80101u ) && lc_ipv4_private( 0x64400001u ) );
	CHECK( !lc_ipv4_private( 0x08080808u ) && !lc_ipv4_private( 0xac200001u ) && !lc_ipv4_private( 0x64800001u ) );
}

static void test_colors( void )
{
	char out[64];
	unsigned char col[64];

	lc_decode_colors( "^1Re^2d x", out, sizeof( out ), col );
	CHECK_STR( out, "Red x" );
	CHECK( col[0] == 1 && col[1] == 1 && col[2] == 2 && col[3] == 2 );
	lc_decode_colors( "plain", out, sizeof( out ), col );
	CHECK_STR( out, "plain" );
	CHECK( col[0] == 7 );
	lc_decode_colors( "^^071v1", out, sizeof( out ), col );		/* a left-over ^ makes a code, as in the game */
	CHECK_STR( out, "1v1" );
	CHECK( col[0] == 7 );
	lc_decode_colors( "a^", out, sizeof( out ), NULL );
	CHECK_STR( out, "a^" );
	lc_decode_colors( "^ab", out, sizeof( out ), col );			/* letters make codes too: 'a' & 7 = 1 */
	CHECK_STR( out, "b" );
	CHECK( col[0] == 1 );
	lc_strip_colors( "^0Bl^7ack", out, sizeof( out ) );
	CHECK_STR( out, "Black" );
	lc_strip_colors( "no codes", out, sizeof( out ) );
	CHECK_STR( out, "no codes" );
	lc_strip_colors( "^1^2^3x", out, sizeof( out ) );
	CHECK_STR( out, "x" );
	lc_strip_colors( "^1abcdefghij", out, 5 );					/* truncation keeps it terminated */
	CHECK_STR( out, "abcd" );

	CHECK( lc_cmod_number( "v1.29_GIT_f037859-2023-09-30 linux-x86_64", out, sizeof( out ) ) && !strcmp( out, "1.29" ) );
	CHECK( lc_cmod_number( "cMod v2.0 Alpha", out, sizeof( out ) ) && !strcmp( out, "2.0" ) );
	CHECK( lc_cmod_number( "ioEF:cMod HM v1.30 win", out, sizeof( out ) ) && !strcmp( out, "1.30" ) );
	CHECK( !lc_cmod_number( "cMod nothing", out, sizeof( out ) ) );
	CHECK( !lc_cmod_number( "ver1.2", out, sizeof( out ) ) );		/* \b before the v */
	CHECK( lc_cmod_rank( 0, "v1.29" ) == 0 );
	CHECK( lc_cmod_rank( 1, "v1.29" ) > 1.28 && lc_cmod_rank( 1, "v1.29" ) < 1.30 );
	CHECK( lc_cmod_rank( 1, "" ) == 0.01 );
}

static const char INFO_PKT[] = "\xff\xff\xff\xffinfoResponse\n\\hostname\\^1Test ^7Server\\mapname\\HM_Test1\\clients\\3\\sv_maxclients\\12"
	"\\g_humanplayers\\2\\gametype\\3\\g_needpass\\1\\protocol\\24";
static const char STATUS_PKT[] = "\xff\xff\xff\xffstatusResponse\n\\version\\ioEF:cMod HM v1.29 linux\\g_humanplayers\\2\\sv_hostname\\x\n"
	"10 48 \"^1Alice\"\n3 0 \"Bot Guy\"\n5 120 \"Bob\"\r\nnot a player line\n\n";

static void test_parse_info_status( void )
{
	lc_server_info i;
	lc_server_status st;

	CHECK( lc_parse_info_response( (const unsigned char *)INFO_PKT, sizeof( INFO_PKT ) - 1, "1.2.3.4:27960", &i ) );
	CHECK_STR( i.address, "1.2.3.4:27960" );
	CHECK_STR( i.hostname, "^1Test ^7Server" );
	CHECK_STR( i.name, "Test Server" );
	CHECK_STR( i.map, "hm_test1" );
	CHECK( i.players == 3 && i.maxPlayers == 12 && i.humans == 2 && i.gametype == 3 && i.needPass == 1 );
	/* defaults: no hostname -> the address; no g_humanplayers -> -1; needpass spelled the other way */
	{
		static const char p[] = "\xff\xff\xff\xffinfoResponse\n\\mapname\\ctf_dn1\\clients\\0\\needpass\\1";
		CHECK( lc_parse_info_response( (const unsigned char *)p, sizeof( p ) - 1, "5.6.7.8:27961", &i ) );
		CHECK_STR( i.name, "5.6.7.8:27961" );
		CHECK_STR( i.hostname, "5.6.7.8:27961" );
		CHECK( i.humans == -1 && i.needPass == 1 && i.gametype == 0 && i.players == 0 );
	}
	/* ISO-8859-1 hostnames come out as UTF-8 */
	{
		static const char p[] = "\xff\xff\xff\xffinfoResponse\n\\hostname\\Caf\xe9";
		CHECK( lc_parse_info_response( (const unsigned char *)p, sizeof( p ) - 1, "1.1.1.1:1", &i ) );
		CHECK_STR( i.name, "Caf\xc3\xa9" );
	}
	/* not info responses */
	CHECK( !lc_parse_info_response( (const unsigned char *)"\xff\xff\xff\xffstatusResponse\n\\a\\b", 22, "1.1.1.1:1", &i ) );
	CHECK( !lc_parse_info_response( (const unsigned char *)"\xff\xff\xff\xffinfoRespons", 16, "1.1.1.1:1", &i ) );
	CHECK( !lc_parse_info_response( (const unsigned char *)"junkjunkjunkjunkjunkjunk", 24, "1.1.1.1:1", &i ) );

	CHECK( lc_parse_status_response( (const unsigned char *)STATUS_PKT, sizeof( STATUS_PKT ) - 1, &st ) );
	CHECK( st.humans == 2 && st.isCmod );
	CHECK_STR( st.cmodVersion, "" );
	CHECK( st.playerCount == 3 );
	CHECK_STR( st.players[0].name, "^1Alice" );
	CHECK( st.players[0].score == 10 && st.players[0].ping == 48 );
	CHECK_STR( st.players[1].name, "Bot Guy" );
	CHECK( st.players[1].ping == 0 );
	CHECK_STR( st.players[2].name, "Bob" );
	CHECK( st.players[2].score == 5 && st.players[2].ping == 120 );
	CHECK( st.when > 0 );
	{
		static const char p[] = "\xff\xff\xff\xffstatusResponse\n\\version\\Elite Force 1.2\\version_cmod\\v1.31 test\\g_unlagged\\1\n";
		CHECK( lc_parse_status_response( (const unsigned char *)p, sizeof( p ) - 1, &st ) );
		CHECK( st.isCmod && !strcmp( st.cmodVersion, "v1.31 test" ) && st.humans == -1 && st.playerCount == 0 );
		static const char q[] = "\xff\xff\xff\xffstatusResponse\n\\version\\ioEF 1.36 other\\g_unlagged\\1\n";
		CHECK( lc_parse_status_response( (const unsigned char *)q, sizeof( q ) - 1, &st ) );
		CHECK( !st.isCmod );
	}
	CHECK( !lc_parse_status_response( (const unsigned char *)"\xff\xff\xff\xffstatusRespons", 18, &st ) );
}

static void test_helpers( void )
{
	char out[512], url[512];
	lc_player p[4] = { { "A", 5, 0 }, { "B", 3, 50 }, { "C", 9, 40 }, { "D", 1, 0 } };

	lc_sort_players( p, 4 );
	CHECK_STR( p[0].name, "C" );
	CHECK_STR( p[1].name, "B" );
	CHECK_STR( p[2].name, "A" );
	CHECK_STR( p[3].name, "D" );
	CHECK( lc_tag_bots( p, 4, 2 ) );
	CHECK( !lc_tag_bots( p, 4, 3 ) );
	CHECK( lc_tag_bots( p, 4, -1 ) );

	lc_efservers_info_url( "hm_voy1", out, sizeof( out ) );
	CHECK_STR( out, "https://efservers.com/pk3.php?preview&popup=1&map=hm%5Fvoy1" );
	lc_efservers_3d_lookup_url( "ctf_dn1", out, sizeof( out ) );
	CHECK_STR( out, "https://efservers.com/getPK3info.php?pk3=ctf%5Fdn1&preview" );
	CHECK( lc_efservers_parse_3d( "<iframe src=\"https://efservers.com/bsp/index.php?pk3=hm_voy1-v2.pk3&amp;file=maps/hm_voy1.bsp\"></iframe>", url, sizeof( url ) ) );
	CHECK_STR( url, "https://efservers.com/bsp/index.php?pk3=hm_voy1-v2.pk3&file=maps/hm_voy1.bsp" );
	CHECK( lc_efservers_parse_3d( "x bsp/index.php?pk3=a.b+c&file=maps/q.bsp.bsp y", url, sizeof( url ) ) );
	CHECK_STR( url, "https://efservers.com/bsp/index.php?pk3=a.b+c&file=maps/q.bsp.bsp" );
	CHECK( !lc_efservers_parse_3d( "<html>no 3d here: bsp/index.php?pk3=x&file=models/x.md3</html>", url, sizeof( url ) ) );
	CHECK( !lc_efservers_parse_3d( "", url, sizeof( url ) ) );

	CHECK_STR( lc_map_state( NULL, "" ) == LC_MAP_HAVE ? "have" : "no", "have" );
	CHECK( lc_map_state( NULL, "hm_voy3" ) == LC_MAP_EXPANSION );
	CHECK( lc_map_state( NULL, "CTF_Dn1" ) == LC_MAP_EXPANSION );
	CHECK( lc_map_state( NULL, "hm_unknown" ) == LC_MAP_MISSING );
	CHECK( lc_map_is_builtin( NULL, "hm_voy3" ) && lc_map_is_builtin( NULL, "" ) && !lc_map_is_builtin( NULL, "hm_unknown" ) );
}

/* ---- the fake servers and master ---- */

typedef struct {
	lc_sock s;
	int port;
	volatile int stop;
	lc_thread *thr;
	char info[1024], status[2048];
	const unsigned char *list;			/* a master: the getserversResponse payload to send */
	size_t listLen;
	volatile int infoSeen, statusSeen, serversSeen;
} fake_t;

static void fake_run( void *u )
{
	fake_t *f = (fake_t *)u;
	unsigned char buf[2048];

	while ( !f->stop ) {
		struct sockaddr_in from;
		socklen_t fl = sizeof( from );
		int n;
		if ( lc_sock_wait_readable( f->s, 50 ) <= 0 )
			continue;
		n = (int)recvfrom( f->s, (char *)buf, sizeof( buf ) - 1, 0, (struct sockaddr *)&from, &fl );
		if ( n < 5 )
			continue;
		buf[n] = 0;
		if ( !strncmp( (char *)buf + 4, "getinfo", 7 ) && f->info[0] ) {
			f->infoSeen++;
			sendto( f->s, f->info, (int)strlen( f->info ), 0, (struct sockaddr *)&from, fl );
		} else if ( !strncmp( (char *)buf + 4, "getstatus", 9 ) && f->status[0] ) {
			f->statusSeen++;
			sendto( f->s, f->status, (int)strlen( f->status ), 0, (struct sockaddr *)&from, fl );
		} else if ( !strncmp( (char *)buf + 4, "getservers", 10 ) && f->list ) {
			f->serversSeen++;
			sendto( f->s, (const char *)f->list, (int)f->listLen, 0, (struct sockaddr *)&from, fl );
		}
	}
}

static fake_t *fake_start( void )
{
	fake_t *f = (fake_t *)calloc( 1, sizeof( *f ) );
	struct sockaddr_in a;
	socklen_t al = sizeof( a );

	f->s = socket( AF_INET, SOCK_DGRAM, 0 );
	memset( &a, 0, sizeof( a ) );
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl( 0x7f000001u );
	a.sin_port = 0;
	CHECK( bind( f->s, (struct sockaddr *)&a, sizeof( a ) ) == 0 );
	getsockname( f->s, (struct sockaddr *)&a, &al );
	f->port = ntohs( a.sin_port );
	f->thr = lc_thread_start( fake_run, f );
	return f;
}

static void fake_stop( fake_t *f )
{
	f->stop = 1;
	lc_thread_join( f->thr );
	lc_sock_close( f->s );
	free( f );
}

static void make_info( fake_t *f, const char *host, const char *map, int clients, int max, int humans, int gametype, int needpass )
{
	char tail[512];
	lc_snprintf( tail, sizeof( tail ), "\\hostname\\%s\\mapname\\%s\\clients\\%d\\sv_maxclients\\%d\\gametype\\%d\\g_needpass\\%d", host, map, clients, max, gametype, needpass );
	memcpy( f->info, "\xff\xff\xff\xffinfoResponse\n", 18 );
	f->info[18] = 0;
	lc_strlcat( f->info, tail, sizeof( f->info ) );
	if ( humans >= 0 ) {
		char h[32];
		lc_snprintf( h, sizeof( h ), "\\g_humanplayers\\%d", humans );
		lc_strlcat( f->info, h, sizeof( f->info ) );
	}
}

static void make_status( fake_t *f, const char *version, const char *players )
{
	memcpy( f->status, "\xff\xff\xff\xffstatusResponse\n", 18 );
	f->status[18] = 0;
	lc_strlcat( f->status, "\\version\\", sizeof( f->status ) );
	lc_strlcat( f->status, version, sizeof( f->status ) );
	lc_strlcat( f->status, "\n", sizeof( f->status ) );
	lc_strlcat( f->status, players, sizeof( f->status ) );
}

typedef struct {
	int got, gotStatus, done, answered;
	lc_server_info info[8];
	char statusAddr[8][24];
	lc_server_status status[8];
} q_t;

static void q_info( const lc_server_info *i, void *u )
{
	q_t *q = (q_t *)u;
	if ( q->got < 8 )
		q->info[q->got] = *i;
	q->got++;
}

static void q_status( const char *a, const lc_server_status *s, void *u )
{
	q_t *q = (q_t *)u;
	if ( q->gotStatus < 8 ) {
		lc_strlcpy( q->statusAddr[q->gotStatus], a, 24 );
		q->status[q->gotStatus] = *s;
	}
	q->gotStatus++;
}

static void q_done( int answered, void *u )
{
	q_t *q = (q_t *)u;
	q->done++;
	q->answered = answered;
}

static void test_loopback_query( void )
{
	fake_t *a = fake_start(), *b = fake_start();
	char addrA[32], addrB[32], deadAddr[32];
	const char *addrs[4];
	lc_query_callbacks cb;
	q_t q;
	lc_server_status st;
	volatile int cancel = 0;
	double t0;
	int i;

	make_info( a, "^2Alpha ^7Game", "HM_Alpha", 4, 16, 2, 0, 0 );
	make_status( a, "ioEF:cMod HM v1.29 linux", "7 33 \"^1Pat\"\n2 0 \"Munro\"\n" );
	make_info( b, "Bravo", "hm_bravo", 0, 8, -1, 4, 1 );
	make_status( b, "Elite Force 1.2", "" );
	lc_snprintf( addrA, sizeof( addrA ), "127.0.0.1:%d", a->port );
	lc_snprintf( addrB, sizeof( addrB ), "127.0.0.1:%d", b->port );
	lc_snprintf( deadAddr, sizeof( deadAddr ), "127.0.0.1:%d", 9 );		/* nobody listens */
	addrs[0] = addrA; addrs[1] = addrB; addrs[2] = deadAddr; addrs[3] = "not an address";
	memset( &cb, 0, sizeof( cb ) );
	cb.got_info = q_info; cb.got_status = q_status; cb.done = q_done; cb.user = &q;
	memset( &q, 0, sizeof( q ) );
	t0 = lc_now( );
	lc_query_servers( addrs, 4, 0, &cancel, &cb );
	CHECK( q.done == 1 && q.answered == 2 && q.got == 2 );
	CHECK( lc_now( ) - t0 < 6.0 );
	{
		for ( i = 0; i < q.got; i++ ) {
			const lc_server_info *in = &q.info[i];
			CHECK( in->ping >= 0 && in->ping < 2000 );
			if ( !strcmp( in->address, addrA ) ) {
				CHECK_STR( in->name, "Alpha Game" );
				CHECK_STR( in->map, "hm_alpha" );
				CHECK( in->players == 4 && in->maxPlayers == 16 && in->humans == 2 && in->gametype == 0 && !in->needPass );
			} else {
				CHECK_STR( in->address, addrB );
				CHECK_STR( in->name, "Bravo" );
				CHECK( in->humans == -1 && in->gametype == 4 && in->needPass );
			}
		}
	}
	CHECK( q.gotStatus == 2 );
	for ( i = 0; i < q.gotStatus; i++ )
		if ( !strcmp( q.statusAddr[i], addrA ) )
			CHECK( q.status[i].isCmod && !strcmp( q.status[i].cmodVersion, "" ) && q.status[i].playerCount == 2 );
		else
			CHECK( !strcmp( q.statusAddr[i], addrB ) && !q.status[i].isCmod );
	CHECK( a->infoSeen == 1 && a->statusSeen == 1 && b->infoSeen == 1 && b->statusSeen == 1 );

	/* cancelled: no callbacks at all */
	memset( &q, 0, sizeof( q ) );
	cancel = 1;
	lc_query_servers( addrs, 2, 0, &cancel, &cb );
	CHECK( q.got == 0 && q.done == 0 );

	/* the single status query (7.5) */
	memset( &st, 0, sizeof( st ) );
	CHECK( lc_query_status( addrA, &st ) );
	CHECK( st.playerCount == 2 && !strcmp( st.players[0].name, "^1Pat" ) );
	CHECK( lc_query_status( addrB, &st ) && st.playerCount == 0 );
	CHECK( !lc_query_status( deadAddr, &st ) );			/* never answers: 2 tries of 1 s */

	fake_stop( a );
	fake_stop( b );
}

static void test_loopback_master( void )
{
	fake_t *m = fake_start(), *s1 = fake_start();
	static unsigned char list[64];
	size_t n = 0;
	char master[40];
	g_entries = 0;

	list[n++] = 0xff; list[n++] = 0xff; list[n++] = 0xff; list[n++] = 0xff;
	memcpy( list + n, "getserversResponse", 18 ); n += 18;
	list[n++] = '\\'; list[n++] = 127; list[n++] = 0; list[n++] = 0; list[n++] = 1;
	list[n++] = (unsigned char)( s1->port >> 8 ); list[n++] = (unsigned char)s1->port;
	list[n++] = '\\'; memcpy( list + n, "EOT", 3 ); n += 3;
	m->list = list;
	m->listLen = n;
	lc_snprintf( master, sizeof( master ), "127.0.0.1:%d", m->port );
	CHECK( lc_query_master( master, 500, list_cb, NULL ) == 1 );
	CHECK( g_entries == 1 && g_list[0].port == s1->port && g_list[0].ip[0] == 127 );
	CHECK( m->serversSeen == 1 );
	CHECK( lc_query_master_count( master ) == 1 );
	CHECK( lc_query_master( "127.0.0.1:9", 300, list_cb, NULL ) == -1 );		/* no answer */
	CHECK( lc_query_master_count( "no-such-host.invalid" ) == -1 );

	/* the listing check (9.10): listed / missing / down per master */
	{
		char masters[3][65], want[40];
		lc_list_result r;
		lc_strlcpy( masters[0], master, 65 );
		lc_strlcpy( masters[1], master, 65 );
		lc_strlcpy( masters[2], "127.0.0.1:9", 65 );
		lc_snprintf( want, sizeof( want ), "127.0.0.1:%d", s1->port );
		lc_list_check( (const char (*)[65])masters, 3, want, &r );
		CHECK( r.listed[0] && r.listed[1] && !r.missing[0] && !r.down[0] );
		CHECK( r.down[2] && !r.listed[2] );
		lc_list_check( (const char (*)[65])masters, 2, NULL, &r );		/* no public IP known: everything is missing */
		CHECK( r.missing[0] && r.missing[1] && !r.listed[0] );
	}
	fake_stop( m );
	fake_stop( s1 );
}

/* ---- the browser model ---- */

typedef struct { volatile int changes; } b_t;
static void b_changed( int source, void *u ) { (void)source; ( (b_t *)u )->changes++; }

static void wait_idle( lc_browser *b, int source )
{
	double t0 = lc_now( );
	while ( lc_browser_is_busy( b, source ) && lc_now( ) - t0 < 12 )
		lc_sleep_ms( 20 );
}

static void test_browser( void )
{
	fake_t *m = fake_start(), *s[4], *deadMaster = fake_start();
	unsigned char list[128];
	size_t n = 0;
	int i;
	char masters[2][65], addr[4][32];
	lc_browser *br;
	b_t bt = { 0 };
	const lc_browser_row *rows;
	lc_sort_key keys[8];
	int nk, count;
	const char *favs[1];

	for ( i = 0; i < 4; i++ )
		s[i] = fake_start();
	make_info( s[0], "Zulu", "hm_one", 6, 12, 2, 0, 0 );
	make_status( s[0], "ioEF:cMod HM v1.29 x", "1 10 \"A\"\n" );
	make_info( s[1], "alpha", "hm_two", 8, 12, 5, 3, 1 );
	make_status( s[1], "Elite Force 1.2", "" );
	make_info( s[2], "^3Mike", "hm_one", 3, 12, 0, 0, 0 );			/* only bots */
	make_status( s[2], "Elite Force 1.2", "" );
	make_info( s[3], "Delta", "hm_three", 0, 12, -1, 4, 0 );			/* empty, doesn't say */
	make_status( s[3], "Elite Force 1.2", "" );
	memcpy( list, "\xff\xff\xff\xffgetserversResponse", 22 );
	n = 22;
	for ( i = 0; i < 4; i++ ) {
		list[n++] = '\\'; list[n++] = 127; list[n++] = 0; list[n++] = 0; list[n++] = 1;
		list[n++] = (unsigned char)( s[i]->port >> 8 ); list[n++] = (unsigned char)s[i]->port;
		lc_snprintf( addr[i], sizeof( addr[i] ), "127.0.0.1:%d", s[i]->port );
	}
	list[n++] = '\\'; memcpy( list + n, "EOT", 3 ); n += 3;
	m->list = list;
	m->listLen = n;
	lc_snprintf( masters[0], 65, "127.0.0.1:%d", m->port );
	lc_snprintf( masters[1], 65, "127.0.0.1:%d", 9 );		/* a master that never answers */

	br = lc_browser_new( (const char (*)[65])masters, 2, NULL );
	CHECK( lc_browser_source_count( br ) == 4 );
	{
		char label[32];
		lc_browser_source_label( br, 0, label, sizeof( label ) ); CHECK_STR( label, "Local" );
		lc_browser_source_label( br, 1, label, sizeof( label ) ); CHECK_STR( label, "Combined" );
		lc_browser_source_label( br, 3, label, sizeof( label ) ); CHECK_STR( label, "Internet 2" );
	}
	lc_browser_set_loopback_fix( br, 0 );
	CHECK( !lc_browser_fetched( br, 1 ) );
	lc_browser_fetch( br, 2, NULL, b_changed, &bt );
	CHECK( lc_browser_fetched( br, 2 ) && lc_browser_is_busy( br, 2 ) );
	wait_idle( br, 2 );
	CHECK( !lc_browser_is_busy( br, 2 ) );
	CHECK( bt.changes >= 5 );
	{
		char expect[160];
		lc_snprintf( expect, sizeof( expect ), "%s \xc2\xb7 4 servers", masters[0] );
		CHECK_STR( lc_browser_status_text( br, 2 ), expect );
	}
	lc_browser_default_sort( keys );
	favs[0] = addr[3];
	count = lc_browser_rows( br, 2, 0, 0, favs, 1, keys, 4, &rows );
	CHECK( count == 4 );
	if ( count == 4 ) {
		/* default: favourites first, then humans (? counts as 0), players, ping */
		CHECK_STR( rows[0].info.address, addr[3] );		/* the favourite */
		CHECK( rows[0].favorite );
		CHECK_STR( rows[1].info.address, addr[1] );		/* 5 humans */
		CHECK_STR( rows[2].info.address, addr[0] );		/* 2 humans */
		CHECK_STR( rows[3].info.address, addr[2] );		/* 0 humans */
		CHECK( !rows[1].favorite );
		CHECK( rows[2].isCmod && rows[2].cmodVersion[0] == 0 );		/* getstatus filled the cMod column */
		CHECK( !rows[1].thisComputer );
		CHECK( rows[1].info.needPass && rows[1].info.gametype == 3 );
	}
	/* filters (8.5) */
	count = lc_browser_rows( br, 2, 1, 0, favs, 1, keys, 4, &rows );
	CHECK( count == 1 && !strcmp( rows[0].info.address, addr[3] ) );
	count = lc_browser_rows( br, 2, 0, 1, favs, 0, keys, 4, &rows );		/* people: humans > 0, or unknown with players > 0 */
	CHECK( count == 2 );
	/* sorting by header clicks: name ascending, then toggled */
	nk = 4;
	nk = lc_sort_click( keys, nk, 8, LC_COL_NAME );
	CHECK( nk == 5 && keys[0].column == LC_COL_NAME && keys[0].ascending );
	count = lc_browser_rows( br, 2, 0, 0, NULL, 0, keys, nk, &rows );
	CHECK( count == 4 && !strcmp( rows[0].info.name, "alpha" ) && !strcmp( rows[1].info.name, "Delta" ) &&
		!strcmp( rows[2].info.name, "Mike" ) && !strcmp( rows[3].info.name, "Zulu" ) );
	nk = lc_sort_click( keys, nk, 8, LC_COL_NAME );
	CHECK( nk == 5 && !keys[0].ascending );
	count = lc_browser_rows( br, 2, 0, 0, NULL, 0, keys, nk, &rows );
	CHECK( !strcmp( rows[0].info.name, "Zulu" ) );
	nk = lc_sort_click( keys, nk, 8, LC_COL_CMOD );
	CHECK( keys[0].column == LC_COL_CMOD && !keys[0].ascending );
	count = lc_browser_rows( br, 2, 0, 0, NULL, 0, keys, nk, &rows );
	CHECK( !strcmp( rows[0].info.name, "Zulu" ) );				/* the only cMod */
	/* the status the fetch stored (8.6) */
	{
		lc_server_status st;
		CHECK( lc_browser_get_status( br, addr[0], &st ) && st.playerCount == 1 );
		CHECK( lc_browser_get_status( br, addr[2], &st ) && st.playerCount == 0 && !st.isCmod );
		CHECK( !lc_browser_get_status( br, "9.9.9.9:1", &st ) );
		lc_browser_forget_status( br, addr[0] );
		CHECK( !lc_browser_get_status( br, addr[0], &st ) );
		memset( &st, 0, sizeof( st ) );
		st.isCmod = 1;
		lc_strlcpy( st.cmodVersion, "v2.0 beta", sizeof( st.cmodVersion ) );
		lc_browser_store_status( br, addr[2], &st );
		count = lc_browser_rows( br, 2, 0, 0, NULL, 0, keys, 4, &rows );
		for ( i = 0; i < count; i++ )
			if ( !strcmp( rows[i].info.address, addr[2] ) )
				CHECK( rows[i].isCmod && !strcmp( rows[i].cmodVersion, "v2.0 beta" ) );
	}

	/* the dead master's own source: no answer */
	lc_browser_fetch( br, 3, NULL, b_changed, &bt );
	wait_idle( br, 3 );
	{
		char expect[160];
		lc_snprintf( expect, sizeof( expect ), "%s didn't answer. It may be offline: try another source, or change the list in Servers\xe2\x80\xa6.", masters[1] );
		CHECK_STR( lc_browser_status_text( br, 3 ), expect );
		CHECK( lc_browser_rows( br, 3, 0, 0, NULL, 0, keys, 4, &rows ) == 0 );
	}

	/* Combined: one master answers, one doesn't; with the Loopback Host Fix the servers behind
	   our "public address" (here 127.0.0.1) are rewritten to 127.0.0.1 and marked */
	lc_browser_set_loopback_fix( br, 1 );
	lc_browser_fetch( br, 1, "127.0.0.1", b_changed, &bt );
	wait_idle( br, 1 );
	{
		char expect[160];
		lc_snprintf( expect, sizeof( expect ), "4 servers from 1 of 2 master servers" );
		CHECK_STR( lc_browser_status_text( br, 1 ), expect );
	}
	count = lc_browser_rows( br, 1, 0, 0, NULL, 0, keys, 4, &rows );
	CHECK( count == 4 );
	for ( i = 0; i < count; i++ ) {
		CHECK( rows[i].thisComputer );
		CHECK( rows[i].listedOnCount == 1 && !strcmp( rows[i].listedOn[0], "Internet 1" ) );
	}
	/* a refetch while the old one is gone: results cleared and filled again */
	lc_browser_fetch( br, 1, "127.0.0.1", b_changed, &bt );
	CHECK( lc_browser_rows( br, 1, 0, 0, NULL, 0, keys, 4, &rows ) == 0 );
	lc_browser_cancel_all( br );
	CHECK( !lc_browser_is_busy( br, 1 ) );
	lc_browser_free( br );

	/* Local with a broadcast: whatever is on the network, the fetch ends with a status line */
	br = lc_browser_new( (const char (*)[65])masters, 0, NULL );
	CHECK( lc_browser_source_count( br ) == 1 );
	lc_browser_fetch( br, 0, NULL, NULL, NULL );
	wait_idle( br, 0 );
	CHECK( lc_browser_status_text( br, 0 )[0] != 0 && strncmp( lc_browser_status_text( br, 0 ), "Looking", 7 ) != 0 );
	lc_browser_free( br );

	for ( i = 0; i < 4; i++ )
		fake_stop( s[i] );
	fake_stop( m );
	fake_stop( deadMaster );
}

int main( void )
{
	lc_net_init( );
	test_master_lists( );
	test_addresses( );
	test_colors( );
	test_parse_info_status( );
	test_helpers( );
	{
		double t = lc_now( );
		test_loopback_query( );
		fprintf( stderr, "loopback query %.1fs\n", lc_now( ) - t ); t = lc_now( );
		test_loopback_master( );
		fprintf( stderr, "loopback master %.1fs\n", lc_now( ) - t ); t = lc_now( );
		test_browser( );
		fprintf( stderr, "browser %.1fs\n", lc_now( ) - t );
	}
	return TEST_DONE( );
}
