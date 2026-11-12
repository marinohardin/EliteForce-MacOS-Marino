/*
===========================================================================
Launcher core: hosting. The dedicated-server console parser and state machine
(servers.md 9.3-9.8, 9.12, 9.14), the child-process helper, the running-servers
registry (9.13). This file is part of VoyagerSP (GPLv2 or later, like the rest
of the engine).
===========================================================================
*/

#include "lc_host.h"
#include "lc_net.h"		/* lc_strip_colors */

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#	include <winsock2.h>
#	include <windows.h>
#else
#	include <errno.h>
#	include <fcntl.h>
#	include <signal.h>
#	include <sys/wait.h>
#	include <unistd.h>
#	ifdef __linux__
#		include <sys/prctl.h>
#	endif
extern char **environ;
#endif

/* ---- the console parser ------------------------------------------------------------------- */

struct lc_srv {
	lc_srv_callbacks cb;
	int configuredPort, port;
	char pending[8192];
	size_t pendingLen;
	lc_srv_state state;
	int ready, stopping, exited;
	int userStatus, autoStatus;
	double lastPoll;
	int inStatus, statusShown;
	char currentMap[64];
	lc_srv_player rows[64];
	int nRows;
	lc_srv_player players[64];
	int nPlayers;
	double stopAt;
	int escalation;
};

enum { C_NORMAL = 0, C_ORANGE, C_RED, C_GREEN, C_SECONDARY, C_ACCENT };

static void srv_log( lc_srv *s, const char *line, int color )
{
	if ( s->cb.log )
		s->cb.log( line, color, s->cb.user );
}

static void srv_state( lc_srv *s, lc_srv_state st, const char *detail )
{
	s->state = st;
	if ( s->cb.state )
		s->cb.state( st, detail, s->cb.user );
}

static void srv_info( lc_srv *s )
{
	if ( s->cb.info )
		s->cb.info( s->currentMap[0] ? s->currentMap : NULL, s->port, s->cb.user );
}

static void srv_send( lc_srv *s, const char *cmd )
{
	if ( s->cb.send )
		s->cb.send( cmd, s->cb.user );
}

lc_srv *lc_srv_new( const lc_srv_callbacks *cb, int configuredPort )
{
	lc_srv *s = (lc_srv *)calloc( 1, sizeof( *s ) );
	if ( !s )
		return NULL;
	s->cb = *cb;
	s->configuredPort = configuredPort;
	s->port = configuredPort;
	s->state = LC_SRV_STARTING;
	return s;
}

void lc_srv_free( lc_srv *s )
{
	free( s );
}

lc_srv_state lc_srv_get_state( const lc_srv *s )
{
	return s->state;
}

int lc_srv_is_ready( const lc_srv *s )
{
	return s->ready;
}

int lc_srv_is_stopping( const lc_srv *s )
{
	return s->stopping;
}

int lc_srv_bound_port( const lc_srv *s )
{
	return s->port;
}

/* tell the UI the state's text is wanted: the UI maps states to texts (9.3) */
static void became_ready( lc_srv *s )
{
	if ( s->ready )
		return;
	s->ready = 1;
	if ( s->cb.ready )
		s->cb.ready( s->port, s->cb.user );
}

static void end_status( lc_srv *s )
{
	s->inStatus = 0;
	memcpy( s->players, s->rows, sizeof( s->rows[0] ) * (size_t)s->nRows );
	s->nPlayers = s->nRows;
	s->nRows = 0;
	if ( s->state == LC_SRV_STARTING || s->state == LC_SRV_IDLE )
		srv_state( s, LC_SRV_RUNNING, NULL );
	became_ready( s );
	if ( s->cb.players )
		s->cb.players( s->players, s->nPlayers, s->cb.user );
	srv_info( s );
}

static int is_blank( const char *p )
{
	for ( ; *p; p++ )
		if ( !isspace( (unsigned char)*p ) )
			return 0;
	return 1;
}

/* rowRE: ^\s*(\d+)\s+(-?\d+)\s+(\d+|CON|ZMB)\s(.*)$ */
static int parse_row( const char *raw, char *num, size_t ns, char *score, size_t ss, char *ping, size_t ps, const char **rest )
{
	const char *p = raw, *a;
	size_t n;

	while ( isspace( (unsigned char)*p ) )
		p++;
	a = p;
	while ( isdigit( (unsigned char)*p ) )
		p++;
	if ( p == a || !isspace( (unsigned char)*p ) )
		return 0;
	n = (size_t)( p - a );
	if ( n >= ns )
		n = ns - 1;
	memcpy( num, a, n );
	num[n] = 0;
	while ( isspace( (unsigned char)*p ) )
		p++;
	a = p;
	if ( *p == '-' )
		p++;
	while ( isdigit( (unsigned char)*p ) )
		p++;
	if ( p == a || ( p == a + 1 && *a == '-' ) || !isspace( (unsigned char)*p ) )
		return 0;
	n = (size_t)( p - a );
	if ( n >= ss )
		n = ss - 1;
	memcpy( score, a, n );
	score[n] = 0;
	while ( isspace( (unsigned char)*p ) )
		p++;
	a = p;
	if ( !strncmp( p, "CON", 3 ) || !strncmp( p, "ZMB", 3 ) )
		p += 3;
	else
		while ( isdigit( (unsigned char)*p ) )
			p++;
	if ( p == a || !isspace( (unsigned char)*p ) )
		return 0;
	n = (size_t)( p - a );
	if ( n >= ps )
		n = ps - 1;
	memcpy( ping, a, n );
	ping[n] = 0;
	*rest = p + 1;
	return 1;
}

