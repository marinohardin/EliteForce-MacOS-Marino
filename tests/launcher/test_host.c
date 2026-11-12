/* lc_host.c: the console parser lc_srv_feed with captured dedicated-server output, the state machine,
   command helpers, the running-servers registry, and lc_proc_spawn against this very executable acting
   as a fake dedicated server (servers.md 9) */
#include "lc_host.h"
#include "lc_test.h"
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#	include <windows.h>
#else
#	include <unistd.h>
#endif

/* ---- the child side: a fake dedicated server on stdin/stdout ---- */

static int child_main( const char *mode )
{
	char line[512];
	const char *e = getenv( "VOYAGERSP_TEST_STRIP" );

	setvbuf( stdout, NULL, _IONBF, 0 );
	printf( "env:%s\n", e ? e : "(stripped)" );
	if ( !strcmp( mode, "--child-exit3" ) )
		return 3;
	if ( !strcmp( mode, "--child-hang" ) ) {
		printf( "hanging\n" );
		for ( ;; )
			lc_sleep_ms( 100 );
	}
	printf( "Opening IP socket: 0.0.0.0:27961\n" );
	printf( "Server ready: map hm_voy1 port 27961 gametype 0 maxclients 12 dedicated 1 hostname Test\n" );
	while ( fgets( line, sizeof( line ), stdin ) ) {
		line[strcspn( line, "\r\n" )] = 0;
		if ( !strcmp( line, "quit" ) )
			return 0;
		if ( !strcmp( line, "status" ) )
			printf( "map: hm_voy1\ncl score ping name            address                                 rate\n-- ----- ---- --------------- --------------------------------------- -----\n"
				" 0    12   48 Munro^7          ^7192.168.1.20:27960                       25000\n\n" );
		else
			printf( "echo:%s\n", line );
	}
	return 0;
}

/* ---- the parser ---- */

typedef struct {
	char log[64][256];
	int logColor[64], nLog;
	lc_srv_state state;
	char detail[128];
	int nStates;
	lc_srv_state states[16];
	char sent[32][128];
	int nSent;
	lc_srv_player players[8];
	int nPlayers, playersCalls;
	char map[64];
	int port, infoCalls;
	int readyCalls, readyPort;
} sink_t;

static void s_send( const char *c, void *u ) { sink_t *s = (sink_t *)u; if ( s->nSent < 32 ) lc_strlcpy( s->sent[s->nSent++], c, 128 ); }
static void s_log( const char *l, int col, void *u )
{
	sink_t *s = (sink_t *)u;
	if ( s->nLog < 64 ) {
		lc_strlcpy( s->log[s->nLog], l, 256 );
		s->logColor[s->nLog++] = col;
	}
}
static void s_state( lc_srv_state st, const char *d, void *u )
{
	sink_t *s = (sink_t *)u;
	s->state = st;
	lc_strlcpy( s->detail, d ? d : "", sizeof( s->detail ) );
	if ( s->nStates < 16 )
		s->states[s->nStates++] = st;
}
static void s_players( const lc_srv_player *p, int n, void *u )
{
	sink_t *s = (sink_t *)u;
	s->playersCalls++;
	s->nPlayers = n;
	if ( n > 8 )
		n = 8;
	memcpy( s->players, p, sizeof( *p ) * (size_t)n );
}
static void s_info( const char *m, int port, void *u ) { sink_t *s = (sink_t *)u; lc_strlcpy( s->map, m ? m : "", sizeof( s->map ) ); s->port = port; s->infoCalls++; }
static void s_ready( int port, void *u ) { sink_t *s = (sink_t *)u; s->readyCalls++; s->readyPort = port; }

static lc_srv *new_srv( sink_t *s, int port )
{
	lc_srv_callbacks cb;
	memset( s, 0, sizeof( *s ) );
	memset( &cb, 0, sizeof( cb ) );
	cb.send = s_send; cb.log = s_log; cb.state = s_state; cb.players = s_players; cb.info = s_info; cb.ready = s_ready; cb.user = s;
	return lc_srv_new( &cb, port );
}

