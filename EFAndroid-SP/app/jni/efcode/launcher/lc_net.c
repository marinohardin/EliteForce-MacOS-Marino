/*
===========================================================================
Launcher core: server queries (getservers / getinfo / getstatus), colour codes,
the server browser model, efservers.com. Spec: servers.md sections 7 and 8.
This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#include "lc_sock.h"
#include "lc_net.h"
#include "lc_nat.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LC_PORT_MASTER		27953
#define LC_PORT_SERVER		27960
#define LC_BUF				16384

static const unsigned char OOB[4] = { 0xff, 0xff, 0xff, 0xff };

/* ---- text helpers ------------------------------------------------------------------- */

/* ISO-8859-1 -> UTF-8; out needs 2n+1 */
static void latin1_to_utf8( const unsigned char *in, size_t n, char *out )
{
	size_t i, o = 0;
	for ( i = 0; i < n; i++ ) {
		if ( in[i] < 0x80 )
			out[o++] = (char)in[i];
		else {
			out[o++] = (char)( 0xc0 | ( in[i] >> 6 ) );
			out[o++] = (char)( 0x80 | ( in[i] & 0x3f ) );
		}
	}
	out[o] = 0;
}

static void lc_lower_str( char *s )
{
	for ( ; *s; s++ )
		*s = (char)tolower( (unsigned char)*s );
}

/* a parsed \k\v\k\v string: keys lower-cased, pointing into a private copy */
#define KV_MAX 128
typedef struct { char *text; int n; const char *key[KV_MAX], *val[KV_MAX]; } kvset;

static void kv_parse( kvset *kv, const char *s )
{
	char *p;
	kv->n = 0;
	kv->text = lc_strdup( s );
	if ( !kv->text )
		return;
	/* s begins with '\\': tokens key,value,key,value */
	p = kv->text;
	if ( *p == '\\' )
		p++;
	while ( *p && kv->n < KV_MAX ) {
		char *k = p, *v, *e;
		e = strchr( p, '\\' );
		if ( !e )
			break;
		*e = 0;
		v = e + 1;
		e = strchr( v, '\\' );
		if ( e ) {
			*e = 0;
			p = e + 1;
		} else
			p = v + strlen( v );
		lc_lower_str( k );
		kv->key[kv->n] = k;
		kv->val[kv->n] = v;
		kv->n++;
	}
}

static const char *kv_get( const kvset *kv, const char *key )
{
	int i;
	/* the last occurrence wins, as a dictionary assignment would */
	for ( i = kv->n - 1; i >= 0; i-- )
		if ( !strcmp( kv->key[i], key ) )
			return kv->val[i];
	return NULL;
}

static void kv_free( kvset *kv )
{
	free( kv->text );
	kv->text = NULL;
}

/* ---- colour codes (7.9) ---------------------------------------------------------------- */

void lc_decode_colors( const char *in, char *out, size_t n, unsigned char *colorIdx )
{
	size_t o = 0;
	int color = 7;
	const unsigned char *p = (const unsigned char *)in;

	if ( !n )
		return;
	for ( ; *p; p++ ) {
		if ( isalnum( *p ) && *p < 128 && o > 0 && out[o - 1] == '^' ) {
			o--;
			color = ( *p - '0' ) & 7;
			continue;
		}
		if ( o + 1 >= n )
			break;
		out[o] = (char)*p;
		if ( colorIdx )
			colorIdx[o] = (unsigned char)color;
		o++;
	}
	out[o] = 0;
}

void lc_strip_colors( const char *in, char *out, size_t n )
{
	if ( !strchr( in, '^' ) ) {
		lc_strlcpy( out, in, n );
		return;
	}
	lc_decode_colors( in, out, n, NULL );
}

int lc_cmod_number( const char *v, char *out, size_t n )
{
	const char *p;

	if ( n )
		out[0] = 0;
	if ( !v )
		return 0;
	for ( p = v; *p; p++ ) {
		const char *q;
		size_t a, b;
		if ( *p != 'v' )
			continue;
		if ( p > v && ( isalnum( (unsigned char)p[-1] ) || p[-1] == '_' ) )
			continue;
		q = p + 1;
		for ( a = 0; isdigit( (unsigned char)q[a] ); a++ ) {}
		if ( !a || q[a] != '.' )
			continue;
		for ( b = 0; isdigit( (unsigned char)q[a + 1 + b] ); b++ ) {}
		if ( !b )
			continue;
		if ( n ) {
			size_t len = a + 1 + b;
			if ( len >= n )
				len = n - 1;
			memcpy( out, q, len );
			out[len] = 0;
		}
		return 1;
	}
	return 0;
}

double lc_cmod_rank( int isCmod, const char *v )
{
	char num[64];
	if ( !isCmod )
		return 0;
	if ( lc_cmod_number( v, num, sizeof( num ) ) )
		return atof( num );
	return 0.01;
}

/* ---- parsing (7.1 - 7.3, 7.6) ------------------------------------------------------------- */

static int hex_val( int c )
{
	if ( c >= '0' && c <= '9' )
		return c - '0';
	if ( c >= 'a' && c <= 'f' )
		return c - 'a' + 10;
	if ( c >= 'A' && c <= 'F' )
		return c - 'A' + 10;
	return -1;
}

int lc_parse_server_list( const unsigned char *buf, size_t n, void (*entry)( lc_addr4 a, void *u ), void *u )
{
	const unsigned char *end = buf + n, *p;
	int hex = 0, count = 0, i;

	p = n ? (const unsigned char *)memchr( buf, '\\', n ) : NULL;
	if ( !p )
		return 0;
	if ( p + 14 <= end && p[13] == '\\' ) {
		hex = 1;
		for ( i = 1; i <= 12; i++ )
			if ( hex_val( p[i] ) < 0 )
				hex = 0;
	}
	while ( p && p + 7 <= end && memcmp( p + 1, "EOT", 3 ) != 0 ) {
		unsigned char v[6];
		lc_addr4 a;

		if ( hex ) {
			if ( p + 13 > end )
				break;
			for ( i = 0; i < 6; i++ ) {
				int hi = hex_val( p[1 + 2 * i] ), lo = hex_val( p[2 + 2 * i] );
				v[i] = (unsigned char)( ( hi < 0 ? 0 : hi ) << 4 | ( lo < 0 ? 0 : lo ) );
			}
			p += 13;
		} else {
			for ( i = 0; i < 6; i++ )
				v[i] = p[1 + i];
			p += 7;
		}
		memcpy( a.ip, v, 4 );
		a.port = (unsigned short)( v[4] << 8 | v[5] );
		count++;
		if ( entry )
			entry( a, u );
		if ( p < end && *p != '\\' )
			p = (const unsigned char *)memchr( p, '\\', (size_t)( end - p ) );
	}
	return count;
}

