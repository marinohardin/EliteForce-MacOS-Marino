/* lc_nat.c: NAT-PMP and UPnP IGD against fake routers on loopback (LC_TEST_GATEWAY, LC_TEST_PMP_PORT and
   LC_TEST_UPNP_LOCATION point the worker at them), result mapping, renewal-free release, LAN addresses
   (servers.md 7.10, 7.11, 9.9) */
#include "lc_nat.h"
#include "lc_sock.h"
#include "lc_test.h"
#include <stdlib.h>
#include <string.h>

static void setenv_(const char *k, const char *v)
{
#ifdef _WIN32
	char buf[256];
	lc_snprintf( buf, sizeof( buf ), "%s=%s", k, v ? v : "" );
	_putenv( buf );
#else
	if ( v )
		setenv( k, v, 1 );
	else
		unsetenv( k );
#endif
}

/* ---- a fake NAT-PMP gateway ---- */
typedef struct {
	lc_sock s;
	int port;
	volatile int stop;
	lc_thread *thr;
	int mode;					/* 0 ok, 1 not authorized (2), 2 unsupported (1), 3 silent, 4 private external address, 5 out of resources (4) */
	volatile int maps, deletes, addrReqs;
	volatile int lastInternal, lastSuggested, lastLifetime;
} pmp_t;

static void pmp_run( void *u )
{
	pmp_t *g = (pmp_t *)u;
	unsigned char buf[64], resp[16];

	while ( !g->stop ) {
		struct sockaddr_in from;
		socklen_t fl = sizeof( from );
		int n, code = 0;
		if ( lc_sock_wait_readable( g->s, 50 ) <= 0 )
			continue;
		n = (int)recvfrom( g->s, (char *)buf, sizeof( buf ), 0, (struct sockaddr *)&from, &fl );
		if ( n < 2 || g->mode == 3 )
			continue;
		code = g->mode == 1 ? 2 : g->mode == 2 ? 1 : g->mode == 5 ? 4 : 0;
		memset( resp, 0, sizeof( resp ) );
		resp[1] = (unsigned char)( 0x80 | buf[1] );
		resp[2] = (unsigned char)( code >> 8 );
		resp[3] = (unsigned char)code;
		resp[7] = 1;			/* epoch */
		if ( buf[1] == 0 && n == 2 ) {			/* public address */
			g->addrReqs++;
			if ( g->mode == 4 ) { resp[8] = 192; resp[9] = 168; resp[10] = 7; resp[11] = 7; }
			else { resp[8] = 203; resp[9] = 0; resp[10] = 113; resp[11] = 7; }
			sendto( g->s, (const char *)resp, 12, 0, (struct sockaddr *)&from, fl );
		} else if ( buf[1] == 1 && n == 12 ) {
			int life = buf[8] << 24 | buf[9] << 16 | buf[10] << 8 | buf[11];
			g->lastInternal = buf[4] << 8 | buf[5];
			g->lastSuggested = buf[6] << 8 | buf[7];
			g->lastLifetime = life;
			if ( life == 0 )
				g->deletes++;
			else
				g->maps++;
			resp[8] = buf[4]; resp[9] = buf[5];
			/* the router grants another external port when asked for 27960 and mode 0 */
			resp[10] = buf[6]; resp[11] = buf[7];
			resp[12] = (unsigned char)( life >> 24 ); resp[13] = (unsigned char)( life >> 16 ); resp[14] = (unsigned char)( life >> 8 ); resp[15] = (unsigned char)life;
			sendto( g->s, (const char *)resp, 16, 0, (struct sockaddr *)&from, fl );
		}
	}
}

static pmp_t *pmp_start( int mode )
{
	pmp_t *g = (pmp_t *)calloc( 1, sizeof( *g ) );
	struct sockaddr_in a;
	socklen_t al = sizeof( a );
	char port[16];

	g->mode = mode;
	g->s = socket( AF_INET, SOCK_DGRAM, 0 );
	memset( &a, 0, sizeof( a ) );
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl( 0x7f000001u );
	bind( g->s, (struct sockaddr *)&a, sizeof( a ) );
	getsockname( g->s, (struct sockaddr *)&a, &al );
	g->port = ntohs( a.sin_port );
	lc_snprintf( port, sizeof( port ), "%d", g->port );
	setenv_( "LC_TEST_PMP_PORT", port );
	g->thr = lc_thread_start( pmp_run, g );
	return g;
}

