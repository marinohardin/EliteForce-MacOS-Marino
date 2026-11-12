/*
===========================================================================
Launcher core: router port mapping (NAT-PMP RFC 6886 + UPnP IGD), the public
address, the LAN address, the internet-listing check. Spec: servers.md 7.10,
7.11, 9.9, 9.10, 10. Hand-written, no library. This file is part of VoyagerSP
(GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#include "lc_sock.h"
#include "lc_nat.h"
#include "lc_net.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#	include <windows.h>
#	include <iphlpapi.h>
#else
#	include <pthread.h>
#	include <ifaddrs.h>
#	include <net/if.h>
#endif

#define PMP_PORT		5351
#define MAP_LIFETIME	7200

/* ---- helpers ------------------------------------------------------------------------- */

int lc_ipv4_private( unsigned a )
{
	return ( a >> 24 ) == 10 || ( a >> 20 ) == 0xAC1 || ( a >> 16 ) == 0xC0A8 || ( a >> 22 ) == 0x191;
}

static void ip_string( unsigned hostOrder, char *out, size_t n )
{
	lc_snprintf( out, n, "%u.%u.%u.%u", hostOrder >> 24, ( hostOrder >> 16 ) & 255, ( hostOrder >> 8 ) & 255, hostOrder & 255 );
}

/* ---- default gateway ----------------------------------------------------------------- */

unsigned lc_default_gateway( void )
{
#ifdef _WIN32
	MIB_IPFORWARDROW row;
	memset( &row, 0, sizeof( row ) );
	if ( GetBestRoute( inet_addr( "8.8.8.8" ), 0, &row ) == NO_ERROR && row.dwForwardNextHop )
		return ntohl( row.dwForwardNextHop );
	return 0;
#elif defined( __linux__ )
	FILE *f = fopen( "/proc/net/route", "r" );
	char line[256];
	unsigned gw = 0;

	if ( !f )
		return 0;
	if ( !fgets( line, sizeof( line ), f ) ) {		/* header */
		fclose( f );
		return 0;
	}
	while ( fgets( line, sizeof( line ), f ) ) {
		char iface[64], *e;
		unsigned long dest, gate, flags;
		if ( sscanf( line, "%63s %lx %lx %lx", iface, &dest, &gate, &flags ) != 4 )
			continue;
		(void)e;
		if ( dest == 0 && ( flags & 2 ) && gate ) {
			/* the file prints the address as a host-endian word of the network-order bytes */
			unsigned g = (unsigned)gate;
			gw = ( g & 255 ) << 24 | ( ( g >> 8 ) & 255 ) << 16 | ( ( g >> 16 ) & 255 ) << 8 | ( g >> 24 );
			break;
		}
	}
	fclose( f );
	return gw;
#else
	/* BSD / macOS (tests only): ask route(8) */
	FILE *p = popen( "/sbin/route -n get default 2>/dev/null", "r" );
	char line[256];
	unsigned gw = 0;
	if ( !p )
		return 0;
	while ( fgets( line, sizeof( line ), p ) ) {
		char *g = strstr( line, "gateway:" );
		struct in_addr ia;
		if ( g ) {
			g += 8;
			while ( *g == ' ' )
				g++;
			g[strcspn( g, " \r\n" )] = 0;
			if ( inet_pton( AF_INET, g, &ia ) == 1 )
				gw = ntohl( ia.s_addr );
			break;
		}
	}
	pclose( p );
	return gw;
#endif
}

/* ---- tiny HTTP client for UPnP (blocking, with timeouts) ------------------------------------------ */

