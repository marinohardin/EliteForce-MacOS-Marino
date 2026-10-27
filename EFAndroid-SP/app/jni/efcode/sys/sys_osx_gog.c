/*
===========================================================================
macOS app bundle: the game files from GOG's Windows installer.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).

GOG sells Elite Force as a Windows setup .exe, an
Inno Setup 5 installer, which a Mac can't run. The launcher's Choose… and
Add Game Files… accept it and take BaseEF's .pk3 files out of it (pak0-pak3,
so the 1.2 patch and the Expansion Pack, and playermaps.pk3).

How GOG's installers are laid out (found by reading one; no code taken from
any installer tool):
  - the loader's offset table ("rDlPtS…") gives where the setup data
    (Offset0) and the file data (Offset1) start;
  - the setup data begins with "Inno Setup Setup Data (5.x.y) (u)", then two
    blocks, each CRC32 + size + a compressed flag, then the data in 4096-byte
    pieces each behind its own CRC32, LZMA1-compressed (5 property bytes,
    then the stream): the setup header with its entries, and the file
    locations (74 bytes each: where the data is, its sizes and SHA-1);
  - GOG splits each file into parts of up to 10 MB, zlib-compresses each part
    itself and stores it uncompressed in the installer ("zlb\x1a" then the
    data). A file entry's AfterInstall says after_install('<md5 of the
    part>', <stored size>, <size>); the first part's BeforeInstall says
    before_install('<md5 of the file>', '<path>', <parts>). After those two
    strings come MinVersion and OnlyBelowVersion (10 bytes each), then the
    entry's location index.
Every step is checked (CRCs, sizes, the SHA-1 of each stored part, the MD5
of each part and of each whole file). Anything unexpected is an error with a
reason, never a guess: the launcher then asks for a BaseEF folder instead.

Decompression uses the system's liblzma (no header in the SDK, so the few
declarations are below) for the setup data, and the engine's own zlib.
===========================================================================
*/

#ifndef __APPLE__
#error This file is for macOS only.
#endif

#include "sys_osx_gog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <CommonCrypto/CommonDigest.h>
#include "../zlib/zlib.h"

// liblzma 5 (/usr/lib/liblzma.5.dylib), the parts used here
typedef uint64_t lzma_vli;
typedef struct { lzma_vli id; void *options; } lzma_filter;
typedef struct {
	const uint8_t *next_in; size_t avail_in; uint64_t total_in;
	uint8_t *next_out; size_t avail_out; uint64_t total_out;
	const void *allocator; void *internal;
	void *reserved_ptr1, *reserved_ptr2, *reserved_ptr3, *reserved_ptr4;
	uint64_t seek_pos, reserved_int2;
	size_t reserved_int3, reserved_int4;
	int reserved_enum1, reserved_enum2;
} lzma_stream;
#define LZMA_OK				0
#define LZMA_STREAM_END		1
#define LZMA_RUN			0
#define LZMA_FILTER_LZMA1	0x4000000000000001ULL
#define LZMA_VLI_UNKNOWN	UINT64_MAX
extern int lzma_properties_decode( lzma_filter *filter, const void *allocator, const uint8_t *props, size_t props_size );
extern int lzma_raw_decoder( lzma_stream *strm, const lzma_filter *filters );
extern int lzma_code( lzma_stream *strm, int action );
extern void lzma_end( lzma_stream *strm );

#define TABLE_ID		"rDlPtS\xcd\xe6\xd7\x7b\x0b\x2a"
#define TABLE_SCAN		( 8 << 20 )		// the loader is small; the table is in its first MBs
#define SETUP_ID		"Inno Setup Setup Data (5."
#define LOCATION_SIZE	74
#define MAX_HEADER		( 64 << 20 )
#define CHUNK_ID		"zlb\x1a"

static void Fail( char *err, size_t errSize, const char *fmt, ... )
{
	va_list ap;

	va_start( ap, fmt );
	vsnprintf( err, errSize, fmt, ap );
	va_end( ap );
}

static int ReadAt( int fd, uint64_t at, void *buf, size_t n )
{
	size_t done = 0;

	while( done < n )
	{
		ssize_t r = pread( fd, (char *)buf + done, n - done, (off_t)( at + done ) );
		if( r <= 0 )
			return 0;
		done += (size_t)r;
	}
	return 1;
}