int lc_parse_address4( const char *s, int defaultPort, struct sockaddr_in *out )
{
	char host[64];
	const char *colon = strrchr( s, ':' );
	int port = defaultPort;
	size_t hl;
	struct in_addr ia;

	if ( colon ) {
		char *end;
		long p;
		if ( !colon[1] )
			return 0;
		p = strtol( colon + 1, &end, 10 );
		if ( *end || p <= 0 || p >= 65536 )
			return 0;
		port = (int)p;
		hl = (size_t)( colon - s );
	} else
		hl = strlen( s );
	if ( port <= 0 || port >= 65536 || hl == 0 || hl >= sizeof( host ) )
		return 0;
	memcpy( host, s, hl );
	host[hl] = 0;
	if ( inet_pton( AF_INET, host, &ia ) != 1 )
		return 0;
	memset( out, 0, sizeof( *out ) );
	out->sin_family = AF_INET;
	out->sin_port = htons( (unsigned short)port );
	out->sin_addr = ia;
	return 1;
}

int lc_parse_info_response( const unsigned char *buf, size_t n, const char *from, lc_server_info *out )
{
	static const char prefix[] = "infoResponse";
	const unsigned char *p;
	char *text;
	kvset kv;
	const char *v;
	char tmp[512];

	if ( n < 17 || memcmp( buf, OOB, 4 ) || memcmp( buf + 4, prefix, 12 ) )
		return 0;
	p = (const unsigned char *)memchr( buf + 16, '\\', n - 16 );
	memset( out, 0, sizeof( *out ) );
	lc_strlcpy( out->address, from ? from : "", sizeof( out->address ) );
	out->humans = -1;
	if ( !p ) {
		/* no info string: the same as an empty one */
		lc_strlcpy( out->hostname, out->address, sizeof( out->hostname ) );
		lc_strlcpy( out->name, out->address, sizeof( out->name ) );
		return 1;
	}
	n -= (size_t)( p - buf );
	text = (char *)malloc( n * 2 + 1 );
	if ( !text )
		return 0;
	latin1_to_utf8( p, n, text );
	kv_parse( &kv, text );
	free( text );

	v = kv_get( &kv, "hostname" );
	lc_strlcpy( out->hostname, v ? v : "", sizeof( out->hostname ) );
	lc_utf8_truncate( out->hostname, sizeof( out->hostname ) - 1 );
	lc_strip_colors( out->hostname, tmp, sizeof( tmp ) );
	lc_trim( tmp );
	lc_strlcpy( out->name, tmp, sizeof( out->name ) );
	lc_utf8_truncate( out->name, sizeof( out->name ) - 1 );
	if ( !out->name[0] ) {
		lc_strlcpy( out->name, out->address, sizeof( out->name ) );
		lc_strlcpy( out->hostname, out->address, sizeof( out->hostname ) );
	}
	v = kv_get( &kv, "mapname" );
	lc_strlcpy( out->map, v ? v : "", sizeof( out->map ) );
	lc_lower_str( out->map );
	v = kv_get( &kv, "clients" );
	out->players = v ? atoi( v ) : 0;
	v = kv_get( &kv, "sv_maxclients" );
	out->maxPlayers = v ? atoi( v ) : 0;
	v = kv_get( &kv, "g_humanplayers" );
	out->humans = v ? atoi( v ) : -1;
	v = kv_get( &kv, "gametype" );
	out->gametype = v ? atoi( v ) : 0;
	v = kv_get( &kv, "g_needpass" );
	out->needPass = v && atoi( v ) != 0;
	v = kv_get( &kv, "needpass" );
	if ( v && atoi( v ) != 0 )
		out->needPass = 1;
	kv_free( &kv );
	return 1;
}

int lc_parse_status_response( const unsigned char *buf, size_t n, lc_server_status *out )
{
	static const char prefix[] = "statusResponse";
	char *text, *line, *next;
	const char *v;

	if ( n < 19 || memcmp( buf, OOB, 4 ) || memcmp( buf + 4, prefix, 14 ) )
		return 0;
	memset( out, 0, sizeof( *out ) );
	out->humans = -1;
	n -= 18;
	text = (char *)malloc( n * 2 + 1 );
	if ( !text )
		return 0;
	latin1_to_utf8( buf + 18, n, text );
	for ( line = text; line; line = next ) {
		next = strchr( line, '\n' );
		if ( next )
			*next++ = 0;
		if ( line[0] == '\\' ) {
			kvset kv;
			kv_parse( &kv, line );
			v = kv_get( &kv, "g_humanplayers" );
			out->humans = v ? atoi( v ) : -1;
			v = kv_get( &kv, "version_cmod" );
			if ( v && *v ) {
				out->isCmod = 1;
				lc_strlcpy( out->cmodVersion, v, sizeof( out->cmodVersion ) );
			} else {
				v = kv_get( &kv, "version" );
				if ( v && ( !strncmp( v, "cMod HM", 7 ) || !strncmp( v, "ioEF:cMod HM", 12 ) ) ) {
					out->isCmod = 1;
					out->cmodVersion[0] = 0;
				}
			}
			kv_free( &kv );
		} else if ( out->playerCount < 64 ) {
			char *e;
			long score, ping;
			char *q;
			size_t len;
			score = strtol( line, &e, 10 );
			if ( e == line )
				continue;
			q = e;
			ping = strtol( q, &e, 10 );
			if ( e == q )
				continue;
			while ( *e == ' ' || *e == '\t' )
				e++;
			if ( *e != '"' )
				continue;
			q = e + 1;
			/* trim '"' and '\r' from both ends */
			while ( *q == '"' || *q == '\r' )
				q++;
			len = strlen( q );
			while ( len && ( q[len - 1] == '"' || q[len - 1] == '\r' ) )
				q[--len] = 0;
			out->players[out->playerCount].score = (int)score;
			out->players[out->playerCount].ping = (int)ping;
			lc_strlcpy( out->players[out->playerCount].name, q, sizeof( out->players[0].name ) );
			lc_utf8_truncate( out->players[out->playerCount].name, sizeof( out->players[0].name ) - 1 );
			out->playerCount++;
		}
	}
	free( text );
	out->when = lc_time( );
	return 1;
}

/* ---- master queries (7.1) ---------------------------------------------------------------- */

