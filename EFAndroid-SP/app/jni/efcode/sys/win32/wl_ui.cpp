/*
===========================================================================
Windows launcher: shared UI toolkit. See wl_ui.h.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#include "wl_ui.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
using std::min;
using std::max;
#include <objidl.h>
#include <gdiplus.h>
#include <dwmapi.h>
#include <uxtheme.h>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace wl {

/* ---------------------------------------------------------------- strings */

ws W( const char *s )
{
	if ( !s || !*s ) return ws( );
	int n = MultiByteToWideChar( CP_UTF8, 0, s, -1, 0, 0 );
	if ( n <= 0 ) {
		n = MultiByteToWideChar( 1252, 0, s, -1, 0, 0 );
		ws r( n > 0 ? n - 1 : 0, 0 );
		if ( n > 1 ) MultiByteToWideChar( 1252, 0, s, -1, &r[0], n );
		return r;
	}
	ws r( n - 1, 0 );
	MultiByteToWideChar( CP_UTF8, 0, s, -1, &r[0], n );
	return r;
}

str U( const wchar_t *w, int len )
{
	if ( !w ) return str( );
	if ( len < 0 ) len = (int)wcslen( w );
	if ( len == 0 ) return str( );
	int n = WideCharToMultiByte( CP_UTF8, 0, w, len, 0, 0, 0, 0 );
	str r( n, 0 );
	WideCharToMultiByte( CP_UTF8, 0, w, len, &r[0], n, 0, 0 );
	return r;
}

str fmt( const char *f, ... )
{
	char buf[4096];
	va_list ap;
	va_start( ap, f );
	vsnprintf( buf, sizeof buf, f, ap );
	va_end( ap );
	return str( buf );
}

/* ----------------------------------------------------------------- theme */

Theme T;

COLORREF blend( COLORREF a, COLORREF b, int pct )
{
	int r = GetRValue( a ) + ( GetRValue( b ) - GetRValue( a ) ) * pct / 100;
	int g = GetGValue( a ) + ( GetGValue( b ) - GetGValue( a ) ) * pct / 100;
	int bl = GetBValue( a ) + ( GetBValue( b ) - GetBValue( a ) ) * pct / 100;
	return RGB( r, g, bl );
}

void themeInit( )
{
	bool dark = false;
	DWORD v = 1, sz = sizeof v;
	if ( RegGetValueW( HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
			L"AppsUseLightTheme", RRF_RT_REG_DWORD, 0, &v, &sz ) == ERROR_SUCCESS )
		dark = ( v == 0 );
	const char *f = testEnv( "VOYAGERSP_TEST_LAUNCHER_APPEARANCE" );
	if ( f ) {
		if ( !lc_stricmp( f, "dark" ) ) dark = true;
		else if ( !lc_stricmp( f, "light" ) ) dark = false;
	}
	T.dark = dark;
	if ( dark ) {
		T.bg = RGB( 32, 32, 32 ); T.text = RGB( 245, 245, 245 );
		T.text2 = blend( T.bg, T.text, 62 ); T.text3 = blend( T.bg, T.text, 36 );
		T.sep = blend( T.bg, RGB( 255, 255, 255 ), 14 ); T.group = blend( T.bg, RGB( 255, 255, 255 ), 5 );
		T.field = RGB( 45, 45, 45 ); T.btn = RGB( 58, 58, 58 );
		T.orange = RGB( 255, 159, 10 ); T.green = RGB( 48, 209, 88 ); T.red = RGB( 255, 69, 58 );
		T.purple = RGB( 191, 90, 242 ); T.blue = RGB( 10, 132, 255 ); T.amber = RGB( 255, 214, 10 );
		T.yellow = RGB( 255, 214, 10 ); T.teal = RGB( 64, 200, 224 ); T.pink = RGB( 255, 55, 95 );
	} else {
		T.bg = RGB( 243, 243, 243 ); T.text = RGB( 28, 28, 30 );
		T.text2 = blend( T.bg, T.text, 62 ); T.text3 = blend( T.bg, T.text, 38 );
		T.sep = blend( T.bg, RGB( 0, 0, 0 ), 12 ); T.group = blend( T.bg, RGB( 0, 0, 0 ), 3 );
		T.field = RGB( 255, 255, 255 ); T.btn = RGB( 251, 251, 251 );
		T.orange = RGB( 224, 128, 0 ); T.green = RGB( 40, 160, 70 ); T.red = RGB( 215, 40, 30 );
		T.purple = RGB( 150, 60, 200 ); T.blue = RGB( 0, 102, 220 ); T.amber = RGB( 179, 120, 0 );
		T.yellow = RGB( 230, 180, 0 ); T.teal = RGB( 20, 150, 170 ); T.pink = RGB( 220, 30, 70 );
	}
	COLORREF acc = RGB( 0, 120, 212 );
	DWORD ac = 0, asz = sizeof ac;
	if ( RegGetValueW( HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", L"AccentColor",
			RRF_RT_REG_DWORD, 0, &ac, &asz ) == ERROR_SUCCESS && ac )
		acc = RGB( ac & 255, ( ac >> 8 ) & 255, ( ac >> 16 ) & 255 );
	T.accent = acc;
	if ( dark ) {
		/* an accent too dark to read on a dark window is lifted */
		int lum = ( GetRValue( acc ) * 299 + GetGValue( acc ) * 587 + GetBValue( acc ) * 114 ) / 1000;
		if ( lum < 90 ) T.accent = blend( acc, RGB( 255, 255, 255 ), 35 );
	}
}

COLORREF roleColor( int r )
{
	switch ( r ) {
	case R_TEXT2: return T.text2;
	case R_TEXT3: return T.text3;
	case R_ORANGE: return T.orange;
	case R_GREEN: return T.green;
	case R_RED: return T.red;
	case R_ACCENT: return T.accent;
	case R_PURPLE: return T.purple;
	case R_BLUE: return T.blue;
	case R_AMBER: return T.amber;
	case R_YELLOW: return T.yellow;
	case R_TEAL: return T.teal;
	case R_PINK: return T.pink;
	case R_BLACK: return T.dark ? blend( T.text, T.bg, 50 ) : RGB( 0, 0, 0 );
	default: return T.text;
	}
}

/* ------------------------------------------------------------- dpi, fonts */

int g_dpi = 96;
static std::map<int, HFONT> g_fonts;

int S( int v ) { return MulDiv( v, g_dpi, 96 ); }

void setDpi( int dpi ) { if ( dpi >= 96 && dpi != g_dpi ) { g_dpi = dpi; fontsReset( ); } }

void fontsReset( )
{
	for ( auto &p : g_fonts ) DeleteObject( p.second );
	g_fonts.clear( );
}

HFONT F( int px, int weight, bool mono )
{
	int key = px * 10000 + weight * 2 + ( mono ? 1 : 0 );
	auto i = g_fonts.find( key );
	if ( i != g_fonts.end( ) ) return i->second;
	HFONT f = CreateFontW( -S( px ), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, mono ? ( FIXED_PITCH | FF_MODERN ) : ( DEFAULT_PITCH | FF_SWISS ),
		mono ? L"Consolas" : L"Segoe UI" );
	g_fonts[key] = f;
	return f;
}

int dpiOf( HWND w )
{
	typedef UINT ( WINAPI *GetDpiForWindowFn )( HWND );
	static GetDpiForWindowFn fn = (GetDpiForWindowFn)GetProcAddress( GetModuleHandleW( L"user32.dll" ), "GetDpiForWindow" );
	if ( fn && w ) { UINT d = fn( w ); if ( d ) return (int)d; }
	HDC dc = GetDC( 0 );
	int d = GetDeviceCaps( dc, LOGPIXELSX );
	ReleaseDC( 0, dc );
	return d ? d : 96;
}

int textHeight( const ws &text, int widthDesign, HFONT f, UINT flags )
{
	HDC dc = GetDC( 0 );
	HGDIOBJ old = SelectObject( dc, f );
	RECT r = { 0, 0, S( widthDesign ), 0 };
	DrawTextW( dc, text.c_str( ), -1, &r, flags | DT_CALCRECT | DT_NOPREFIX );
	SelectObject( dc, old );
	ReleaseDC( 0, dc );
	return r.bottom;
}

int textWidth( const ws &text, HFONT f )
{
	HDC dc = GetDC( 0 );
	HGDIOBJ old = SelectObject( dc, f );
	RECT r = { 0, 0, 0, 0 };
	DrawTextW( dc, text.c_str( ), -1, &r, DT_CALCRECT | DT_NOPREFIX | DT_SINGLELINE );
	SelectObject( dc, old );
	ReleaseDC( 0, dc );
	return r.right;
}

/* ------------------------------------------------------------------ gdi+ */

static ULONG_PTR g_gdipToken;
using namespace Gdiplus;

static inline Color GC( COLORREF c, int a = 255 ) { return Color( (BYTE)a, GetRValue( c ), GetGValue( c ), GetBValue( c ) ); }

void fillRoundRect( HDC dc, RECT r, int rad, COLORREF fill, COLORREF stroke, int sw )
{
	Graphics g( dc );
	g.SetSmoothingMode( SmoothingModeAntiAlias );
	GraphicsPath p;
	float x = (float)r.left + sw / 2.0f, y = (float)r.top + sw / 2.0f;
	float w = (float)( r.right - r.left ) - sw, h = (float)( r.bottom - r.top ) - sw, d = (float)rad * 2;
	if ( d > h ) d = h;
	if ( d > w ) d = w;
	p.AddArc( x, y, d, d, 180, 90 );
	p.AddArc( x + w - d, y, d, d, 270, 90 );
	p.AddArc( x + w - d, y + h - d, d, d, 0, 90 );
	p.AddArc( x, y + h - d, d, d, 90, 90 );
	p.CloseFigure( );
	if ( fill != CLR_NONE ) { SolidBrush b( GC( fill ) ); g.FillPath( &b, &p ); }
	if ( stroke != CLR_NONE && sw > 0 ) { Pen pen( GC( stroke ), (REAL)sw ); g.DrawPath( &pen, &p ); }
}

static void pathRound( GraphicsPath &p, float x, float y, float w, float h, float r )
{
	float d = r * 2;
	p.AddArc( x, y, d, d, 180, 90 );
	p.AddArc( x + w - d, y, d, d, 270, 90 );
	p.AddArc( x + w - d, y + h - d, d, d, 0, 90 );
	p.AddArc( x, y + h - d, d, d, 90, 90 );
	p.CloseFigure( );
}