/* tailRE: ^(.*?)\s+(\S+)\s+(-?\d+)\s*$ on rest: name, address (rate ignored); 1 on a match */
static int parse_tail( const char *rest, char *name, size_t nsz, char *addr, size_t asz )
{
	size_t len = strlen( rest ), e = len, rs, as, ae, ws;

	while ( e > 0 && isspace( (unsigned char)rest[e - 1] ) )
		e--;
	rs = e;
	while ( rs > 0 && isdigit( (unsigned char)rest[rs - 1] ) )
		rs--;
	if ( rs == e )
		return 0;
	if ( rs > 0 && rest[rs - 1] == '-' )
		rs--;
	if ( rs == 0 || !isspace( (unsigned char)rest[rs - 1] ) )
		return 0;
	ae = rs;
	while ( ae > 0 && isspace( (unsigned char)rest[ae - 1] ) )
		ae--;
	if ( ae == 0 )
		return 0;
	as = ae;
	while ( as > 0 && !isspace( (unsigned char)rest[as - 1] ) )
		as--;
	if ( as == 0 )
		return 0;			/* \s+ before the address needs a space */
	ws = as;
	while ( ws > 0 && isspace( (unsigned char)rest[ws - 1] ) )
		ws--;
	{
		size_t nl = ws < nsz - 1 ? ws : nsz - 1, al = ae - as < asz - 1 ? ae - as : asz - 1;
		memcpy( name, rest, nl );
		name[nl] = 0;
		memcpy( addr, rest + as, al );
		addr[al] = 0;
	}
	return 1;
}

static void replace_color7( const char *in, char *out, size_t n )
{
	size_t o = 0;
	for ( ; *in && o + 1 < n; in++ ) {
		if ( in[0] == '^' && in[1] == '7' ) {
			out[o++] = ' ';
			in++;
		} else
			out[o++] = *in;
	}
	out[o] = 0;
}

static void handle_line( lc_srv *s, const char *raw )
{
	char line[2048];
	int show = 1;
	const char *rest;

	lc_strip_colors( raw, line, sizeof( line ) );
	if ( s->inStatus ) {
		char num[16], score[16], ping[16];
		show = s->statusShown;
		if ( is_blank( line ) )
			end_status( s );
		else if ( s->nRows < 64 && parse_row( raw, num, sizeof( num ), score, sizeof( score ), ping, sizeof( ping ), &rest ) ) {
			char r2[2048], name[128], addr[128], clean[128];
			lc_srv_player *pl = &s->rows[s->nRows++];
			int bot;

			replace_color7( rest, r2, sizeof( r2 ) );
			if ( !parse_tail( r2, name, sizeof( name ), addr, sizeof( addr ) ) ) {
				lc_strlcpy( name, r2, sizeof( name ) );
				addr[0] = 0;
			}
			bot = !strcmp( addr, "bot" );
			memset( pl, 0, sizeof( *pl ) );
			lc_strip_colors( name, clean, sizeof( clean ) );
			lc_trim( clean );
			lc_strlcpy( pl->name, clean, sizeof( pl->name ) );
			lc_strlcpy( pl->score, score, sizeof( pl->score ) );
			lc_strlcpy( pl->ping, bot ? "bot" : !strcmp( ping, "CON" ) ? "\xe2\x80\xa6" : ping, sizeof( pl->ping ) );
			pl->bot = bot;
			lc_strlcpy( pl->address, addr, sizeof( pl->address ) );
			lc_strlcpy( pl->num, num, sizeof( pl->num ) );
		}
	} else if ( !strncmp( line, "map: ", 5 ) ) {
		s->inStatus = 1;
		s->statusShown = s->userStatus > 0 || !s->autoStatus;
		if ( s->userStatus > 0 )
			s->userStatus--;
		else if ( s->autoStatus > 0 )
			s->autoStatus--;
		show = s->statusShown;
		lc_strlcpy( s->currentMap, line + 5, sizeof( s->currentMap ) );
		lc_trim( s->currentMap );
		s->nRows = 0;
	} else if ( !strcmp( line, "Server is not running." ) && s->autoStatus > 0 && !s->userStatus ) {
		s->autoStatus--;
		show = 0;
		if ( s->state == LC_SRV_RUNNING ) {
			s->currentMap[0] = 0;
			s->nPlayers = 0;
			if ( s->cb.players )
				s->cb.players( s->players, 0, s->cb.user );
			srv_state( s, LC_SRV_IDLE, NULL );
			srv_info( s );
		}
	} else if ( !strncmp( line, "Server ready: map ", 18 ) ) {
		const char *p = line + 18, *e;
		char mapBuf[64];
		size_t n;
		e = p;
		while ( *e && !isspace( (unsigned char)*e ) )
			e++;
		if ( e > p && !strncmp( e, " port ", 6 ) && isdigit( (unsigned char)e[6] ) ) {
			n = (size_t)( e - p );
			if ( n >= sizeof( mapBuf ) )
				n = sizeof( mapBuf ) - 1;
			memcpy( mapBuf, p, n );
			mapBuf[n] = 0;
			lc_strlcpy( s->currentMap, mapBuf, sizeof( s->currentMap ) );
			s->port = atoi( e + 6 );
			if ( s->port != s->configuredPort ) {
				char msg[128];
				lc_snprintf( msg, sizeof( msg ), "Port %d is in use; the server took %d.", s->configuredPort, s->port );
				srv_log( s, msg, C_ORANGE );
			}
			srv_state( s, LC_SRV_RUNNING, NULL );
			became_ready( s );
			srv_info( s );
		}
	} else if ( !strncmp( line, "Opening IP socket: ", 19 ) ) {
		const char *p = line + 19, *c;
		while ( *p && *p != ' ' )
			p++;
		/* [^ ]*:(\d+): the last colon of the token */
		c = p;
		while ( c > line + 19 && c[-1] != ':' )
			c--;
		if ( c > line + 19 && isdigit( (unsigned char)*c ) ) {
			s->port = atoi( c );
			srv_info( s );
		}
	}
	if ( show && ( !strcmp( line, "]" ) || !strcmp( line, "] " ) ) )
		show = 0;
	if ( show )
		srv_log( s, line, !strncmp( line, "WARNING", 7 ) ? C_ORANGE :
			( !strncmp( line, "ERROR", 5 ) || !strncmp( line, "Error", 5 ) ) ? C_RED : C_NORMAL );
}