static int logged( sink_t *s, const char *text, int color )
{
	int i;
	for ( i = 0; i < s->nLog; i++ )
		if ( !strcmp( s->log[i], text ) && ( color < 0 || s->logColor[i] == color ) )
			return 1;
	return 0;
}

static const char STATUS_BLOCK[] =
	"map: hm_voy1\n"
	"cl score ping name            address                                 rate\n"
	"-- ----- ---- --------------- --------------------------------------- -----\n"
	" 0    12   48 Munro^7          ^7192.168.1.20:27960                       25000\n"
	" 1     3    0 Chakotay^7       ^7bot                                      25000\n"
	" 2     0  CON Seven Of Nine    192.168.1.30:27960   25000\n"
	"12  -5  ZMB ^1Red^7 Guy        10.0.0.2:27961       25000\n"
	"\n";

static void test_parser( void )
{
	sink_t s;
	lc_srv *srv = new_srv( &s, 27960 );
	const char *chunk;
	size_t i, n;

	CHECK( lc_srv_get_state( srv ) == LC_SRV_STARTING );
	/* bytes arrive in odd pieces, with CRLF line ends */
	chunk = "Opening IP socket: 0.0.0.0:27961\r\nWARNING: something odd\nERROR: bad\nError: also bad\n]\n] \nSome plain line\nServer re";
	n = strlen( chunk );
	for ( i = 0; i < n; i += 7 )
		lc_srv_feed( srv, chunk + i, n - i < 7 ? n - i : 7 );
	CHECK( s.port == 27961 && s.infoCalls == 1 );
	CHECK( logged( &s, "Opening IP socket: 0.0.0.0:27961", 0 ) );
	CHECK( logged( &s, "WARNING: something odd", 1 ) );
	CHECK( logged( &s, "ERROR: bad", 2 ) );
	CHECK( logged( &s, "Error: also bad", 2 ) );
	CHECK( logged( &s, "Some plain line", 0 ) );
	CHECK( !logged( &s, "]", -1 ) && !logged( &s, "] ", -1 ) );			/* the dedicated prompt is never shown */
	CHECK( s.readyCalls == 0 && lc_srv_get_state( srv ) == LC_SRV_STARTING );
	lc_srv_feed( srv, "ady: map hm_voy1 port 27961 gametype 0 maxclients 12 dedicated 1 hostname Test\n", strlen( "ady: map hm_voy1 port 27961 gametype 0 maxclients 12 dedicated 1 hostname Test\n" ) );
	CHECK( s.readyCalls == 1 && s.readyPort == 27961 && lc_srv_is_ready( srv ) );
	CHECK( lc_srv_get_state( srv ) == LC_SRV_RUNNING );
	CHECK_STR( s.map, "hm_voy1" );
	CHECK( logged( &s, "Port 27960 is in use; the server took 27961.", 1 ) );
	lc_srv_feed( srv, "Server ready: map hm_borg1 port 27961 gametype 0\n", strlen( "Server ready: map hm_borg1 port 27961 gametype 0\n" ) );		/* every map load: ready fires once */
	CHECK( s.readyCalls == 1 );
	CHECK_STR( s.map, "hm_borg1" );

	/* a hidden poll: nothing shown, players filled */
	lc_srv_poll_tick( srv, 100.0 );
	CHECK( s.nSent == 1 && !strcmp( s.sent[0], "status" ) );
	lc_srv_poll_tick( srv, 102.0 );					/* one in flight: not again before 10 s */
	CHECK( s.nSent == 1 );
	{
		int before = s.nLog;
		lc_srv_feed( srv, STATUS_BLOCK, sizeof( STATUS_BLOCK ) - 1 );
		CHECK( s.nLog == before );					/* the hidden answer is not shown */
	}
	CHECK( s.playersCalls >= 1 && s.nPlayers == 4 );
	CHECK_STR( s.players[0].name, "Munro" );
	CHECK_STR( s.players[0].score, "12" );
	CHECK_STR( s.players[0].ping, "48" );
	CHECK_STR( s.players[0].address, "192.168.1.20:27960" );
	CHECK_STR( s.players[0].num, "0" );
	CHECK( !s.players[0].bot );
	CHECK_STR( s.players[1].name, "Chakotay" );
	CHECK( s.players[1].bot && !strcmp( s.players[1].ping, "bot" ) && !strcmp( s.players[1].address, "bot" ) );
	CHECK_STR( s.players[2].name, "Seven Of Nine" );
	CHECK_STR( s.players[2].ping, "\xe2\x80\xa6" );			/* CON shows as an ellipsis */
	CHECK_STR( s.players[2].num, "2" );
	CHECK_STR( s.players[3].name, "Red  Guy" );		/* the ^7 ending the name becomes a space, as in the launcher */
	CHECK( !strcmp( s.players[3].ping, "ZMB" ) && !strcmp( s.players[3].score, "-5" ) && !strcmp( s.players[3].num, "12" ) );
	CHECK_STR( s.map, "hm_voy1" );
	lc_srv_poll_tick( srv, 105.0 );					/* the answer came: a new poll goes out */
	CHECK( s.nSent == 2 );
	lc_srv_feed( srv, "Server is not running.\n", strlen( "Server is not running.\n" ) );		/* hidden; the server has no map: Idle */
	CHECK( lc_srv_get_state( srv ) == LC_SRV_IDLE && s.nPlayers == 0 && s.map[0] == 0 );
	CHECK( !logged( &s, "Server is not running.", -1 ) );
	lc_srv_feed( srv, STATUS_BLOCK, sizeof( STATUS_BLOCK ) - 1 );		/* an unasked answer counts too: back to Running */
	CHECK( lc_srv_get_state( srv ) == LC_SRV_RUNNING );

	/* a typed status is shown, a typed command is echoed in the accent colour */
	lc_srv_user_command( srv, "  status " );
	CHECK( logged( &s, "] status", 5 ) );
	CHECK_STR( s.sent[s.nSent - 1], "status" );
	{
		int before = s.nLog;
		lc_srv_feed( srv, STATUS_BLOCK, sizeof( STATUS_BLOCK ) - 1 );
		CHECK( s.nLog > before );
		CHECK( logged( &s, " 0    12   48 Munro^7          ^7192.168.1.20:27960                       25000", -1 ) == 0 );	/* colour-stripped lines */
		CHECK( logged( &s, "map: hm_voy1", 0 ) );
	}
	lc_srv_user_command( srv, "say hello" );
	CHECK( logged( &s, "] say hello", 5 ) && !strcmp( s.sent[s.nSent - 1], "say hello" ) );
	{
		int sent = s.nSent;
		lc_srv_user_command( srv, "   " );
		CHECK( s.nSent == sent );
	}
	/* stopping: quit, escalation, exit */
	lc_srv_user_command( srv, "QUIT" );
	CHECK( lc_srv_is_stopping( srv ) && lc_srv_get_state( srv ) == LC_SRV_STOPPING );
	CHECK( logged( &s, "] quit", 5 ) && !strcmp( s.sent[s.nSent - 1], "quit" ) );
	CHECK( lc_srv_stop_tick( srv, lc_now( ) + 1 ) == 0 );
	CHECK( lc_srv_stop_tick( srv, lc_now( ) + 5.5 ) == 1 );
	CHECK( lc_srv_stop_tick( srv, lc_now( ) + 6 ) == 0 );
	CHECK( lc_srv_stop_tick( srv, lc_now( ) + 9 ) == 2 );
	lc_srv_exited( srv, 0, 0 );
	CHECK( lc_srv_get_state( srv ) == LC_SRV_STOPPED );
	CHECK( logged( &s, "The server stopped.", 4 ) );
	lc_srv_free( srv );

	/* exits on their own (9.14) */
	srv = new_srv( &s, 27960 );
	lc_srv_exited( srv, 0, 1 );
	CHECK( s.state == LC_SRV_FAILED && !strcmp( s.detail, "no game files" ) );
	CHECK( logged( &s, "The server found no Elite Force game files (exit code 1).", 2 ) );
	lc_srv_free( srv );
	srv = new_srv( &s, 27960 );
	lc_srv_exited( srv, 0, 3 );
	CHECK( s.state == LC_SRV_FAILED && !strcmp( s.detail, "the game folder can't be used" ) );
	CHECK( logged( &s, "The server couldn't use the game folder (exit code 3).", 2 ) );
	lc_srv_free( srv );
	srv = new_srv( &s, 27960 );
	lc_srv_exited( srv, 1, 11 );
	CHECK( s.state == LC_SRV_FAILED && !strcmp( s.detail, "the server didn't start, see the console below" ) );
	CHECK( logged( &s, "The server crashed (signal 11).", 2 ) );
	lc_srv_free( srv );
	srv = new_srv( &s, 27960 );
	lc_srv_feed( srv, "Server ready: map hm_x port 27960 gametype 0\npartial line at the end", strlen( "Server ready: map hm_x port 27960 gametype 0\npartial line at the end" ) );
	CHECK( lc_srv_is_ready( srv ) && s.port == 27960 && !logged( &s, "Port 27960 is in use; the server took 27960.", -1 ) );
	lc_srv_exited( srv, 0, 1 );											/* after ready: the plain quit message */
	CHECK( s.state == LC_SRV_FAILED && !strcmp( s.detail, "the server quit unexpectedly" ) );
	CHECK( logged( &s, "The server quit (exit code 1).", 2 ) );
	CHECK( logged( &s, "partial line at the end", 0 ) );				/* the pending remainder is flushed at exit */
	lc_srv_free( srv );
	srv = new_srv( &s, 27960 );
	lc_srv_start_failed( srv, "Couldn't write /x/vsp_server.cfg." );
	CHECK( s.state == LC_SRV_FAILED && !strcmp( s.detail, "couldn't start" ) && logged( &s, "Couldn't write /x/vsp_server.cfg.", 2 ) );
	lc_srv_free( srv );
}