void drawGlyph( HDC dc, int gl, int x, int y, int sz, COLORREF c )
{
	Graphics g( dc );
	g.SetSmoothingMode( SmoothingModeAntiAlias );
	g.SetPixelOffsetMode( PixelOffsetModeHalf );
	float u = sz / 16.0f;			/* the glyphs are designed on a 16 x 16 grid */
	g.TranslateTransform( (REAL)x, (REAL)y );
	g.ScaleTransform( u, u );
	float pw = 1.5f;
	Pen pen( GC( c ), pw );
	pen.SetStartCap( LineCapRound ); pen.SetEndCap( LineCapRound ); pen.SetLineJoin( LineJoinRound );
	SolidBrush br( GC( c ) );
	auto circleFill = [&]( ) { g.FillEllipse( &br, 0.5f, 0.5f, 15.0f, 15.0f ); };
	auto knock = [&]( ) { return SolidBrush( GC( T.dark ? RGB( 30, 30, 30 ) : RGB( 255, 255, 255 ) ) ); };
	switch ( gl ) {
	case G_CHECK_CIRCLE: {
		circleFill( );
		Pen w( GC( RGB( 255, 255, 255 ) ), 1.8f ); w.SetStartCap( LineCapRound ); w.SetEndCap( LineCapRound ); w.SetLineJoin( LineJoinRound );
		PointF p[3] = { PointF( 4.6f, 8.3f ), PointF( 7.0f, 10.7f ), PointF( 11.6f, 5.6f ) };
		g.DrawLines( &w, p, 3 );
		break; }
	case G_X_CIRCLE: {
		circleFill( );
		Pen w( GC( RGB( 255, 255, 255 ) ), 1.8f ); w.SetStartCap( LineCapRound ); w.SetEndCap( LineCapRound );
		g.DrawLine( &w, 5.3f, 5.3f, 10.7f, 10.7f ); g.DrawLine( &w, 10.7f, 5.3f, 5.3f, 10.7f );
		break; }
	case G_EXCL_CIRCLE: {
		circleFill( );
		Pen w( GC( RGB( 255, 255, 255 ) ), 1.8f ); w.SetStartCap( LineCapRound ); w.SetEndCap( LineCapRound );
		g.DrawLine( &w, 8.0f, 4.3f, 8.0f, 9.0f );
		SolidBrush wb( GC( RGB( 255, 255, 255 ) ) ); g.FillEllipse( &wb, 7.0f, 10.4f, 2.0f, 2.0f );
		break; }
	case G_TRIANGLE: {
		GraphicsPath p; PointF pts[3] = { PointF( 8, 0.8f ), PointF( 15.5f, 14.5f ), PointF( 0.5f, 14.5f ) };
		p.AddPolygon( pts, 3 ); g.FillPath( &br, &p );
		Pen jp( GC( c ), 2.0f ); jp.SetLineJoin( LineJoinRound ); g.DrawPath( &jp, &p );
		Pen w( GC( RGB( 255, 255, 255 ) ), 1.7f ); w.SetStartCap( LineCapRound ); w.SetEndCap( LineCapRound );
		g.DrawLine( &w, 8.0f, 5.5f, 8.0f, 10.0f );
		SolidBrush wb( GC( RGB( 255, 255, 255 ) ) ); g.FillEllipse( &wb, 7.1f, 11.2f, 1.8f, 1.8f );
		break; }
	case G_DOTTED: {
		Pen dp( GC( c ), 1.4f ); dp.SetDashStyle( DashStyleDot ); g.DrawEllipse( &dp, 1.0f, 1.0f, 14.0f, 14.0f );
		break; }
	case G_REFRESH: case G_DOWN_CIRCLE: {
		if ( gl == G_DOWN_CIRCLE ) {
			circleFill( );
			Pen w( GC( RGB( 255, 255, 255 ) ), 1.9f ); w.SetStartCap( LineCapRound ); w.SetEndCap( LineCapRound ); w.SetLineJoin( LineJoinRound );
			g.DrawLine( &w, 8.0f, 3.8f, 8.0f, 11.4f );
			PointF p[3] = { PointF( 4.8f, 8.4f ), PointF( 8.0f, 11.6f ), PointF( 11.2f, 8.4f ) };
			g.DrawLines( &w, p, 3 );
		} else {
			g.DrawArc( &pen, 2.5f, 2.5f, 11.0f, 11.0f, -60, 280 );
			SolidBrush hb( GC( c ) );
			PointF p[3] = { PointF( 9.4f, 0.8f ), PointF( 14.4f, 2.6f ), PointF( 10.4f, 6.0f ) };
			g.FillPolygon( &hb, p, 3 );
		}
		break; }
	case G_STAR: case G_STAR_FILL: {
		PointF p[10];
		for ( int i = 0; i < 10; i++ ) {
			double a = -3.14159265 / 2 + i * 3.14159265 / 5, r = ( i & 1 ) ? 3.4 : 7.4;
			p[i] = PointF( (REAL)( 8 + r * cos( a ) ), (REAL)( 8.4 + r * sin( a ) ) );
		}
		if ( gl == G_STAR_FILL ) g.FillPolygon( &br, p, 10 );
		g.DrawPolygon( &pen, p, 10 );
		break; }
	case G_LOCK: {
		GraphicsPath p; pathRound( p, 3, 7, 10, 8, 1.8f ); g.FillPath( &br, &p );
		g.DrawArc( &pen, 4.8f, 1.5f, 6.4f, 8.0f, 180, 180 );
		g.DrawLine( &pen, 4.8f, 5.5f, 4.8f, 7.5f ); g.DrawLine( &pen, 11.2f, 5.5f, 11.2f, 7.5f );
		break; }
	case G_GLOBE: {
		g.DrawEllipse( &pen, 1.5f, 1.5f, 13.0f, 13.0f );
		g.DrawEllipse( &pen, 5.0f, 1.5f, 6.0f, 13.0f );
		g.DrawLine( &pen, 1.8f, 8.0f, 14.2f, 8.0f );
		break; }
	case G_PLUS: g.DrawLine( &pen, 8, 3, 8, 13 ); g.DrawLine( &pen, 3, 8, 13, 8 ); break;
	case G_MINUS: g.DrawLine( &pen, 3, 8, 13, 8 ); break;
	case G_UP: { PointF p[3] = { PointF( 3.5f, 10.5f ), PointF( 8, 5.5f ), PointF( 12.5f, 10.5f ) }; g.DrawLines( &pen, p, 3 ); break; }
	case G_DOWN: { PointF p[3] = { PointF( 3.5f, 5.5f ), PointF( 8, 10.5f ), PointF( 12.5f, 5.5f ) }; g.DrawLines( &pen, p, 3 ); break; }
	case G_PERSON: case G_PERSON3: {
		auto person = [&]( float cx, float s ) {
			g.FillEllipse( &br, cx - 2.4f * s, 2.0f, 4.8f * s, 4.8f * s );
			GraphicsPath p; p.AddArc( cx - 4.6f * s, 8.0f, 9.2f * s, 10.0f, 180, 180 ); p.CloseFigure( ); g.FillPath( &br, &p );
		};
		if ( gl == G_PERSON ) person( 8, 1.0f );
		else { person( 3.4f, 0.7f ); person( 12.6f, 0.7f ); person( 8, 0.9f ); }
		break; }
	case G_SERVER: {
		for ( int i = 0; i < 3; i++ ) {
			GraphicsPath p; pathRound( p, 1.5f, 1.5f + i * 4.8f, 13, 3.8f, 1.2f ); g.FillPath( &br, &p );
			SolidBrush kb = knock( ); g.FillEllipse( &kb, 3.2f, 2.6f + i * 4.8f, 1.6f, 1.6f );
		}
		break; }
	case G_MAP: {
		PointF p[4] = { PointF( 1.5f, 3.5f ), PointF( 5.5f, 2.0f ), PointF( 10.5f, 3.5f ), PointF( 14.5f, 2.0f ) };
		(void)p;
		g.DrawPolygon( &pen, (PointF[]){ PointF( 1.5f, 3.5f ), PointF( 5.5f, 2.0f ), PointF( 10.5f, 3.5f ), PointF( 14.5f, 2.0f ),
			PointF( 14.5f, 12.5f ), PointF( 10.5f, 14.0f ), PointF( 5.5f, 12.5f ), PointF( 1.5f, 14.0f ) }, 8 );
		g.DrawLine( &pen, 5.5f, 2.0f, 5.5f, 12.5f ); g.DrawLine( &pen, 10.5f, 3.5f, 10.5f, 14.0f );
		break; }
	case G_QUESTION: {
		Font f( L"Segoe UI", 11.0f, FontStyleBold, UnitPixel );
		StringFormat sf; sf.SetAlignment( StringAlignmentCenter ); sf.SetLineAlignment( StringAlignmentCenter );
		g.DrawString( L"?", 1, &f, RectF( 0, 0, 16, 16 ), &sf, &br );
		break; }
	case G_PLAY: { PointF p[3] = { PointF( 4, 2.5f ), PointF( 13, 8 ), PointF( 4, 13.5f ) }; g.FillPolygon( &br, p, 3 ); break; }
	case G_FORWARD: {
		PointF p[3] = { PointF( 2.5f, 3 ), PointF( 11, 8 ), PointF( 2.5f, 13 ) }; g.FillPolygon( &br, p, 3 );
		g.FillRectangle( &br, 11.8f, 3.0f, 2.2f, 10.0f ); break; }
	case G_RESTART: {
		g.DrawArc( &pen, 2.5f, 2.5f, 11.0f, 11.0f, -40, 290 );
		PointF p[3] = { PointF( 0.8f, 1.8f ), PointF( 6.0f, 2.0f ), PointF( 3.4f, 6.2f ) }; SolidBrush hb( GC( c ) ); g.FillPolygon( &hb, p, 3 );
		break; }
	case G_BOTADD: case G_BOTSDEL: {
		g.FillEllipse( &br, 3.0f, 2.0f, 5.0f, 5.0f );
		GraphicsPath p; p.AddArc( 0.8f, 9.0f, 9.4f, 10.0f, 180, 180 ); p.CloseFigure( ); g.FillPath( &br, &p );
		if ( gl == G_BOTADD ) { g.DrawLine( &pen, 12.5f, 3.5f, 12.5f, 9.5f ); g.DrawLine( &pen, 9.5f, 6.5f, 15.5f, 6.5f ); }
		else g.DrawLine( &pen, 10.0f, 12.5f, 15.5f, 2.5f );
		break; }
	case G_BUBBLE: {
		GraphicsPath p; pathRound( p, 1.5f, 2.0f, 13, 9, 2.5f ); g.DrawPath( &pen, &p );
		PointF t[3] = { PointF( 4.5f, 11.0f ), PointF( 4.5f, 14.5f ), PointF( 8.0f, 11.0f ) }; g.FillPolygon( &br, t, 3 );
		break; }
	case G_DOT: g.FillEllipse( &br, 2.0f, 2.0f, 12.0f, 12.0f ); break;
	case G_TERMINAL: {
		GraphicsPath p; pathRound( p, 1.5f, 2.5f, 13, 11, 2.0f ); g.DrawPath( &pen, &p );
		PointF a[3] = { PointF( 4.5f, 6.0f ), PointF( 7.0f, 8.3f ), PointF( 4.5f, 10.6f ) }; g.DrawLines( &pen, a, 3 );
		g.DrawLine( &pen, 8.5f, 10.8f, 11.5f, 10.8f );
		break; }
	case G_PUZZLE: case G_PUZZLE_FILL: {
		GraphicsPath p; pathRound( p, 2.0f, 4.5f, 11, 9.5f, 1.6f );
		if ( gl == G_PUZZLE_FILL ) g.FillPath( &br, &p );
		g.DrawPath( &pen, &p );
		if ( gl == G_PUZZLE_FILL ) g.FillEllipse( &br, 5.6f, 1.2f, 4.2f, 4.2f ); else g.DrawEllipse( &pen, 5.6f, 1.2f, 4.2f, 4.2f );
		break; }
	case G_FOLDER: {
		GraphicsPath p; pathRound( p, 1.0f, 3.5f, 14, 10, 1.8f ); g.FillPath( &br, &p );
		g.FillRectangle( &br, 1.0f, 2.2f, 6.0f, 3.0f );
		break; }
	case G_INFO: {
		circleFill( );
		SolidBrush wb( GC( RGB( 255, 255, 255 ) ) ); g.FillEllipse( &wb, 7.0f, 3.4f, 2.0f, 2.0f );
		Pen w( GC( RGB( 255, 255, 255 ) ), 1.8f ); w.SetStartCap( LineCapRound ); w.SetEndCap( LineCapRound );
		g.DrawLine( &w, 8.0f, 7.2f, 8.0f, 12.2f );
		break; }
	case G_MORE: for ( int i = 0; i < 3; i++ ) g.FillEllipse( &br, 1.8f + i * 4.6f, 6.6f, 2.8f, 2.8f ); break;
	case G_APP: {
		GraphicsPath p; pathRound( p, 0.5f, 0.5f, 15, 15, 3.5f ); g.FillPath( &br, &p );
		SolidBrush wb( GC( RGB( 255, 255, 255 ) ) );
		PointF t[4] = { PointF( 8, 3 ), PointF( 11, 12 ), PointF( 8, 10 ), PointF( 5, 12 ) }; g.FillPolygon( &wb, t, 4 );
		break; }
	}
}