/* GET/POST http://host:port/path; the response body (NUL-terminated, free it) and status. 1 on a response */
static int http_request( const char *host, int port, const char *method, const char *path,
	const char *extraHeaders, const char *body, volatile int *stop, int timeoutMs, char **respBody, int *status )
{
	struct addrinfo hints, *res = NULL;
	char portStr[16], *req, *buf = NULL;
	size_t cap = 0, len = 0, bodyLen = body ? strlen( body ) : 0;
	lc_sock s;
	double until = lc_now( ) + timeoutMs / 1000.0;
	int ok = 0, st = 0;
	char *hdrEnd;

	*respBody = NULL;
	*status = 0;
	lc_snprintf( portStr, sizeof( portStr ), "%d", port );
	memset( &hints, 0, sizeof( hints ) );
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	if ( getaddrinfo( host, portStr, &hints, &res ) != 0 || !res )
		return 0;
	s = socket( res->ai_family, SOCK_STREAM, 0 );
	if ( s == LC_BADSOCK ) {
		freeaddrinfo( res );
		return 0;
	}
	lc_sock_set_nonblock( s );
	if ( connect( s, res->ai_addr, (int)res->ai_addrlen ) != 0 ) {
		int e = lc_sock_errno( );
#ifdef _WIN32
		int inprog = e == WSAEWOULDBLOCK;
#else
		int inprog = e == EINPROGRESS;
#endif
		if ( !inprog || lc_sock_wait_writable( s, timeoutMs ) <= 0 ) {
			lc_sock_close( s );
			freeaddrinfo( res );
			return 0;
		}
	}
	freeaddrinfo( res );
	req = (char *)malloc( strlen( path ) + strlen( host ) + ( extraHeaders ? strlen( extraHeaders ) : 0 ) + 256 );
	sprintf( req, "%s %s HTTP/1.1\r\nHost: %s:%d\r\nConnection: close\r\nUser-Agent: EliteForce-Launcher/1.0 UPnP/1.1\r\n%sContent-Length: %u\r\n\r\n",
		method, path, host, port, extraHeaders ? extraHeaders : "", (unsigned)bodyLen );
	{
		size_t rl = strlen( req ), sent = 0;
		char *all = (char *)malloc( rl + bodyLen + 1 );
		memcpy( all, req, rl );
		if ( body )
			memcpy( all + rl, body, bodyLen );
		free( req );
		rl += bodyLen;
		while ( sent < rl && lc_now( ) < until && !( stop && *stop ) ) {
			int n = (int)send( s, all + sent, (int)( rl - sent ), 0 );
			if ( n > 0 )
				sent += (size_t)n;
			else if ( lc_sock_errno( ) == LC_EWOULDBLOCK )
				lc_sock_wait_writable( s, 100 );
			else
				break;
		}
		free( all );
		if ( sent < rl ) {
			lc_sock_close( s );
			return 0;
		}
	}
	for ( ;; ) {
		int n;
		if ( lc_now( ) >= until || ( stop && *stop ) )
			break;
		if ( cap - len < 4097 ) {
			cap = cap ? cap * 2 : 8192;
			buf = (char *)realloc( buf, cap );
		}
		if ( lc_sock_wait_readable( s, 100 ) <= 0 )
			continue;
		n = (int)recv( s, buf + len, (int)( cap - len - 1 ), 0 );
		if ( n <= 0 ) {
			ok = 1;			/* closed: complete */
			break;
		}
		len += (size_t)n;
		if ( len > 1024 * 1024 )
			break;
	}
	lc_sock_close( s );
	if ( !buf )
		return 0;
	buf[len] = 0;
	if ( len < 12 || strncmp( buf, "HTTP/", 5 ) ) {
		free( buf );
		return 0;
	}
	st = atoi( buf + 9 );
	hdrEnd = strstr( buf, "\r\n\r\n" );
	if ( !hdrEnd ) {
		free( buf );
		return 0;
	}
	{
		char *b = hdrEnd + 4;
		/* chunked? */
		char saved = *hdrEnd;
		*hdrEnd = 0;
		if ( strstr( buf, "hunked" ) && ( strstr( buf, "ransfer-Encoding" ) || strstr( buf, "ransfer-encoding" ) ) ) {
			char *out = (char *)malloc( strlen( b ) + 1 ), *o = out;
			char *p = b;
			*hdrEnd = saved;
			for ( ;; ) {
				char *e;
				unsigned long cl = strtoul( p, &e, 16 );
				if ( e == p )
					break;
				p = strstr( e, "\r\n" );
				if ( !p )
					break;
				p += 2;
				if ( cl == 0 || strlen( p ) < cl )
					break;
				memcpy( o, p, cl );
				o += cl;
				p += cl;
				if ( !strncmp( p, "\r\n", 2 ) )
					p += 2;
			}
			*o = 0;
			*respBody = out;
		} else {
			*hdrEnd = saved;
			*respBody = lc_strdup( b );
		}
	}
	free( buf );
	*status = st;
	return ok || st > 0;
}

/* "http://host:port/path" -> parts; 1 if parsable */
static int split_url( const char *url, char *host, size_t hs, int *port, char *path, size_t ps )
{
	const char *p, *slash, *colon;
	size_t hl;

	if ( strncmp( url, "http://", 7 ) )
		return 0;
	p = url + 7;
	slash = strchr( p, '/' );
	colon = strchr( p, ':' );
	if ( colon && ( !slash || colon < slash ) ) {
		hl = (size_t)( colon - p );
		*port = atoi( colon + 1 );
	} else {
		hl = slash ? (size_t)( slash - p ) : strlen( p );
		*port = 80;
	}
	if ( hl >= hs || !hl )
		return 0;
	memcpy( host, p, hl );
	host[hl] = 0;
	lc_strlcpy( path, slash ? slash : "/", ps );
	return 1;
}

/* text between <tag> and </tag> after from; returns pointer past the closing tag, or NULL */
static const char *xml_tag( const char *from, const char *tag, char *out, size_t n )
{
	char open[64], close[64];
	const char *a, *b;
	lc_snprintf( open, sizeof( open ), "<%s>", tag );
	lc_snprintf( close, sizeof( close ), "</%s>", tag );
	a = strstr( from, open );
	if ( !a )
		return NULL;
	a += strlen( open );
	b = strstr( a, close );
	if ( !b )
		return NULL;
	if ( out ) {
		size_t l = (size_t)( b - a );
		if ( l >= n )
			l = n - 1;
		memcpy( out, a, l );
		out[l] = 0;
	}
	return b + strlen( close );
}

/* ---- the handle ---------------------------------------------------------------------------------------- */

struct lc_nat {
	int mode;				/* 0 address only, 1 map */
	int port;
	void (*cbAddr)( lc_nat_result r, const char *ip, int err, void *u );
	void (*cbMap)( lc_nat_result r, const char *ip, int extPort, int err, void *u );
	void *user;
	lc_thread *thr;
	volatile int stop;
	volatile int selfRelease;
	volatile int started;
#ifdef _WIN32
	DWORD tid;
#else
	pthread_t tid;
#endif
	/* NAT-PMP */
	unsigned gw;
	int pmpMapped, pmpExtPort, pmpLifetime;
	/* UPnP */
	int upMapped, extPort;
	char upHost[128], upPath[512], upService[128], localIp[32];
	int upPort, upExtPort;
	char extIp[32];
};

static int in_worker( lc_nat *n )
{
	if ( !n->started )
		return 0;
#ifdef _WIN32
	return GetCurrentThreadId( ) == n->tid;
#else
	return pthread_equal( pthread_self( ), n->tid );
#endif
}