static void pmp_stop( pmp_t *g )
{
	g->stop = 1;
	lc_thread_join( g->thr );
	lc_sock_close( g->s );
	free( g );
}

/* ---- a fake UPnP IGD: HTTP on loopback ---- */
typedef struct {
	lc_sock s;
	int port;
	volatile int stop;
	lc_thread *thr;
	int addError;				/* the first AddPortMapping fails with this UPnP error (0: none) */
	int alwaysError;			/* every AddPortMapping fails with this */
	volatile int adds, deletes, descs;
	volatile int lastExt, lastLease;
	char lastClient[40];
	char extIp[32];
} igd_t;

static void http_reply( lc_sock c, int status, const char *body )
{
	char head[256];
	int n = lc_snprintf( head, sizeof( head ), "HTTP/1.1 %d X\r\nContent-Type: text/xml\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", status, (int)strlen( body ) );
	send( c, head, n, 0 );
	send( c, body, (int)strlen( body ), 0 );
}

static const char DESC[] =
	"<?xml version=\"1.0\"?><root xmlns=\"urn:schemas-upnp-org:device-1-0\"><device><deviceType>urn:schemas-upnp-org:device:InternetGatewayDevice:1</deviceType>"
	"<deviceList><device><deviceType>urn:schemas-upnp-org:device:WANDevice:1</deviceType><deviceList><device>"
	"<deviceType>urn:schemas-upnp-org:device:WANConnectionDevice:1</deviceType><serviceList>"
	"<service><serviceType>urn:schemas-upnp-org:service:WANCommonInterfaceConfig:1</serviceType><controlURL>/ctl/Common</controlURL></service>"
	"<service><serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType><serviceId>urn:upnp-org:serviceId:WANIPConn1</serviceId>"
	"<controlURL>/ctl/IPConn</controlURL><eventSubURL>/evt/IPConn</eventSubURL></service>"
	"</serviceList></device></deviceList></device></deviceList></device></root>";

static void igd_run( void *u )
{
	igd_t *g = (igd_t *)u;

	while ( !g->stop ) {
		lc_sock c;
		char req[4096];
		int n = 0, got;
		char *body;

		if ( lc_sock_wait_readable( g->s, 50 ) <= 0 )
			continue;
		c = accept( g->s, NULL, NULL );
		if ( c == LC_BADSOCK )
			continue;
		/* read the whole request: headers and Content-Length bytes of body */
		for ( ;; ) {
			char *he;
			if ( lc_sock_wait_readable( c, 1000 ) <= 0 )
				break;
			got = (int)recv( c, req + n, (int)sizeof( req ) - 1 - n, 0 );
			if ( got <= 0 )
				break;
			n += got;
			req[n] = 0;
			he = strstr( req, "\r\n\r\n" );
			if ( he ) {
				const char *cl = strstr( req, "Content-Length:" );
				int want = cl ? atoi( cl + 15 ) : 0;
				if ( n >= (int)( he + 4 - req ) + want )
					break;
			}
		}
		req[n] = 0;
		body = strstr( req, "\r\n\r\n" );
		body = body ? body + 4 : req + n;
		if ( !strncmp( req, "GET /desc.xml", 13 ) ) {
			g->descs++;
			http_reply( c, 200, DESC );
		} else if ( !strncmp( req, "POST /ctl/IPConn", 16 ) ) {
			if ( strstr( req, "#GetExternalIPAddress" ) ) {
				char resp[512];
				lc_snprintf( resp, sizeof( resp ), "<s:Envelope><s:Body><u:GetExternalIPAddressResponse><NewExternalIPAddress>%s</NewExternalIPAddress></u:GetExternalIPAddressResponse></s:Body></s:Envelope>", g->extIp );
				http_reply( c, 200, resp );
			} else if ( strstr( req, "#AddPortMapping" ) ) {
				const char *p = strstr( body, "<NewExternalPort>" );
				const char *l = strstr( body, "<NewLeaseDuration>" );
				const char *cl = strstr( body, "<NewInternalClient>" );
				int err = 0;
				g->lastExt = p ? atoi( p + 17 ) : 0;
				g->lastLease = l ? atoi( l + 18 ) : -1;
				if ( cl ) {
					size_t len = strcspn( cl + 19, "<" );
					if ( len > 39 )
						len = 39;
					memcpy( g->lastClient, cl + 19, len );
					g->lastClient[len] = 0;
				}
				if ( g->alwaysError )
					err = g->alwaysError;
				else if ( g->addError == 725 && g->lastLease != 0 )		/* only permanent leases */
					err = 725;
				if ( err ) {
					char resp[512];
					lc_snprintf( resp, sizeof( resp ), "<s:Envelope><s:Body><s:Fault><detail><UPnPError><errorCode>%d</errorCode></UPnPError></detail></s:Fault></s:Body></s:Envelope>", err );
					http_reply( c, 500, resp );
				} else {
					g->adds++;
					http_reply( c, 200, "<s:Envelope><s:Body><u:AddPortMappingResponse/></s:Body></s:Envelope>" );
				}
			} else if ( strstr( req, "#DeletePortMapping" ) ) {
				g->deletes++;
				http_reply( c, 200, "<s:Envelope><s:Body><u:DeletePortMappingResponse/></s:Body></s:Envelope>" );
			} else
				http_reply( c, 500, "" );
		} else
			http_reply( c, 404, "" );
		lc_sock_close( c );
	}
}