void drawAppIcon( HDC dc, int x, int y, int size )
{
	static std::map<int, HICON> cache;
	HICON &ic = cache[size];
	if ( !ic ) ic = (HICON)LoadImageW( GetModuleHandleW( 0 ), MAKEINTRESOURCEW( 1 ), IMAGE_ICON, size, size, 0 );
	if ( ic ) DrawIconEx( dc, x, y, ic, size, size, 0, 0, DI_NORMAL );
	else drawGlyph( dc, G_APP, x, y, size, T.accent );
}

/* ---------------------------------------------------------------- images */

static HBITMAP dibFromRgba( const unsigned char *bgra, int w, int h, bool topDown )
{
	BITMAPINFO bi = {};
	bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
	bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
	void *bits = 0;
	HBITMAP b = CreateDIBSection( 0, &bi, DIB_RGB_COLORS, &bits, 0, 0 );
	if ( !b ) return 0;
	for ( int y = 0; y < h; y++ ) {
		int sy = topDown ? y : h - 1 - y;
		memcpy( (unsigned char *)bits + (size_t)y * w * 4, bgra + (size_t)sy * w * 4, (size_t)w * 4 );
	}
	return b;
}

static HBITMAP decodeTga( const unsigned char *d, size_t n, int *ow, int *oh )
{
	if ( n < 18 ) return 0;
	int idLen = d[0], cmType = d[1], type = d[2], w = d[12] | ( d[13] << 8 ), h = d[14] | ( d[15] << 8 ), bpp = d[16], desc = d[17];
	if ( cmType != 0 || w <= 0 || h <= 0 || w > 4096 || h > 4096 || ( bpp != 24 && bpp != 32 && bpp != 8 ) ) return 0;
	if ( type != 2 && type != 10 && type != 3 && type != 11 ) return 0;
	int bytes = bpp / 8;
	const unsigned char *p = d + 18 + idLen, *end = d + n;
	std::vector<unsigned char> out( (size_t)w * h * 4 );
	size_t total = (size_t)w * h, i = 0;
	auto put = [&]( const unsigned char *px ) {
		unsigned char *o = &out[i * 4];
		if ( bytes == 1 ) { o[0] = o[1] = o[2] = px[0]; o[3] = 255; }
		else { o[0] = px[0]; o[1] = px[1]; o[2] = px[2]; o[3] = bytes == 4 ? px[3] : 255; }
		i++;
	};
	if ( type == 2 || type == 3 ) {
		while ( i < total && p + bytes <= end ) { put( p ); p += bytes; }
	} else {
		while ( i < total && p < end ) {
			int c = *p++; int cnt = ( c & 127 ) + 1;
			if ( c & 128 ) { if ( p + bytes > end ) break; for ( int k = 0; k < cnt && i < total; k++ ) put( p ); p += bytes; }
			else for ( int k = 0; k < cnt && i < total && p + bytes <= end; k++ ) { put( p ); p += bytes; }
		}
	}
	if ( i < total ) return 0;
	bool topDown = ( desc & 0x20 ) != 0;
	*ow = w; *oh = h;
	return dibFromRgba( out.data( ), w, h, topDown );
}

HBITMAP decodeImage( const unsigned char *data, size_t len, bool isTga, int *w, int *h )
{
	if ( !data || !len ) return 0;
	if ( isTga ) return decodeTga( data, len, w, h );
	HGLOBAL hg = GlobalAlloc( GMEM_MOVEABLE, len );
	if ( !hg ) return 0;
	memcpy( GlobalLock( hg ), data, len ); GlobalUnlock( hg );
	IStream *st = 0;
	if ( CreateStreamOnHGlobal( hg, TRUE, &st ) != S_OK ) { GlobalFree( hg ); return 0; }
	HBITMAP hb = 0;
	{
		Bitmap bmp( st );
		if ( bmp.GetLastStatus( ) == Ok ) {
			*w = (int)bmp.GetWidth( ); *h = (int)bmp.GetHeight( );
			bmp.GetHBITMAP( Color( 255, 0, 0, 0 ), &hb );
		}
	}
	st->Release( );
	return hb;
}

HBITMAP scaledBitmap( HBITMAP src, int w, int h )
{
	BITMAP bm;
	if ( !GetObject( src, sizeof bm, &bm ) ) return 0;
	Bitmap in( src, 0 );
	BITMAPINFO bi = {};
	bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
	bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
	void *bits = 0;
	HBITMAP out = CreateDIBSection( 0, &bi, DIB_RGB_COLORS, &bits, 0, 0 );
	HDC dc = CreateCompatibleDC( 0 );
	HGDIOBJ old = SelectObject( dc, out );
	{
		Graphics g( dc );
		g.SetInterpolationMode( InterpolationModeHighQualityBicubic );
		g.DrawImage( &in, Rect( 0, 0, w, h ), 0, 0, bm.bmWidth, bm.bmHeight, UnitPixel );
	}
	SelectObject( dc, old );
	DeleteDC( dc );
	return out;
}