static void sleep_stoppable( lc_nat *n, int ms )
{
	while ( ms > 0 && !n->stop ) {
		int step = ms > 100 ? 100 : ms;
		lc_sleep_ms( step );
		ms -= step;
	}
}

/* ---- NAT-PMP --------------------------------------------------------------------------------------------- */

/* send req to the gateway, wait for the matching response; tries with 250 ms doubling. len or -1 */
static int pmp_exchange( lc_nat *n, const unsigned char *req, size_t reqLen, unsigned char *resp, size_t respMax, int tries )
{
	lc_sock s;
	struct sockaddr_in to;
	int wait = 250, t, got = -1;

	s = socket( AF_INET, SOCK_DGRAM, 0 );
	if ( s == LC_BADSOCK )
		return -1;
	memset( &to, 0, sizeof( to ) );
	to.sin_family = AF_INET;
	to.sin_port = htons( PMP_PORT );
	to.sin_addr.s_addr = htonl( n->gw );
	for ( t = 0; t < tries && got < 0 && !n->stop; t++ ) {
		double until = lc_now( ) + wait / 1000.0;
		sendto( s, (const char *)req, (int)reqLen, 0, (struct sockaddr *)&to, sizeof( to ) );
		while ( lc_now( ) < until && !n->stop ) {
			struct sockaddr_in from;
			socklen_t fl = sizeof( from );
			int r;
			if ( lc_sock_wait_readable( s, 50 ) <= 0 )
				continue;
			r = (int)recvfrom( s, (char *)resp, (int)respMax, 0, (struct sockaddr *)&from, &fl );
			if ( r >= 8 && from.sin_addr.s_addr == to.sin_addr.s_addr && resp[0] == 0 && resp[1] == ( req[1] | 0x80 ) ) {
				got = r;
				break;
			}
		}
		wait *= 2;
	}
	lc_sock_close( s );
	return got;
}

/* result: 1 got an answer (code in *code, address in *ext), 0 silence */
static int pmp_public_address( lc_nat *n, int tries, int *code, unsigned *ext )
{
	unsigned char req[2] = { 0, 0 }, resp[16];
	int r = pmp_exchange( n, req, 2, resp, sizeof( resp ), tries );
	if ( r < 12 )
		return 0;
	*code = resp[2] << 8 | resp[3];
	*ext = (unsigned)resp[8] << 24 | (unsigned)resp[9] << 16 | (unsigned)resp[10] << 8 | resp[11];
	return 1;
}

static int pmp_map( lc_nat *n, int port, int suggested, int lifetime, int tries, int *code, int *extPort, int *granted )
{
	unsigned char req[12], resp[24];
	int r;

	req[0] = 0; req[1] = 1; req[2] = 0; req[3] = 0;
	req[4] = (unsigned char)( port >> 8 ); req[5] = (unsigned char)port;
	req[6] = (unsigned char)( suggested >> 8 ); req[7] = (unsigned char)suggested;
	req[8] = (unsigned char)( lifetime >> 24 ); req[9] = (unsigned char)( lifetime >> 16 );
	req[10] = (unsigned char)( lifetime >> 8 ); req[11] = (unsigned char)lifetime;
	r = pmp_exchange( n, req, 12, resp, sizeof( resp ), tries );
	if ( r < 16 )
		return 0;
	*code = resp[2] << 8 | resp[3];
	*extPort = resp[10] << 8 | resp[11];
	*granted = (int)( (unsigned)resp[12] << 24 | (unsigned)resp[13] << 16 | (unsigned)resp[14] << 8 | resp[15] );
	return 1;
}

/* ---- UPnP IGD ---------------------------------------------------------------------------------------------- */

/* M-SEARCH; fills locations (up to max, NUL-terminated URLs). count */
static int ssdp_discover( lc_nat *n, char locations[][256], int max )
{
	static const char *const targets[] = {
		"urn:schemas-upnp-org:device:InternetGatewayDevice:1",
		"urn:schemas-upnp-org:device:InternetGatewayDevice:2",
		"urn:schemas-upnp-org:service:WANIPConnection:1",
		"upnp:rootdevice" };
	lc_sock s = socket( AF_INET, SOCK_DGRAM, 0 );
	struct sockaddr_in to, local;
	int count = 0, i, round;
	char req[512], buf[2048];
	double until;
	unsigned char ttl = 2;

	if ( s == LC_BADSOCK )
		return 0;
	memset( &local, 0, sizeof( local ) );
	local.sin_family = AF_INET;
	bind( s, (struct sockaddr *)&local, sizeof( local ) );
	setsockopt( s, IPPROTO_IP, IP_MULTICAST_TTL, (const char *)&ttl, sizeof( ttl ) );
	memset( &to, 0, sizeof( to ) );
	to.sin_family = AF_INET;
	to.sin_port = htons( 1900 );
	inet_pton( AF_INET, "239.255.255.250", &to.sin_addr );
	for ( round = 0; round < 2; round++ ) {
		for ( i = 0; i < 4; i++ ) {
			lc_snprintf( req, sizeof( req ),
				"M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\nST: %s\r\n\r\n", targets[i] );
			sendto( s, req, (int)strlen( req ), 0, (struct sockaddr *)&to, sizeof( to ) );
		}
		until = lc_now( ) + ( round == 0 ? 1.2 : 1.3 );
		while ( lc_now( ) < until && !n->stop && count < max ) {
			int r;
			char *loc;
			if ( lc_sock_wait_readable( s, 100 ) <= 0 )
				continue;
			r = (int)recv( s, buf, sizeof( buf ) - 1, 0 );
			if ( r <= 0 )
				continue;
			buf[r] = 0;
			for ( loc = buf; *loc; loc++ )
				if ( ( loc == buf || loc[-1] == '\n' ) && !lc_strnicmp( loc, "location:", 9 ) ) {
					char url[256];
					size_t l;
					int dup = 0;
					loc += 9;
					while ( *loc == ' ' )
						loc++;
					l = strcspn( loc, "\r\n" );
					if ( l >= sizeof( url ) )
						break;
					memcpy( url, loc, l );
					url[l] = 0;
					for ( i = 0; i < count; i++ )
						if ( !strcmp( locations[i], url ) )
							dup = 1;
					if ( !dup )
						lc_strlcpy( locations[count++], url, 256 );
					break;
				}
		}
		if ( count && round == 0 )
			break;
	}
	lc_sock_close( s );
	return count;
}