/* a raw line (without \n): \r removed, UTF-8 or ISO-8859-1 */
static void handle_raw( lc_srv *s, const char *b, size_t n )
{
	char *text = (char *)malloc( n * 2 + 1 );
	size_t i;

	if ( !text )
		return;
	while ( n && b[n - 1] == '\r' )
		n--;
	memcpy( text, b, n );
	text[n] = 0;
	if ( !lc_utf8_valid( text ) ) {
		size_t o = 0;
		for ( i = 0; i < n; i++ ) {
			unsigned char c = (unsigned char)b[i];
			if ( c < 0x80 )
				text[o++] = (char)c;
			else {
				text[o++] = (char)( 0xc0 | ( c >> 6 ) );
				text[o++] = (char)( 0x80 | ( c & 0x3f ) );
			}
		}
		text[o] = 0;
	}
	handle_line( s, text );
	free( text );
}

void lc_srv_feed( lc_srv *s, const char *bytes, size_t n )
{
	size_t i;
	for ( i = 0; i < n; i++ ) {
		if ( bytes[i] == '\n' ) {
			handle_raw( s, s->pending, s->pendingLen );
			s->pendingLen = 0;
		} else if ( s->pendingLen < sizeof( s->pending ) )
			s->pending[s->pendingLen++] = bytes[i];
		else {
			/* a runaway line: flush it */
			handle_raw( s, s->pending, s->pendingLen );
			s->pendingLen = 0;
			s->pending[s->pendingLen++] = bytes[i];
		}
	}
}

void lc_srv_poll_tick( lc_srv *s, double now )
{
	if ( s->autoStatus > 0 && now - s->lastPoll < 10 )
		return;
	s->autoStatus = 1;
	s->lastPoll = now;
	srv_send( s, "status" );
}

void lc_srv_begin_stop( lc_srv *s )
{
	if ( s->stopping || s->exited )
		return;
	s->stopping = 1;
	s->stopAt = lc_now( );
	s->escalation = 0;
	srv_state( s, LC_SRV_STOPPING, NULL );
	srv_log( s, "] quit", C_ACCENT );
	srv_send( s, "quit" );
}

int lc_srv_stop_tick( lc_srv *s, double now )
{
	if ( !s->stopping || s->exited )
		return 0;
	if ( s->escalation == 0 && now - s->stopAt >= 5 ) {
		lc_log( "Server: no exit after quit, sending SIGTERM" );
		s->escalation = 1;
		s->stopAt = now;
		return 1;
	}
	if ( s->escalation == 1 && now - s->stopAt >= 3 ) {
		lc_log( "Server: still running, sending SIGKILL" );
		s->escalation = 2;
		return 2;
	}
	return 0;
}

void lc_srv_user_command( lc_srv *s, const char *cmd )
{
	char c[512];
	char lower[512];
	size_t i;

	lc_strlcpy( c, cmd ? cmd : "", sizeof( c ) );
	lc_trim( c );
	if ( !c[0] )
		return;
	for ( i = 0; c[i]; i++ )
		lower[i] = (char)tolower( (unsigned char)c[i] );
	lower[i] = 0;
	if ( !strcmp( lower, "quit" ) ) {
		lc_srv_begin_stop( s );
		return;
	}
	{
		char shown[520];
		lc_snprintf( shown, sizeof( shown ), "] %s", c );
		srv_log( s, shown, C_ACCENT );
	}
	if ( !strcmp( lower, "status" ) )
		s->userStatus++;
	srv_send( s, c );
}