bool savePng( HBITMAP bmp, const char *path )
{
	Bitmap b( bmp, 0 );
	CLSID png = { 0x557cf406, 0x1a04, 0x11d3, { 0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };
	ws wp = W( path );
	return b.Save( wp.c_str( ), &png, 0 ) == Ok;
}

/* ------------------------------------------------------- theme application */

static HBRUSH g_bgBrush, g_fieldBrush;
static HBRUSH bgBrush( ) { if ( g_bgBrush ) DeleteObject( g_bgBrush ); g_bgBrush = CreateSolidBrush( T.bg ); return g_bgBrush; }
static HBRUSH fieldBrush( ) { if ( g_fieldBrush ) DeleteObject( g_fieldBrush ); g_fieldBrush = CreateSolidBrush( T.field ); return g_fieldBrush; }

static void setPreferredAppMode( bool dark )
{
	typedef int ( WINAPI *SetPreferredAppModeFn )( int );
	typedef void ( WINAPI *FlushFn )( );
	HMODULE ux = LoadLibraryW( L"uxtheme.dll" );
	if ( !ux ) return;
	SetPreferredAppModeFn sp = (SetPreferredAppModeFn)GetProcAddress( ux, MAKEINTRESOURCEA( 135 ) );
	FlushFn fl = (FlushFn)GetProcAddress( ux, MAKEINTRESOURCEA( 136 ) );
	if ( sp ) sp( dark ? 2 : 0 );		/* 2 = ForceDark, 0 = Default */
	if ( fl ) fl( );
}

static BOOL CALLBACK themeChild( HWND h, LPARAM )
{
	wchar_t cls[64];
	GetClassNameW( h, cls, 64 );
	const wchar_t *theme = 0;
	if ( !lstrcmpiW( cls, L"Button" ) || !lstrcmpiW( cls, L"ListBox" ) || !lstrcmpiW( cls, L"SysListView32" ) ||
		!lstrcmpiW( cls, L"SysHeader32" ) || !lstrcmpiW( cls, L"msctls_trackbar32" ) || !lstrcmpiW( cls, L"msctls_updown32" ) ||
		!lstrcmpiW( cls, L"msctls_progress32" ) || !lstrcmpiW( cls, L"ScrollBar" ) )
		theme = T.dark ? L"DarkMode_Explorer" : L"Explorer";
	else if ( !lstrcmpiW( cls, L"Edit" ) || !lstrcmpiW( cls, L"ComboBox" ) )
		theme = T.dark ? L"DarkMode_CFD" : L"Explorer";
	if ( theme ) SetWindowTheme( h, theme, 0 );
	return TRUE;
}

void applyTheme( HWND w )
{
	BOOL dark = T.dark;
	DwmSetWindowAttribute( w, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof dark );
	DwmSetWindowAttribute( w, 19, &dark, sizeof dark );
	EnumChildWindows( w, themeChild, 0 );
}

void initToolkit( )
{
	static bool done;
	if ( done ) return;
	done = true;
	INITCOMMONCONTROLSEX ic = { sizeof ic, ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES | ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES |
		ICC_PROGRESS_CLASS | ICC_TAB_CLASSES | ICC_UPDOWN_CLASS };
	InitCommonControlsEx( &ic );
	CoInitializeEx( 0, COINIT_APARTMENTTHREADED );
	GdiplusStartupInput gi;
	GdiplusStartup( &g_gdipToken, &gi, 0 );
	themeInit( );
	setPreferredAppMode( T.dark );
	setDpi( dpiOf( 0 ) );
}

void pumpFor( int ms )
{
	DWORD end = GetTickCount( ) + ms;
	for ( ;; ) {
		MSG m;
		while ( PeekMessageW( &m, 0, 0, 0, PM_REMOVE ) ) { TranslateMessage( &m ); DispatchMessageW( &m ); }
		if ( (int)( end - GetTickCount( ) ) <= 0 ) break;
		MsgWaitForMultipleObjects( 0, 0, FALSE, 10, QS_ALLINPUT );
	}
}

void setText( HWND h, const str &t ) { SetWindowTextW( h, W( t ).c_str( ) ); }
str getText( HWND h )
{
	int n = GetWindowTextLengthW( h );
	ws w( n + 1, 0 );
	GetWindowTextW( h, &w[0], n + 1 );
	return U( w.c_str( ), n );
}

/* ----------------------------------------------------- custom control classes */

static void parentBg( HWND h, HDC dc, int w, int hgt )
{
	/* ask the parent to paint what is behind this control */
	HWND p = GetParent( h );
	RECT cr; GetWindowRect( h, &cr );
	POINT pt = { cr.left, cr.top };
	ScreenToClient( p, &pt );
	POINT old;
	SetViewportOrgEx( dc, -pt.x, -pt.y, &old );
	SendMessageW( p, WM_ERASEBKGND, (WPARAM)dc, 1 );
	SetViewportOrgEx( dc, old.x, old.y, 0 );
	(void)w; (void)hgt;
}

struct MemDC {
	HDC dc; HBITMAP bmp, old; HDC target; int w, h;
	MemDC( HDC t, int w_, int h_ ) : target( t ), w( w_ ), h( h_ )
	{ dc = CreateCompatibleDC( t ); bmp = CreateCompatibleBitmap( t, w, h ); old = (HBITMAP)SelectObject( dc, bmp ); }
	~MemDC( ) { BitBlt( target, 0, 0, w, h, dc, 0, 0, SRCCOPY ); SelectObject( dc, old ); DeleteObject( bmp ); DeleteDC( dc ); }
};

#define PAINT_BEGIN HDC dc; PAINTSTRUCT ps; bool prn = ( m == WM_PRINTCLIENT ); if ( prn ) dc = (HDC)w; else dc = BeginPaint( h, &ps );
#define PAINT_END if ( !prn ) EndPaint( h, &ps );

struct LabelData { int role; UINT dt; HFONT font; };
#define LBL_SETROLE ( WM_APP + 1 )
#define LBL_SETDT ( WM_APP + 2 )

static LRESULT CALLBACK labelProc( HWND h, UINT m, WPARAM w, LPARAM l )
{
	LabelData *d = (LabelData *)GetWindowLongPtrW( h, GWLP_USERDATA );
	switch ( m ) {
	case WM_NCCREATE: d = new LabelData{ R_TEXT, DT_WORDBREAK, 0 }; SetWindowLongPtrW( h, GWLP_USERDATA, (LONG_PTR)d ); break;
	case WM_NCDESTROY: delete d; SetWindowLongPtrW( h, GWLP_USERDATA, 0 ); break;
	case WM_SETFONT: if ( d ) { d->font = (HFONT)w; if ( l ) InvalidateRect( h, 0, FALSE ); } return 0;
	case LBL_SETROLE: if ( d && d->role != (int)w ) { d->role = (int)w; InvalidateRect( GetParent( h ), 0, FALSE ); RedrawWindow( h, 0, 0, RDW_INVALIDATE | RDW_UPDATENOW ); } return 0;
	case LBL_SETDT: d->dt = (UINT)w; return 0;
	case WM_ERASEBKGND: return 1;
	case WM_SETTEXT: {
		LRESULT r = DefWindowProcW( h, m, w, l );
		RECT rc; GetWindowRect( h, &rc ); MapWindowPoints( 0, GetParent( h ), (POINT *)&rc, 2 );
		InvalidateRect( GetParent( h ), &rc, TRUE );		/* repaint what's behind, then us */
		return r; }
	case WM_NCHITTEST: return HTTRANSPARENT;
	case WM_PRINTCLIENT: case WM_PAINT: {
		PAINT_BEGIN
		RECT rc; GetClientRect( h, &rc );
		{
			MemDC md( dc, rc.right, rc.bottom );
			parentBg( h, md.dc, rc.right, rc.bottom );
			wchar_t buf[2048];
			int n = GetWindowTextW( h, buf, 2048 );
			SetBkMode( md.dc, TRANSPARENT );
			SetTextColor( md.dc, roleColor( d->role ) );
			HGDIOBJ of = SelectObject( md.dc, d->font ? d->font : F( 13 ) );
			DrawTextW( md.dc, buf, n, &rc, d->dt | DT_NOPREFIX );
			SelectObject( md.dc, of );
		}
		PAINT_END return 0; }
	}
	return DefWindowProcW( h, m, w, l );
}

/* ---- icon button ---- */
struct IconBtn { int glyph, size; bool hot, down; };
static LRESULT CALLBACK iconBtnProc( HWND h, UINT m, WPARAM w, LPARAM l )
{
	IconBtn *d = (IconBtn *)GetWindowLongPtrW( h, GWLP_USERDATA );
	switch ( m ) {
	case WM_NCCREATE: d = new IconBtn{ 0, 12, false, false }; SetWindowLongPtrW( h, GWLP_USERDATA, (LONG_PTR)d ); break;
	case WM_NCDESTROY: delete d; break;
	case WM_ERASEBKGND: return 1;
	case WM_GETDLGCODE: return DLGC_BUTTON;
	case WM_MOUSEMOVE: if ( !d->hot ) { d->hot = true; TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, h, 0 }; TrackMouseEvent( &t ); InvalidateRect( h, 0, FALSE ); } break;
	case WM_MOUSELEAVE: d->hot = false; d->down = false; InvalidateRect( h, 0, FALSE ); break;
	case WM_ENABLE: InvalidateRect( h, 0, FALSE ); break;
	case WM_SETFOCUS: case WM_KILLFOCUS: InvalidateRect( h, 0, FALSE ); break;
	case WM_LBUTTONDOWN: if ( IsWindowEnabled( h ) ) { SetFocus( h ); d->down = true; SetCapture( h ); InvalidateRect( h, 0, FALSE ); } return 0;
	case WM_LBUTTONUP:
		if ( d->down ) {
			d->down = false; ReleaseCapture( ); InvalidateRect( h, 0, FALSE );
			RECT rc; GetClientRect( h, &rc ); POINT p = { (short)LOWORD( l ), (short)HIWORD( l ) };
			if ( PtInRect( &rc, p ) ) SendMessageW( GetParent( h ), WM_COMMAND, MAKEWPARAM( GetDlgCtrlID( h ), BN_CLICKED ), (LPARAM)h );
		}
		return 0;
	case WM_KEYUP: if ( w == VK_SPACE && IsWindowEnabled( h ) ) SendMessageW( GetParent( h ), WM_COMMAND, MAKEWPARAM( GetDlgCtrlID( h ), BN_CLICKED ), (LPARAM)h ); return 0;
	case WM_PRINTCLIENT: case WM_PAINT: {
		PAINT_BEGIN
		RECT rc; GetClientRect( h, &rc );
		{
			MemDC md( dc, rc.right, rc.bottom );
			parentBg( h, md.dc, rc.right, rc.bottom );
			bool en = IsWindowEnabled( h ) != 0;
			COLORREF fill = d->down ? blend( T.btn, T.text, 14 ) : d->hot && en ? blend( T.btn, T.text, 7 ) : T.btn;
			fillRoundRect( md.dc, rc, S( 5 ), fill, T.sep, 1 );
			int sz = S( d->size );
			drawGlyph( md.dc, d->glyph, ( rc.right - sz ) / 2, ( rc.bottom - sz ) / 2, sz, en ? T.text : T.text3 );
			if ( GetFocus( ) == h ) { RECT f = rc; InflateRect( &f, -S( 2 ), -S( 2 ) ); DrawFocusRect( md.dc, &f ); }
		}
		PAINT_END return 0; }
	}
	return DefWindowProcW( h, m, w, l );
}

/* ---- segmented control ---- */
struct SegData { std::vector<ws> items; std::vector<bool> disabled; int sel; int hot; };
static LRESULT CALLBACK segProc( HWND h, UINT m, WPARAM w, LPARAM l )
{
	SegData *d = (SegData *)GetWindowLongPtrW( h, GWLP_USERDATA );
	switch ( m ) {
	case WM_NCCREATE: d = new SegData{ {}, {}, 0, -1 }; SetWindowLongPtrW( h, GWLP_USERDATA, (LONG_PTR)d ); break;
	case WM_NCDESTROY: delete d; break;
	case WM_ERASEBKGND: return 1;
	case WM_GETDLGCODE: return DLGC_WANTARROWS;
	case SEG_SETSEL: d->sel = (int)w; InvalidateRect( h, 0, FALSE ); return 0;
	case SEG_GETSEL: return d->sel;
	case SEG_CLEAR: d->items.clear( ); d->disabled.clear( ); d->sel = 0; return 0;
	case SEG_ADD: d->items.push_back( W( (const char *)w ) ); d->disabled.push_back( l != 0 ); InvalidateRect( h, 0, FALSE ); return 0;
	case SEG_DISABLE: if ( (int)w < (int)d->disabled.size( ) ) d->disabled[(int)w] = l != 0; InvalidateRect( h, 0, FALSE ); return 0;
	case WM_SETFOCUS: case WM_KILLFOCUS: case WM_ENABLE: InvalidateRect( h, 0, FALSE ); return 0;
	case WM_LBUTTONDOWN: {
		if ( !IsWindowEnabled( h ) ) return 0;
		RECT rc; GetClientRect( h, &rc );
		int n = (int)d->items.size( ); if ( !n ) return 0;
		int i = (short)LOWORD( l ) * n / max( 1, (int)rc.right );
		SetFocus( h );
		if ( i >= 0 && i < n && !d->disabled[i] && i != d->sel ) {
			d->sel = i; InvalidateRect( h, 0, FALSE );
			SendMessageW( GetParent( h ), WM_COMMAND, MAKEWPARAM( GetDlgCtrlID( h ), 1 ), (LPARAM)h );
		}
		return 0; }
	case WM_KEYDOWN: {
		int n = (int)d->items.size( ), dir = w == VK_RIGHT || w == VK_DOWN ? 1 : w == VK_LEFT || w == VK_UP ? -1 : 0;
		if ( dir ) {
			for ( int i = d->sel + dir; i >= 0 && i < n; i += dir )
				if ( !d->disabled[i] ) { d->sel = i; InvalidateRect( h, 0, FALSE ); SendMessageW( GetParent( h ), WM_COMMAND, MAKEWPARAM( GetDlgCtrlID( h ), 1 ), (LPARAM)h ); break; }
		}
		return 0; }
	case WM_PRINTCLIENT: case WM_PAINT: {
		PAINT_BEGIN
		RECT rc; GetClientRect( h, &rc );
		{
			MemDC md( dc, rc.right, rc.bottom );
			parentBg( h, md.dc, rc.right, rc.bottom );
			bool en = IsWindowEnabled( h ) != 0;
			fillRoundRect( md.dc, rc, S( 6 ), T.btn, T.sep, 1 );
			int n = (int)d->items.size( );
			for ( int i = 0; i < n; i++ ) {
				RECT r = { rc.right * i / n, 0, rc.right * ( i + 1 ) / n, rc.bottom };
				bool sel = i == d->sel;
				if ( sel ) { RECT s = r; InflateRect( &s, -S( 2 ), -S( 2 ) ); fillRoundRect( md.dc, s, S( 4 ), en ? T.accent : T.text3, CLR_NONE, 0 ); }
				else if ( i > 0 && i - 1 != d->sel ) { HPEN p = CreatePen( PS_SOLID, 1, T.sep ); HGDIOBJ o = SelectObject( md.dc, p ); MoveToEx( md.dc, r.left, r.top + S( 5 ), 0 ); LineTo( md.dc, r.left, r.bottom - S( 5 ) ); SelectObject( md.dc, o ); DeleteObject( p ); }
				SetBkMode( md.dc, TRANSPARENT );
				SetTextColor( md.dc, sel ? RGB( 255, 255, 255 ) : ( en && !d->disabled[i] ? T.text : T.text3 ) );
				HGDIOBJ of = SelectObject( md.dc, F( 13 ) );
				DrawTextW( md.dc, d->items[i].c_str( ), -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX );
				SelectObject( md.dc, of );
			}
			if ( GetFocus( ) == h ) { RECT f = rc; InflateRect( &f, -1, -1 ); DrawFocusRect( md.dc, &f ); }
		}
		PAINT_END return 0; }
	}
	return DefWindowProcW( h, m, w, l );
}