static void soap_envelope( char *out, size_t n, const char *service, const char *action, const char *args )
{
	lc_snprintf( out, n,
		"<?xml version=\"1.0\"?>\r\n<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
		"s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>"
		"<u:%s xmlns:u=\"%s\">%s</u:%s></s:Body></s:Envelope>\r\n", action, service, args, action );
}

/* returns the HTTP status (0 on failure), *err = UPnP error code of a fault; resp out (free) */
static int soap_call( lc_nat *n, const char *action, const char *args, char **resp, int *err )
{
	char env[2048], hdr[512], *body = NULL;
	int status = 0;

	*resp = NULL;
	*err = 0;
	soap_envelope( env, sizeof( env ), n->upService, action, args );
	lc_snprintf( hdr, sizeof( hdr ), "Content-Type: text/xml; charset=\"utf-8\"\r\nSOAPAction: \"%s#%s\"\r\n", n->upService, action );
	if ( !http_request( n->upHost, n->upPort, "POST", n->upPath, hdr, env, &n->stop, 4000, &body, &status ) )
		return 0;
	if ( status >= 300 && body ) {
		char code[32];
		if ( xml_tag( body, "errorCode", code, sizeof( code ) ) )
			*err = atoi( code );
	}
	*resp = body;
	return status;
}

/* fetch the device description at location, find a WAN connection service. 1 if found */
static int upnp_find_service( lc_nat *n, const char *location )
{
	char host[128], path[512], *xml = NULL, urlBase[256] = "";
	int port, status = 0;
	const char *p;
	int bestRank = 0;

	if ( !split_url( location, host, sizeof( host ), &port, path, sizeof( path ) ) )
		return 0;
	if ( !http_request( host, port, "GET", path, "", NULL, &n->stop, 4000, &xml, &status ) || status != 200 || !xml ) {
		free( xml );
		return 0;
	}
	xml_tag( xml, "URLBase", urlBase, sizeof( urlBase ) );
	for ( p = xml; ( p = strstr( p, "<serviceType>" ) ) != NULL; ) {
		char type[160], ctl[512];
		const char *svcEnd = strstr( p, "</service>" );
		const char *after = xml_tag( p, "serviceType", type, sizeof( type ) );
		int rank = 0;

		if ( !after )
			break;
		if ( strstr( type, ":WANIPConnection:2" ) )
			rank = 4;
		else if ( strstr( type, ":WANIPConnection:1" ) )
			rank = 3;
		else if ( strstr( type, ":WANPPPConnection:1" ) )
			rank = 2;
		if ( rank > bestRank && svcEnd ) {
			const char *c = strstr( p, "<controlURL>" );
			if ( c && c < svcEnd && xml_tag( c, "controlURL", ctl, sizeof( ctl ) ) ) {
				char h2[128], path2[512];
				int port2;
				if ( !strncmp( ctl, "http://", 7 ) ) {
					if ( !split_url( ctl, h2, sizeof( h2 ), &port2, path2, sizeof( path2 ) ) ) {
						p = after;
						continue;
					}
				} else {
					const char *base = urlBase[0] ? urlBase : location;
					char bh[128], bp[512];
					int bport;
					if ( !split_url( base, bh, sizeof( bh ), &bport, bp, sizeof( bp ) ) ) {
						p = after;
						continue;
					}
					lc_strlcpy( h2, bh, sizeof( h2 ) );
					port2 = bport;
					if ( ctl[0] == '/' )
						lc_strlcpy( path2, ctl, sizeof( path2 ) );
					else {
						char dir[512];
						char *sl;
						lc_strlcpy( dir, bp, sizeof( dir ) );
						sl = strrchr( dir, '/' );
						if ( sl )
							sl[1] = 0;
						lc_snprintf( path2, sizeof( path2 ), "%s%s", dir, ctl );
					}
				}
				bestRank = rank;
				lc_strlcpy( n->upHost, h2, sizeof( n->upHost ) );
				n->upPort = port2;
				lc_strlcpy( n->upPath, path2, sizeof( n->upPath ) );
				lc_strlcpy( n->upService, type, sizeof( n->upService ) );
			}
		}
		p = after;
	}
	free( xml );
	return bestRank > 0;
}

/* the address of the interface that reaches host */
static int local_ip_towards( const char *host, char *out, size_t n )
{
	lc_sock s = socket( AF_INET, SOCK_DGRAM, 0 );
	struct sockaddr_in to, me;
	socklen_t ml = sizeof( me );
	int ok = 0;

	if ( s == LC_BADSOCK )
		return 0;
	memset( &to, 0, sizeof( to ) );
	to.sin_family = AF_INET;
	to.sin_port = htons( 9 );
	if ( inet_pton( AF_INET, host, &to.sin_addr ) == 1 && connect( s, (struct sockaddr *)&to, sizeof( to ) ) == 0 &&
			getsockname( s, (struct sockaddr *)&me, &ml ) == 0 ) {
		ip_string( ntohl( me.sin_addr.s_addr ), out, n );
		ok = 1;
	}
	lc_sock_close( s );
	return ok;
}