static igd_t *igd_start( void )
{
	igd_t *g = (igd_t *)calloc( 1, sizeof( *g ) );
	struct sockaddr_in a;
	socklen_t al = sizeof( a );
	char loc[128];

	lc_strlcpy( g->extIp, "198.51.100.23", sizeof( g->extIp ) );
	g->s = socket( AF_INET, SOCK_STREAM, 0 );
	memset( &a, 0, sizeof( a ) );
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl( 0x7f000001u );
	bind( g->s, (struct sockaddr *)&a, sizeof( a ) );
	listen( g->s, 8 );
	getsockname( g->s, (struct sockaddr *)&a, &al );
	g->port = ntohs( a.sin_port );
	lc_snprintf( loc, sizeof( loc ), "http://127.0.0.1:%d/desc.xml", g->port );
	setenv_( "LC_TEST_UPNP_LOCATION", loc );
	g->thr = lc_thread_start( igd_run, g );
	return g;
}

static void igd_stop( igd_t *g )
{
	g->stop = 1;
	lc_thread_join( g->thr );
	lc_sock_close( g->s );
	free( g );
}

/* ---- callbacks ---- */
typedef struct {
	volatile int called;
	lc_nat_result r;
	char ip[32];
	int extPort, err;
} res_t;

static void map_cb( lc_nat_result r, const char *ip, int extPort, int err, void *u )
{
	res_t *x = (res_t *)u;
	x->r = r;
	lc_strlcpy( x->ip, ip ? ip : "", sizeof( x->ip ) );
	x->extPort = extPort;
	x->err = err;
	x->called++;
}

static void addr_cb( lc_nat_result r, const char *ip, int err, void *u )
{
	map_cb( r, ip, 0, err, u );
}

static int wait_called( res_t *x, double seconds )
{
	double t0 = lc_now( );
	while ( !x->called && lc_now( ) - t0 < seconds )
		lc_sleep_ms( 20 );
	return x->called;
}