static int split_master( const char *m, char *host, size_t hs, char *port, size_t ps )
{
	const char *colon;
	size_t n;

	lc_strlcpy( port, "27953", ps );
	if ( m[0] == '[' ) {
		const char *rb = strchr( m, ']' );
		if ( !rb )
			return 0;
		n = (size_t)( rb - m - 1 );
		if ( n >= hs )
			return 0;
		memcpy( host, m + 1, n );
		host[n] = 0;
		if ( rb[1] == ':' && rb[2] )
			lc_strlcpy( port, rb + 2, ps );
		return 1;
	}
	colon = strchr( m, ':' );
	if ( colon && !strchr( colon + 1, ':' ) ) {
		n = (size_t)( colon - m );
		if ( n >= hs )
			return 0;
		memcpy( host, m, n );
		host[n] = 0;
		if ( colon[1] )
			lc_strlcpy( port, colon + 1, ps );
		return 1;
	}
	lc_strlcpy( host, m, hs );
	return 1;
}

int lc_query_master( const char *master, int timeoutMs, void (*entry)( lc_addr4 a, void *u ), void *u )
{
	char host[256], port[16];
	struct addrinfo hints, *res = NULL;
	lc_sock s;
	unsigned char *buf, req[4 + 24];
	int packets = 0;
	size_t reqLen;

	lc_net_init( );
	if ( !master || !split_master( master, host, sizeof( host ), port, sizeof( port ) ) || !host[0] )
		return -1;
	memset( &hints, 0, sizeof( hints ) );
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	if ( getaddrinfo( host, port, &hints, &res ) != 0 || !res )
		return -1;
	s = socket( res->ai_family, SOCK_DGRAM, 0 );
	if ( s == LC_BADSOCK ) {
		freeaddrinfo( res );
		return -1;
	}
	memcpy( req, OOB, 4 );
	memcpy( req + 4, "getservers 24 full empty", 24 );
	reqLen = 28;
	buf = (unsigned char *)malloc( LC_BUF );
	if ( buf && sendto( s, (const char *)req, (int)reqLen, 0, res->ai_addr, (int)res->ai_addrlen ) >= 0 ) {
		for ( ;; ) {
			int r = lc_sock_wait_readable( s, timeoutMs );
			int got;
			if ( r <= 0 )
				break;
			got = (int)recv( s, (char *)buf, LC_BUF, 0 );
			if ( got <= 0 )
				break;
			packets++;
			lc_parse_server_list( buf, (size_t)got, entry, u );
		}
	}
	free( buf );
	lc_sock_close( s );
	freeaddrinfo( res );
	return packets ? packets : -1;
}

typedef struct { unsigned long long *v; int n, cap; } addr_set;

static void addr_set_add( lc_addr4 a, void *u )
{
	addr_set *s = (addr_set *)u;
	unsigned long long key = (unsigned long long)a.ip[0] << 40 | (unsigned long long)a.ip[1] << 32 | (unsigned long long)a.ip[2] << 24 |
		(unsigned long long)a.ip[3] << 16 | a.port;
	int i;
	for ( i = 0; i < s->n; i++ )
		if ( s->v[i] == key )
			return;
	if ( s->n == s->cap ) {
		s->cap = s->cap ? s->cap * 2 : 128;
		s->v = (unsigned long long *)realloc( s->v, sizeof( *s->v ) * (size_t)s->cap );
	}
	s->v[s->n++] = key;
}

int lc_query_master_count( const char *master )
{
	addr_set s = { NULL, 0, 0 };
	int r = lc_query_master( master, 2000, addr_set_add, &s );
	int n = r < 0 ? -1 : s.n;
	free( s.v );
	return n;
}

/* ---- batch query (7.4) ---------------------------------------------------------------------- */

typedef struct {
	unsigned ip;				/* host order */
	unsigned short port;
	double sentAt;
	int awaitingInfo;
	int statusOut, statusDone;
} qentry;

static void make_key( const struct sockaddr_in *a, unsigned *ip, unsigned short *port )
{
	*ip = ntohl( a->sin_addr.s_addr );
	*port = ntohs( a->sin_port );
}

void lc_query_servers( const char *const *addresses, int n, int broadcast, const volatile int *cancel, const lc_query_callbacks *cb )
{
	lc_sock s;
	qentry *ents = NULL;
	int nEnts = 0, asked = 0, answered = 0, i, sends = 0, statusOut = 0;
	double start, deadline;
	unsigned char *buf;
	struct sockaddr_in to;
	static const char getinfo[] = "getinfo browse", getstatus[] = "getstatus";
	unsigned char pkt[4 + sizeof( getinfo )];

#define CANCELLED() ( cancel && *cancel )
	lc_net_init( );
	s = socket( AF_INET, SOCK_DGRAM, 0 );
	if ( s == LC_BADSOCK ) {
		if ( !CANCELLED() && cb->done )
			cb->done( 0, cb->user );
		return;
	}
	buf = (unsigned char *)malloc( LC_BUF );
	ents = (qentry *)calloc( (size_t)( n > 0 ? n : 1 ), sizeof( qentry ) );
	memcpy( pkt, OOB, 4 );
	memcpy( pkt + 4, getinfo, sizeof( getinfo ) - 1 );
	start = lc_now( );
	if ( broadcast ) {
		int on = 1, p;
		setsockopt( s, SOL_SOCKET, SO_BROADCAST, (const char *)&on, sizeof( on ) );
		for ( p = 0; p < 4; p++ ) {
			memset( &to, 0, sizeof( to ) );
			to.sin_family = AF_INET;
			to.sin_port = htons( (unsigned short)( LC_PORT_SERVER + p ) );
			to.sin_addr.s_addr = htonl( 0xffffffffu );
			sendto( s, (const char *)pkt, (int)( 4 + sizeof( getinfo ) - 1 ), 0, (struct sockaddr *)&to, sizeof( to ) );
		}
	}
	for ( i = 0; i < n && ents; i++ ) {
		if ( CANCELLED() )
			break;
		if ( !lc_parse_address4( addresses[i], LC_PORT_SERVER, &to ) )
			continue;
		make_key( &to, &ents[nEnts].ip, &ents[nEnts].port );
		ents[nEnts].sentAt = lc_now( );
		ents[nEnts].awaitingInfo = 1;
		nEnts++;
		asked++;
		sendto( s, (const char *)pkt, (int)( 4 + sizeof( getinfo ) - 1 ), 0, (struct sockaddr *)&to, sizeof( to ) );
		if ( ++sends % 32 == 0 )
			lc_sleep_ms( 2 );
	}
	deadline = lc_now( ) + ( broadcast ? 1.5 : 2.5 );

	while ( buf && !CANCELLED() && lc_now( ) < deadline + ( statusOut > 0 ? 1.0 : 0 ) &&
			( broadcast || answered < asked || statusOut > 0 ) ) {
		struct sockaddr_in from;
		socklen_t fl = sizeof( from );
		int got, k, found = -1;
		unsigned ip;
		unsigned short port;
		char key[32];
		lc_server_info info;
		lc_server_status st;

		if ( lc_sock_wait_readable( s, 100 ) <= 0 )
			continue;
		got = (int)recvfrom( s, (char *)buf, LC_BUF, 0, (struct sockaddr *)&from, &fl );
		if ( got <= 0 )
			continue;
		make_key( &from, &ip, &port );
		lc_sock_addr_string( &from, key, sizeof( key ) );
		for ( k = 0; k < nEnts; k++ )
			if ( ents[k].ip == ip && ents[k].port == port ) {
				found = k;
				break;
			}
		/* a status answer we asked for */
		if ( found >= 0 && ents[found].statusOut && lc_parse_status_response( buf, (size_t)got, &st ) ) {
			ents[found].statusOut = 0;
			ents[found].statusDone = 1;
			statusOut--;
			if ( !CANCELLED() && cb->got_status )
				cb->got_status( key, &st, cb->user );
			continue;
		}
		if ( !lc_parse_info_response( buf, (size_t)got, key, &info ) )
			continue;
		if ( found >= 0 && ents[found].awaitingInfo ) {
			info.ping = (int)( ( lc_now( ) - ents[found].sentAt ) * 1000 );
			ents[found].awaitingInfo = 0;
			answered++;
		} else if ( broadcast ) {
			info.ping = (int)( ( lc_now( ) - start ) * 1000 );
			answered++;
		} else
			continue;
		if ( info.ping < 0 )
			info.ping = 0;
		if ( !CANCELLED() && cb->got_info )
			cb->got_info( &info, cb->user );
		/* ask for the status once per address, from the same socket */
		if ( found < 0 ) {
			/* broadcast answer from an address we never sent to: track it */
			qentry *ne = (qentry *)realloc( ents, sizeof( qentry ) * (size_t)( nEnts + 1 ) );
			if ( ne ) {
				ents = ne;
				memset( &ents[nEnts], 0, sizeof( qentry ) );
				ents[nEnts].ip = ip;
				ents[nEnts].port = port;
				found = nEnts++;
			}
		}
		if ( found >= 0 && !ents[found].statusOut ) {
			unsigned char sp[4 + sizeof( getstatus )];
			memcpy( sp, OOB, 4 );
			memcpy( sp + 4, getstatus, sizeof( getstatus ) - 1 );
			ents[found].statusOut = 1;
			statusOut++;
			sendto( s, (const char *)sp, (int)( 4 + sizeof( getstatus ) - 1 ), 0, (struct sockaddr *)&from, sizeof( from ) );
		}
	}
	lc_sock_close( s );
	free( buf );
	free( ents );
	if ( !CANCELLED() && cb->done )
		cb->done( answered, cb->user );
#undef CANCELLED
}