void lc_srv_exited( lc_srv *s, int signaled, int code )
{
	char msg[160];

	if ( s->pendingLen ) {
		handle_raw( s, s->pending, s->pendingLen );
		s->pendingLen = 0;
	}
	s->exited = 1;
	lc_log( "Server: exited (%s %d)", signaled ? "signal" : "code", code );
	s->currentMap[0] = 0;
	s->nPlayers = 0;
	s->inStatus = 0;
	if ( s->cb.players )
		s->cb.players( s->players, 0, s->cb.user );
	srv_info( s );
	if ( s->stopping ) {
		srv_state( s, LC_SRV_STOPPED, NULL );
		srv_log( s, "The server stopped.", C_SECONDARY );
		return;
	}
	if ( signaled )
		lc_snprintf( msg, sizeof( msg ), "The server crashed (signal %d).", code );
	else if ( code == 1 && !s->ready )
		lc_strlcpy( msg, "The server found no Elite Force game files (exit code 1).", sizeof( msg ) );
	else if ( code == 3 && !s->ready )
		lc_strlcpy( msg, "The server couldn't use the game folder (exit code 3).", sizeof( msg ) );
	else
		lc_snprintf( msg, sizeof( msg ), "The server quit (exit code %d).", code );
	srv_log( s, msg, C_RED );
	if ( s->ready )
		srv_state( s, LC_SRV_FAILED, "the server quit unexpectedly" );
	else if ( !signaled && code == 1 )
		srv_state( s, LC_SRV_FAILED, "no game files" );
	else if ( !signaled && code == 3 )
		srv_state( s, LC_SRV_FAILED, "the game folder can't be used" );
	else
		srv_state( s, LC_SRV_FAILED, "the server didn't start, see the console below" );
}

void lc_srv_start_failed( lc_srv *s, const char *logLine )
{
	if ( logLine && *logLine )
		srv_log( s, logLine, C_RED );
	srv_state( s, LC_SRV_FAILED, "couldn't start" );
}

static const char *const bot_names[28] = {
	"Munro", "Janeway", "Chakotay", "Tuvok", "Paris", "Torres", "Kim", "Seven", "Doctor", "Neelix", "Biessman",
	"Chang", "Chell", "Foster", "Jurot", "Telsia", "Gowron", "Kahless", "Sela", "Alexandria", "Laird", "Oviedo",
	"Nelson", "Odell", "Jaworski", "Csatlos", "Vorik", "BorgQueen" };

const char *lc_srv_addbot_command( lc_srv *s, int skill, char *buf, size_t n )
{
	int free_[28], nFree = 0, i, j;
	unsigned r;

	for ( i = 0; i < 28; i++ ) {
		int taken = 0;
		for ( j = 0; j < s->nPlayers; j++ )
			if ( !lc_stricmp( s->players[j].name, bot_names[i] ) )
				taken = 1;
		if ( !taken )
			free_[nFree++] = i;
	}
	lc_random_bytes( &r, sizeof( r ) );
	if ( nFree )
		i = free_[r % (unsigned)nFree];
	else
		i = (int)( r % 28 );
	if ( skill < 1 )
		skill = 1;
	if ( skill > 5 )
		skill = 5;
	lc_snprintf( buf, n, "addbot %s %d", bot_names[i], skill );
	return buf;
}

void lc_srv_say_command( const char *text, char *buf, size_t n )
{
	char t[1024];
	size_t i, chars = 0, o = 0;

	lc_strlcpy( t, text ? text : "", sizeof( t ) );
	for ( i = 0; t[i]; i++ ) {
		if ( t[i] == '"' )
			t[i] = '\'';
		else if ( t[i] == ';' )
			t[i] = ',';
	}
	lc_trim( t );
	/* cut to 150 characters (not bytes) */
	for ( i = 0; t[i]; i++ ) {
		if ( ( (unsigned char)t[i] & 0xc0 ) != 0x80 ) {
			if ( chars == 150 )
				break;
			chars++;
		}
		o = i + 1;
	}
	t[o] = 0;
	if ( !t[0] ) {
		if ( n )
			buf[0] = 0;
		return;
	}
	lc_snprintf( buf, n, "say \"%s\"", t );
}

void lc_srv_next_map( lc_srv *s )			{ lc_srv_user_command( s, "vstr nextmap" ); }
void lc_srv_restart_map( lc_srv *s )		{ lc_srv_user_command( s, "map_restart" ); }
void lc_srv_heartbeat( lc_srv *s )			{ srv_send( s, "heartbeat" ); }

void lc_srv_change_map_slot( lc_srv *s, int slot )
{
	char c[64];
	lc_snprintf( c, sizeof( c ), "vstr vsp_map%d", slot );
	lc_srv_user_command( s, c );
}