static void test_pmp( void )
{
	pmp_t *g;
	res_t r;
	lc_nat *n;

	setenv_( "LC_TEST_GATEWAY", "127.0.0.1" );
	setenv_( "LC_TEST_UPNP_LOCATION", NULL );

	/* a mapping: same port out, public address from the address request */
	g = pmp_start( 0 );
	memset( &r, 0, sizeof( r ) );
	n = lc_nat_map_udp( 27960, map_cb, &r );
	CHECK( n != NULL && wait_called( &r, 10 ) );
	CHECK( r.r == LC_NAT_OK && !strcmp( r.ip, "203.0.113.7" ) && r.extPort == 27960 );
	CHECK( g->maps == 1 && g->lastInternal == 27960 && g->lastSuggested == 27960 && g->lastLifetime == 7200 );
	lc_nat_release( n );					/* lifetime 0 for the same internal port */
	CHECK( g->deletes == 1 && g->lastInternal == 27960 && g->lastLifetime == 0 );
	pmp_stop( g );

	/* the address only */
	g = pmp_start( 0 );
	memset( &r, 0, sizeof( r ) );
	n = lc_nat_public_address( addr_cb, &r );
	CHECK( n != NULL && wait_called( &r, 10 ) );
	CHECK( r.r == LC_NAT_OK && !strcmp( r.ip, "203.0.113.7" ) );
	CHECK( g->maps == 0 && g->addrReqs == 1 );
	lc_nat_release( n );
	pmp_stop( g );

	/* a private external address: mapped, but behind a second NAT */
	g = pmp_start( 4 );
	memset( &r, 0, sizeof( r ) );
	n = lc_nat_map_udp( 27961, map_cb, &r );
	CHECK( n != NULL && wait_called( &r, 10 ) );
	CHECK( r.r == LC_NAT_PRIVATE_EXTERNAL && !strcmp( r.ip, "192.168.7.7" ) );
	lc_nat_release( n );
	pmp_stop( g );

	/* result 2: the router says no */
	g = pmp_start( 1 );
	memset( &r, 0, sizeof( r ) );
	n = lc_nat_map_udp( 27960, map_cb, &r );
	CHECK( n != NULL && wait_called( &r, 10 ) && r.r == LC_NAT_DISABLED );
	lc_nat_release( n );
	pmp_stop( g );

	/* result 4: another error number */
	g = pmp_start( 5 );
	memset( &r, 0, sizeof( r ) );
	n = lc_nat_map_udp( 27960, map_cb, &r );
	CHECK( n != NULL && wait_called( &r, 10 ) && r.r == LC_NAT_OTHER && r.err == 4 );
	lc_nat_release( n );
	pmp_stop( g );

	/* released from inside the callback */
	g = pmp_start( 0 );
	{
		/* callback calling lc_nat_release is exercised by the public-address module below */
		pmp_stop( g );
	}

	/* unsupported (result 1), then UPnP finds nothing: the verdict is "unsupported" */
	g = pmp_start( 2 );
	setenv_( "LC_TEST_UPNP_LOCATION", "http://127.0.0.1:1/none.xml" );		/* nothing listens: the description fetch fails */
	memset( &r, 0, sizeof( r ) );
	n = lc_nat_map_udp( 27960, map_cb, &r );
	CHECK( n != NULL && wait_called( &r, 12 ) && r.r == LC_NAT_UNSUPPORTED );
	lc_nat_release( n );
	pmp_stop( g );
	setenv_( "LC_TEST_UPNP_LOCATION", NULL );
}