int lc_query_status( const char *address, lc_server_status *out )
{
	struct sockaddr_in to;
	lc_sock s;
	unsigned char req[4 + 9], *buf;
	int tries, ok = 0;

	lc_net_init( );
	if ( !lc_parse_address4( address, LC_PORT_SERVER, &to ) )
		return 0;
	s = socket( AF_INET, SOCK_DGRAM, 0 );
	if ( s == LC_BADSOCK )
		return 0;
	buf = (unsigned char *)malloc( LC_BUF );
	memcpy( req, OOB, 4 );
	memcpy( req + 4, "getstatus", 9 );
	for ( tries = 0; tries < 2 && !ok && buf; tries++ ) {
		double until;
		sendto( s, (const char *)req, (int)sizeof( req ), 0, (struct sockaddr *)&to, sizeof( to ) );
		until = lc_now( ) + 1.0;
		while ( lc_now( ) < until && !ok ) {
			int got;
			if ( lc_sock_wait_readable( s, 100 ) <= 0 )
				continue;
			got = (int)recv( s, (char *)buf, LC_BUF, 0 );
			if ( got > 0 && lc_parse_status_response( buf, (size_t)got, out ) )
				ok = 1;
		}
	}
	free( buf );
	lc_sock_close( s );
	return ok;
}

/* ---- map state, players ------------------------------------------------------------------------ */

int lc_map_state( const lc_map_catalog *c, const char *map )
{
	char l[64];
	int i, n;

	if ( !map || !*map )
		return LC_MAP_HAVE;
	lc_strlcpy( l, map, sizeof( l ) );
	lc_lower_str( l );
	n = lc_map_catalog_count( c );
	for ( i = 0; i < n; i++ )
		if ( !lc_stricmp( lc_map_catalog_at( c, i )->name, l ) )
			return LC_MAP_HAVE;
	return lc_is_expansion_map( l ) ? LC_MAP_EXPANSION : LC_MAP_MISSING;
}

int lc_map_is_builtin( const lc_map_catalog *c, const char *map )
{
	const lc_map_info *m;
	if ( !map || !*map )
		return 1;
	if ( lc_is_expansion_map( map ) )
		return 1;
	m = lc_map_find( c, map );
	return m && ( m->retail || m->expansion );
}

void lc_sort_players( lc_player *p, int n )
{
	int i, j;
	/* stable insertion sort: ping > 0 before ping 0, then score descending */
	for ( i = 1; i < n; i++ ) {
		lc_player t = p[i];
		int tp = t.ping > 0;
		for ( j = i - 1; j >= 0; j-- ) {
			int jp = p[j].ping > 0;
			if ( jp > tp || ( jp == tp && p[j].score >= t.score ) )
				break;
			p[j + 1] = p[j];
		}
		p[j + 1] = t;
	}
}

int lc_tag_bots( const lc_player *p, int n, int humans )
{
	int i, pinged = 0;
	for ( i = 0; i < n; i++ )
		if ( p[i].ping > 0 )
			pinged++;
	return humans < 0 || pinged == humans;
}

/* ---- efservers (7.8) ------------------------------------------------------------------------------ */

static void pct_encode( const char *in, char *out, size_t n )
{
	size_t o = 0;
	for ( ; *in && o + 4 < n; in++ ) {
		if ( ( *in >= 'a' && *in <= 'z' ) || ( *in >= 'A' && *in <= 'Z' ) || ( *in >= '0' && *in <= '9' ) )
			out[o++] = *in;
		else
			o += (size_t)lc_snprintf( out + o, n - o, "%%%02X", (unsigned char)*in );
	}
	out[o < n ? o : n - 1] = 0;
}

void lc_efservers_info_url( const char *map, char *out, size_t n )
{
	char e[256];
	pct_encode( map, e, sizeof( e ) );
	lc_snprintf( out, n, "https://efservers.com/pk3.php?preview&popup=1&map=%s", e );
}

