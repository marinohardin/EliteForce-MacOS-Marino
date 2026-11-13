/*
===========================================================================
SHA-1 (FIPS 180-1) and MD5 (RFC 1321), small and self-contained, for the GOG
installer reader (sys_osx_gog.c) where CommonCrypto isn't available.

Written for this project from the specifications. Public domain: anyone may
use, copy, modify and distribute it for any purpose, without any conditions.
===========================================================================
*/

#ifndef SYS_DIGEST_H
#define SYS_DIGEST_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define DG_SHA1_DIGEST_LENGTH	20
#define DG_MD5_DIGEST_LENGTH	16

typedef struct {
	uint32_t	h[5];
	uint64_t	bytes;
	uint8_t		block[64];
	size_t		used;
} dg_sha1_t;

typedef struct {
	uint32_t	h[4];
	uint64_t	bytes;
	uint8_t		block[64];
	size_t		used;
} dg_md5_t;

#define DG_ROL( x, n )	( ( (x) << (n) ) | ( (x) >> ( 32 - (n) ) ) )

/* ---- SHA-1 ---- */

static void dg_sha1_block( dg_sha1_t *c, const uint8_t *p )
{
	uint32_t w[80], a, b, d, e, f, k, t, cc;
	int i;

	for( i = 0; i < 16; i++ )
		w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
	for( i = 16; i < 80; i++ )
		w[i] = DG_ROL( w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1 );

	a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3]; e = c->h[4];
	for( i = 0; i < 80; i++ )
	{
		if( i < 20 )		{ f = ( b & cc ) | ( ~b & d );			k = 0x5A827999; }
		else if( i < 40 )	{ f = b ^ cc ^ d;						k = 0x6ED9EBA1; }
		else if( i < 60 )	{ f = ( b & cc ) | ( b & d ) | ( cc & d );	k = 0x8F1BBCDC; }
		else				{ f = b ^ cc ^ d;						k = 0xCA62C1D6; }
		t = DG_ROL( a, 5 ) + f + e + k + w[i];
		e = d; d = cc; cc = DG_ROL( b, 30 ); b = a; a = t;
	}
	c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e;
}

static void dg_sha1_init( dg_sha1_t *c )
{
	c->h[0] = 0x67452301; c->h[1] = 0xEFCDAB89; c->h[2] = 0x98BADCFE;
	c->h[3] = 0x10325476; c->h[4] = 0xC3D2E1F0;
	c->bytes = 0;
	c->used = 0;
}

static void dg_sha1_update( dg_sha1_t *c, const void *data, size_t n )
{
	const uint8_t *p = (const uint8_t *)data;

	c->bytes += n;
	while( n )
	{
		size_t take = 64 - c->used < n ? 64 - c->used : n;
		memcpy( c->block + c->used, p, take );
		c->used += take;
		p += take;
		n -= take;
		if( c->used == 64 )
		{
			dg_sha1_block( c, c->block );
			c->used = 0;
		}
	}
}

static void dg_sha1_final( uint8_t out[DG_SHA1_DIGEST_LENGTH], dg_sha1_t *c )
{
	uint64_t bits = c->bytes * 8;
	uint8_t pad = 0x80, zero = 0, len[8];
	int i;

	dg_sha1_update( c, &pad, 1 );
	while( c->used != 56 )
		dg_sha1_update( c, &zero, 1 );
	for( i = 0; i < 8; i++ )
		len[i] = (uint8_t)( bits >> ( 56 - i * 8 ) );
	dg_sha1_update( c, len, 8 );
	for( i = 0; i < 20; i++ )
		out[i] = (uint8_t)( c->h[i / 4] >> ( 24 - ( i % 4 ) * 8 ) );
}

/* ---- MD5 ---- */

static void dg_md5_block( dg_md5_t *c, const uint8_t *p )
{
	static const uint32_t K[64] = {
		0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
		0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
		0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
		0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
		0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
		0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
		0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
		0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
	};
	static const int R[64] = {
		7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
		5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
		4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
		6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
	};
	uint32_t m[16], a, b, cc, d, f, t;
	int i, g;

	for( i = 0; i < 16; i++ )
		m[i] = p[i * 4] | (uint32_t)p[i * 4 + 1] << 8 | (uint32_t)p[i * 4 + 2] << 16 | (uint32_t)p[i * 4 + 3] << 24;

	a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3];
	for( i = 0; i < 64; i++ )
	{
		if( i < 16 )		{ f = ( b & cc ) | ( ~b & d );	g = i; }
		else if( i < 32 )	{ f = ( d & b ) | ( ~d & cc );	g = ( 5 * i + 1 ) & 15; }
		else if( i < 48 )	{ f = b ^ cc ^ d;				g = ( 3 * i + 5 ) & 15; }
		else				{ f = cc ^ ( b | ~d );			g = ( 7 * i ) & 15; }
		t = d;
		d = cc;
		cc = b;
		b = b + DG_ROL( a + f + K[i] + m[g], R[i] );
		a = t;
	}
	c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d;
}

static void dg_md5_init( dg_md5_t *c )
{
	c->h[0] = 0x67452301; c->h[1] = 0xefcdab89; c->h[2] = 0x98badcfe; c->h[3] = 0x10325476;
	c->bytes = 0;
	c->used = 0;
}

static void dg_md5_update( dg_md5_t *c, const void *data, size_t n )
{
	const uint8_t *p = (const uint8_t *)data;

	c->bytes += n;
	while( n )
	{
		size_t take = 64 - c->used < n ? 64 - c->used : n;
		memcpy( c->block + c->used, p, take );
		c->used += take;
		p += take;
		n -= take;
		if( c->used == 64 )
		{
			dg_md5_block( c, c->block );
			c->used = 0;
		}
	}
}

static void dg_md5_final( uint8_t out[DG_MD5_DIGEST_LENGTH], dg_md5_t *c )
{
	uint64_t bits = c->bytes * 8;
	uint8_t pad = 0x80, zero = 0, len[8];
	int i;

	dg_md5_update( c, &pad, 1 );
	while( c->used != 56 )
		dg_md5_update( c, &zero, 1 );
	for( i = 0; i < 8; i++ )
		len[i] = (uint8_t)( bits >> ( i * 8 ) );
	dg_md5_update( c, len, 8 );
	for( i = 0; i < 16; i++ )
		out[i] = (uint8_t)( c->h[i / 4] >> ( ( i % 4 ) * 8 ) );
}

#undef DG_ROL

#endif