void lc_srv_change_map( lc_srv *s, const char *map )
{
	char c[128];
	lc_snprintf( c, sizeof( c ), "map %s", map );
	lc_srv_user_command( s, c );
}

void lc_srv_add_bot( lc_srv *s, int skill )
{
	char c[128];
	lc_srv_addbot_command( s, skill, c, sizeof( c ) );
	lc_srv_user_command( s, c );
}

void lc_srv_remove_bots( lc_srv *s )
{
	lc_srv_user_command( s, "set bot_minplayers 0" );
	lc_srv_user_command( s, "kick allbots" );
	srv_log( s, "Bots won't refill this game; Add Bot still adds them.", C_SECONDARY );
}

void lc_srv_kick( lc_srv *s, const char *num )
{
	char c[64];
	lc_snprintf( c, sizeof( c ), "clientkick %s", num );
	lc_srv_user_command( s, c );
}

void lc_srv_say( lc_srv *s, const char *text )
{
	char c[800];
	lc_srv_say_command( text, c, sizeof( c ) );
	if ( c[0] )
		lc_srv_user_command( s, c );
}

/* ---- the child process (9.5) ------------------------------------------------------------------------------ */

struct lc_proc {
	void (*out)( const char *b, size_t n, void *u );
	void (*exited)( int signaled, int code, void *u );
	void *user;
	lc_thread *thr;
	volatile int done;
	int pid;
#ifdef _WIN32
	HANDLE hProcess, hIn, hOut, hJob;
#else
	int inFd, outFd;
#endif
};

static void proc_reader( void *u );

#ifdef _WIN32

static wchar_t *utf8_to_wide( const char *s )
{
	int n = MultiByteToWideChar( CP_UTF8, 0, s, -1, NULL, 0 );
	wchar_t *w = (wchar_t *)malloc( sizeof( wchar_t ) * (size_t)( n > 0 ? n : 1 ) );
	if ( !w )
		return NULL;
	if ( n <= 0 || !MultiByteToWideChar( CP_UTF8, 0, s, -1, w, n ) )
		w[0] = 0;
	return w;
}

/* CommandLineToArgvW-compatible quoting */
static void quote_arg( const char *a, char *out, size_t *pos, size_t cap )
{
	int needQuote = !*a || strpbrk( a, " \t\n\v\"" ) != NULL;
	size_t p = *pos;
	const char *c;
	int backslashes;

#define PUT( ch ) do { if ( p + 1 < cap ) out[p] = (ch); p++; } while ( 0 )
	if ( !needQuote ) {
		for ( c = a; *c; c++ )
			PUT( *c );
		*pos = p;
		return;
	}
	PUT( '"' );
	for ( c = a; ; c++ ) {
		backslashes = 0;
		while ( *c == '\\' ) {
			c++;
			backslashes++;
		}
		if ( !*c ) {
			while ( backslashes-- > 0 ) { PUT( '\\' ); PUT( '\\' ); }
			break;
		}
		if ( *c == '"' ) {
			while ( backslashes-- > 0 ) { PUT( '\\' ); PUT( '\\' ); }
			PUT( '\\' );
			PUT( '"' );
		} else {
			while ( backslashes-- > 0 )
				PUT( '\\' );
			PUT( *c );
		}
	}
	PUT( '"' );
	*pos = p;
#undef PUT
}

static wchar_t *build_env_block( const char *const *envp )
{
	/* explicit environment: UTF-16, double NUL; none given: ours minus VOYAGERSP_TEST_* */
	size_t total = 1, i;
	wchar_t *block, *p;

	if ( envp ) {
		for ( i = 0; envp[i]; i++ )
			total += strlen( envp[i] ) * 2 + 2;
		block = (wchar_t *)malloc( sizeof( wchar_t ) * total );
		p = block;
		for ( i = 0; envp[i]; i++ ) {
			int n = MultiByteToWideChar( CP_UTF8, 0, envp[i], -1, p, (int)( total - (size_t)( p - block ) ) );
			if ( n > 0 )
				p += n;
		}
		*p = 0;
		if ( p == block )
			*++p = 0;
		return block;
	} else {
		wchar_t *env = GetEnvironmentStringsW( ), *q;
		size_t len = 0;
		if ( !env )
			return NULL;
		for ( q = env; *q; q += wcslen( q ) + 1 )
			len += wcslen( q ) + 1;
		block = (wchar_t *)malloc( sizeof( wchar_t ) * ( len + 2 ) );
		p = block;
		for ( q = env; *q; q += wcslen( q ) + 1 ) {
			if ( !_wcsnicmp( q, L"VOYAGERSP_TEST_", 15 ) )
				continue;
			memcpy( p, q, sizeof( wchar_t ) * ( wcslen( q ) + 1 ) );
			p += wcslen( q ) + 1;
		}
		*p = 0;
		if ( p == block )
			*++p = 0;
		FreeEnvironmentStringsW( env );
		return block;
	}
}

