/*
===========================================================================
Windows launcher: shared UI toolkit (theme, DPI, fonts, custom controls,
window base class, alerts, snapshots, glyph icons).

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#ifndef WL_UI_H
#define WL_UI_H

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <functional>
#include <algorithm>

extern "C" {
#include "../../launcher/lc_common.h"
}

namespace wl {

using std::min;
using std::max;
inline long min( int a, long b ) { return a < b ? a : b; }
inline long min( long a, int b ) { return a < b ? a : b; }
inline long max( int a, long b ) { return a > b ? a : b; }
inline long max( long a, int b ) { return a > b ? a : b; }

typedef std::wstring ws;
typedef std::string str;

ws W( const char *utf8 );
inline ws W( const str &s ) { return W( s.c_str() ); }
str U( const wchar_t *w, int len = -1 );
inline str U( const ws &w ) { return U( w.c_str(), (int)w.size() ); }
str fmt( const char *f, ... )
#if defined( __GNUC__ )
	__attribute__(( format( printf, 1, 2 ) ))
#endif
	;

/* ---- colours and theme ---- */
enum Role { R_TEXT, R_TEXT2, R_TEXT3, R_ORANGE, R_GREEN, R_RED, R_ACCENT, R_PURPLE, R_BLUE, R_AMBER,
	R_YELLOW, R_TEAL, R_PINK, R_BLACK };
struct Theme {
	bool dark;
	COLORREF bg, text, text2, text3, sep, group, field, accent, orange, green, red, purple, blue, amber,
		yellow, teal, pink, btn;
};
extern Theme T;
void themeInit( );					/* AppsUseLightTheme or VOYAGERSP_TEST_LAUNCHER_APPEARANCE */
COLORREF roleColor( int role );
COLORREF blend( COLORREF base, COLORREF over, int pctOver );

/* ---- DPI, fonts ---- */
extern int g_dpi;
int S( int v );						/* design pixel (96 dpi) -> device pixel */
HFONT F( int px, int weight = 400, bool mono = false );
void fontsReset( );
void setDpi( int dpi );
int dpiOf( HWND w );
int textHeight( const ws &text, int widthDesign, HFONT f, UINT flags = DT_WORDBREAK );
int textWidth( const ws &text, HFONT f );

/* ---- glyph icons (GDI+, drawn) ---- */
enum Glyph { G_CHECK_CIRCLE, G_X_CIRCLE, G_EXCL_CIRCLE, G_TRIANGLE, G_DOTTED, G_REFRESH, G_STAR, G_STAR_FILL,
	G_LOCK, G_GLOBE, G_PLUS, G_MINUS, G_UP, G_DOWN, G_PERSON, G_PERSON3, G_SERVER, G_MAP, G_QUESTION, G_PLAY,
	G_FORWARD, G_RESTART, G_BOTADD, G_BOTSDEL, G_BUBBLE, G_DOT, G_DOWN_CIRCLE, G_TERMINAL, G_PUZZLE,
	G_PUZZLE_FILL, G_FOLDER, G_INFO, G_APP, G_MORE };
void drawGlyph( HDC dc, int glyph, int x, int y, int size, COLORREF c );	/* device pixels */
void fillRoundRect( HDC dc, RECT r, int radius, COLORREF fill, COLORREF stroke, int strokeW );
void drawAppIcon( HDC dc, int x, int y, int size );

/* ---- images ---- */
HBITMAP decodeImage( const unsigned char *data, size_t len, bool isTga, int *w, int *h );
bool savePng( HBITMAP bmp, const char *path );
HBITMAP scaledBitmap( HBITMAP src, int w, int h );