static void test_commands( void )
{
	sink_t s;
	lc_srv *srv = new_srv( &s, 27960 );
	char buf[512];

	lc_srv_say_command( "  Hello \"world\"; bye  ", buf, sizeof( buf ) );
	CHECK_STR( buf, "say \"Hello 'world', bye\"" );
	lc_srv_say_command( "   ", buf, sizeof( buf ) );
	CHECK_STR( buf, "" );
	{
		char longText[400];
		memset( longText, 'x', sizeof( longText ) - 1 );
		longText[sizeof( longText ) - 1] = 0;
		lc_srv_say_command( longText, buf, sizeof( buf ) );
		CHECK( strlen( buf ) == 150 + 6 );		/* say "" + 150 characters */
	}
	lc_srv_next_map( srv );
	lc_srv_restart_map( srv );
	lc_srv_change_map_slot( srv, 3 );
	lc_srv_change_map( srv, "hm_borg1" );
	lc_srv_say( srv, "hi; there" );
	lc_srv_say( srv, "" );
	lc_srv_kick( srv, "4" );
	lc_srv_remove_bots( srv );
	lc_srv_heartbeat( srv );
	CHECK( s.nSent == 9 );
	CHECK_STR( s.sent[0], "vstr nextmap" );
	CHECK_STR( s.sent[1], "map_restart" );
	CHECK_STR( s.sent[2], "vstr vsp_map3" );
	CHECK_STR( s.sent[3], "map hm_borg1" );
	CHECK_STR( s.sent[4], "say \"hi, there\"" );
	CHECK_STR( s.sent[5], "clientkick 4" );
	CHECK_STR( s.sent[6], "set bot_minplayers 0" );
	CHECK_STR( s.sent[7], "kick allbots" );
	CHECK_STR( s.sent[8], "heartbeat" );
	CHECK( logged( &s, "Bots won't refill this game; Add Bot still adds them.", 4 ) );
	CHECK( logged( &s, "] vstr nextmap", 5 ) );
	/* Add Bot: a name nobody has, at the skill */
	lc_srv_feed( srv, "Server ready: map hm_x port 27960 gametype 0\n", strlen( "Server ready: map hm_x port 27960 gametype 0\n" ) );
	{
		int k;
		char c[128];
		for ( k = 0; k < 20; k++ ) {
			lc_srv_addbot_command( srv, 4, c, sizeof( c ) );
			CHECK( !strncmp( c, "addbot ", 7 ) && c[strlen( c ) - 2] == ' ' && c[strlen( c ) - 1] == '4' );
		}
		lc_srv_addbot_command( srv, 9, c, sizeof( c ) );
		CHECK( c[strlen( c ) - 1] == '5' );
	}
	lc_srv_free( srv );
}