/* ---- mode card ---- */
struct CardData { int glyph; ws title, detail; bool sel; };
#define CARD_SETSEL ( WM_APP + 120 )
static LRESULT CALLBACK cardProc( HWND h, UINT m, WPARAM w, LPARAM l )
{
	CardData *d = (CardData *)GetWindowLongPtrW( h, GWLP_USERDATA );
	switch ( m ) {
	case WM_NCCREATE: d = new CardData{ 0, ws( ), ws( ), false }; SetWindowLongPtrW( h, GWLP_USERDATA, (LONG_PTR)d ); break;
	case WM_NCDESTROY: delete d; break;
	case WM_ERASEBKGND: return 1;
	case WM_GETDLGCODE: return DLGC_WANTARROWS | DLGC_WANTCHARS;
	case WM_SETFOCUS: case WM_KILLFOCUS: InvalidateRect( h, 0, FALSE ); break;
	case CARD_SETSEL: d->sel = w != 0; InvalidateRect( h, 0, FALSE ); return 0;
	case WM_LBUTTONDOWN: SetFocus( h ); SendMessageW( GetParent( h ), WM_COMMAND, MAKEWPARAM( GetDlgCtrlID( h ), BN_CLICKED ), (LPARAM)h ); return 0;
	case WM_LBUTTONDBLCLK: SendMessageW( GetParent( h ), WM_COMMAND, MAKEWPARAM( GetDlgCtrlID( h ), BN_DOUBLECLICKED ), (LPARAM)h ); return 0;
	case WM_KEYDOWN:
		if ( w == VK_LEFT ) SendMessageW( GetParent( h ), WM_COMMAND, MAKEWPARAM( GetDlgCtrlID( h ), 100 ), (LPARAM)h );
		else if ( w == VK_RIGHT ) SendMessageW( GetParent( h ), WM_COMMAND, MAKEWPARAM( GetDlgCtrlID( h ), 101 ), (LPARAM)h );
		else if ( w == VK_SPACE ) SendMessageW( GetParent( h ), WM_COMMAND, MAKEWPARAM( GetDlgCtrlID( h ), BN_CLICKED ), (LPARAM)h );
		return 0;
	case WM_PRINTCLIENT: case WM_PAINT: {
		PAINT_BEGIN
		RECT rc; GetClientRect( h, &rc );
		{
			MemDC md( dc, rc.right, rc.bottom );
			parentBg( h, md.dc, rc.right, rc.bottom );
			COLORREF fill = d->sel ? blend( T.bg, T.accent, 14 ) : T.group;
			fillRoundRect( md.dc, rc, S( 12 ), fill, d->sel ? T.accent : T.sep, d->sel ? S( 2 ) : 1 );
			drawGlyph( md.dc, d->glyph, S( 14 ), S( 13 ), S( 19 ), d->sel ? T.accent : T.text2 );
			SetBkMode( md.dc, TRANSPARENT );
			HGDIOBJ of = SelectObject( md.dc, F( 15, 600 ) );
			SetTextColor( md.dc, T.text );
			RECT tr = { S( 14 + 30 ), S( 11 ), rc.right - S( 8 ), S( 36 ) };
			DrawTextW( md.dc, d->title.c_str( ), -1, &tr, DT_SINGLELINE | DT_NOPREFIX );
			SelectObject( md.dc, F( 12 ) );
			SetTextColor( md.dc, T.text2 );
			RECT dr = { S( 14 ), S( 13 + 24 + 6 ), rc.right - S( 14 ), rc.bottom };
			DrawTextW( md.dc, d->detail.c_str( ), -1, &dr, DT_WORDBREAK | DT_NOPREFIX );
			SelectObject( md.dc, of );
			if ( GetFocus( ) == h ) { RECT f = rc; InflateRect( &f, -S( 4 ), -S( 4 ) ); DrawFocusRect( md.dc, &f ); }
		}
		PAINT_END return 0; }
	}
	return DefWindowProcW( h, m, w, l );
}

/* ---- spinner ---- */
struct SpinData { bool on; int phase; };
static LRESULT CALLBACK spinProc( HWND h, UINT m, WPARAM w, LPARAM l )
{
	SpinData *d = (SpinData *)GetWindowLongPtrW( h, GWLP_USERDATA );
	switch ( m ) {
	case WM_NCCREATE: d = new SpinData{ false, 0 }; SetWindowLongPtrW( h, GWLP_USERDATA, (LONG_PTR)d ); break;
	case WM_NCDESTROY: KillTimer( h, 1 ); delete d; break;
	case WM_ERASEBKGND: return 1;
	case WM_NCHITTEST: return HTTRANSPARENT;
	case WM_TIMER: d->phase = ( d->phase + 1 ) % 12; InvalidateRect( h, 0, FALSE ); return 0;
	case WM_APP + 1: d->on = w != 0; if ( d->on ) SetTimer( h, 1, 90, 0 ); else KillTimer( h, 1 ); ShowWindow( h, d->on ? SW_SHOWNA : SW_HIDE ); return 0;
	case WM_PRINTCLIENT: case WM_PAINT: {
		PAINT_BEGIN
		RECT rc; GetClientRect( h, &rc );
		{
			MemDC md( dc, rc.right, rc.bottom );
			parentBg( h, md.dc, rc.right, rc.bottom );
			if ( d->on ) {
				Graphics g( md.dc ); g.SetSmoothingMode( SmoothingModeAntiAlias );
				float cx = rc.right / 2.0f, cy = rc.bottom / 2.0f, r = min( rc.right, rc.bottom ) / 2.0f - 1;
				for ( int i = 0; i < 12; i++ ) {
					double a = ( i - d->phase ) * 3.14159265 / 6; int al = 255 - ( ( i + 12 ) % 12 ) * 18;
					Pen p( GC( T.text2, max( 40, al ) ), max( 1.5f, r / 5 ) ); p.SetStartCap( LineCapRound ); p.SetEndCap( LineCapRound );
					g.DrawLine( &p, cx + (REAL)( cos( a ) * r * 0.55 ), cy + (REAL)( sin( a ) * r * 0.55 ), cx + (REAL)( cos( a ) * r ), cy + (REAL)( sin( a ) * r ) );
				}
			}
		}
		PAINT_END return 0; }
	}
	return DefWindowProcW( h, m, w, l );
}

static void registerClasses( )
{
	static bool done;
	if ( done ) return;
	done = true;
	struct { const wchar_t *n; WNDPROC p; UINT st; } cl[] = {
		{ L"EFLabel", labelProc, 0 }, { L"EFIconBtn", iconBtnProc, 0 }, { L"EFSeg", segProc, 0 },
		{ L"EFCard", cardProc, CS_DBLCLKS }, { L"EFSpin", spinProc, 0 } };
	for ( auto &c : cl ) {
		WNDCLASSW wc = {};
		wc.lpfnWndProc = c.p; wc.hInstance = GetModuleHandleW( 0 ); wc.lpszClassName = c.n;
		wc.hCursor = LoadCursor( 0, IDC_ARROW ); wc.style = c.st;
		RegisterClassW( &wc );
	}
}

/* ------------------------------------------------------------- tooltips */

static std::map<HWND, HWND> g_tips;
void setTooltip( HWND parent, HWND c, const char *text )
{
	if ( !c ) return;
	HWND &tt = g_tips[parent];
	if ( !tt || !IsWindow( tt ) ) {
		tt = CreateWindowExW( WS_EX_TOPMOST, TOOLTIPS_CLASSW, 0, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0, parent, 0, GetModuleHandleW( 0 ), 0 );
		SendMessageW( tt, TTM_SETMAXTIPWIDTH, 0, S( 340 ) );
		SendMessageW( tt, TTM_SETDELAYTIME, TTDT_INITIAL, 500 );
		SendMessageW( tt, TTM_SETDELAYTIME, TTDT_AUTOPOP, 20000 );
	}
	static std::map<HWND, ws> store;
	ws &keep = store[c];
	keep = text && *text ? W( text ) : ws( );
	TOOLINFOW ti = { sizeof ti };
	ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS; ti.hwnd = parent; ti.uId = (UINT_PTR)c;
	if ( SendMessageW( tt, TTM_GETTOOLINFOW, 0, (LPARAM)&ti ) ) SendMessageW( tt, TTM_DELTOOLW, 0, (LPARAM)&ti );
	if ( keep.empty( ) ) return;
	ti.lpszText = (LPWSTR)keep.c_str( );
	SendMessageW( tt, TTM_ADDTOOLW, 0, (LPARAM)&ti );
}

/* ------------------------------------------------------------ Wnd base */

static const wchar_t *kWndClass = L"EFWnd";
static LRESULT CALLBACK wndThunk( HWND h, UINT m, WPARAM w, LPARAM l )
{
	Wnd *self = (Wnd *)GetWindowLongPtrW( h, GWLP_USERDATA );
	if ( m == WM_NCCREATE ) {
		self = (Wnd *)( (CREATESTRUCTW *)l )->lpCreateParams;
		SetWindowLongPtrW( h, GWLP_USERDATA, (LONG_PTR)self );
		if ( self ) self->hwnd = h;
	}
	if ( !self ) return DefWindowProcW( h, m, w, l );
	return self->msg( m, w, l );
}