/* find an IGD; fills n->up*. 1 if found */
static int upnp_discover( lc_nat *n )
{
	char locs[8][256];
	int count = ssdp_discover( n, locs, 8 ), i, pass;
	char gwStr[32];

	ip_string( n->gw, gwStr, sizeof( gwStr ) );
	/* prefer descriptions served by the gateway itself */
	for ( pass = 0; pass < 2; pass++ )
		for ( i = 0; i < count && !n->stop; i++ ) {
			char h[128], p[512];
			int port;
			int isGw = split_url( locs[i], h, sizeof( h ), &port, p, sizeof( p ) ) && !strcmp( h, gwStr );
			if ( ( pass == 0 ) != isGw )
				continue;
			if ( upnp_find_service( n, locs[i] ) ) {
				if ( !local_ip_towards( n->upHost, n->localIp, sizeof( n->localIp ) ) )
					local_ip_towards( gwStr, n->localIp, sizeof( n->localIp ) );
				return 1;
			}
		}
	return 0;
}

static int upnp_external_ip( lc_nat *n, char *ip, size_t size )
{
	char *resp = NULL;
	int err, st = soap_call( n, "GetExternalIPAddress", "", &resp, &err );
	int ok = 0;
	if ( st == 200 && resp && xml_tag( resp, "NewExternalIPAddress", ip, size ) && ip[0] )
		ok = 1;
	free( resp );
	return ok;
}

/* AddPortMapping; returns 0 ok, else the UPnP error (or -1 no answer) */
static int upnp_add_mapping( lc_nat *n, int port, int lease )
{
	char args[1024], *resp = NULL;
	int err = 0, st, attempt;

	for ( attempt = 0; attempt < 3; attempt++ ) {
		lc_snprintf( args, sizeof( args ),
			"<NewRemoteHost></NewRemoteHost><NewExternalPort>%d</NewExternalPort><NewProtocol>UDP</NewProtocol>"
			"<NewInternalPort>%d</NewInternalPort><NewInternalClient>%s</NewInternalClient><NewEnabled>1</NewEnabled>"
			"<NewPortMappingDescription>Elite Force</NewPortMappingDescription><NewLeaseDuration>%d</NewLeaseDuration>",
			port, port, n->localIp, lease );
		st = soap_call( n, "AddPortMapping", args, &resp, &err );
		free( resp );
		resp = NULL;
		if ( !st )
			return -1;
		if ( st == 200 )
			return 0;
		if ( err == 725 && lease != 0 ) {		/* only permanent leases */
			lease = 0;
			continue;
		}
		if ( err == 718 || err == 727 ) {		/* conflict: remove ours, retry */
			lc_snprintf( args, sizeof( args ), "<NewRemoteHost></NewRemoteHost><NewExternalPort>%d</NewExternalPort><NewProtocol>UDP</NewProtocol>", port );
			soap_call( n, "DeletePortMapping", args, &resp, &err );
			free( resp );
			resp = NULL;
			continue;
		}
		return err ? err : 500;
	}
	return 718;
}

static void upnp_delete_mapping( lc_nat *n, int port )
{
	char args[256], *resp = NULL;
	int err;
	lc_snprintf( args, sizeof( args ), "<NewRemoteHost></NewRemoteHost><NewExternalPort>%d</NewExternalPort><NewProtocol>UDP</NewProtocol>", port );
	soap_call( n, "DeletePortMapping", args, &resp, &err );
	free( resp );
}

/* ---- the worker ---------------------------------------------------------------------------------------------------- */

static void report( lc_nat *n, lc_nat_result r, const char *ip, int extPort, int err )
{
	if ( n->stop && !n->selfRelease )
		return;
	if ( n->mode == 0 ) {
		if ( n->cbAddr )
			n->cbAddr( r, ip, err, n->user );
	} else if ( n->cbMap )
		n->cbMap( r, ip, extPort, err, n->user );
}

static void do_delete( lc_nat *n )
{
	if ( n->pmpMapped && n->gw ) {
		int code, ext, granted;
		n->stop = 0;		/* the exchange polls stop */
		pmp_map( n, n->port, 0, 0, 2, &code, &ext, &granted );
		n->pmpMapped = 0;
	}
	if ( n->upMapped ) {
		upnp_delete_mapping( n, n->upExtPort ? n->upExtPort : n->port );
		n->upMapped = 0;
	}
}

