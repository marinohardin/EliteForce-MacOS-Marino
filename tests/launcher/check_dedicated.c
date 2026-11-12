/* Not a unit test (not in ctest): starts the real game executable as a dedicated server through
   lc_proc_spawn, drives it with the console parser, and queries it with lc_query_servers.
     check_dedicated <game exe> <basepath (folder with baseEF)> <homepath (throwaway!)> <port>
   Exit 0 when "Server ready" was parsed, getinfo/getstatus answered, and `quit` ended it. */
#include "lc_host.h"
#include "lc_net.h"
#include "lc_test.h"
#include <stdlib.h>
#include <string.h>

static lc_proc *g_proc;
static lc_mutex *g_mu;
static volatile int g_exited, g_ready, g_players, g_sigCode;
static int g_port;

static void s_send( const char *c, void *u ) { char b[256]; (void)u; lc_snprintf( b, sizeof( b ), "%s\n", c ); if ( g_proc ) lc_proc_write( g_proc, b, strlen( b ) ); }
static void s_log( const char *l, int col, void *u ) { (void)u; printf( "[server %d] %s\n", col, l ); }
static void s_state( lc_srv_state s, const char *d, void *u ) { (void)u; printf( "[state] %d %s\n", (int)s, d ? d : "" ); }
static void s_players( const lc_srv_player *p, int n, void *u ) { (void)u; (void)p; g_players = n; printf( "[players] %d\n", n ); }
static void s_ready( int port, void *u ) { (void)u; g_port = port; g_ready = 1; printf( "[ready] port %d\n", port ); }

static lc_srv *g_srv;
static void p_out( const char *b, size_t n, void *u ) { (void)u; lc_mutex_lock( g_mu ); lc_srv_feed( g_srv, b, n ); lc_mutex_unlock( g_mu ); }
static void p_exited( int sig, int code, void *u ) { (void)u; g_sigCode = code; lc_mutex_lock( g_mu ); lc_srv_exited( g_srv, sig, code ); lc_mutex_unlock( g_mu ); g_exited = 1; }

static int got_info, got_status;
static void q_info( const lc_server_info *i, void *u ) { (void)u; got_info++; printf( "[info] %s | %s | map %s | %d/%d | gametype %d | %d ms\n", i->address, i->name, i->map, i->players, i->maxPlayers, i->gametype, i->ping ); }
static void q_status( const char *a, const lc_server_status *s, void *u ) { (void)u; got_status++; printf( "[status] %s cMod=%d (%s) players=%d\n", a, s->isCmod, s->cmodVersion, s->playerCount ); }

int main( int argc, char **argv )
{
	lc_srv_callbacks cb;
	lc_server_settings s;
	const lc_map_catalog *cat;
	char home[LC_MAX_PATH], cfgPath[LC_MAX_PATH], err[256], portStr[16], *text;
	const char *args[24];
	lc_query_callbacks qcb;
	char addr[32];
	const char *addrs[1];
	int n = 0;
	double t0;

	if ( argc < 5 ) {
		fprintf( stderr, "usage: check_dedicated <exe> <basepath> <homepath> <port>\n" );
		return 2;
	}
	lc_net_init( );
	g_mu = lc_mutex_new( );
	lc_snprintf( portStr, sizeof( portStr ), "%s", argv[4] );
	lc_path_join( home, sizeof( home ), argv[3], "baseEF" );
	lc_mkdir_p( home );
	{
		char bp[LC_MAX_PATH];
		lc_path_join( bp, sizeof( bp ), argv[2], "baseEF" );
		cat = lc_map_catalog_get( bp, home );
	}
	printf( "%d Holomatch maps found\n", lc_map_catalog_count( cat ) );
	lc_server_settings_defaults( &s, "Check Run" );
	s.port = atoi( portStr );
	s.rotationCount = 1;
	lc_strlcpy( s.rotation[0], "hm_voy1", 64 );
	CHECK( lc_server_cfg_text( &s, cat, NULL, 0, 1, &text, err, sizeof( err ), NULL, NULL ) );
	lc_path_join( cfgPath, sizeof( cfgPath ), home, "vsp_server.cfg" );
	CHECK( lc_write_file_atomic( cfgPath, text, strlen( text ) ) );
	free( text );

	memset( &cb, 0, sizeof( cb ) );
	cb.send = s_send; cb.log = s_log; cb.state = s_state; cb.players = s_players; cb.ready = s_ready;
	g_srv = lc_srv_new( &cb, s.port );
	args[n++] = argv[1];
	args[n++] = "+set"; args[n++] = "com_holomatch"; args[n++] = "1";
	args[n++] = "+set"; args[n++] = "dedicated"; args[n++] = "1";
	args[n++] = "+set"; args[n++] = "net_port"; args[n++] = portStr;
	args[n++] = "+set"; args[n++] = "fs_basepath"; args[n++] = argv[2];
	args[n++] = "+set"; args[n++] = "fs_homepath"; args[n++] = argv[3];
	args[n++] = "+exec"; args[n++] = "vsp_server.cfg";
	args[n] = NULL;
	g_proc = lc_proc_spawn( argv[1], args, NULL, p_out, p_exited, NULL );
	CHECK( g_proc != NULL );
	if ( !g_proc )
		return TEST_DONE( );
	t0 = lc_now( );
	while ( !g_ready && !g_exited && lc_now( ) - t0 < 90 ) {
		lc_sleep_ms( 100 );
	}
	CHECK( g_ready );
	if ( g_ready ) {
		lc_snprintf( addr, sizeof( addr ), "127.0.0.1:%d", g_port );
		addrs[0] = addr;
		memset( &qcb, 0, sizeof( qcb ) );
		qcb.got_info = q_info; qcb.got_status = q_status;
		lc_query_servers( addrs, 1, 0, NULL, &qcb );
		CHECK( got_info == 1 && got_status == 1 );
		lc_mutex_lock( g_mu );
		lc_srv_poll_tick( g_srv, lc_now( ) );
		lc_mutex_unlock( g_mu );
		t0 = lc_now( );
		while ( g_players < 0 || ( !g_players && lc_now( ) - t0 < 8 && 1 ) ) {
			lc_sleep_ms( 100 );
			if ( g_players >= 0 && lc_now( ) - t0 > 3 )
				break;
		}
		lc_mutex_lock( g_mu );
		lc_srv_user_command( g_srv, "status" );
		lc_mutex_unlock( g_mu );
		lc_sleep_ms( 1500 );
		lc_mutex_lock( g_mu );
		lc_srv_begin_stop( g_srv );
		lc_mutex_unlock( g_mu );
		t0 = lc_now( );
		while ( !g_exited && lc_now( ) - t0 < 15 )
			lc_sleep_ms( 100 );
		CHECK( g_exited && g_sigCode == 0 );
	}
	if ( !g_exited )
		lc_proc_kill( g_proc );
	lc_proc_free( g_proc );
	printf( "%s\n", lc_test_failures ? "FAILED" : "OK" );
	return TEST_DONE( );
}