void lc_efservers_3d_lookup_url( const char *map, char *out, size_t n )
{
	char e[256];
	pct_encode( map, e, sizeof( e ) );
	lc_snprintf( out, n, "https://efservers.com/getPK3info.php?pk3=%s&preview", e );
}

static int url_char( int c )
{
	return isalnum( c ) || c == '_' || c == '.' || c == '+' || c == '-';
}

int lc_efservers_parse_3d( const char *html, char *url, size_t n )
{
	static const char needle[] = "bsp/index.php?pk3=";
	const char *p = html;

	if ( n )
		url[0] = 0;
	while ( ( p = strstr( p, needle ) ) != NULL ) {
		const char *g1 = p + sizeof( needle ) - 1, *q = g1, *g2, *runEnd, *last = NULL, *r;
		char pk3[256], file[256];
		size_t l1;

		while ( url_char( (unsigned char)*q ) )
			q++;
		l1 = (size_t)( q - g1 );
		if ( !l1 || *q != '&' ) {
			p++;
			continue;
		}
		q++;
		if ( !strncmp( q, "amp;", 4 ) )
			q += 4;
		if ( strncmp( q, "file=maps/", 10 ) ) {
			p++;
			continue;
		}
		g2 = q + 5;
		runEnd = q + 10;
		while ( url_char( (unsigned char)*runEnd ) )
			runEnd++;
		/* [A-Za-z0-9_.+-]+\.bsp: the last ".bsp" inside the run, with at least one char before it */
		for ( r = q + 11; r + 4 <= runEnd; r++ )
			if ( !strncmp( r, ".bsp", 4 ) )
				last = r;
		if ( !last ) {
			p++;
			continue;
		}
		if ( l1 >= sizeof( pk3 ) || (size_t)( last + 4 - g2 ) >= sizeof( file ) ) {
			p++;
			continue;
		}
		memcpy( pk3, g1, l1 );
		pk3[l1] = 0;
		memcpy( file, g2, (size_t)( last + 4 - g2 ) );
		file[last + 4 - g2] = 0;
		lc_snprintf( url, n, "https://efservers.com/bsp/index.php?pk3=%s&file=%s", pk3, file );
		return 1;
	}
	return 0;
}

/* ---- sorting ---------------------------------------------------------------------------------------- */

void lc_browser_default_sort( lc_sort_key keys[4] )
{
	keys[0].column = LC_COL_FAV;		keys[0].ascending = 0;
	keys[1].column = LC_COL_HUMANS;		keys[1].ascending = 0;
	keys[2].column = LC_COL_PLAYERS;	keys[2].ascending = 0;
	keys[3].column = LC_COL_PING;		keys[3].ascending = 1;
}

static int column_default_ascending( lc_column c )
{
	switch ( c ) {
	case LC_COL_NAME: case LC_COL_MAP: case LC_COL_TYPE: case LC_COL_PING:
		return 1;
	default:
		return 0;
	}
}

int lc_sort_click( lc_sort_key *keys, int nKeys, int max, lc_column col )
{
	lc_sort_key tmp[32];
	int i, o = 0, wasFirst = nKeys > 0 && keys[0].column == col;

	tmp[o].column = col;
	tmp[o].ascending = wasFirst ? !keys[0].ascending : column_default_ascending( col );
	o++;
	for ( i = 0; i < nKeys && o < max && o < 32; i++ )
		if ( keys[i].column != col )
			tmp[o++] = keys[i];
	memcpy( keys, tmp, sizeof( lc_sort_key ) * (size_t)o );
	return o;
}

typedef struct { const lc_browser_row *row; int index; const lc_sort_key *keys; int nKeys; } sort_item;

static int cmp_int( int a, int b ) { return a < b ? -1 : a > b ? 1 : 0; }

static int row_compare( const lc_browser_row *a, const lc_browser_row *b, lc_column col )
{
	switch ( col ) {
	case LC_COL_FAV:	return cmp_int( a->favorite, b->favorite );
	case LC_COL_NAME:	return lc_stricmp( a->info.name, b->info.name );
	case LC_COL_LOCK:	return cmp_int( a->info.needPass, b->info.needPass );
	case LC_COL_MAP:	return lc_stricmp( a->info.map, b->info.map );
	case LC_COL_PLAYERS: return cmp_int( a->info.players, b->info.players );
	case LC_COL_HUMANS:	return cmp_int( a->info.humans < 0 ? 0 : a->info.humans, b->info.humans < 0 ? 0 : b->info.humans );
	case LC_COL_TYPE:	return cmp_int( a->info.gametype, b->info.gametype );
	case LC_COL_CMOD: {
		double x = lc_cmod_rank( a->isCmod, a->cmodVersion ), y = lc_cmod_rank( b->isCmod, b->cmodVersion );
		return x < y ? -1 : x > y ? 1 : 0;
	}
	case LC_COL_PING:	return cmp_int( a->info.ping, b->info.ping );
	}
	return 0;
}

static int sort_cmp( const void *pa, const void *pb )
{
	const sort_item *a = (const sort_item *)pa, *b = (const sort_item *)pb;
	int k;
	for ( k = 0; k < a->nKeys; k++ ) {
		int c = row_compare( a->row, b->row, a->keys[k].column );
		if ( c )
			return a->keys[k].ascending ? c : -c;
	}
	return a->index - b->index;		/* stable */
}

/* ---- the browser model (8) --------------------------------------------------------------------------------- */

#define SRC_COUNT ( 2 + LC_MAX_MASTERS )

typedef struct lc_fetch {
	struct lc_browser *b;
	int source;
	volatile int cancel;
	char publicIP[32];
	void (*changed)( int source, void *u );
	void *user;
	lc_thread *thr;
	volatile int done;
	struct lc_fetch *next;
	/* per-fetch merge data */
	char (*addrs)[24];
	int nAddrs, answeredMasters, nMastersAsked, answeredInfos;
	char (*listed)[LC_MAX_MASTERS][16];
	int *listedCount;
	char (*thisComputer);
} lc_fetch;

typedef struct {
	lc_browser_row *rows;
	int n, cap;
	char status[320];
	int fetched, busy;
	lc_fetch *cur;
} lc_source;

typedef struct { char address[24]; lc_server_status st; } status_entry;

struct lc_browser {
	lc_mutex *mu;
	char masters[LC_MAX_MASTERS][65];
	int nMasters;
	const lc_map_catalog *cat;
	int loopbackFix;
	lc_source src[SRC_COUNT];
	status_entry *statuses;
	int nStatuses, capStatuses;
	lc_fetch *fetches;
	lc_browser_row *snap;
	lc_browser_row *sorted;
	int snapCap;
	char statusCopy[320];
};