void centerOn( HWND w, HWND owner )
{
	RECT r, o;
	GetWindowRect( w, &r );
	MONITORINFO mi = { sizeof mi };
	HMONITOR mon = MonitorFromWindow( owner && IsWindowVisible( owner ) ? owner : w, MONITOR_DEFAULTTOPRIMARY );
	GetMonitorInfoW( mon, &mi );
	if ( owner && IsWindowVisible( owner ) && !IsIconic( owner ) ) GetWindowRect( owner, &o ); else o = mi.rcWork;
	int x = o.left + ( ( o.right - o.left ) - ( r.right - r.left ) ) / 2, y = o.top + ( ( o.bottom - o.top ) - ( r.bottom - r.top ) ) / 2;
	x = max( (int)mi.rcWork.left, min( x, (int)mi.rcWork.right - ( r.right - r.left ) ) );
	y = max( (int)mi.rcWork.top, min( y, (int)mi.rcWork.bottom - ( r.bottom - r.top ) ) );
	SetWindowPos( w, 0, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE );
}

bool Wnd::create( const wchar_t *title, int cw, int ch, DWORD style, DWORD ex, HWND own, bool center )
{
	registerClasses( );
	static bool reg;
	if ( !reg ) {
		reg = true;
		WNDCLASSW wc = {};
		wc.lpfnWndProc = wndThunk; wc.hInstance = GetModuleHandleW( 0 ); wc.lpszClassName = kWndClass;
		wc.hCursor = LoadCursor( 0, IDC_ARROW ); wc.hIcon = LoadIconW( GetModuleHandleW( 0 ), MAKEINTRESOURCEW( 1 ) );
		RegisterClassW( &wc );
	}
	owner = own;
	if ( own ) setDpi( dpiOf( own ) );
	RECT r = { 0, 0, S( cw ), S( ch ) };
	typedef BOOL ( WINAPI *AdjFn )( LPRECT, DWORD, BOOL, DWORD, UINT );
	static AdjFn adj = (AdjFn)GetProcAddress( GetModuleHandleW( L"user32.dll" ), "AdjustWindowRectExForDpi" );
	if ( adj ) adj( &r, style, FALSE, ex, g_dpi ); else AdjustWindowRectEx( &r, style, FALSE, ex );
	hwnd = CreateWindowExW( ex, kWndClass, title, style | WS_CLIPSIBLINGS, 0, 0, r.right - r.left, r.bottom - r.top, own, 0, GetModuleHandleW( 0 ), this );
	if ( !hwnd ) return false;
	if ( center ) centerOn( hwnd, own );
	return true;
}

void Wnd::show( ) { applyTheme( hwnd ); ShowWindow( hwnd, SW_SHOW ); UpdateWindow( hwnd ); }

void Wnd::end( int r ) { result = r; done = true; PostMessageW( hwnd, WM_NULL, 0, 0 ); }

str g_snapPath; int g_snapDelayMs = 700; bool g_snapClose = true;
std::vector<Wnd *> g_sheets;

int Wnd::runModal( )
{
	if ( owner && IsWindow( owner ) ) { ownerWasEnabled = IsWindowEnabled( owner ) != 0; EnableWindow( owner, FALSE ); }
	done = false;
	show( );
	SetForegroundWindow( hwnd );
	g_sheets.push_back( this );
	if ( !g_snapPath.empty( ) ) SetTimer( hwnd, 0x7E57, g_snapDelayMs, 0 );
	while ( !done ) {
		MSG m;
		BOOL r = GetMessageW( &m, 0, 0, 0 );
		if ( r == 0 ) { PostQuitMessage( (int)m.wParam ); break; }
		if ( r < 0 ) break;
		if ( preTranslate( &m ) ) continue;
		if ( IsDialogMessageW( hwnd, &m ) ) continue;
		TranslateMessage( &m ); DispatchMessageW( &m );
	}
	g_sheets.erase( std::remove( g_sheets.begin( ), g_sheets.end( ), this ), g_sheets.end( ) );
	if ( owner && IsWindow( owner ) ) { if ( ownerWasEnabled ) EnableWindow( owner, TRUE ); SetForegroundWindow( owner ); }
	DestroyWindow( hwnd );
	hwnd = 0;
	return result;
}

void Wnd::paintBg( HDC dc, RECT rc )
{
	HBRUSH b = CreateSolidBrush( T.bg );
	FillRect( dc, &rc, b );
	DeleteObject( b );
	for ( auto &gr : groups ) {
		if ( !gr.visible ) continue;
		Group g = gr; OffsetRect( &g.r, 0, -scrollY );
		RECT t; if ( !IntersectRect( &t, &g.r, &rc ) ) continue;
		{
			/* drawn off screen (GDI+ ignores the DC origin a child sets when it asks us for its background), then copied */
			int gw = g.r.right - g.r.left, gh = g.r.bottom - g.r.top;
			HDC md = CreateCompatibleDC( dc ); HBITMAP mb = CreateCompatibleBitmap( dc, gw, gh ); HGDIOBJ mo = SelectObject( md, mb );
			RECT all = { 0, 0, gw, gh }; HBRUSH bb = CreateSolidBrush( T.bg ); FillRect( md, &all, bb ); DeleteObject( bb );
			if ( g.kind == 1 ) fillRoundRect( md, all, S( g.radius ), blend( T.bg, T.accent, 15 ), CLR_NONE, 0 );
			else fillRoundRect( md, all, S( g.radius ), T.group, T.sep, 1 );
			BitBlt( dc, g.r.left, g.r.top, gw, gh, md, 0, 0, SRCCOPY );
			SelectObject( md, mo ); DeleteObject( mb ); DeleteDC( md );
			if ( g.kind == 1 ) continue;
		}
		if ( !g.caption.empty( ) ) {
			SetBkMode( dc, TRANSPARENT ); SetTextColor( dc, T.text2 );
			HGDIOBJ o = SelectObject( dc, F( 11, 600 ) );
			RECT cr = { g.r.left + S( 2 ), g.r.top - S( 19 ), g.r.right, g.r.top - S( 3 ) };
			DrawTextW( dc, g.caption.c_str( ), -1, &cr, DT_SINGLELINE | DT_NOPREFIX | DT_BOTTOM );
			SelectObject( dc, o );
		}
	}
}

LRESULT Wnd::msg( UINT m, WPARAM w, LPARAM l )
{
	bool handled = false;
	LRESULT r = onOther( m, w, l, &handled );
	if ( handled ) return r;
	switch ( m ) {
	case WM_ERASEBKGND: {
		/* a child asks us to paint what is behind it: its DC has a viewport origin that puts our client origin right */
		RECT rc; GetClientRect( hwnd, &rc );
		paintBg( (HDC)w, rc );
		return 1; }
	case WM_COMMAND: {
		int id = LOWORD( w ), code = HIWORD( w );
		if ( id == IDCANCEL && code == 0 && !l ) { if ( onClose( ) ) return 0; }
		onCommand( id, code, (HWND)l );
		return 0; }
	case WM_PAINT: { PAINTSTRUCT ps; HDC pdc = BeginPaint( hwnd, &ps ); paintOver( pdc ); EndPaint( hwnd, &ps ); return 0; }
	case WM_PRINTCLIENT: { RECT prc; GetClientRect( hwnd, &prc ); paintBg( (HDC)w, prc ); paintOver( (HDC)w ); return 0; }
	case WM_NOTIFY: return onNotify( (NMHDR *)l );
	case WM_TIMER:
		if ( w == 0x7E57 ) {
			KillTimer( hwnd, 0x7E57 );
			str p = g_snapPath; g_snapPath.clear( );
			snapshotWindow( hwnd, p.c_str( ) );
			if ( g_snapClose ) end( IDCANCEL );
			return 0;
		}
		onTimer( (int)w ); return 0;
	case WM_CLOSE: if ( onClose( ) ) return 0; break;
	case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: {
		HDC dc = (HDC)w; SetTextColor( dc, T.text ); SetBkColor( dc, T.field );
		return (LRESULT)fieldBrush( ); }
	case WM_CTLCOLORSTATIC: case WM_CTLCOLORBTN: {
		HDC dc = (HDC)w; SetTextColor( dc, T.text ); SetBkColor( dc, T.bg );
		return (LRESULT)bgBrush( ); }
	case WM_DPICHANGED: {
		RECT *r = (RECT *)l;
		setDpi( HIWORD( w ) );
		SetWindowPos( hwnd, 0, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE );
		onDpiChanged( );
		return 0; }
	case WM_SETTINGCHANGE:
		if ( l && !lstrcmpW( (const wchar_t *)l, L"ImmersiveColorSet" ) && !testEnv( "VOYAGERSP_TEST_LAUNCHER_APPEARANCE" ) ) {
			themeInit( ); applyTheme( hwnd ); InvalidateRect( hwnd, 0, TRUE );
			RedrawWindow( hwnd, 0, 0, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN );
		}
		break;
	default:
		if ( m >= WM_APP ) { onUser( m, w, l ); return 0; }
	}
	return DefWindowProcW( hwnd, m, w, l );
}

HWND Wnd::ctl_( const wchar_t *cls, const wchar_t *text, DWORD style, DWORD ex, int x, int y, int w, int h, int id, int px, int weight )
{
	HWND c = CreateWindowExW( ex, cls, text, style | WS_CHILD | WS_VISIBLE, S( x ), S( y ), S( w ), S( h ), hwnd, (HMENU)(INT_PTR)id, GetModuleHandleW( 0 ), 0 );
	if ( c ) { SendMessageW( c, WM_SETFONT, (WPARAM)F( px, weight ), TRUE ); if ( id ) ids[id] = c; }
	return c;
}

HWND Wnd::label( int x, int y, int w, int h, const char *text, int px, int weight, int role, UINT dt, int id )
{
	HWND c = ctl_( L"EFLabel", W( text ).c_str( ), 0, 0, x, y, w, h, id, px, weight );
	SendMessageW( c, LBL_SETROLE, role, 0 );
	SendMessageW( c, LBL_SETDT, dt, 0 );
	return c;
}
HWND Wnd::button( int x, int y, int w, int h, const char *text, int id, bool def, int px )
{
	return ctl_( L"BUTTON", W( text ).c_str( ), WS_TABSTOP | ( def ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON ), 0, x, y, w, h, id, px );
}
HWND Wnd::check( int x, int y, int w, int h, const char *text, int id, bool on, int px )
{
	HWND c = ctl_( L"BUTTON", W( text ).c_str( ), WS_TABSTOP | BS_AUTOCHECKBOX | BS_MULTILINE, 0, x, y, w, h, id, px );
	SendMessageW( c, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0 );
	return c;
}
HWND Wnd::radio( int x, int y, int w, int h, const char *text, int id, bool first, int px )
{
	return ctl_( L"BUTTON", W( text ).c_str( ), WS_TABSTOP | BS_AUTORADIOBUTTON | ( first ? WS_GROUP : 0 ), 0, x, y, w, h, id, px );
}
HWND Wnd::edit( int x, int y, int w, int h, int id, const char *text, DWORD extra, int px )
{
	return ctl_( L"EDIT", W( text ).c_str( ), WS_TABSTOP | ES_AUTOHSCROLL | extra, WS_EX_CLIENTEDGE, x, y, w, h, id, px );
}
HWND Wnd::combo( int x, int y, int w, int id, int px )
{
	return ctl_( L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_HASSTRINGS, 0, x, y, w, 220, id, px );
}
int Wnd::addGroup( int x, int y, int w, int h, const char *caption, int radius )
{
	groups.push_back( Group{ { S( x ), S( y ), S( x + w ), S( y + h ) }, radius, W( caption ) } );
	return (int)groups.size( ) - 1;
}
void Wnd::moveCtl( HWND c, int x, int y, int w, int h ) { SetWindowPos( c, 0, S( x ), S( y ), S( w ), S( h ), SWP_NOZORDER | SWP_NOACTIVATE ); }
void Wnd::setEnabled( int id, bool on ) { HWND c = ctl( id ); if ( c ) EnableWindow( c, on ); }
void Wnd::showCtl( int id, bool on ) { HWND c = ctl( id ); if ( c ) ShowWindow( c, on ? SW_SHOWNA : SW_HIDE ); }