lc_proc *lc_proc_spawn( const char *exe, const char *const *argv, const char *const *envp,
	void (*out)( const char *b, size_t n, void *u ), void (*exited)( int signaled, int code, void *u ), void *u )
{
	lc_proc *p;
	SECURITY_ATTRIBUTES sa;
	HANDLE inR = NULL, inW = NULL, outR = NULL, outW = NULL;
	STARTUPINFOW si;
	PROCESS_INFORMATION pi;
	char *cmd;
	size_t pos = 0, cap = 32768, i;
	wchar_t *wexe, *wcmd, *env;
	BOOL ok;

	lc_net_init( );
	p = (lc_proc *)calloc( 1, sizeof( *p ) );
	if ( !p )
		return NULL;
	p->out = out;
	p->exited = exited;
	p->user = u;
	sa.nLength = sizeof( sa );
	sa.lpSecurityDescriptor = NULL;
	sa.bInheritHandle = TRUE;
	if ( !CreatePipe( &outR, &outW, &sa, 0 ) || !CreatePipe( &inR, &inW, &sa, 0 ) ) {
		free( p );
		return NULL;
	}
	SetHandleInformation( outR, HANDLE_FLAG_INHERIT, 0 );
	SetHandleInformation( inW, HANDLE_FLAG_INHERIT, 0 );

	cmd = (char *)malloc( cap );
	for ( i = 0; argv && argv[i]; i++ ) {
		if ( i )
			cmd[pos < cap ? pos : cap - 1] = ' ', pos++;
		quote_arg( argv[i], cmd, &pos, cap );
	}
	if ( !argv || !argv[0] )
		quote_arg( exe, cmd, &pos, cap );
	cmd[pos < cap ? pos : cap - 1] = 0;
	wexe = utf8_to_wide( exe );
	wcmd = utf8_to_wide( cmd );
	free( cmd );
	env = build_env_block( envp );

	memset( &si, 0, sizeof( si ) );
	si.cb = sizeof( si );
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = inR;
	si.hStdOutput = outW;
	si.hStdError = outW;
	memset( &pi, 0, sizeof( pi ) );
	ok = CreateProcessW( wexe, wcmd, NULL, NULL, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, env, NULL, &si, &pi );
	free( wexe );
	free( wcmd );
	free( env );
	CloseHandle( inR );
	CloseHandle( outW );
	if ( !ok ) {
		CloseHandle( outR );
		CloseHandle( inW );
		free( p );
		return NULL;
	}
	/* kill the child with us (KILL_ON_JOB_CLOSE) */
	p->hJob = CreateJobObjectW( NULL, NULL );
	if ( p->hJob ) {
		JOBOBJECT_EXTENDED_LIMIT_INFORMATION li;
		memset( &li, 0, sizeof( li ) );
		li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
		SetInformationJobObject( p->hJob, JobObjectExtendedLimitInformation, &li, sizeof( li ) );
		if ( !AssignProcessToJobObject( p->hJob, pi.hProcess ) ) {
			CloseHandle( p->hJob );
			p->hJob = NULL;
		}
	}
	ResumeThread( pi.hThread );
	CloseHandle( pi.hThread );
	p->hProcess = pi.hProcess;
	p->hIn = inW;
	p->hOut = outR;
	p->pid = (int)pi.dwProcessId;
	p->thr = lc_thread_start( proc_reader, p );
	if ( !p->thr ) {
		TerminateProcess( p->hProcess, 1 );
		CloseHandle( p->hProcess );
		CloseHandle( inW );
		CloseHandle( outR );
		if ( p->hJob )
			CloseHandle( p->hJob );
		free( p );
		return NULL;
	}
	return p;
}

static void proc_reader( void *u )
{
	lc_proc *p = (lc_proc *)u;
	char buf[4096];
	DWORD got, code = 0;

	for ( ;; ) {
		if ( !ReadFile( p->hOut, buf, sizeof( buf ), &got, NULL ) || got == 0 )
			break;
		if ( p->out )
			p->out( buf, got, p->user );
	}
	WaitForSingleObject( p->hProcess, INFINITE );
	GetExitCodeProcess( p->hProcess, &code );
	p->done = 1;
	if ( p->exited )
		p->exited( code >= 0xC0000000u ? 1 : 0, (int)code, p->user );
}

int lc_proc_write( lc_proc *p, const char *b, size_t n )
{
	DWORD w = 0;
	if ( p->done || !p->hIn )
		return 0;
	return WriteFile( p->hIn, b, (DWORD)n, &w, NULL ) && w == n;
}

void lc_proc_terminate( lc_proc *p )
{
	if ( !p->done )
		TerminateProcess( p->hProcess, 1 );
}

void lc_proc_kill( lc_proc *p )
{
	lc_proc_terminate( p );
}

void lc_proc_free( lc_proc *p )
{
	if ( !p )
		return;
	if ( !p->done )
		TerminateProcess( p->hProcess, 1 );
	if ( p->thr )
		lc_thread_join( p->thr );
	if ( p->hIn )
		CloseHandle( p->hIn );
	CloseHandle( p->hOut );
	CloseHandle( p->hProcess );
	if ( p->hJob )
		CloseHandle( p->hJob );
	free( p );
}

