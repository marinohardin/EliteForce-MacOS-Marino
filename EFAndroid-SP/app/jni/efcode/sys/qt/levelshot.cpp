/*
===========================================================================
ef-launcher (Qt 6): levelshot decoding. See levelshot.h.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#include "levelshot.h"

#include <QHash>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "jpeglib.h"
#include "lc_common.h"
}

namespace Ui {

namespace {

struct JErr {
	jpeg_error_mgr pub;
	jmp_buf jb;
};

void jerrExit( j_common_ptr c )
{
	longjmp( reinterpret_cast<JErr *>( c->err )->jb, 1 );
}

QImage decodeJpeg( const unsigned char *data, size_t len )
{
	jpeg_decompress_struct cinfo;
	JErr err;
	cinfo.err = jpeg_std_error( &err.pub );
	err.pub.error_exit = jerrExit;
	QImage img;
	if ( setjmp( err.jb ) ) {
		jpeg_destroy_decompress( &cinfo );
		return QImage();
	}
	jpeg_create_decompress( &cinfo );
	jpeg_mem_src( &cinfo, const_cast<unsigned char *>( data ), (unsigned long)len );
	jpeg_read_header( &cinfo, TRUE );
	cinfo.out_color_space = JCS_RGB;
	jpeg_start_decompress( &cinfo );
	img = QImage( int( cinfo.output_width ), int( cinfo.output_height ), QImage::Format_RGB888 );
	while ( cinfo.output_scanline < cinfo.output_height ) {
		JSAMPROW row = img.scanLine( int( cinfo.output_scanline ) );
		jpeg_read_scanlines( &cinfo, &row, 1 );
	}
	jpeg_finish_decompress( &cinfo );
	jpeg_destroy_decompress( &cinfo );
	return img;
}

/* uncompressed and RLE true-colour / grey TGA, 8/24/32 bit */
QImage decodeTga( const unsigned char *d, size_t len )
{
	if ( len < 18 )
		return QImage();
	const int idLen = d[0], cmapType = d[1], type = d[2];
	const int w = d[12] | ( d[13] << 8 ), h = d[14] | ( d[15] << 8 ), bpp = d[16], desc = d[17];
	if ( cmapType != 0 || ( type != 2 && type != 3 && type != 10 && type != 11 ) || w <= 0 || h <= 0 || w > 8192 || h > 8192 )
		return QImage();
	if ( bpp != 8 && bpp != 24 && bpp != 32 )
		return QImage();
	const int px = bpp / 8;
	size_t pos = 18 + size_t( idLen );
	QImage img( w, h, QImage::Format_ARGB32 );
	const bool rle = type >= 10, topDown = ( desc & 0x20 ) != 0;
	int n = 0;
	const int total = w * h;
	auto put = [&]( const unsigned char *p ) {
		const int y = n / w, x = n % w;
		const int row = topDown ? y : h - 1 - y;
		QRgb c;
		if ( px == 1 )
			c = qRgba( p[0], p[0], p[0], 255 );
		else
			c = qRgba( p[2], p[1], p[0], px == 4 ? p[3] : 255 );
		reinterpret_cast<QRgb *>( img.scanLine( row ) )[x] = c;
		++n;
	};
	while ( n < total ) {
		if ( pos >= len )
			return QImage();
		if ( rle ) {
			const int hdr = d[pos++];
			const int cnt = ( hdr & 0x7f ) + 1;
			if ( hdr & 0x80 ) {
				if ( pos + px > len )
					return QImage();
				for ( int i = 0; i < cnt && n < total; ++i )
					put( d + pos );
				pos += px;
			} else {
				for ( int i = 0; i < cnt && n < total; ++i ) {
					if ( pos + px > len )
						return QImage();
					put( d + pos );
					pos += px;
				}
			}
		} else {
			if ( pos + px > len )
				return QImage();
			put( d + pos );
			pos += px;
		}
	}
	return img;
}

}	/* namespace */

QImage decodeImage( const unsigned char *data, size_t len, bool isTga )
{
	if ( !data || !len )
		return QImage();
	if ( len > 3 && data[0] == 0xFF && data[1] == 0xD8 )
		return decodeJpeg( data, len );
	if ( len > 8 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G' )
		return QImage::fromData( data, int( len ), "PNG" );
	if ( isTga )
		return decodeTga( data, len );
	QImage j = decodeJpeg( data, len );
	return j.isNull() ? decodeTga( data, len ) : j;
}

QPixmap levelshotThumb( const lc_map_info *m, qreal dpr )
{
	static QHash<QString, QPixmap> cache;
	if ( !m )
		return QPixmap();
	const QString key = QString::fromUtf8( m->name );
	auto it = cache.find( key );
	if ( it != cache.end() )
		return it.value();
	QPixmap pm;
	size_t len = 0;
	int isTga = 0;
	unsigned char *bytes = lc_map_levelshot_bytes( m, &len, &isTga );
	if ( bytes ) {
		const QImage img = decodeImage( bytes, len, isTga != 0 );
		free( bytes );
		if ( !img.isNull() ) {
			pm = QPixmap::fromImage( img.scaled( QSize( int( 56 * dpr ), int( 42 * dpr ) ), Qt::IgnoreAspectRatio,
				Qt::SmoothTransformation ) );
			pm.setDevicePixelRatio( dpr );
		}
	}
	cache.insert( key, pm );
	return pm;
}

}