static void test_upnp( void )
{
	igd_t *g;
	res_t r;
	lc_nat *n;
	pmp_t *silent;

	setenv_( "LC_TEST_GATEWAY", "127.0.0.1" );
	/* NAT-PMP silent: falls through to UPnP */
	silent = pmp_start( 3 );
	g = igd_start();
	memset( &r, 0, sizeof( r ) );
	n = lc_nat_map_udp( 27960, map_cb, &r );
	CHECK( n != NULL && wait_called( &r, 20 ) );
	CHECK( r.r == LC_NAT_OK && !strcmp( r.ip, "198.51.100.23" ) && r.extPort == 27960 );
	CHECK( g->adds == 1 && g->lastExt == 27960 && g->lastLease == 7200 && g->descs >= 1 );
	CHECK( !strcmp( g->lastClient, "127.0.0.1" ) );
	lc_nat_release( n );
	CHECK( g->deletes == 1 );
	igd_stop( g );

	/* only permanent leases (725): retried with lease 0 */
	g = igd_start();
	g->addError = 725;
	memset( &r, 0, sizeof( r ) );
	n = lc_nat_map_udp( 27962, map_cb, &r );
	CHECK( n != NULL && wait_called( &r, 20 ) && r.r == LC_NAT_OK );
	CHECK( g->lastLease == 0 && g->lastExt == 27962 );
	lc_nat_release( n );
	igd_stop( g );

	/* not authorized (606): disabled in the router */
	g = igd_start();
	g->alwaysError = 606;
	memset( &r, 0, sizeof( r ) );
	n = lc_nat_map_udp( 27960, map_cb, &r );
	CHECK( n != NULL && wait_called( &r, 20 ) && r.r == LC_NAT_DISABLED );
	lc_nat_release( n );
	igd_stop( g );

	/* another error: its number */
	g = igd_start();
	g->alwaysError = 402;
	memset( &r, 0, sizeof( r ) );
	n = lc_nat_map_udp( 27960, map_cb, &r );
	CHECK( n != NULL && wait_called( &r, 20 ) && r.r == LC_NAT_OTHER && r.err == 402 );
	lc_nat_release( n );
	igd_stop( g );

	/* a private external address */
	g = igd_start();
	lc_strlcpy( g->extIp, "10.1.2.3", sizeof( g->extIp ) );
	memset( &r, 0, sizeof( r ) );
	n = lc_nat_map_udp( 27960, map_cb, &r );
	CHECK( n != NULL && wait_called( &r, 20 ) && r.r == LC_NAT_PRIVATE_EXTERNAL && !strcmp( r.ip, "10.1.2.3" ) );
	lc_nat_release( n );
	igd_stop( g );

	/* the address only, over UPnP; no mapping is made */
	g = igd_start();
	memset( &r, 0, sizeof( r ) );
	n = lc_nat_public_address( addr_cb, &r );
	CHECK( n != NULL && wait_called( &r, 20 ) && r.r == LC_NAT_OK && !strcmp( r.ip, "198.51.100.23" ) );
	CHECK( g->adds == 0 );
	lc_nat_release( n );
	igd_stop( g );
	pmp_stop( silent );
	setenv_( "LC_TEST_UPNP_LOCATION", NULL );
}

static void test_no_router( void )
{
	res_t r;
	lc_nat *n;
	pmp_t *silent = pmp_start( 3 );

	/* silence from NAT-PMP and no UPnP device: a timeout verdict, never a hang */
	setenv_( "LC_TEST_GATEWAY", "127.0.0.1" );
	setenv_( "LC_TEST_UPNP_LOCATION", "http://127.0.0.1:1/none.xml" );
	memset( &r, 0, sizeof( r ) );
	n = lc_nat_map_udp( 27960, map_cb, &r );
	CHECK( n != NULL && wait_called( &r, 14 ) );
	CHECK( r.r == LC_NAT_TIMEOUT );
	lc_nat_release( n );
	pmp_stop( silent );
	setenv_( "LC_TEST_UPNP_LOCATION", NULL );
}

static void test_public_module( void )
{
	pmp_t *g;

	setenv_( "VOYAGERSP_TEST_NAT", "203.0.113.50" );
	CHECK( lc_public_address( ) == NULL );
	lc_public_address_start( );
	CHECK( lc_public_address( ) && !strcmp( lc_public_address( ), "203.0.113.50" ) );
	CHECK( lc_public_address_wait( 100 ) );
	setenv_( "VOYAGERSP_TEST_NAT", NULL );
	(void)g;
}

static void test_lan( void )
{
	char a[64], addrs[8][16], names[8][64];
	int n;

	setenv_( "LC_TEST_GATEWAY", NULL );
	n = lc_lan_addresses( addrs, names, 8 );
	CHECK( n >= 0 );
	if ( n > 0 ) {
		struct in_addr ia;
		CHECK( inet_pton( AF_INET, addrs[0], &ia ) == 1 );
		CHECK( names[0][0] != 0 );
		CHECK( strncmp( addrs[0], "127.", 4 ) != 0 && strncmp( addrs[0], "169.254.", 8 ) != 0 );
	}
	if ( lc_lan_address( a, sizeof( a ) ) )
		CHECK( strlen( a ) >= 7 );
	fprintf( stderr, "LAN addresses: %d (primary %s)\n", n, n > 0 ? addrs[0] : "-" );
}

int main( void )
{
	lc_net_init( );
	test_pmp( );
	test_upnp( );
	test_no_router( );
	test_public_module( );
	test_lan( );
	return TEST_DONE( );
}
