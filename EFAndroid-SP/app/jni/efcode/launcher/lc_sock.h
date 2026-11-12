/*
===========================================================================
Launcher core, internal: the little socket portability layer lc_net.c, lc_nat.c
and lc_coop.c share (Winsock on Windows, BSD sockets elsewhere). Not a public
header: nothing outside efcode/launcher includes it.
===========================================================================
*/

#ifndef LC_SOCK_H
#define LC_SOCK_H

#include "lc_common.h"

#ifdef _WIN32
#	include <winsock2.h>
#	include <ws2tcpip.h>
typedef SOCKET lc_sock;
#	define LC_BADSOCK			INVALID_SOCKET
#	define lc_sock_close( s )	closesocket( s )
#	define lc_sock_errno()		WSAGetLastError()
#	define LC_EWOULDBLOCK		WSAEWOULDBLOCK
#	define LC_ECONNREFUSED		WSAECONNRESET	/* ICMP port unreachable on a connected UDP socket */
#else
#	include <errno.h>
#	include <fcntl.h>
#	include <unistd.h>
#	include <poll.h>
#	include <sys/types.h>
#	include <sys/socket.h>
#	include <netinet/in.h>
#	include <arpa/inet.h>
#	include <netdb.h>
typedef int lc_sock;
#	define LC_BADSOCK			(-1)
#	define lc_sock_close( s )	close( s )
#	define lc_sock_errno()		errno
#	define LC_EWOULDBLOCK		EWOULDBLOCK
#	define LC_ECONNREFUSED		ECONNREFUSED
#endif

/* wait up to ms for data to read on s: >0 readable, 0 timeout, <0 error */
static inline int lc_sock_wait_readable( lc_sock s, int ms )
{
#ifdef _WIN32
	WSAPOLLFD p;
	p.fd = s;
	p.events = POLLRDNORM;
	p.revents = 0;
	return WSAPoll( &p, 1, ms );
#else
	struct pollfd p;
	p.fd = s;
	p.events = POLLIN;
	p.revents = 0;
	return poll( &p, 1, ms );
#endif
}

static inline int lc_sock_wait_writable( lc_sock s, int ms )
{
#ifdef _WIN32
	WSAPOLLFD p;
	p.fd = s;
	p.events = POLLWRNORM;
	p.revents = 0;
	return WSAPoll( &p, 1, ms );
#else
	struct pollfd p;
	p.fd = s;
	p.events = POLLOUT;
	p.revents = 0;
	return poll( &p, 1, ms );
#endif
}

static inline int lc_sock_set_nonblock( lc_sock s )
{
#ifdef _WIN32
	u_long on = 1;
	return ioctlsocket( s, FIONBIO, &on );
#else
	int fl = fcntl( s, F_GETFL, 0 );
	return fcntl( s, F_SETFL, fl | O_NONBLOCK );
#endif
}

/* "a.b.c.d:port" */
static inline void lc_sock_addr_string( const struct sockaddr_in *a, char *out, size_t n )
{
	unsigned ip = ntohl( a->sin_addr.s_addr );
	lc_snprintf( out, n, "%u.%u.%u.%u:%u", ip >> 24, ( ip >> 16 ) & 255, ( ip >> 8 ) & 255, ip & 255, (unsigned)ntohs( a->sin_port ) );
}

#endif