void setRole( HWND h, int role ) { SendMessageW( h, LBL_SETROLE, role, 0 ); }

HWND mkSeg( HWND p, int x, int y, int w, int h, int id )
{
	registerClasses( );
	HWND c = CreateWindowExW( 0, L"EFSeg", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, S( x ), S( y ), S( w ), S( h ), p, (HMENU)(INT_PTR)id, GetModuleHandleW( 0 ), 0 );
	return c;
}
void segSetItems( HWND seg, const std::vector<str> &items, const std::vector<str> & )
{
	SendMessageW( seg, SEG_CLEAR, 0, 0 );
	for ( auto &s : items ) SendMessageW( seg, SEG_ADD, (WPARAM)s.c_str( ), 0 );
}
HWND mkCard( HWND p, int x, int y, int w, int h, int id, int glyph, const char *title, const char *detail )
{
	registerClasses( );
	HWND c = CreateWindowExW( 0, L"EFCard", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS, S( x ), S( y ), S( w ), S( h ), p, (HMENU)(INT_PTR)id, GetModuleHandleW( 0 ), 0 );
	CardData *d = (CardData *)GetWindowLongPtrW( c, GWLP_USERDATA );
	d->glyph = glyph; d->title = W( title ); d->detail = W( detail );
	return c;
}
void cardSetSelected( HWND c, bool sel ) { SendMessageW( c, CARD_SETSEL, sel, 0 ); }
HWND mkIconButton( HWND p, int x, int y, int w, int h, int id, int glyph, const char *tip, int sizePx )
{
	registerClasses( );
	HWND c = CreateWindowExW( 0, L"EFIconBtn", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, S( x ), S( y ), S( w ), S( h ), p, (HMENU)(INT_PTR)id, GetModuleHandleW( 0 ), 0 );
	IconBtn *d = (IconBtn *)GetWindowLongPtrW( c, GWLP_USERDATA );
	d->glyph = glyph; d->size = sizePx;
	if ( tip && *tip ) { setTooltip( p, c, tip ); SetWindowTextW( c, W( tip ).c_str( ) ); }
	return c;
}
void iconButtonSetGlyph( HWND b, int glyph ) { IconBtn *d = (IconBtn *)GetWindowLongPtrW( b, GWLP_USERDATA ); if ( d ) { d->glyph = glyph; InvalidateRect( b, 0, FALSE ); } }
HWND mkSpinner( HWND p, int x, int y, int sz, int id )
{
	registerClasses( );
	return CreateWindowExW( 0, L"EFSpin", L"", WS_CHILD, S( x ), S( y ), S( sz ), S( sz ), p, (HMENU)(INT_PTR)id, GetModuleHandleW( 0 ), 0 );
}
void spinnerRun( HWND s, bool on ) { SendMessageW( s, WM_APP + 1, on, 0 ); }

/* ----------------------------------------------------------------- snapshots */

/* Render a window's client area and its children by hand (WM_PRINTCLIENT / WM_PRINT): the same on
   Windows and under Wine, whatever is in front of it on the screen. */
static void printChildren( HWND parent, HDC dc, int ox, int oy )
{
	HWND c = GetWindow( parent, GW_CHILD );
	if ( !c ) return;
	c = GetWindow( c, GW_HWNDLAST );
	for ( ; c; c = GetWindow( c, GW_HWNDPREV ) ) {
		if ( !IsWindowVisible( c ) ) continue;
		wchar_t cls[64]; GetClassNameW( c, cls, 64 );
		if ( !lstrcmpW( cls, TOOLTIPS_CLASSW ) ) continue;
		RECT r; GetWindowRect( c, &r );
		MapWindowPoints( 0, parent, (POINT *)&r, 2 );
		int w = r.right - r.left, h = r.bottom - r.top;
		if ( w <= 0 || h <= 0 ) continue;
		int savedDc = SaveDC( dc );
		IntersectClipRect( dc, ox + r.left, oy + r.top, ox + r.left + w, oy + r.top + h );
		SetViewportOrgEx( dc, ox + r.left, oy + r.top, 0 );
		if ( !lstrcmpW( cls, L"EFLabel" ) || !lstrcmpW( cls, L"EFSpin" ) || !lstrcmpW( cls, L"EFCard" ) ||
			!lstrcmpW( cls, L"EFSeg" ) || !lstrcmpW( cls, L"EFIconBtn" ) ) {
			/* these paint through a memory DC that asks the parent for the background */
			SendMessageW( c, WM_PRINTCLIENT, (WPARAM)dc, PRF_CLIENT );
		} else if ( !lstrcmpiW( cls, L"Button" ) ) {
			/* Wine's WM_PRINT draws nothing for buttons: draw them as the system would, roughly */
			LONG st = GetWindowLongW( c, GWL_STYLE ); int bt = st & BS_TYPEMASK;
			RECT rc = { 0, 0, w, h };
			wchar_t txt[512]; int n = GetWindowTextW( c, txt, 512 );
			bool en = IsWindowEnabled( c ) != 0;
			HDC m = CreateCompatibleDC( dc ); (void)m; DeleteDC( m );
			SetViewportOrgEx( dc, ox, oy, 0 );
			SendMessageW( GetParent( c ), WM_ERASEBKGND, (WPARAM)dc, 1 );
			SetViewportOrgEx( dc, ox + r.left, oy + r.top, 0 );
			SetBkMode( dc, TRANSPARENT );
			SelectObject( dc, (HFONT)SendMessageW( c, WM_GETFONT, 0, 0 ) );
			SetTextColor( dc, en ? T.text : T.text3 );
			if ( bt == BS_AUTOCHECKBOX || bt == BS_CHECKBOX || bt == BS_AUTORADIOBUTTON || bt == BS_RADIOBUTTON ) {
				bool rad = bt == BS_AUTORADIOBUTTON || bt == BS_RADIOBUTTON, on = SendMessageW( c, BM_GETCHECK, 0, 0 ) == BST_CHECKED;
				int bs = S( 14 ), by = ( h - bs ) / 2;
				RECT br = { 0, by, bs, by + bs };
				fillRoundRect( dc, br, rad ? bs / 2 : S( 3 ), on && en ? T.accent : T.field, on && en ? T.accent : T.text3, 1 );
				if ( on ) { if ( rad ) { RECT d = br; InflateRect( &d, -S( 4 ), -S( 4 ) ); fillRoundRect( dc, d, S( 3 ), en ? RGB( 255, 255, 255 ) : T.text3, CLR_NONE, 0 ); }
					else { HPEN p = CreatePen( PS_SOLID, max( 2, S( 2 ) ), en ? RGB( 255, 255, 255 ) : T.text3 ); HGDIOBJ o = SelectObject( dc, p );
						MoveToEx( dc, br.left + S( 3 ), by + S( 7 ), 0 ); LineTo( dc, br.left + S( 6 ), by + S( 10 ) ); LineTo( dc, br.left + S( 11 ), by + S( 4 ) ); SelectObject( dc, o ); DeleteObject( p ); } }
				RECT tr = { bs + S( 6 ), 0, w, h };
				DrawTextW( dc, txt, n, &tr, DT_VCENTER | ( ( st & BS_MULTILINE ) ? DT_WORDBREAK : DT_SINGLELINE ) | DT_NOPREFIX );
			} else {
				bool def = bt == BS_DEFPUSHBUTTON;
				fillRoundRect( dc, rc, S( 4 ), def ? blend( T.btn, T.accent, 10 ) : T.btn, def ? T.accent : T.sep, def ? 2 : 1 );
				DrawTextW( dc, txt, n, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX );
			}
		} else if ( !lstrcmpiW( cls, L"SysListView32" ) ) {
			/* report-mode list: header and rows drawn through the owner's custom draw */
			RECT wr = { 0, 0, w, h }; HBRUSH fb = CreateSolidBrush( T.sep ); FillRect( dc, &wr, fb ); DeleteObject( fb );
			RECT cl = { 1, 1, w - 1, h - 1 }; HBRUSH bb = CreateSolidBrush( T.field ); FillRect( dc, &cl, bb ); DeleteObject( bb );
			IntersectClipRect( dc, 1, 1, w - 1, h - 1 );
			SetViewportOrgEx( dc, ox + r.left + 1, oy + r.top + 1, 0 );
			HWND hd = ListView_GetHeader( c ); RECT hr = { 0, 0, 0, 0 }; if ( hd ) GetWindowRect( hd, &hr );
			int hh = hr.bottom - hr.top; bool hdrShown = ( GetWindowLongW( c, GWL_STYLE ) & LVS_NOSORTHEADER ) == 0 || true;
			int nc = hd ? Header_GetItemCount( hd ) : 0, x = 0;
			for ( int i = 0; i < nc; i++ ) {
				wchar_t tx[64] = L""; LVCOLUMNW lc = {}; lc.mask = LVCF_TEXT | LVCF_WIDTH; lc.pszText = tx; lc.cchTextMax = 64; ListView_GetColumn( c, i, &lc );
				RECT hc = { x, 0, x + lc.cx, hh }; HBRUSH hb2 = CreateSolidBrush( blend( T.field, T.text, 5 ) ); FillRect( dc, &hc, hb2 ); DeleteObject( hb2 );
				SetBkMode( dc, TRANSPARENT ); SetTextColor( dc, T.text2 ); SelectObject( dc, F( 12 ) ); RECT tr = hc; tr.left += S( 4 );
				DrawTextW( dc, tx, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX );
				x += lc.cx;
			}
			(void)hdrShown;
			int cnt = ListView_GetItemCount( c ), top = ListView_GetTopIndex( c ), id = GetDlgCtrlID( c );
			for ( int i = top; i < cnt && i < top + 40; i++ ) {
				RECT ir; if ( !ListView_GetItemRect( c, i, &ir, LVIR_BOUNDS ) ) break;
				if ( ir.top >= h ) break;
				for ( int j = 0; j < nc; j++ ) {
					RECT sr; if ( !ListView_GetSubItemRect( c, i, j, LVIR_BOUNDS, &sr ) ) continue;
					NMLVCUSTOMDRAW cd = {}; cd.nmcd.hdr.hwndFrom = c; cd.nmcd.hdr.idFrom = id; cd.nmcd.hdr.code = NM_CUSTOMDRAW;
					cd.nmcd.dwDrawStage = CDDS_ITEMPREPAINT | CDDS_SUBITEM; cd.nmcd.hdc = dc; cd.nmcd.rc = sr; cd.nmcd.dwItemSpec = i; cd.iSubItem = j;
					SendMessageW( parent, WM_NOTIFY, id, (LPARAM)&cd );
				}
			}
		} else if ( !lstrcmpiW( cls, L"ListBox" ) && ( GetWindowLongW( c, GWL_STYLE ) & LBS_OWNERDRAWFIXED ) ) {
			/* owner-drawn list: ask the owner to draw each visible item (Wine prints these with an empty clip) */
			RECT cl; GetClientRect( c, &cl );
			int bx = ( w - cl.right ) / 2, by = ( h - cl.bottom ) / 2;
			RECT wr = { 0, 0, w, h }; HBRUSH fb = CreateSolidBrush( T.sep ); FillRect( dc, &wr, fb ); DeleteObject( fb );
			SetViewportOrgEx( dc, ox + r.left + bx, oy + r.top + by, 0 );
			HBRUSH bb = CreateSolidBrush( T.field ); FillRect( dc, &cl, bb ); DeleteObject( bb );
			int top = (int)SendMessageW( c, LB_GETTOPINDEX, 0, 0 ), cnt = (int)SendMessageW( c, LB_GETCOUNT, 0, 0 );
			for ( int i = top; i < cnt; i++ ) {
				RECT ir; SendMessageW( c, LB_GETITEMRECT, i, (LPARAM)&ir );
				if ( ir.top >= cl.bottom ) break;
				DRAWITEMSTRUCT di = {}; di.CtlType = ODT_LISTBOX; di.CtlID = GetDlgCtrlID( c ); di.itemID = i; di.itemAction = ODA_DRAWENTIRE;
				di.itemState = SendMessageW( c, LB_GETSEL, i, 0 ) > 0 ? ODS_SELECTED : 0; di.hwndItem = c; di.hDC = dc; di.rcItem = ir;
				IntersectClipRect( dc, 0, 0, cl.right, cl.bottom );
				SendMessageW( parent, WM_DRAWITEM, di.CtlID, (LPARAM)&di );
			}
		} else {
			SendMessageW( c, WM_PRINT, (WPARAM)dc, PRF_CLIENT | PRF_NONCLIENT | PRF_ERASEBKGND );
			printChildren( c, dc, ox + r.left, oy + r.top );
		}
		RestoreDC( dc, savedDc );
	}
}