/* ---- window base ---- */
struct Group { RECT r; int radius; ws caption; bool visible = true; int kind = 0; };
struct Wnd {
	HWND hwnd = 0, owner = 0;
	bool done = false, ownerWasEnabled = false;
	int result = 0, scrollY = 0;
	std::vector<Group> groups;
	std::map<int, HWND> ids;
	virtual ~Wnd( ) {}
	bool create( const wchar_t *title, int cw, int ch, DWORD style, DWORD ex, HWND owner, bool centerOnOwner = true );
	int runModal( );
	void end( int r );
	void show( );
	virtual LRESULT msg( UINT m, WPARAM w, LPARAM l );
	virtual void paintBg( HDC dc, RECT rc );
	virtual void paintOver( HDC dc ) { (void)dc; }
	virtual void onCommand( int id, int code, HWND ctl ) { (void)id; (void)code; (void)ctl; }
	virtual void onTimer( int id ) { (void)id; }
	virtual void onUser( UINT m, WPARAM w, LPARAM l ) { (void)m; (void)w; (void)l; }
	virtual bool preTranslate( MSG *m ) { (void)m; return false; }
	virtual bool onClose( ) { end( IDCANCEL ); return true; }
	virtual void onDpiChanged( ) {}
	virtual LRESULT onNotify( NMHDR *h ) { (void)h; return 0; }
	virtual LRESULT onOther( UINT m, WPARAM w, LPARAM l, bool *handled ) { (void)m; (void)w; (void)l; *handled = false; return 0; }
	/* controls (design pixels) */
	HWND ctl( int id ) { auto i = ids.find( id ); return i == ids.end( ) ? 0 : i->second; }
	HWND label( int x, int y, int w, int h, const char *text, int px = 13, int weight = 400, int role = R_TEXT,
		UINT dt = DT_WORDBREAK, int id = 0 );
	HWND button( int x, int y, int w, int h, const char *text, int id, bool def = false, int px = 13 );
	HWND check( int x, int y, int w, int h, const char *text, int id, bool on = false, int px = 13 );
	HWND radio( int x, int y, int w, int h, const char *text, int id, bool first = false, int px = 13 );
	HWND edit( int x, int y, int w, int h, int id, const char *text = "", DWORD extra = 0, int px = 13 );
	HWND combo( int x, int y, int w, int id, int px = 13 );
	HWND ctl_( const wchar_t *cls, const wchar_t *text, DWORD style, DWORD ex, int x, int y, int w, int h, int id,
		int px = 13, int weight = 400 );
	int addGroup( int x, int y, int w, int h, const char *caption, int radius = 10 );
	void moveCtl( HWND c, int x, int y, int w, int h );
	void setEnabled( int id, bool on );
	void showCtl( int id, bool on );
};
HWND mainWindowOrActive( );
void applyTheme( HWND w );			/* dark title bar + control themes for w and its children */
void centerOn( HWND w, HWND owner );
void setText( HWND h, const str &utf8 );
str getText( HWND h );
void setRole( HWND h, int role );
void setTooltip( HWND parent, HWND ctl, const char *text );
void initToolkit( );				/* common controls, gdi+, themes */
void pumpFor( int ms );				/* process messages for ms */

/* custom control classes */
enum { SEG_SETSEL = WM_APP + 100, SEG_GETSEL, SEG_ADD, SEG_DISABLE, SEG_CLEAR };
HWND mkSeg( HWND parent, int x, int y, int w, int h, int id );
void segSetItems( HWND seg, const std::vector<str> &items, const std::vector<str> &tips = {} );
HWND mkCard( HWND parent, int x, int y, int w, int h, int id, int glyph, const char *title, const char *detail );
void cardSetSelected( HWND card, bool sel );
HWND mkIconButton( HWND parent, int x, int y, int w, int h, int id, int glyph, const char *tip, int sizePx = 12 );
void iconButtonSetGlyph( HWND b, int glyph );
HWND mkSpinner( HWND parent, int x, int y, int sz, int id );
void spinnerRun( HWND spin, bool on );

/* alerts: message (bold), informative text; buttons[0] is the default; cancelIdx answers Esc (-1: none).
   icon 0 info, 1 warning, 2 critical, 3 none. Honours VOYAGERSP_TEST_NODIALOG / _CONFIRM. */
int Alert( HWND owner, const str &msg, const str &info, const std::vector<str> &buttons, int icon = 1,
	int cancelIdx = -1, bool destructiveFirst = false );
extern str g_snapPath;			// the next modal window is rendered to this PNG after g_snapDelayMs (and closed if g_snapClose)
extern int g_snapDelayMs;
extern bool g_snapClose;
extern std::vector<Wnd *> g_sheets;	// open modal windows (a link closes them)
extern str g_alertSnapshotPath;		/* when set, the next alert is rendered to it, then answered */
int PromptText( HWND owner, const str &msg, const str &info, const str &placeholder, bool secret,
	const std::vector<str> &buttons, str *outText );		/* returns button index */

/* snapshot */
bool snapshotWindow( HWND w, const char *pngPath );
void copyToClipboard( HWND owner, const str &utf8 );
int  inputBox( );
str  expandHome( const str &p );		/* path with the home folder as ~ */

/* test env */
inline const char *testEnv( const char *n ) { return lc_env( n ); }
inline bool testOn( const char *n ) { return lc_test_env( n ) != 0; }

}	// namespace wl

#endif