/* ---- the registry ---- */

static void test_registry( void )
{
	char root[LC_MAX_PATH], dir[LC_MAX_PATH], stale[LC_MAX_PATH];
	struct lc_running r[8];
	int n;

	lc_snprintf( root, sizeof( root ), "%s%chosttest-%ld", lc_homepath( ), LC_PATH_SEP, lc_getpid( ) );
	lc_mkdir_p( root );
	CHECK( lc_running_list( root, r, 8 ) == 0 );
	CHECK( lc_running_register( root, "My \"Server\"", 27960 ) );
	lc_path_join( dir, sizeof( dir ), root, "RunningServers" );
	lc_path_join( stale, sizeof( stale ), dir, "999999.json" );
	{
		FILE *f = lc_fopen( stale, "wb" );
		fputs( "{\"name\": \"gone\", \"port\": 1, \"started\": 5}\n", f );
		fclose( f );
	}
	n = lc_running_list( root, r, 8 );
	CHECK( n == 1 );
	CHECK( r[0].pid == (int)lc_getpid( ) && r[0].port == 27960 );
	CHECK_STR( r[0].name, "My \"Server\"" );
	CHECK( r[0].started > 1600000000LL );
	CHECK( !lc_is_file( stale ) );				/* the stale one was pruned */
	CHECK( lc_running_register( root, "Renamed", 27961 ) );		/* ready: re-registered with the bound port */
	n = lc_running_list( root, r, 8 );
	CHECK( n == 1 && r[0].port == 27961 && !strcmp( r[0].name, "Renamed" ) );
	lc_running_unregister( root );
	CHECK( lc_running_list( root, r, 8 ) == 0 );
}