static void nat_worker( void *u )
{
	lc_nat *n = (lc_nat *)u;
	int code = 0, wantUpnp = 1, extPort = 0, granted = 0;
	unsigned ext = 0;
	char ip[32] = "";
	lc_nat_result result = LC_NAT_TIMEOUT;
	int err = 0, haveResult = 0, renewAt = 0;
	int pmpSilent = 0;

#ifdef _WIN32
	n->tid = GetCurrentThreadId( );
#else
	n->tid = pthread_self( );
#endif
	n->started = 1;
	lc_net_init( );
	n->gw = lc_default_gateway( );
	if ( !n->gw ) {
		report( n, LC_NAT_UNSUPPORTED, "", 0, 0 );
		goto out;
	}

	/* ---- NAT-PMP ---- */
	if ( n->mode == 0 ) {
		if ( pmp_public_address( n, 3, &code, &ext ) ) {
			if ( code == 0 && ext ) {
				ip_string( ext, ip, sizeof( ip ) );
				result = LC_NAT_OK;
				haveResult = 1;
				wantUpnp = 0;
			} else if ( code == 2 ) {
				result = LC_NAT_DISABLED;
				haveResult = 1;
				wantUpnp = 0;
			}
		} else
			pmpSilent = 1;
	} else {
		if ( pmp_map( n, n->port, n->port, MAP_LIFETIME, 4, &code, &extPort, &granted ) ) {
			if ( code == 0 ) {
				unsigned a = 0;
				int c2 = 0;
				n->pmpMapped = 1;
				n->pmpExtPort = extPort ? extPort : n->port;
				n->pmpLifetime = granted > 0 ? granted : MAP_LIFETIME;
				renewAt = n->pmpLifetime / 2;
				wantUpnp = 0;
				if ( pmp_public_address( n, 4, &c2, &a ) && c2 == 0 && a ) {
					ip_string( a, ip, sizeof( ip ) );
					result = lc_ipv4_private( a ) ? LC_NAT_PRIVATE_EXTERNAL : LC_NAT_OK;
				} else
					result = LC_NAT_TIMEOUT;
				haveResult = 1;
			} else if ( code == 2 ) {
				result = LC_NAT_DISABLED;
				haveResult = 1;
				wantUpnp = 0;
			} else if ( code == 1 || code == 5 ) {
				/* unsupported: try UPnP below */
			} else {
				result = LC_NAT_OTHER;
				err = code;
				haveResult = 1;
				wantUpnp = 0;
			}
		} else
			pmpSilent = 1;
	}

	/* ---- UPnP ---- */
	if ( wantUpnp && !n->stop ) {
		if ( !upnp_discover( n ) ) {
			result = pmpSilent ? LC_NAT_TIMEOUT : LC_NAT_UNSUPPORTED;
			haveResult = 1;
		} else if ( n->mode == 0 ) {
			if ( upnp_external_ip( n, ip, sizeof( ip ) ) ) {
				struct in_addr ia;
				result = LC_NAT_OK;
				if ( inet_pton( AF_INET, ip, &ia ) == 1 && ntohl( ia.s_addr ) == 0 )
					result = LC_NAT_TIMEOUT;
			} else
				result = LC_NAT_OTHER;
			haveResult = 1;
		} else {
			int e = upnp_add_mapping( n, n->port, MAP_LIFETIME );
			if ( e == 0 ) {
				struct in_addr ia;
				int tries;
				n->upMapped = 1;
				n->upExtPort = n->port;
				renewAt = MAP_LIFETIME / 2;
				result = LC_NAT_TIMEOUT;
				for ( tries = 0; tries < 3 && !n->stop; tries++ ) {
					if ( upnp_external_ip( n, ip, sizeof( ip ) ) && inet_pton( AF_INET, ip, &ia ) == 1 && ia.s_addr ) {
						result = lc_ipv4_private( ntohl( ia.s_addr ) ) ? LC_NAT_PRIVATE_EXTERNAL : LC_NAT_OK;
						break;
					}
					sleep_stoppable( n, 1000 );
				}
				extPort = n->port;
			} else if ( e == 606 || e == 603 ) {
				result = LC_NAT_DISABLED;
			} else if ( e < 0 ) {
				result = LC_NAT_TIMEOUT;
			} else {
				result = LC_NAT_OTHER;
				err = e;
			}
			haveResult = 1;
		}
	}

	if ( !haveResult )
		result = LC_NAT_TIMEOUT;
	n->extPort = 0;
	if ( result == LC_NAT_OK || result == LC_NAT_PRIVATE_EXTERNAL ) {
		lc_strlcpy( n->extIp, ip, sizeof( n->extIp ) );
		n->extPort = n->pmpMapped ? n->pmpExtPort : n->upMapped ? n->upExtPort : 0;
	}
	report( n, result, ip, n->pmpMapped ? n->pmpExtPort : n->upMapped ? n->upExtPort : 0, err );

	/* ---- keep a mapping alive until released ---- */
	if ( n->mode == 1 && ( n->pmpMapped || n->upMapped ) ) {
		double next = lc_now( ) + ( renewAt > 60 ? renewAt : 60 );
		while ( !n->stop ) {
			lc_sleep_ms( 200 );
			if ( lc_now( ) < next || n->stop )
				continue;
			if ( n->pmpMapped ) {
				int c, ep, g;
				if ( pmp_map( n, n->port, n->pmpExtPort, MAP_LIFETIME, 3, &c, &ep, &g ) && c == 0 ) {
					if ( ep )
						n->pmpExtPort = ep;
					next = lc_now( ) + ( g > 120 ? g / 2 : 60 );
				} else
					next = lc_now( ) + 60;
			} else if ( n->upMapped ) {
				upnp_add_mapping( n, n->port, MAP_LIFETIME );
				next = lc_now( ) + MAP_LIFETIME / 2;
			}
		}
	}
out:
	if ( n->selfRelease ) {
		/* released from inside the callback: finish the cleanup here */
		do_delete( n );
		n->thr = NULL;			/* the detached thread frees itself below */
		free( n );
		return;
	}
	if ( n->mode == 1 )
		return;			/* lc_nat_release deletes the mapping after joining */
}

static lc_nat *nat_start( lc_nat *n )
{
	n->thr = lc_thread_start( nat_worker, n );
	if ( !n->thr ) {
		free( n );
		return NULL;
	}
	return n;
}

