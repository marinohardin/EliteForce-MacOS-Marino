/*
===========================================================================
Single-player co-op on Windows and Linux: what sys_coop_net.c (written for macOS,
compiled here unchanged) needs from the platform. The build force-includes this
header (-include) when it compiles sys_coop_net.c, and puts sys/coop_compat/ (and
on Windows sys/coop_compat/win32/) on the include path for the POSIX headers that
file names. Never included on macOS.

  - compression_encode_buffer / compression_decode_buffer with COMPRESSION_ZLIB:
    Apple's COMPRESSION_ZLIB is raw DEFLATE (RFC 1951) without the zlib header and
    checksum, level 5; here zlib's deflateInit2( level 5, windowBits -15 ) and
    inflateInit2( -15 ). Any conforming raw-deflate stream interoperates.
  - arc4random: the operating system's random numbers.
  - Windows: Winsock for the BSD socket calls (close, fcntl( O_NONBLOCK ), errno).
    An ICMP "port unreachable" on a connected UDP socket is WSAECONNRESET there; errno
    reads as ECONNREFUSED for it, which is what the guest looks for.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#ifndef SYS_COOP_COMPAT_H
#define SYS_COOP_COMPAT_H

#if defined( __APPLE__ ) && !defined( LC_COOP_COMPAT_TEST )
#error sys_coop_compat.h is not for macOS (it has libcompression and arc4random).
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32

#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

#define close( fd )				closesocket( fd )

#ifndef F_SETFL
#define F_SETFL					4
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK				0x4000
#endif
static inline int coopCompatNonBlock( int fd )
{
	u_long on = 1;
	return ioctlsocket( (SOCKET)fd, FIONBIO, &on ) ? -1 : 0;
}
#define fcntl( fd, cmd, flags )	coopCompatNonBlock( fd )

static inline int coopCompatErrno( void )
{
	int e = WSAGetLastError();
	return e == WSAECONNRESET ? ECONNREFUSED : e;
}
#undef errno
#define errno					( coopCompatErrno() )

extern BOOLEAN WINAPI SystemFunction036( PVOID buffer, ULONG length );		// RtlGenRandom
static inline unsigned int coopCompatRandom( void )
{
	unsigned int v = 0;
	SystemFunction036( &v, sizeof( v ) );
	return v;
}

#else // Linux

static inline unsigned int coopCompatRandom( void )
{
	unsigned int v = 0;
	FILE *f = fopen( "/dev/urandom", "rb" );

	if ( !f || fread( &v, sizeof( v ), 1, f ) != 1 )
		v = (unsigned int)rand();
	if ( f )
		fclose( f );
	return v;
}

#endif

#define arc4random				coopCompatRandom

/* ---- Compression: zlib raw deflate / inflate ------------------------------------------------ */

#include "zlib.h"

#define COMPRESSION_ZLIB		0x205

// the replica frames are compressed once per game frame: keep the (big) deflate state
static inline size_t compression_encode_buffer( uint8_t *dst, size_t dstSize, const uint8_t *src, size_t srcSize,
	void *scratch, int algorithm )
{
	static z_stream z;
	static int ready;
	size_t out;
	int r;

	(void)scratch;
	if ( algorithm != COMPRESSION_ZLIB || srcSize > 0x7fffffffu || dstSize > 0x7fffffffu )
		return 0;
	if ( !ready )
	{
		memset( &z, 0, sizeof( z ) );
		if ( deflateInit2( &z, 5, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY ) != Z_OK )
			return 0;
		ready = 1;
	}
	else if ( deflateReset( &z ) != Z_OK )
		return 0;
	z.next_in = (Bytef *)src;
	z.avail_in = (uInt)srcSize;
	z.next_out = (Bytef *)dst;
	z.avail_out = (uInt)dstSize;
	r = deflate( &z, Z_FINISH );
	out = (size_t)z.total_out;
	return r == Z_STREAM_END ? out : 0;			// 0 when dst was too small, as libcompression
}

static inline size_t compression_decode_buffer( uint8_t *dst, size_t dstSize, const uint8_t *src, size_t srcSize,
	void *scratch, int algorithm )
{
	static z_stream z;
	static int ready;
	int r;

	(void)scratch;
	if ( algorithm != COMPRESSION_ZLIB || srcSize > 0x7fffffffu || dstSize > 0x7fffffffu )
		return 0;
	if ( !ready )
	{
		memset( &z, 0, sizeof( z ) );
		if ( inflateInit2( &z, -15 ) != Z_OK )
			return 0;
		ready = 1;
	}
	else if ( inflateReset( &z ) != Z_OK )
		return 0;
	z.next_in = (Bytef *)src;
	z.avail_in = (uInt)srcSize;
	z.next_out = (Bytef *)dst;
	z.avail_out = (uInt)dstSize;
	r = inflate( &z, Z_FINISH );
	return r == Z_STREAM_END ? (size_t)z.total_out : 0;
}

#endif // SYS_COOP_COMPAT_H