/* ---- processes ---- */

typedef struct {
	lc_mutex *mu;
	char out[8192];
	size_t len;
	volatile int exited, signaled, code;
} proc_t;

static void p_out( const char *b, size_t n, void *u )
{
	proc_t *p = (proc_t *)u;
	lc_mutex_lock( p->mu );
	if ( p->len + n < sizeof( p->out ) ) {
		memcpy( p->out + p->len, b, n );
		p->len += n;
		p->out[p->len] = 0;
	}
	lc_mutex_unlock( p->mu );
}

static void p_exited( int signaled, int code, void *u )
{
	proc_t *p = (proc_t *)u;
	p->signaled = signaled;
	p->code = code;
	p->exited = 1;
}

static int wait_for( proc_t *p, const char *text, double seconds )
{
	double t0 = lc_now( );
	for ( ;; ) {
		int found;
		lc_mutex_lock( p->mu );
		found = strstr( p->out, text ) != NULL;
		lc_mutex_unlock( p->mu );
		if ( found )
			return 1;
		if ( lc_now( ) - t0 > seconds )
			return 0;
		lc_sleep_ms( 20 );
	}
}

static int wait_exit( proc_t *p, double seconds )
{
	double t0 = lc_now( );
	while ( !p->exited && lc_now( ) - t0 < seconds )
		lc_sleep_ms( 20 );
	return p->exited;
}