lc_nat *lc_nat_public_address( void (*cb)( lc_nat_result r, const char *ip, int err, void *u ), void *u )
{
	lc_nat *n = (lc_nat *)calloc( 1, sizeof( *n ) );
	if ( !n )
		return NULL;
	n->mode = 0;
	n->cbAddr = cb;
	n->user = u;
	return nat_start( n );
}

lc_nat *lc_nat_map_udp( int port, void (*cb)( lc_nat_result r, const char *ip, int extPort, int err, void *u ), void *u )
{
	lc_nat *n = (lc_nat *)calloc( 1, sizeof( *n ) );
	if ( !n )
		return NULL;
	n->mode = 1;
	n->port = port;
	n->cbMap = cb;
	n->user = u;
	return nat_start( n );
}

void lc_nat_release( lc_nat *n )
{
	if ( !n )
		return;
	if ( in_worker( n ) ) {
		/* inside the callback: the worker cleans up and frees when it unwinds */
		n->selfRelease = 1;
		n->stop = 1;
		lc_thread_detach( n->thr );
		return;
	}
	n->stop = 1;
	if ( n->thr )
		lc_thread_join( n->thr );
	do_delete( n );
	free( n );
}

/* ---- the public address (10) ---------------------------------------------------------------------------------------- */

static lc_mutex *g_pubMu;
static char g_pub[32];
static lc_nat *g_pubNat;
static volatile int g_pubPending, g_pubStarted;

static lc_mutex *pub_mutex( void )
{
	if ( !g_pubMu )
		g_pubMu = lc_mutex_new( );
	return g_pubMu;
}

static void pub_cb( lc_nat_result r, const char *ip, int err, void *u )
{
	(void)err;
	(void)u;
	lc_mutex_lock( pub_mutex( ) );
	if ( r == LC_NAT_OK && ip && *ip && strcmp( ip, "0.0.0.0" ) ) {
		lc_strlcpy( g_pub, ip, sizeof( g_pub ) );
		lc_log( "Public address (from the router): %s", ip );
	}
	g_pubPending = 0;
	lc_mutex_unlock( pub_mutex( ) );
}

void lc_public_address_start( void )
{
	const char *t = lc_env( "VOYAGERSP_TEST_NAT" );

	lc_mutex_lock( pub_mutex( ) );
	if ( g_pub[0] || g_pubStarted ) {
		lc_mutex_unlock( pub_mutex( ) );
		return;
	}
	g_pubStarted = 1;
	if ( t ) {
		if ( strcmp( t, "fail" ) )
			lc_strlcpy( g_pub, t, sizeof( g_pub ) );
		lc_mutex_unlock( pub_mutex( ) );
		return;
	}
	g_pubPending = 1;
	g_pubNat = lc_nat_public_address( pub_cb, NULL );
	if ( !g_pubNat )
		g_pubPending = 0;
	lc_mutex_unlock( pub_mutex( ) );
}

const char *lc_public_address( void )
{
	const char *r;
	lc_mutex_lock( pub_mutex( ) );
	r = g_pub[0] ? g_pub : NULL;
	lc_mutex_unlock( pub_mutex( ) );
	return r;
}

int lc_public_address_pending( void )
{
	return g_pubPending;
}

int lc_public_address_wait( int ms )
{
	while ( !lc_public_address( ) && g_pubPending && ms > 0 ) {
		lc_sleep_ms( 50 );
		ms -= 50;
	}
	return lc_public_address( ) != NULL;
}

/* ---- LAN addresses (7.10) --------------------------------------------------------------------------------------------- */

typedef struct { char ip[16]; char name[64]; int score; int order; } lan_t;

static int lan_cmp( const void *a, const void *b )
{
	const lan_t *x = (const lan_t *)a, *y = (const lan_t *)b;
	if ( x->score != y->score )
		return y->score - x->score;
	return x->order - y->order;
}