bool snapshotWindow( HWND w, const char *path )
{
	if ( !w || !path || !*path ) return false;
	RECT cr; GetClientRect( w, &cr );
	int cw = cr.right, ch = cr.bottom;
	HDC sdc = GetDC( 0 );
	BITMAPINFO bi = {};
	bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = cw; bi.bmiHeader.biHeight = -ch;
	bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
	void *bits = 0;
	HBITMAP bmp = CreateDIBSection( sdc, &bi, DIB_RGB_COLORS, &bits, 0, 0 );
	HDC mdc = CreateCompatibleDC( sdc );
	HGDIOBJ old = SelectObject( mdc, bmp );
	UpdateWindow( w );
	SendMessageW( w, WM_PRINTCLIENT, (WPARAM)mdc, PRF_CLIENT | PRF_ERASEBKGND );
	printChildren( w, mdc, 0, 0 );
	for ( size_t i = 3; i < (size_t)cw * ch * 4; i += 4 ) ( (unsigned char *)bits )[i] = 255;
	SelectObject( mdc, old );
	bool res = savePng( bmp, path );
	DeleteDC( mdc ); DeleteObject( bmp ); ReleaseDC( 0, sdc );
	lc_log( "[test] snapshot %s (%dx%d): %s", path, cw, ch, res ? "ok" : "FAILED" );
	return res;
}

void copyToClipboard( HWND owner, const str &utf8 )
{
	if ( !OpenClipboard( owner ) ) return;
	EmptyClipboard( );
	ws w = W( utf8 );
	HGLOBAL g = GlobalAlloc( GMEM_MOVEABLE, ( w.size( ) + 1 ) * 2 );
	if ( g ) { memcpy( GlobalLock( g ), w.c_str( ), ( w.size( ) + 1 ) * 2 ); GlobalUnlock( g ); SetClipboardData( CF_UNICODETEXT, g ); }
	CloseClipboard( );
}

str expandHome( const str &p ) { return p; }

/* ------------------------------------------------------------------ alerts */

str g_alertSnapshotPath;

struct AlertWnd : Wnd {
	str msgText, info; std::vector<str> btns; int icon, cancelIdx, defIdx;
	int bw[8];
	bool onClose( ) override
	{
		if ( cancelIdx >= 0 ) { end( cancelIdx ); return true; }
		if ( btns.size( ) == 1 ) { end( 0 ); return true; }
		return true;
	}
	void onCommand( int id, int code, HWND ) override
	{
		if ( id >= 100 && id < 108 && code == BN_CLICKED ) end( id - 100 );
		else if ( id == IDOK ) end( defIdx );
	}
	void paintOver( HDC dc ) override
	{
		{
			int g = icon == 0 ? G_INFO : icon == 1 ? G_TRIANGLE : icon == 2 ? G_TRIANGLE : -1;
			if ( g >= 0 ) drawGlyph( dc, g, S( 22 ), S( 22 ), S( 36 ), icon == 0 ? T.accent : icon == 1 ? T.orange : T.red );
		}
	}
	void onTimer( int id ) override
	{
		if ( id == 1 ) {
			KillTimer( hwnd, 1 );
			snapshotWindow( hwnd, g_alertSnapshotPath.c_str( ) );
			g_alertSnapshotPath.clear( );
			end( defIdx );
		}
	}
};

int Alert( HWND owner, const str &msg, const str &info, const std::vector<str> &buttons, int icon, int cancelIdx, bool )
{
	lc_log( "[dialog] %s \xE2\x80\x94 %s", msg.c_str( ), info.c_str( ) );
	if ( testOn( "VOYAGERSP_TEST_NODIALOG" ) || testOn( "VOYAGERSP_TEST_CONFIRM" ) ) return 0;
	initToolkit( );
	AlertWnd a;
	a.msgText = msg; a.info = info; a.btns = buttons; a.icon = icon; a.cancelIdx = cancelIdx; a.defIdx = 0;
	const int W0 = 440, tx = icon == 3 ? 22 : 78, tw = W0 - tx - 22;
	if ( !owner ) owner = GetActiveWindow( );
	if ( owner ) setDpi( dpiOf( owner ) );
	int mh = textHeight( W( msg ), tw, F( 14, 700 ) ) * 96 / g_dpi, ih = info.empty( ) ? 0 : textHeight( W( info ), tw, F( 12 ) ) * 96 / g_dpi;
	int bodyH = 22 + max( 40, mh + ( ih ? 8 + ih : 0 ) ) + 24;
	int H = bodyH + 44;
	if ( !a.create( L"Elite Force", W0, H, WS_POPUP | WS_CAPTION | WS_SYSMENU, 0, owner ) ) return 0;
	a.label( tx, 22, tw, mh + 2, msg.c_str( ), 14, 700, R_TEXT );
	if ( ih ) a.label( tx, 22 + mh + 8, tw, ih + 2, info.c_str( ), 12, 400, R_TEXT2 );
	int n = (int)buttons.size( ), x = W0 - 20;
	for ( int i = n - 1; i >= 0; i-- ) {
		int w = max( 80, textWidth( W( buttons[i] ), F( 13 ) ) * 96 / g_dpi + 28 );
		x -= w;
		a.button( x, H - 46, w, 28, buttons[i].c_str( ), 100 + i, i == 0 );
		x -= 8;
	}
	SetFocus( a.ctl( 100 ) );
	if ( !g_alertSnapshotPath.empty( ) ) SetTimer( a.hwnd, 1, 300, 0 );
	return a.runModal( );
}

struct PromptWnd : Wnd {
	std::vector<str> btns; HWND field; str text;
	void onCommand( int id, int code, HWND ) override
	{
		if ( id >= 100 && id < 108 && code == BN_CLICKED ) { text = getText( field ); end( id - 100 ); }
		else if ( id == IDOK ) { text = getText( field ); end( 0 ); }
	}
};

int PromptText( HWND owner, const str &msg, const str &info, const str &ph, bool secret, const std::vector<str> &buttons, str *out )
{
	lc_log( "[dialog] %s \xE2\x80\x94 %s", msg.c_str( ), info.c_str( ) );
	if ( testOn( "VOYAGERSP_TEST_NODIALOG" ) || testOn( "VOYAGERSP_TEST_CONFIRM" ) ) return 0;
	initToolkit( );
	PromptWnd a; a.btns = buttons;
	const int W0 = 420;
	if ( owner ) setDpi( dpiOf( owner ) );
	int mh = textHeight( W( msg ), W0 - 44, F( 14, 700 ) ) * 96 / g_dpi, ih = textHeight( W( info ), W0 - 44, F( 12 ) ) * 96 / g_dpi;
	int H = 20 + mh + 6 + ih + 12 + 26 + 18 + 44;
	if ( !a.create( L"Elite Force", W0, H, WS_POPUP | WS_CAPTION | WS_SYSMENU, 0, owner ) ) return -1;
	a.label( 22, 20, W0 - 44, mh + 2, msg.c_str( ), 14, 700 );
	a.label( 22, 20 + mh + 6, W0 - 44, ih + 2, info.c_str( ), 12, 400, R_TEXT2 );
	a.field = a.edit( 22, 20 + mh + 6 + ih + 12, W0 - 44, 24, 50, "", secret ? ES_PASSWORD : 0 );
	if ( !ph.empty( ) ) SendMessageW( a.field, EM_SETCUEBANNER, TRUE, (LPARAM)W( ph ).c_str( ) );
	int n = (int)buttons.size( ), x = W0 - 20;
	for ( int i = n - 1; i >= 0; i-- ) {
		int w = max( 80, textWidth( W( buttons[i] ), F( 13 ) ) * 96 / g_dpi + 28 );
		x -= w; a.button( x, H - 46, w, 28, buttons[i].c_str( ), 100 + i, i == 0 ); x -= 8;
	}
	SetFocus( a.field );
	int r = a.runModal( );
	if ( out ) *out = a.text;
	return r;
}

HWND mainWindowOrActive( ) { return GetActiveWindow( ); }

}	// namespace wl