static uint32_t LE32( const uint8_t *p )	{ return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t LE64( const uint8_t *p )	{ return LE32( p ) | (uint64_t)LE32( p + 4 ) << 32; }

// One setup-data block at *at: checked, joined and LZMA1-decompressed. Moves
// *at past it. Returns a malloc'd buffer, or NULL with err set.
static uint8_t *ReadBlock( int fd, uint64_t *at, uint64_t fileSize, size_t *outLen, char *err, size_t errSize )
{
	uint8_t head[9], *stored = NULL, *raw = NULL, *out = NULL;
	uint32_t size, left;
	size_t rawLen = 0, cap;
	uint64_t p = *at;
	lzma_filter filters[2] = { { LZMA_FILTER_LZMA1, NULL }, { LZMA_VLI_UNKNOWN, NULL } };
	lzma_stream s;
	int r;

	if( !ReadAt( fd, p, head, 9 ) || crc32( 0, head + 4, 5 ) != LE32( head ) )
		return Fail( err, errSize, "its setup data is damaged (block header)" ), NULL;
	size = LE32( head + 4 );
	if( head[8] != 1 || size < 9 || p + 9 + size > fileSize || size > MAX_HEADER )
		return Fail( err, errSize, "its setup data is stored in a way this app doesn't know" ), NULL;
	if( !( stored = malloc( size ) ) || !ReadAt( fd, p + 9, stored, size ) )
		return free( stored ), Fail( err, errSize, "its setup data can't be read" ), NULL;
	// 4096-byte pieces, each behind its CRC32
	raw = malloc( size );
	for( left = size; raw && left > 0; )
	{
		const uint8_t *piece = stored + ( size - left );
		uint32_t n = left <= 4 ? 0 : left - 4 < 4096 ? left - 4 : 4096;
		if( !n || crc32( 0, piece + 4, n ) != LE32( piece ) )
		{
			free( stored ); free( raw );
			return Fail( err, errSize, "its setup data is damaged (checksum)" ), NULL;
		}
		memcpy( raw + rawLen, piece + 4, n );
		rawLen += n;
		left -= 4 + n;
	}
	free( stored );
	if( !raw || rawLen < 5 || lzma_properties_decode( &filters[0], NULL, raw, 5 ) != LZMA_OK )
	{
		free( raw );
		return Fail( err, errSize, "its setup data isn't compressed the way this app expects" ), NULL;
	}
	memset( &s, 0, sizeof( s ) );
	if( lzma_raw_decoder( &s, filters ) != LZMA_OK )
	{
		free( filters[0].options ); free( raw );
		return Fail( err, errSize, "the system's LZMA decoder couldn't start" ), NULL;
	}
	cap = rawLen * 4 + 65536;
	out = malloc( cap );
	s.next_in = raw + 5;
	s.avail_in = rawLen - 5;
	*outLen = 0;
	do
	{
		if( *outLen == cap )
		{
			uint8_t *bigger = cap * 2 <= MAX_HEADER ? realloc( out, cap * 2 ) : NULL;
			if( !bigger )
				break;
			out = bigger;
			cap *= 2;
		}
		s.next_out = out + *outLen;
		s.avail_out = cap - *outLen;
		r = lzma_code( &s, LZMA_RUN );
		*outLen = cap - s.avail_out;
	} while( r == LZMA_OK && ( s.avail_in > 0 || s.avail_out == 0 ) );
	lzma_end( &s );
	free( filters[0].options );
	free( raw );
	if( ( r != LZMA_OK && r != LZMA_STREAM_END ) || s.avail_in > 0 || !*outLen )
	{
		free( out );
		return Fail( err, errSize, "its setup data couldn't be decompressed" ), NULL;
	}
	*at = p + 9 + size;
	return out;
}

// A setup-header string (4-byte length, then UTF-16LE) at *q, as ASCII in
// buf. 0 if it doesn't fit in the header or isn't plain ASCII.
static int ReadString( const uint8_t *h, size_t hLen, size_t *q, char *buf, size_t bufSize )
{
	uint32_t len, i;

	if( *q + 4 > hLen )
		return 0;
	len = LE32( h + *q );
	if( ( len & 1 ) || *q + 4 + len > hLen || len / 2 >= bufSize )
		return 0;
	for( i = 0; i < len / 2; i++ )
	{
		uint16_t c = h[*q + 4 + i * 2] | h[*q + 5 + i * 2] << 8;
		if( c == 0 || c > 126 )
			return 0;
		buf[i] = (char)c;
	}
	buf[i] = 0;
	*q += 4 + len;
	return 1;
}

static int Hex16( const char *hex, uint8_t out[16] )
{
	for( int i = 0; i < 16; i++ )
	{
		unsigned v;
		if( sscanf( hex + i * 2, "%2x", &v ) != 1 )
			return 0;
		out[i] = (uint8_t)v;
	}
	return 1;
}

// after_install('<md5>', <stored>, <size>)
static int ParseAfter( const char *s, uint8_t md5[16], uint64_t *stored, uint64_t *size )
{
	char hex[33];
	unsigned long long a, b;

	if( sscanf( s, "after_install('%32[0-9a-f]', %llu, %llu)", hex, &a, &b ) != 3 || strlen( hex ) != 32 )
		return 0;
	*stored = a;
	*size = b;
	return Hex16( hex, md5 );
}

// before_install('<md5>', '<path>', <parts>)
static int ParseBefore( const char *s, uint8_t md5[16], char *path, size_t pathSize, int *parts )
{
	char hex[33], p[256];

	if( sscanf( s, "before_install('%32[0-9a-f]', '%255[^']', %d)", hex, p, parts ) != 3 || strlen( hex ) != 32 ||
		*parts < 1 || *parts > 4096 || strlen( p ) >= pathSize )
		return 0;
	strcpy( path, p );
	return Hex16( hex, md5 );
}

// "BaseEF\<name>.pk3", any case, no deeper folder: the name
static const char *BaseEFPk3( const char *path )
{
	const char *name = path + 7, *dot;

	if( strncasecmp( path, "BaseEF\\", 7 ) || strchr( name, '\\' ) || strchr( name, '/' ) || !*name )
		return NULL;
	dot = strrchr( name, '.' );
	return dot && !strcasecmp( dot, ".pk3" ) && strlen( name ) < sizeof( ((gogFile_t *)0)->name ) ? name : NULL;
}

void GOG_Free( gogInstaller_t *inst )
{
	if( !inst )
		return;
	for( int i = 0; i < inst->numFiles; i++ )
		free( inst->files[i].parts );
	free( inst->files );
	memset( inst, 0, sizeof( *inst ) );
}

int GOG_ReadInstaller( const char *path, gogInstaller_t *inst, char *err, size_t errSize )
{
	int fd = open( path, O_RDONLY ), ok = 0;
	struct stat st;
	uint8_t *scan = NULL, *h = NULL, *loc = NULL, id[64], table[40];
	size_t scanLen, hLen = 0, locLen = 0, q;
	uint64_t offset0, offset1, at;
	const uint8_t *t;
	gogFile_t *cur = NULL;

	memset( inst, 0, sizeof( *inst ) );
	if( fd < 0 || fstat( fd, &st ) )
	{
		Fail( err, errSize, "it can't be read (%s)", strerror( errno ) );
		goto done;
	}
	// the loader's offset table
	scanLen = st.st_size < TABLE_SCAN ? (size_t)st.st_size : TABLE_SCAN;
	if( scanLen < 2 || !( scan = malloc( scanLen ) ) || !ReadAt( fd, 0, scan, scanLen ) || scan[0] != 'M' || scan[1] != 'Z' )
	{
		Fail( err, errSize, "it isn't a Windows installer" );
		goto done;
	}
	t = memmem( scan, scanLen, TABLE_ID, 12 );
	if( !t || t + 40 > scan + scanLen )
	{
		Fail( err, errSize, "it isn't an Inno Setup installer like GOG's" );
		goto done;
	}
	memcpy( table, t, 40 );
	offset0 = LE32( table + 32 );
	offset1 = LE32( table + 36 );
	if( LE32( table + 12 ) != 1 || offset0 + 64 > (uint64_t)st.st_size || offset1 >= (uint64_t)st.st_size )
	{
		Fail( err, errSize, "it's an Inno Setup version this app doesn't know, or the download is incomplete" );
		goto done;
	}
	// the setup data: Inno Setup 5, Unicode
	if( !ReadAt( fd, offset0, id, 64 ) || memcmp( id, SETUP_ID, strlen( SETUP_ID ) ) || !memmem( id, 64, ") (u)", 5 ) )
	{
		Fail( err, errSize, "it's an Inno Setup version this app doesn't know (%.40s)", id[0] == 'I' ? (char *)id : "?" );
		goto done;
	}
	at = offset0 + 64;
	if( !( h = ReadBlock( fd, &at, st.st_size, &hLen, err, errSize ) ) ||
		!( loc = ReadBlock( fd, &at, st.st_size, &locLen, err, errSize ) ) )
		goto done;
	if( locLen % LOCATION_SIZE )
	{
		Fail( err, errSize, "its file list isn't laid out the way this app expects" );
		goto done;
	}

	// the file entries: find each AfterInstall string by its text
	for( q = 4; q + 30 < hLen; q++ )		// any byte: entries have odd-sized fields
	{
		static const uint8_t mark[] = { 'a',0,'f',0,'t',0,'e',0,'r',0,'_',0,'i',0,'n',0,'s',0,'t',0,'a',0,'l',0,'l',0,'(',0 };
		char after[256], before[512], path[256];
		uint8_t md5[16], partMd5[16];
		uint64_t stored, size;
		size_t p = q - 4;
		int parts;
		uint32_t index;
		const uint8_t *l;
		gogPart_t *part;

		if( memcmp( h + q, mark, sizeof( mark ) ) )
			continue;
		if( !ReadString( h, hLen, &p, after, sizeof( after ) ) || !ParseAfter( after, partMd5, &stored, &size ) ||
			!ReadString( h, hLen, &p, before, sizeof( before ) ) || p + 24 > hLen )
			continue;		// another script line that mentions it
		index = LE32( h + p + 20 );
		if( (uint64_t)index * LOCATION_SIZE >= locLen )
		{
			Fail( err, errSize, "its file list points outside itself" );
			goto done;
		}
		if( before[0] )
		{
			const char *name;
			cur = NULL;
			if( !ParseBefore( before, md5, path, sizeof( path ), &parts ) )
				continue;
			if( !( name = BaseEFPk3( path ) ) )
				continue;		// not a BaseEF .pk3: not needed
			inst->files = realloc( inst->files, sizeof( gogFile_t ) * ( inst->numFiles + 1 ) );
			cur = &inst->files[inst->numFiles++];
			memset( cur, 0, sizeof( *cur ) );
			strcpy( cur->name, name );
			memcpy( cur->md5, md5, 16 );
			cur->numParts = parts;
			cur->parts = calloc( parts, sizeof( gogPart_t ) );
		}
		if( !cur || cur->partsFound >= cur->numParts )
			continue;
		// where this part is stored
		l = loc + (size_t)index * LOCATION_SIZE;
		part = &cur->parts[cur->partsFound++];
		part->offset = offset1 + LE32( l + 8 ) + 4;
		part->stored = stored;
		part->size = size;
		memcpy( part->md5, partMd5, 16 );
		memcpy( part->sha1, l + 36, 20 );
		cur->size += size;
		// stored as is: not split across disks, not encrypted or compressed by Inno
		if( LE32( l ) != LE32( l + 4 ) || LE64( l + 12 ) != 0 || LE64( l + 20 ) != stored || LE64( l + 28 ) != stored ||
			part->offset + stored > (uint64_t)st.st_size )
		{
			Fail( err, errSize, "%s is stored in a way this app doesn't know, or the download is incomplete", cur->name );
			goto done;
		}
		q = p - 1;
	}
	for( int i = 0; i < inst->numFiles; i++ )
		if( inst->files[i].partsFound != inst->files[i].numParts )
		{
			Fail( err, errSize, "%s is incomplete in its file list", inst->files[i].name );
			goto done;
		}
	if( !inst->numFiles )
	{
		Fail( err, errSize, "it has no Elite Force game files (BaseEF\\*.pk3) that this app can find" );
		goto done;
	}
	ok = 1;
done:
	free( scan );
	free( h );
	free( loc );
	if( fd >= 0 )
		close( fd );
	if( !ok )
		GOG_Free( inst );
	return ok;
}

int GOG_ExtractFile( const char *installer, const gogFile_t *f, const char *to,
	volatile unsigned long long *progress, volatile signed char *cancel, char *err, size_t errSize )
{
	static uint8_t in[1 << 20], out[1 << 20];
	char partPath[1024];
	int fd = open( installer, O_RDONLY ), outFd = -1, ok = 0;
	CC_MD5_CTX fileMd5;
	uint8_t digest[CC_SHA1_DIGEST_LENGTH];

	snprintf( partPath, sizeof( partPath ), "%s.part", to );
	if( fd < 0 || ( outFd = open( partPath, O_WRONLY | O_CREAT | O_TRUNC, 0644 ) ) < 0 )
	{
		Fail( err, errSize, fd < 0 ? "Can't read the installer: %s" : "Can't write %s: %s",
			fd < 0 ? strerror( errno ) : partPath, strerror( errno ) );
		goto done;
	}
	CC_MD5_Init( &fileMd5 );
	for( int i = 0; i < f->numParts; i++ )
	{
		const gogPart_t *p = &f->parts[i];
		CC_SHA1_CTX sha;
		CC_MD5_CTX md5;
		z_stream z;
		uint64_t read = 0, written = 0;
		int zr = Z_OK;

		CC_SHA1_Init( &sha );
		CC_MD5_Init( &md5 );
		memset( &z, 0, sizeof( z ) );
		if( inflateInit( &z ) != Z_OK )
		{
			Fail( err, errSize, "zlib couldn't start" );
			goto done;
		}
		while( read < p->stored && zr != Z_STREAM_END )
		{
			size_t n = p->stored - read < sizeof( in ) ? (size_t)( p->stored - read ) : sizeof( in );
			if( *cancel )
			{
				inflateEnd( &z );
				Fail( err, errSize, "Cancelled." );
				goto done;
			}
			if( !ReadAt( fd, p->offset + read, in, n ) )
			{
				inflateEnd( &z );
				Fail( err, errSize, "Can't read the installer (%s)", strerror( errno ) );
				goto done;
			}
			CC_SHA1_Update( &sha, in, (CC_LONG)n );
			read += n;
			z.next_in = in;
			z.avail_in = (uInt)n;
			do
			{
				size_t got;
				z.next_out = out;
				z.avail_out = sizeof( out );
				zr = inflate( &z, Z_NO_FLUSH );
				if( zr != Z_OK && zr != Z_STREAM_END && zr != Z_BUF_ERROR )
					break;
				got = sizeof( out ) - z.avail_out;
				if( written + got > p->size || write( outFd, out, got ) != (ssize_t)got )
				{
					zr = Z_DATA_ERROR;
					break;
				}
				CC_MD5_Update( &md5, out, (CC_LONG)got );
				CC_MD5_Update( &fileMd5, out, (CC_LONG)got );
				written += got;
				*progress += got;
			} while( z.avail_out == 0 && zr != Z_STREAM_END );
			if( zr != Z_OK && zr != Z_STREAM_END && zr != Z_BUF_ERROR )
				break;
		}
		inflateEnd( &z );
		CC_SHA1_Final( digest, &sha );
		if( read != p->stored || memcmp( digest, p->sha1, CC_SHA1_DIGEST_LENGTH ) || zr != Z_STREAM_END || written != p->size )
		{
			Fail( err, errSize, "%s is damaged in the installer (part %d of %d)", f->name, i + 1, f->numParts );
			goto done;
		}
		CC_MD5_Final( digest, &md5 );
		if( memcmp( digest, p->md5, 16 ) )
		{
			Fail( err, errSize, "%s doesn't match its checksum (part %d of %d)", f->name, i + 1, f->numParts );
			goto done;
		}
	}
	CC_MD5_Final( digest, &fileMd5 );
	if( memcmp( digest, f->md5, 16 ) )
	{
		Fail( err, errSize, "%s doesn't match its checksum", f->name );
		goto done;
	}
	if( close( outFd ) )
	{
		outFd = -1;
		Fail( err, errSize, "Can't write %s: %s", partPath, strerror( errno ) );
		goto done;
	}
	outFd = -1;
	if( rename( partPath, to ) )
	{
		Fail( err, errSize, "Can't rename %s: %s", partPath, strerror( errno ) );
		goto done;
	}
	ok = 1;
done:
	if( outFd >= 0 )
		close( outFd );
	if( !ok )
		unlink( partPath );
	if( fd >= 0 )
		close( fd );
	return ok;
}
