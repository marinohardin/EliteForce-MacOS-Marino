/* sys_coop_compat.h: the zlib stand-ins for libcompression's COMPRESSION_ZLIB (raw deflate, no header, no
   checksum), as sys_coop_net.c uses them for replica frames; arc4random. The engine's zlib (inflate side)
   and the vendored deflate sources are linked in by this test's CMake entry. (launcher-data-coop.md 6.5) */
#define LC_COOP_COMPAT_TEST 1
#include "sys_coop_compat.h"
#include "lc_server.h"		/* lc_inflate_raw: an independent inflater to cross-check the stream format */
#include "lc_test.h"

static void fill_frame( unsigned char *p, size_t n )
{
	/* like a replica frame: runs, small integers, some noise */
	size_t i;
	unsigned x = 12345;
	for ( i = 0; i < n; i++ ) {
		x = x * 1103515245u + 12345u;
		p[i] = ( i % 64 < 40 ) ? (unsigned char)( i / 64 ) : (unsigned char)( x >> 16 );
	}
}

int main( void )
{
	enum { N = 60000 };
	static unsigned char src[N], z[N + 4096], back[N], z2[N + 4096];
	size_t zlen, got, i;
	unsigned int r1, r2;
	int same = 0;

	fill_frame( src, N );
	zlen = compression_encode_buffer( z, sizeof( z ), src, N, NULL, COMPRESSION_ZLIB );
	CHECK( zlen > 0 && zlen < N );
	/* raw deflate: no zlib header (0x78 ..) is required, and our own inflater reads it */
	CHECK( lc_inflate_raw( z, zlen, back, N ) == N && !memcmp( src, back, N ) );
	memset( back, 0, sizeof( back ) );
	got = compression_decode_buffer( back, N, z, zlen, NULL, COMPRESSION_ZLIB );
	CHECK( got == N && !memcmp( src, back, N ) );

	/* the encoder is reused every frame: a second, different buffer must not see the first */
	fill_frame( src + 100, 5000 );
	zlen = compression_encode_buffer( z2, sizeof( z2 ), src, 5100, NULL, COMPRESSION_ZLIB );
	CHECK( zlen > 0 );
	memset( back, 0, sizeof( back ) );
	CHECK( compression_decode_buffer( back, 5100, z2, zlen, NULL, COMPRESSION_ZLIB ) == 5100 && !memcmp( src, back, 5100 ) );

	/* too small an output buffer: 0, as libcompression */
	CHECK( compression_encode_buffer( z, 10, src, N, NULL, COMPRESSION_ZLIB ) == 0 );
	zlen = compression_encode_buffer( z, sizeof( z ), src, N, NULL, COMPRESSION_ZLIB );
	CHECK( compression_decode_buffer( back, N - 1, z, zlen, NULL, COMPRESSION_ZLIB ) == 0 );
	/* damaged or truncated input */
	CHECK( compression_decode_buffer( back, N, z, zlen / 2, NULL, COMPRESSION_ZLIB ) == 0 );
	z[zlen / 3] ^= 0xff;
	z[zlen / 3 + 1] ^= 0x55;
	got = compression_decode_buffer( back, N, z, zlen, NULL, COMPRESSION_ZLIB );
	CHECK( got == 0 || memcmp( src, back, N ) != 0 || got == N );		/* must not crash */
	/* an empty and a tiny buffer */
	zlen = compression_encode_buffer( z, sizeof( z ), src, 0, NULL, COMPRESSION_ZLIB );
	CHECK( zlen > 0 );
	CHECK( compression_decode_buffer( back, 8, z, zlen, NULL, COMPRESSION_ZLIB ) == 0 );		/* zero bytes decode to 0 */
	zlen = compression_encode_buffer( z, sizeof( z ), src, 1, NULL, COMPRESSION_ZLIB );
	CHECK( compression_decode_buffer( back, 1, z, zlen, NULL, COMPRESSION_ZLIB ) == 1 && back[0] == src[0] );
	/* the unknown algorithm */
	CHECK( compression_encode_buffer( z, sizeof( z ), src, 10, NULL, 0x100 ) == 0 );

	/* arc4random */
	for ( i = 0; i < 16; i++ ) {
		r1 = arc4random( );
		r2 = arc4random( );
		same += r1 == r2;
	}
	CHECK( same < 3 );
	return TEST_DONE( );
}