static void test_process( void )
{
	const char *exe = lc_exe_path( );
	const char *argv[3];
	proc_t p;
	lc_proc *pr;
	sink_t s;
	lc_srv *srv;

#ifdef _WIN32
	_putenv( "VOYAGERSP_TEST_STRIP=1" );
#else
	setenv( "VOYAGERSP_TEST_STRIP", "1", 1 );
#endif
	memset( &p, 0, sizeof( p ) );
	p.mu = lc_mutex_new( );
	argv[0] = exe;
	argv[1] = "--child";
	argv[2] = NULL;
	pr = lc_proc_spawn( exe, argv, NULL, p_out, p_exited, &p );
	CHECK( pr != NULL );
	if ( !pr )
		return;
	CHECK( lc_proc_pid( pr ) > 0 && lc_proc_running( pr ) );
	CHECK( wait_for( &p, "Server ready: map hm_voy1 port 27961", 15 ) );
	CHECK( strstr( p.out, "env:(stripped)" ) != NULL );		/* VOYAGERSP_TEST_* is not passed on */
	/* drive it through the parser: the full path of 9.5-9.7 */
	srv = new_srv( &s, 27960 );
	lc_mutex_lock( p.mu );
	lc_srv_feed( srv, p.out, p.len );
	lc_mutex_unlock( p.mu );
	CHECK( lc_srv_is_ready( srv ) && s.readyPort == 27961 );
	CHECK( lc_proc_write( pr, "echo hello world\n", 17 ) );
	CHECK( wait_for( &p, "echo:echo hello world", 10 ) );
	CHECK( lc_proc_write( pr, "status\n", 7 ) );
	CHECK( wait_for( &p, "Munro", 10 ) );
	lc_mutex_lock( p.mu );
	{
		const char *st = strstr( p.out, "map: hm_voy1" );
		lc_srv_poll_tick( srv, 1.0 );
		if ( st )
			lc_srv_feed( srv, st, strlen( st ) );
	}
	lc_mutex_unlock( p.mu );
	CHECK( s.nPlayers == 1 && !strcmp( s.players[0].name, "Munro" ) );
	CHECK( lc_proc_write( pr, "quit\n", 5 ) );
	CHECK( wait_exit( &p, 10 ) );
	CHECK( !p.signaled && p.code == 0 );
	CHECK( !lc_proc_write( pr, "x\n", 2 ) );
	lc_proc_free( pr );
	lc_srv_free( srv );

	/* exit code 3 before ready */
	p.exited = 0; p.len = 0; p.out[0] = 0;
	argv[1] = "--child-exit3";
	pr = lc_proc_spawn( exe, argv, NULL, p_out, p_exited, &p );
	CHECK( pr != NULL );
	CHECK( wait_exit( &p, 10 ) );
	CHECK( !p.signaled && p.code == 3 );
	lc_proc_free( pr );

	/* terminate and kill a hung one */
	p.exited = 0; p.len = 0; p.out[0] = 0;
	argv[1] = "--child-hang";
	pr = lc_proc_spawn( exe, argv, NULL, p_out, p_exited, &p );
	CHECK( pr != NULL );
	CHECK( wait_for( &p, "hanging", 10 ) );
	lc_proc_terminate( pr );
	CHECK( wait_exit( &p, 10 ) );
#ifndef _WIN32
	CHECK( p.signaled && p.code == 15 );
#endif
	lc_proc_free( pr );
	p.exited = 0; p.len = 0; p.out[0] = 0;
	pr = lc_proc_spawn( exe, argv, NULL, p_out, p_exited, &p );
	CHECK( pr != NULL );
	CHECK( wait_for( &p, "hanging", 10 ) );
	lc_proc_kill( pr );
	CHECK( wait_exit( &p, 10 ) );
#ifndef _WIN32
	CHECK( p.signaled && p.code == 9 );
#endif
	lc_proc_free( pr );

	/* a program that doesn't exist */
	argv[0] = "/nonexistent/program";
	CHECK( lc_proc_spawn( "/nonexistent/program", argv, NULL, p_out, p_exited, &p ) == NULL );
	lc_mutex_free( p.mu );
}

int main( int argc, char **argv )
{
	if ( argc > 1 && !strncmp( argv[1], "--child", 7 ) )
		return child_main( argv[1] );
	test_parser( );
	test_commands( );
	test_registry( );
	test_process( );
	return TEST_DONE( );
}