lc_browser *lc_browser_new( const char (*masters)[65], int n, const lc_map_catalog *c )
{
	lc_browser *b = (lc_browser *)calloc( 1, sizeof( *b ) );
	int i;

	if ( !b )
		return NULL;
	b->mu = lc_mutex_new( );
	if ( n > LC_MAX_MASTERS )
		n = LC_MAX_MASTERS;
	for ( i = 0; i < n; i++ )
		lc_strlcpy( b->masters[i], masters[i], sizeof( b->masters[i] ) );
	b->nMasters = n < 0 ? 0 : n;
	b->cat = c;
	b->loopbackFix = 1;
	return b;
}

void lc_browser_set_loopback_fix( lc_browser *b, int on )
{
	b->loopbackFix = on;
}

int lc_browser_source_count( lc_browser *b )
{
	return 1 + ( b->nMasters > 0 ? 1 : 0 ) + b->nMasters;
}

void lc_browser_source_label( lc_browser *b, int source, char *out, size_t n )
{
	if ( source == 0 )
		lc_strlcpy( out, "Local", n );
	else if ( source == 1 && b->nMasters > 0 )
		lc_strlcpy( out, "Combined", n );
	else if ( b->nMasters > 7 )
		lc_snprintf( out, n, "%d", source - 1 );
	else
		lc_snprintf( out, n, "Internet %d", source - 1 );
}

static void notify( lc_fetch *f )
{
	if ( f->changed )
		f->changed( f->source, f->user );
}

static void set_status( lc_browser *b, int source, const char *fmt, ... )
#if defined( __GNUC__ )
	__attribute__ (( format( printf, 3, 4 ) ))
#endif
	;
static void set_status( lc_browser *b, int source, const char *fmt, ... )
{
	va_list ap;
	va_start( ap, fmt );
	vsnprintf( b->src[source].status, sizeof( b->src[source].status ), fmt, ap );
	va_end( ap );
}

typedef struct { lc_addr4 *a; int n, cap; } entry_list;

static void entry_list_add( lc_addr4 a, void *u )
{
	entry_list *l = (entry_list *)u;
	if ( l->n == l->cap ) {
		l->cap = l->cap ? l->cap * 2 : 128;
		l->a = (lc_addr4 *)realloc( l->a, sizeof( lc_addr4 ) * (size_t)l->cap );
	}
	l->a[l->n++] = a;
}

typedef struct { char master[65]; entry_list list; int answered; lc_thread *thr; } master_job;

static void master_job_run( void *u )
{
	master_job *j = (master_job *)u;
	j->answered = lc_query_master( j->master, 2000, entry_list_add, &j->list ) >= 0;
}

static void on_info( const lc_server_info *info, void *u )
{
	lc_fetch *f = (lc_fetch *)u;
	lc_browser *b = f->b;
	lc_source *s = &b->src[f->source];
	lc_browser_row *r;
	int i;

	lc_mutex_lock( b->mu );
	if ( f->cancel ) {
		lc_mutex_unlock( b->mu );
		return;
	}
	if ( s->n == s->cap ) {
		s->cap = s->cap ? s->cap * 2 : 64;
		s->rows = (lc_browser_row *)realloc( s->rows, sizeof( lc_browser_row ) * (size_t)s->cap );
	}
	r = &s->rows[s->n++];
	memset( r, 0, sizeof( *r ) );
	r->info = *info;
	for ( i = 0; i < f->nAddrs; i++ )
		if ( !strcmp( f->addrs[i], info->address ) ) {
			r->thisComputer = f->thisComputer ? f->thisComputer[i] : 0;
			if ( f->source == 1 ) {
				r->listedOnCount = f->listedCount[i];
				memcpy( r->listedOn, f->listed[i], sizeof( r->listedOn ) );
			}
			break;
		}
	lc_mutex_unlock( b->mu );
	notify( f );
}

static void store_status_locked( lc_browser *b, const char *address, const lc_server_status *st )
{
	int i, k;
	for ( i = 0; i < b->nStatuses; i++ )
		if ( !strcmp( b->statuses[i].address, address ) )
			break;
	if ( i == b->nStatuses ) {
		if ( b->nStatuses == b->capStatuses ) {
			b->capStatuses = b->capStatuses ? b->capStatuses * 2 : 64;
			b->statuses = (status_entry *)realloc( b->statuses, sizeof( status_entry ) * (size_t)b->capStatuses );
		}
		b->nStatuses++;
	}
	lc_strlcpy( b->statuses[i].address, address, sizeof( b->statuses[i].address ) );
	b->statuses[i].st = *st;
	for ( k = 0; k < SRC_COUNT; k++ ) {
		lc_source *s = &b->src[k];
		int r;
		for ( r = 0; r < s->n; r++ )
			if ( !strcmp( s->rows[r].info.address, address ) ) {
				s->rows[r].isCmod = st->isCmod;
				lc_strlcpy( s->rows[r].cmodVersion, st->cmodVersion, sizeof( s->rows[r].cmodVersion ) );
			}
	}
}

static void on_status( const char *address, const lc_server_status *st, void *u )
{
	lc_fetch *f = (lc_fetch *)u;
	lc_browser *b = f->b;

	lc_mutex_lock( b->mu );
	if ( f->cancel ) {
		lc_mutex_unlock( b->mu );
		return;
	}
	store_status_locked( b, address, st );
	lc_mutex_unlock( b->mu );
	notify( f );
}

static void on_done( int answered, void *u )
{
	lc_fetch *f = (lc_fetch *)u;
	f->answeredInfos = answered;
}

static const char *plural( int n, const char *one, const char *many )
{
	return n == 1 ? one : many;
}