#else /* POSIX */

lc_proc *lc_proc_spawn( const char *exe, const char *const *argv, const char *const *envp,
	void (*out)( const char *b, size_t n, void *u ), void (*exited)( int signaled, int code, void *u ), void *u )
{
	lc_proc *p;
	int inPipe[2], outPipe[2], errPipe[2];
	pid_t pid;
	char **env = NULL, *const *useEnv;
	int nEnv = 0, k;
	const char *defaultArgv[2];

	lc_net_init( );		/* ignores SIGPIPE */
	p = (lc_proc *)calloc( 1, sizeof( *p ) );
	if ( !p )
		return NULL;
	p->out = out;
	p->exited = exited;
	p->user = u;
	if ( pipe( inPipe ) || pipe( outPipe ) || pipe( errPipe ) ) {
		free( p );
		return NULL;
	}
	fcntl( errPipe[1], F_SETFD, FD_CLOEXEC );
	fcntl( inPipe[1], F_SETFD, FD_CLOEXEC );
	fcntl( outPipe[0], F_SETFD, FD_CLOEXEC );
	fcntl( errPipe[0], F_SETFD, FD_CLOEXEC );
	if ( !argv || !argv[0] ) {
		defaultArgv[0] = exe;
		defaultArgv[1] = NULL;
		argv = defaultArgv;
	}
	if ( envp )
		useEnv = (char *const *)envp;
	else {
		for ( k = 0; environ && environ[k]; k++ )
			nEnv++;
		env = (char **)calloc( (size_t)nEnv + 1, sizeof( char * ) );
		nEnv = 0;
		for ( k = 0; environ && environ[k]; k++ )
			if ( strncmp( environ[k], "VOYAGERSP_TEST_", 15 ) )
				env[nEnv++] = environ[k];
		env[nEnv] = NULL;
		useEnv = env;
	}
	pid = fork( );
	if ( pid < 0 ) {
		close( inPipe[0] ); close( inPipe[1] ); close( outPipe[0] ); close( outPipe[1] ); close( errPipe[0] ); close( errPipe[1] );
		free( env );
		free( p );
		return NULL;
	}
	if ( pid == 0 ) {
		int e;
		/* child */
		dup2( inPipe[0], 0 );
		dup2( outPipe[1], 1 );
		dup2( outPipe[1], 2 );
		close( inPipe[0] ); close( inPipe[1] ); close( outPipe[0] ); close( outPipe[1] ); close( errPipe[0] );
		signal( SIGPIPE, SIG_DFL );
#ifdef __linux__
		prctl( PR_SET_PDEATHSIG, SIGTERM );
		if ( getppid( ) == 1 )
			_exit( 127 );
#endif
		execve( exe, (char *const *)argv, useEnv );
		e = errno;
		if ( write( errPipe[1], &e, sizeof( e ) ) < 0 ) {}
		_exit( 127 );
	}
	close( inPipe[0] );
	close( outPipe[1] );
	close( errPipe[1] );
	free( env );
	{
		int e = 0;
		ssize_t r;
		do {
			r = read( errPipe[0], &e, sizeof( e ) );
		} while ( r < 0 && errno == EINTR );
		close( errPipe[0] );
		if ( r == (ssize_t)sizeof( e ) ) {
			int st;
			waitpid( pid, &st, 0 );
			close( inPipe[1] );
			close( outPipe[0] );
			free( p );
			errno = e;
			return NULL;
		}
	}
	p->pid = (int)pid;
	p->inFd = inPipe[1];
	p->outFd = outPipe[0];
	p->thr = lc_thread_start( proc_reader, p );
	if ( !p->thr ) {
		int st;
		kill( pid, SIGKILL );
		waitpid( pid, &st, 0 );
		close( p->inFd );
		close( p->outFd );
		free( p );
		return NULL;
	}
	return p;
}

static void proc_reader( void *u )
{
	lc_proc *p = (lc_proc *)u;
	char buf[4096];
	int st = 0, signaled = 0, code = 0;
	pid_t r;

	for ( ;; ) {
		ssize_t n = read( p->outFd, buf, sizeof( buf ) );
		if ( n < 0 && errno == EINTR )
			continue;
		if ( n <= 0 )
			break;
		if ( p->out )
			p->out( buf, (size_t)n, p->user );
	}
	do {
		r = waitpid( (pid_t)p->pid, &st, 0 );
	} while ( r < 0 && errno == EINTR );
	p->done = 1;
	if ( r >= 0 ) {
		if ( WIFSIGNALED( st ) ) {
			signaled = 1;
			code = WTERMSIG( st );
		} else if ( WIFEXITED( st ) )
			code = WEXITSTATUS( st );
	}
	if ( p->exited )
		p->exited( signaled, code, p->user );
}

int lc_proc_write( lc_proc *p, const char *b, size_t n )
{
	size_t done = 0;
	if ( p->done || p->inFd < 0 )
		return 0;
	while ( done < n ) {
		ssize_t w = write( p->inFd, b + done, n - done );
		if ( w < 0 && errno == EINTR )
			continue;
		if ( w <= 0 )
			return 0;
		done += (size_t)w;
	}
	return 1;
}