static int lan_collect( lan_t *out, int max, int skipVirtual )
{
	int n = 0;
#ifdef _WIN32
	ULONG size = 16384;
	IP_ADAPTER_ADDRESSES *aa = NULL, *a;
	ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_INCLUDE_GATEWAYS;
	int tries;
	DWORD r = ERROR_BUFFER_OVERFLOW;

	for ( tries = 0; tries < 3 && r == ERROR_BUFFER_OVERFLOW; tries++ ) {
		free( aa );
		aa = (IP_ADAPTER_ADDRESSES *)malloc( size );
		r = GetAdaptersAddresses( AF_INET, flags, NULL, aa, &size );
	}
	if ( r != NO_ERROR ) {
		free( aa );
		return 0;
	}
	for ( a = aa; a && n < max; a = a->Next ) {
		IP_ADAPTER_UNICAST_ADDRESS *ua;
		if ( a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK )
			continue;
		if ( skipVirtual && a->IfType == IF_TYPE_TUNNEL )
			continue;
		for ( ua = a->FirstUnicastAddress; ua && n < max; ua = ua->Next ) {
			struct sockaddr_in *sa = (struct sockaddr_in *)ua->Address.lpSockaddr;
			unsigned ip;
			if ( !sa || sa->sin_family != AF_INET )
				continue;
			ip = ntohl( sa->sin_addr.s_addr );
			if ( ( ip >> 24 ) == 127 || ( ip >> 16 ) == 0xA9FE )
				continue;
			ip_string( ip, out[n].ip, sizeof( out[n].ip ) );
			if ( !WideCharToMultiByte( CP_UTF8, 0, a->FriendlyName ? a->FriendlyName : L"", -1, out[n].name, sizeof( out[n].name ), NULL, NULL ) )
				out[n].name[0] = 0;
			out[n].score = a->FirstGatewayAddress ? 10 : 0;
			out[n].order = n;
			n++;
		}
	}
	free( aa );
#else
	struct ifaddrs *ifa0 = NULL, *ifa;
	char defIf[64] = "";

#	if defined( __linux__ )
	{
		FILE *f = fopen( "/proc/net/route", "r" );
		char line[256];
		if ( f ) {
			if ( fgets( line, sizeof( line ), f ) )
				while ( fgets( line, sizeof( line ), f ) ) {
					char iface[64];
					unsigned long dest, gate, flags;
					if ( sscanf( line, "%63s %lx %lx %lx", iface, &dest, &gate, &flags ) == 4 && dest == 0 && ( flags & 2 ) ) {
						lc_strlcpy( defIf, iface, sizeof( defIf ) );
						break;
					}
				}
			fclose( f );
		}
	}
#	endif
	if ( getifaddrs( &ifa0 ) != 0 )
		return 0;
	for ( ifa = ifa0; ifa && n < max; ifa = ifa->ifa_next ) {
		struct sockaddr_in *sa;
		unsigned ip;
		const char *nm = ifa->ifa_name;
		int score = 0;

		if ( !ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET )
			continue;
		if ( !( ifa->ifa_flags & IFF_UP ) || !( ifa->ifa_flags & IFF_RUNNING ) || ( ifa->ifa_flags & IFF_LOOPBACK ) )
			continue;
		sa = (struct sockaddr_in *)ifa->ifa_addr;
		ip = ntohl( sa->sin_addr.s_addr );
		if ( ( ip >> 16 ) == 0xA9FE )
			continue;
		if ( skipVirtual && ( !strncmp( nm, "lo", 2 ) || !strncmp( nm, "tun", 3 ) || !strncmp( nm, "utun", 4 ) || !strncmp( nm, "bridge", 6 ) ||
				!strncmp( nm, "tailscale", 9 ) || !strncmp( nm, "wg", 2 ) || !strncmp( nm, "docker", 6 ) || !strncmp( nm, "br-", 3 ) ||
				!strncmp( nm, "virbr", 5 ) || !strncmp( nm, "veth", 4 ) ) )
			continue;
		if ( defIf[0] && !strcmp( nm, defIf ) )
			score = 20;
		else if ( !strcmp( nm, "en0" ) )
			score = 15;
		else if ( !strncmp( nm, "en", 2 ) )
			score = 10;
		ip_string( ip, out[n].ip, sizeof( out[n].ip ) );
		lc_strlcpy( out[n].name, nm, sizeof( out[n].name ) );
		out[n].score = score;
		out[n].order = n;
		n++;
	}
	freeifaddrs( ifa0 );
#endif
	if ( n > 1 )
		qsort( out, (size_t)n, sizeof( lan_t ), lan_cmp );
	return n;
}

int lc_lan_address( char *out, size_t n )
{
	lan_t l[32];
	int c;
	lc_net_init( );
	c = lan_collect( l, 32, 1 );
	if ( c <= 0 ) {
		if ( n )
			out[0] = 0;
		return 0;
	}
	lc_strlcpy( out, l[0].ip, n );
	return 1;
}

int lc_lan_addresses( char addrs[][16], char ifnames[][64], int max )
{
	lan_t l[64];
	int c, i;
	lc_net_init( );
	c = lan_collect( l, 64, 0 );
	if ( c > max )
		c = max;
	for ( i = 0; i < c; i++ ) {
		memcpy( addrs[i], l[i].ip, 16 );
		if ( ifnames )
			lc_strlcpy( ifnames[i], l[i].name, 64 );
	}
	return c;
}

/* ---- the internet-listing check (9.10) ---------------------------------------------------------------------------------------- */

typedef struct {
	char master[65];
	char want[40];
	int answered, found;
	lc_thread *thr;
} check_job;

static void check_entry( lc_addr4 a, void *u )
{
	check_job *j = (check_job *)u;
	char s[40];
	if ( !j->want[0] )
		return;
	lc_snprintf( s, sizeof( s ), "%u.%u.%u.%u:%u", a.ip[0], a.ip[1], a.ip[2], a.ip[3], a.port );
	if ( !strcmp( s, j->want ) )
		j->found = 1;
}

static void check_run( void *u )
{
	check_job *j = (check_job *)u;
	j->answered = lc_query_master( j->master, 2000, check_entry, j ) >= 0;
}

void lc_list_check( const char (*masters)[65], int n, const char *want, lc_list_result *out )
{
	check_job *jobs;
	int i;

	memset( out, 0, sizeof( *out ) );
	if ( n > LC_MAX_MASTERS )
		n = LC_MAX_MASTERS;
	if ( n <= 0 )
		return;
	jobs = (check_job *)calloc( (size_t)n, sizeof( check_job ) );
	for ( i = 0; i < n; i++ ) {
		lc_strlcpy( jobs[i].master, masters[i], sizeof( jobs[i].master ) );
		lc_strlcpy( jobs[i].want, want ? want : "", sizeof( jobs[i].want ) );
		jobs[i].thr = lc_thread_start( check_run, &jobs[i] );
		if ( !jobs[i].thr )
			check_run( &jobs[i] );
	}
	for ( i = 0; i < n; i++ ) {
		if ( jobs[i].thr )
			lc_thread_join( jobs[i].thr );
		if ( !jobs[i].answered )
			out->down[i] = 1;
		else if ( jobs[i].found )
			out->listed[i] = 1;
		else
			out->missing[i] = 1;
	}
	free( jobs );
}