static void fetch_worker( void *u )
{
	lc_fetch *f = (lc_fetch *)u;
	lc_browser *b = f->b;
	int source = f->source;
	int firstMaster = 0, lastMaster = -1, i, j, nJobs = 0;
	master_job *jobs = NULL;
	char (*addrs)[24] = NULL;
	char *thisComputer = NULL;
	char (*listed)[LC_MAX_MASTERS][16] = NULL;
	int *listedCount = NULL;
	int nAddrs = 0, capAddrs = 0, answeredMasters = 0;
	char own[32] = "";
	lc_query_callbacks cb;
	const char **ptrs = NULL;
	char servers[64], quiet[64];

	if ( source == 1 ) {
		firstMaster = 0;
		lastMaster = b->nMasters - 1;
	} else if ( source >= 2 ) {
		firstMaster = lastMaster = source - 2;
	}
	nJobs = lastMaster - firstMaster + 1;
	if ( nJobs > 0 ) {
		jobs = (master_job *)calloc( (size_t)nJobs, sizeof( master_job ) );
		for ( i = 0; i < nJobs; i++ ) {
			lc_strlcpy( jobs[i].master, b->masters[firstMaster + i], sizeof( jobs[i].master ) );
			jobs[i].thr = lc_thread_start( master_job_run, &jobs[i] );
			if ( !jobs[i].thr )
				master_job_run( &jobs[i] );
		}
		for ( i = 0; i < nJobs; i++ )
			if ( jobs[i].thr )
				lc_thread_join( jobs[i].thr );
		/* Loopback Host Fix: wait up to 1 s for the public address */
		if ( b->loopbackFix ) {
			if ( f->publicIP[0] )
				lc_strlcpy( own, f->publicIP, sizeof( own ) );
			else {
				for ( i = 0; i < 20 && !f->cancel && !lc_public_address( ) && lc_public_address_pending( ); i++ )
					lc_sleep_ms( 50 );
				if ( lc_public_address( ) )
					lc_strlcpy( own, lc_public_address( ), sizeof( own ) );
			}
		}
		if ( f->cancel )
			goto finish;
		for ( i = 0; i < nJobs; i++ ) {
			if ( !jobs[i].answered )
				continue;
			answeredMasters++;
			for ( j = 0; j < jobs[i].list.n; j++ ) {
				lc_addr4 a = jobs[i].list.a[j];
				char addr[24], label[16];
				int k, mine = 0;
				lc_snprintf( addr, sizeof( addr ), "%u.%u.%u.%u:%u", a.ip[0], a.ip[1], a.ip[2], a.ip[3], a.port );
				if ( own[0] ) {
					char pre[40];
					lc_snprintf( pre, sizeof( pre ), "%u.%u.%u.%u", a.ip[0], a.ip[1], a.ip[2], a.ip[3] );
					if ( !strcmp( pre, own ) ) {
						lc_snprintf( addr, sizeof( addr ), "127.0.0.1:%u", a.port );
						mine = 1;
					}
				}
				for ( k = 0; k < nAddrs; k++ )
					if ( !strcmp( addrs[k], addr ) )
						break;
				if ( k == nAddrs ) {
					if ( nAddrs == capAddrs ) {
						capAddrs = capAddrs ? capAddrs * 2 : 256;
						addrs = realloc( addrs, sizeof( *addrs ) * (size_t)capAddrs );
						thisComputer = (char *)realloc( thisComputer, (size_t)capAddrs );
						listed = realloc( listed, sizeof( *listed ) * (size_t)capAddrs );
						listedCount = (int *)realloc( listedCount, sizeof( int ) * (size_t)capAddrs );
					}
					lc_strlcpy( addrs[k], addr, sizeof( addrs[k] ) );
					thisComputer[k] = (char)mine;
					listedCount[k] = 0;
					nAddrs++;
				}
				lc_snprintf( label, sizeof( label ), "Internet %d", firstMaster + i + 1 );
				if ( listedCount[k] < LC_MAX_MASTERS )
					lc_strlcpy( listed[k][listedCount[k]++], label, sizeof( listed[k][0] ) );
			}
		}
		lc_mutex_lock( b->mu );
		if ( f->cancel ) {
			lc_mutex_unlock( b->mu );
			goto finish;
		}
		if ( !answeredMasters ) {
			b->src[source].busy = 0;
			if ( source == 1 )
				set_status( b, source, "None of the master servers answered. Check your internet connection, or change the list in Servers\xe2\x80\xa6." );
			else
				set_status( b, source, "%s didn't answer. It may be offline: try another source, or change the list in Servers\xe2\x80\xa6.", b->masters[firstMaster] );
			lc_mutex_unlock( b->mu );
			lc_log( "Browser: %s", b->src[source].status );
			notify( f );
			goto finish;
		}
		if ( source == 1 )
			set_status( b, source, "%d of %d master servers list %d %s, asking each\xe2\x80\xa6", answeredMasters, b->nMasters, nAddrs, plural( nAddrs, "server", "servers" ) );
		else
			set_status( b, source, "%s lists %d %s, asking each\xe2\x80\xa6", b->masters[firstMaster], nAddrs, plural( nAddrs, "server", "servers" ) );
		lc_mutex_unlock( b->mu );
		notify( f );
	}

	f->addrs = addrs;
	f->nAddrs = nAddrs;
	f->thisComputer = thisComputer;
	f->listed = listed;
	f->listedCount = listedCount;
	ptrs = (const char **)malloc( sizeof( char * ) * (size_t)( nAddrs ? nAddrs : 1 ) );
	for ( i = 0; i < nAddrs; i++ )
		ptrs[i] = addrs[i];
	memset( &cb, 0, sizeof( cb ) );
	cb.got_info = on_info;
	cb.got_status = on_status;
	cb.done = on_done;
	cb.user = f;
	lc_query_servers( ptrs, nAddrs, source == 0, &f->cancel, &cb );

	lc_mutex_lock( b->mu );
	if ( !f->cancel ) {
		int n = b->src[source].n, count = nAddrs;
		lc_snprintf( servers, sizeof( servers ), "%d %s", n, plural( n, "server", "servers" ) );
		quiet[0] = 0;
		if ( source != 0 && count - f->answeredInfos > 0 )
			lc_snprintf( quiet, sizeof( quiet ), " \xc2\xb7 %d listed didn't answer", count - f->answeredInfos );
		if ( source == 0 ) {
			if ( n > 0 )
				set_status( b, source, "Your network \xc2\xb7 %s", servers );
			else
				set_status( b, source, "No servers on your network (Holomatch looks on ports 27960 to 27963). Type an address below to join another." );
		} else if ( source == 1 ) {
			if ( count == 0 )
				set_status( b, source, "The master servers list no servers right now." );
			else
				set_status( b, source, "%s from %d of %d master servers%s", servers, answeredMasters, b->nMasters, quiet );
		} else {
			if ( count == 0 )
				set_status( b, source, "%s lists no servers right now.", b->masters[firstMaster] );
			else
				set_status( b, source, "%s \xc2\xb7 %s%s", b->masters[firstMaster], servers, quiet );
		}
		b->src[source].busy = 0;
		lc_mutex_unlock( b->mu );
		lc_log( "Browser: %s", b->src[source].status );
		notify( f );
	} else
		lc_mutex_unlock( b->mu );

finish:
	if ( jobs ) {
		for ( i = 0; i < nJobs; i++ )
			free( jobs[i].list.a );
		free( jobs );
	}
	free( (void *)ptrs );
	f->addrs = NULL;
	f->thisComputer = NULL;
	f->listed = NULL;
	f->listedCount = NULL;
	free( addrs );
	free( thisComputer );
	free( listed );
	free( listedCount );
	f->done = 1;
}