void lc_proc_terminate( lc_proc *p )
{
	if ( !p->done )
		kill( (pid_t)p->pid, SIGTERM );
}

void lc_proc_kill( lc_proc *p )
{
	if ( !p->done )
		kill( (pid_t)p->pid, SIGKILL );
}

void lc_proc_free( lc_proc *p )
{
	if ( !p )
		return;
	if ( !p->done )
		kill( (pid_t)p->pid, SIGKILL );
	if ( p->thr )
		lc_thread_join( p->thr );
	if ( p->inFd >= 0 )
		close( p->inFd );
	close( p->outFd );
	free( p );
}

#endif

int lc_proc_pid( lc_proc *p )
{
	return p->pid;
}

int lc_proc_running( lc_proc *p )
{
	return !p->done;
}

/* ---- the running-servers registry (9.13) --------------------------------------------------------------------------- */

static void running_dir( const char *homeRoot, char *out, size_t n )
{
	lc_path_join( out, n, homeRoot, "RunningServers" );
}

int lc_running_register( const char *homeRoot, const char *name, int port )
{
	char dir[LC_MAX_PATH], file[LC_MAX_PATH], fn[64], text[512], esc[160];
	size_t o = 0, i;

	running_dir( homeRoot, dir, sizeof( dir ) );
	if ( !lc_mkdir_p( dir ) )
		return 0;
	for ( i = 0; name && name[i] && o + 3 < sizeof( esc ); i++ ) {
		unsigned char c = (unsigned char)name[i];
		if ( c == '"' || c == '\\' )
			esc[o++] = '\\';
		if ( c < 32 )
			continue;
		esc[o++] = (char)c;
	}
	esc[o] = 0;
	lc_snprintf( text, sizeof( text ), "{\"name\": \"%s\", \"port\": %d, \"started\": %lld}\n", esc, port, (long long)lc_time( ) );
	lc_snprintf( fn, sizeof( fn ), "%ld.json", lc_getpid( ) );
	lc_path_join( file, sizeof( file ), dir, fn );
	return lc_write_file_atomic( file, text, strlen( text ) );
}

void lc_running_unregister( const char *homeRoot )
{
	char dir[LC_MAX_PATH], file[LC_MAX_PATH], fn[64];
	running_dir( homeRoot, dir, sizeof( dir ) );
	lc_snprintf( fn, sizeof( fn ), "%ld.json", lc_getpid( ) );
	lc_path_join( file, sizeof( file ), dir, fn );
	lc_remove( file );
}

typedef struct { struct lc_running *out; int n, max; char dir[LC_MAX_PATH]; } running_ctx;

static int running_entry( const char *name, int isDir, void *user )
{
	running_ctx *c = (running_ctx *)user;
	size_t l = strlen( name );
	long pid;
	char file[LC_MAX_PATH], *text, *e, *p;
	size_t len;
	struct lc_running r;

	if ( isDir || l < 6 || lc_stricmp( name + l - 5, ".json" ) )
		return 0;
	pid = strtol( name, &e, 10 );
	if ( pid <= 0 || e != name + l - 5 )
		return 0;
	lc_path_join( file, sizeof( file ), c->dir, name );
	if ( !lc_pid_alive( pid ) ) {
		lc_remove( file );
		return 0;
	}
	text = lc_read_file( file, &len );
	if ( !text )
		return 0;
	memset( &r, 0, sizeof( r ) );
	r.pid = (int)pid;
	if ( ( p = strstr( text, "\"name\"" ) ) != NULL && ( p = strchr( p + 6, '"' ) ) != NULL ) {
		size_t o = 0;
		for ( p++; *p && *p != '"' && o + 1 < sizeof( r.name ); p++ ) {
			if ( *p == '\\' && p[1] )
				p++;
			r.name[o++] = *p;
		}
		r.name[o] = 0;
	}
	if ( ( p = strstr( text, "\"port\"" ) ) != NULL && ( p = strchr( p, ':' ) ) != NULL )
		r.port = atoi( p + 1 );
	if ( ( p = strstr( text, "\"started\"" ) ) != NULL && ( p = strchr( p, ':' ) ) != NULL )
		r.started = atoll( p + 1 );
	free( text );
	if ( c->n < c->max )
		c->out[c->n++] = r;
	return 0;
}

static int running_cmp( const void *a, const void *b )
{
	const struct lc_running *x = (const struct lc_running *)a, *y = (const struct lc_running *)b;
	return x->started < y->started ? -1 : x->started > y->started ? 1 : x->pid - y->pid;
}

int lc_running_list( const char *homeRoot, struct lc_running *out, int max )
{
	running_ctx c;
	c.out = out;
	c.n = 0;
	c.max = max;
	running_dir( homeRoot, c.dir, sizeof( c.dir ) );
	lc_list_dir( c.dir, running_entry, &c );
	if ( c.n > 1 )
		qsort( out, (size_t)c.n, sizeof( out[0] ), running_cmp );
	return c.n;
}