static void reap_fetches( lc_browser *b, int all )
{
	lc_fetch **pp = &b->fetches;
	while ( *pp ) {
		lc_fetch *f = *pp;
		if ( all || f->done ) {
			if ( all )
				f->cancel = 1;
			*pp = f->next;
			if ( f->thr )
				lc_thread_join( f->thr );
			free( f );
		} else
			pp = &f->next;
	}
}

void lc_browser_fetch( lc_browser *b, int source, const char *publicIP, void (*changed)( int source, void *u ), void *u )
{
	lc_fetch *f;
	lc_source *s;

	if ( source < 0 || source >= lc_browser_source_count( b ) )
		return;
	lc_mutex_lock( b->mu );
	reap_fetches( b, 0 );
	s = &b->src[source];
	if ( s->cur )
		s->cur->cancel = 1;
	s->n = 0;
	s->fetched = 1;
	s->busy = 1;
	if ( source == 0 )
		set_status( b, 0, "Looking on your network\xe2\x80\xa6" );
	else if ( source == 1 )
		set_status( b, 1, "Asking %d master servers for their servers\xe2\x80\xa6", b->nMasters );
	else
		set_status( b, source, "Asking %s for its servers\xe2\x80\xa6", b->masters[source - 2] );
	f = (lc_fetch *)calloc( 1, sizeof( *f ) );
	f->b = b;
	f->source = source;
	f->changed = changed;
	f->user = u;
	if ( publicIP )
		lc_strlcpy( f->publicIP, publicIP, sizeof( f->publicIP ) );
	s->cur = f;
	f->next = b->fetches;
	b->fetches = f;
	f->thr = lc_thread_start( fetch_worker, f );
	if ( !f->thr ) {
		s->busy = 0;
		set_status( b, source, "Couldn't start the search." );
		f->done = 1;
	}
	lc_mutex_unlock( b->mu );
	if ( changed )
		changed( source, u );
}

void lc_browser_cancel_all( lc_browser *b )
{
	int i;
	lc_mutex_lock( b->mu );
	for ( i = 0; i < SRC_COUNT; i++ ) {
		if ( b->src[i].cur )
			b->src[i].cur->cancel = 1;
		b->src[i].busy = 0;
	}
	lc_mutex_unlock( b->mu );
}

void lc_browser_free( lc_browser *b )
{
	int i;
	if ( !b )
		return;
	lc_browser_cancel_all( b );
	/* join without holding the mutex: workers take it */
	reap_fetches( b, 1 );
	for ( i = 0; i < SRC_COUNT; i++ )
		free( b->src[i].rows );
	free( b->statuses );
	free( b->snap );
	free( b->sorted );
	lc_mutex_free( b->mu );
	free( b );
}

int lc_browser_is_busy( lc_browser *b, int source )
{
	int r;
	if ( source < 0 || source >= SRC_COUNT )
		return 0;
	lc_mutex_lock( b->mu );
	r = b->src[source].busy;
	lc_mutex_unlock( b->mu );
	return r;
}

int lc_browser_fetched( lc_browser *b, int source )
{
	int r;
	if ( source < 0 || source >= SRC_COUNT )
		return 0;
	lc_mutex_lock( b->mu );
	r = b->src[source].fetched;
	lc_mutex_unlock( b->mu );
	return r;
}

int lc_browser_rows( lc_browser *b, int source, int onlyFav, int onlyPeople, const char *const *favorites, int nFav,
	const lc_sort_key *keys, int nKeys, const lc_browser_row **out )
{
	lc_source *s;
	int i, k, n = 0;
	sort_item *items;

	*out = NULL;
	if ( source < 0 || source >= SRC_COUNT )
		return 0;
	lc_mutex_lock( b->mu );
	s = &b->src[source];
	if ( s->n > b->snapCap ) {
		b->snapCap = s->n + 64;
		b->snap = (lc_browser_row *)realloc( b->snap, sizeof( lc_browser_row ) * (size_t)b->snapCap );
		b->sorted = (lc_browser_row *)realloc( b->sorted, sizeof( lc_browser_row ) * (size_t)b->snapCap );
	}
	items = (sort_item *)malloc( sizeof( sort_item ) * (size_t)( s->n ? s->n : 1 ) );
	for ( i = 0; i < s->n; i++ ) {
		lc_browser_row *r = &b->snap[n];
		*r = s->rows[i];
		r->favorite = 0;
		for ( k = 0; k < nFav; k++ )
			if ( !strcmp( favorites[k], r->info.address ) ) {
				r->favorite = 1;
				break;
			}
		if ( onlyFav && !r->favorite )
			continue;
		if ( onlyPeople && !( r->info.humans > 0 || ( r->info.humans < 0 && r->info.players > 0 ) ) )
			continue;
		items[n].row = r;
		items[n].index = n;
		items[n].keys = keys;
		items[n].nKeys = nKeys;
		n++;
	}
	if ( keys && nKeys > 0 && n > 1 )
		qsort( items, (size_t)n, sizeof( sort_item ), sort_cmp );
	for ( i = 0; i < n; i++ )
		b->sorted[i] = *items[i].row;
	free( items );
	lc_mutex_unlock( b->mu );
	*out = b->sorted;
	return n;
}

const char *lc_browser_status_text( lc_browser *b, int source )
{
	if ( source < 0 || source >= SRC_COUNT )
		return "";
	lc_mutex_lock( b->mu );
	lc_strlcpy( b->statusCopy, b->src[source].status, sizeof( b->statusCopy ) );
	lc_mutex_unlock( b->mu );
	return b->statusCopy;
}

int lc_browser_get_status( lc_browser *b, const char *address, lc_server_status *out )
{
	int i, ok = 0;
	lc_mutex_lock( b->mu );
	for ( i = 0; i < b->nStatuses; i++ )
		if ( !strcmp( b->statuses[i].address, address ) ) {
			*out = b->statuses[i].st;
			ok = 1;
			break;
		}
	lc_mutex_unlock( b->mu );
	return ok;
}

void lc_browser_store_status( lc_browser *b, const char *address, const lc_server_status *st )
{
	lc_mutex_lock( b->mu );
	store_status_locked( b, address, st );
	lc_mutex_unlock( b->mu );
}

void lc_browser_forget_status( lc_browser *b, const char *address )
{
	int i;
	lc_mutex_lock( b->mu );
	for ( i = 0; i < b->nStatuses; i++ )
		if ( !strcmp( b->statuses[i].address, address ) ) {
			b->statuses[i] = b->statuses[--b->nStatuses];
			break;
		}
	lc_mutex_unlock( b->mu );
}
