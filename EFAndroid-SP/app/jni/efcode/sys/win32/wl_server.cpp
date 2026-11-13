/*
===========================================================================
Windows launcher: server settings glue, the Master Servers sheet
(servers.md section 6) and the Loopback Host Fix / public address (10).

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#include "wl_app.h"
#include <commctrl.h>

namespace wl {

void serverSettingsLoad( lc_server_settings *s ) { lc_server_settings_load( s, lc_store_shared( ), lc_user_full_name( ) ); }
void publicAddressStart( ) { lc_public_address_start( ); }
str publicAddress( ) { const char *a = lc_public_address( ); return a ? a : ""; }
bool publicAddressPending( ) { return lc_public_address_pending( ) != 0; }
bool loopbackFix( ) { return lc_store_get_bool( lc_store_shared( ), LC_KEY_LOOPBACK_FIX, 1 ) != 0; }
void setLoopbackFix( bool on )
{
	lc_store *st = lc_store_shared( );
	if ( on ) lc_store_remove( st, LC_KEY_LOOPBACK_FIX ); else lc_store_set_int( st, LC_KEY_LOOPBACK_FIX, 0 );
	lc_store_save( st );
}

str masterSummary( int *count, bool *orange, str *tip )
{
	char list[LC_MAX_MASTERS][65];
	int n = lc_masters_load( lc_store_shared( ), list );
	*count = n; *orange = false; tip->clear( );
	for ( int i = 0; i < n; i++ ) { if ( i ) *tip += "\n"; *tip += list[i]; }
	if ( n == 0 ) { *orange = true; return "No master servers"; }
	if ( n == 1 ) return fmt( "1 master server \xC2\xB7 %s", list[0] );
	return fmt( "%d master servers \xC2\xB7 Internet 1: %s", n, list[0] );
}

/* ================================================================= Master Servers */

struct MastersDlg : Wnd {
	std::vector<str> masters;
	std::map<str, int> answers;			/* address -> servers, -1 no answer */
	std::set<str> checking;
	HWND list = 0, edit_ = 0, hint = 0, caption = 0;
	int editing = -1; str editingWas;
	bool dragging = false; int dragRow = -1; POINT dragStart = {};
	enum { ID_LIST = 10, ID_ADD, ID_REMOVE, ID_UP, ID_DOWN, ID_CHECK, ID_RECOMMENDED, ID_ORIGINAL, ID_CAPTION, ID_EDIT };
	struct Res { MastersDlg *d; str addr; int n; };

	static void worker( void *u )
	{
		Res *r = (Res *)u;
		r->n = lc_query_master_count( r->addr.c_str( ) );
		PostMessageW( r->d->hwnd, WM_APP + 20, 0, (LPARAM)r );
	}
	void check( const str &m )
	{
		if ( m.empty( ) || checking.count( m ) ) return;
		checking.insert( m ); answers.erase( m ); reloadRow( m );
		Res *r = new Res{ this, m, 0 };
		lc_thread *t = lc_thread_start( worker, r );
		if ( t ) lc_thread_detach( t ); else { checking.erase( m ); delete r; }
	}
	void checkAll( bool onlyWaiting )
	{
		std::vector<str> c = masters;
		for ( auto &m : c ) if ( !onlyWaiting || !answers.count( m ) ) check( m );
	}
	void onUser( UINT m, WPARAM, LPARAM l ) override
	{
		if ( m != WM_APP + 20 ) return;
		Res *r = (Res *)l;
		checking.erase( r->addr ); answers[r->addr] = r->n;
		lc_log( r->n < 0 ? "Master %s: no answer" : "Master %s: %d servers", r->addr.c_str( ), r->n );
		reloadRow( r->addr );
		delete r;
	}
	void reloadRow( const str &addr )
	{
		for ( size_t i = 0; i < masters.size( ); i++ ) if ( masters[i] == addr ) { RECT rc; SendMessageW( list, LB_GETITEMRECT, i, (LPARAM)&rc ); InvalidateRect( list, &rc, TRUE ); }
	}
	void refill( )
	{
		int top = (int)SendMessageW( list, LB_GETTOPINDEX, 0, 0 );
		SendMessageW( list, LB_RESETCONTENT, 0, 0 );
		for ( auto &m : masters ) SendMessageW( list, LB_ADDSTRING, 0, (LPARAM)W( m ).c_str( ) );
		SendMessageW( list, LB_SETTOPINDEX, top, 0 );
		setText( caption, masters.size( ) == 1 ? "MASTER SERVERS \xC2\xB7 1" : fmt( "MASTER SERVERS \xC2\xB7 %d", (int)masters.size( ) ) );
		buttons( );
	}
	std::vector<int> sel( )
	{
		std::vector<int> r; int n = (int)SendMessageW( list, LB_GETSELCOUNT, 0, 0 );
		if ( n <= 0 ) return r;
		std::vector<int> b( n ); SendMessageW( list, LB_GETSELITEMS, n, (LPARAM)b.data( ) );
		return b;
	}
	void select( const std::vector<int> &rows )
	{
		SendMessageW( list, LB_SETSEL, FALSE, -1 );
		for ( int r : rows ) SendMessageW( list, LB_SETSEL, TRUE, r );
		buttons( );
	}
	void buttons( )
	{
		auto s = sel( ); int n = (int)masters.size( );
		EnableWindow( ctl( ID_ADD ), n < LC_MAX_MASTERS );
		EnableWindow( ctl( ID_REMOVE ), !s.empty( ) );
		EnableWindow( ctl( ID_UP ), !s.empty( ) && s.front( ) > 0 );
		EnableWindow( ctl( ID_DOWN ), !s.empty( ) && s.back( ) < n - 1 );
	}
	void moveRows( std::vector<int> rows, int toAbove )
	{
		std::sort( rows.begin( ), rows.end( ) );
		std::vector<str> moved; int above = 0;
		for ( int r : rows ) { moved.push_back( masters[r] ); if ( r < toAbove ) above++; }
		for ( int i = (int)rows.size( ) - 1; i >= 0; i-- ) masters.erase( masters.begin( ) + rows[i] );
		int at = max( 0, min( toAbove - above, (int)masters.size( ) ) );
		masters.insert( masters.begin( ) + at, moved.begin( ), moved.end( ) );
		refill( );
		std::vector<int> ns; for ( size_t i = 0; i < moved.size( ); i++ ) ns.push_back( at + (int)i );
		select( ns );
		InvalidateRect( list, 0, TRUE );
	}
	void beginEdit( int row )
	{
		endEdit( );
		if ( row < 0 || row >= (int)masters.size( ) ) return;
		editing = row; editingWas = masters[row];
		SendMessageW( list, LB_SETSEL, FALSE, -1 ); SendMessageW( list, LB_SETSEL, TRUE, row ); SendMessageW( list, LB_SETCARETINDEX, row, FALSE );
		RECT rc; SendMessageW( list, LB_GETITEMRECT, row, (LPARAM)&rc );
		MapWindowPoints( list, hwnd, (POINT *)&rc, 2 );
		int x = rc.left + S( 6 + 92 + 8 ), w = rc.right - x - S( 12 + 128 + 14 + 8 );
		edit_ = CreateWindowExW( 0, L"EDIT", W( masters[row] ).c_str( ), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, x, rc.top + S( 6 ), w, rc.bottom - rc.top - S( 12 ), hwnd, (HMENU)(INT_PTR)ID_EDIT, GetModuleHandleW( 0 ), 0 );
		SendMessageW( edit_, WM_SETFONT, (WPARAM)F( 13, 500 ), TRUE );
		SendMessageW( edit_, EM_SETCUEBANNER, TRUE, (LPARAM)L"host or host:port" );
		SetFocus( edit_ ); SendMessageW( edit_, EM_SETSEL, 0, -1 );
		InvalidateRect( list, &rc, TRUE );
	}
	void endEdit( )
	{
		if ( !edit_ ) return;
		HWND e = edit_; edit_ = 0;
		str text = getText( e );
		DestroyWindow( e );
		char clean[65]; lc_clean_master( text.c_str( ), clean );
		int row = -1; for ( size_t i = 0; i < masters.size( ); i++ ) if ( masters[i] == editingWas ) { row = (int)i; break; }
		editing = -1;
		if ( row < 0 ) return;
		str m = clean;
		if ( m.empty( ) ) masters.erase( masters.begin( ) + row );
		else {
			bool dup = false; for ( size_t i = 0; i < masters.size( ); i++ ) if ( (int)i != row && masters[i] == m ) dup = true;
			if ( dup ) { MessageBeep( MB_OK ); if ( editingWas.empty( ) ) masters.erase( masters.begin( ) + row ); }
			else masters[row] = m;
		}
		refill( );
		if ( !m.empty( ) && std::find( masters.begin( ), masters.end( ), m ) != masters.end( ) && !answers.count( m ) ) check( m );
	}
	bool preTranslate( MSG *m ) override
	{
		if ( edit_ && m->hwnd == edit_ && m->message == WM_KEYDOWN ) {
			if ( m->wParam == VK_RETURN || m->wParam == VK_TAB ) { endEdit( ); return true; }
			if ( m->wParam == VK_ESCAPE ) { HWND e = edit_; edit_ = 0; DestroyWindow( e ); editing = -1; refill( ); return true; }
		}
		if ( m->hwnd == list && m->message == WM_KEYDOWN && ( m->wParam == VK_DELETE || m->wParam == VK_BACK ) ) { removeSel( ); return true; }
		return false;
	}
	void removeSel( )
	{
		endEdit( );
		auto s = sel( ); if ( s.empty( ) ) return;
		for ( int i = (int)s.size( ) - 1; i >= 0; i-- ) masters.erase( masters.begin( ) + s[i] );
		refill( );
		if ( !masters.empty( ) ) select( { max( 0, min( s.front( ), (int)masters.size( ) - 1 ) ) } );
	}
	void setList( const char *const *defs, int n )
	{
		endEdit( );
		masters.clear( );
		for ( int i = 0; i < n; i++ ) masters.push_back( defs[i] );
		refill( ); checkAll( true );
	}
	void onCommand( int id, int code, HWND ) override
	{
		switch ( id ) {
		case ID_ADD: if ( masters.size( ) < LC_MAX_MASTERS ) { endEdit( ); masters.push_back( "" ); refill( ); beginEdit( (int)masters.size( ) - 1 ); } break;
		case ID_REMOVE: removeSel( ); break;
		case ID_UP: { endEdit( ); auto s = sel( ); if ( !s.empty( ) ) moveRows( s, s.front( ) - 1 ); break; }
		case ID_DOWN: { endEdit( ); auto s = sel( ); if ( !s.empty( ) ) moveRows( s, s.back( ) + 2 ); break; }
		case ID_CHECK: endEdit( ); checkAll( false ); break;
		case ID_RECOMMENDED: setList( lc_default_masters, 5 ); break;
		case ID_ORIGINAL: setList( lc_original_masters, 2 ); break;
		case ID_LIST:
			if ( code == LBN_SELCHANGE ) { if ( edit_ ) endEdit( ); buttons( ); }
			else if ( code == LBN_DBLCLK ) { POINT p; GetCursorPos( &p ); ScreenToClient( list, &p ); BOOL out; int r = (int)SendMessageW( list, LB_ITEMFROMPOINT, 0, MAKELPARAM( p.x, p.y ) ); out = HIWORD( r ); if ( !out ) beginEdit( LOWORD( r ) ); }
			break;
		case ID_EDIT: if ( code == EN_KILLFOCUS ) endEdit( ); break;
		case IDOK: {
			endEdit( );
			char out[LC_MAX_MASTERS][65]; const char *in[LC_MAX_MASTERS]; int n = 0;
			for ( auto &m : masters ) if ( n < LC_MAX_MASTERS ) in[n++] = m.c_str( );
			int cnt = lc_clean_masters( in, n, out );
			lc_masters_save( lc_store_shared( ), out, cnt ); lc_store_save( lc_store_shared( ) );
			lc_log( "Master servers: %s", [&]( ) { static str j; j.clear( ); for ( int i = 0; i < cnt; i++ ) { if ( i ) j += ", "; j += out[i]; } return j.c_str( ); }( ) );
			end( IDOK ); break; }
		case IDCANCEL: end( IDCANCEL ); break;
		}
	}
	LRESULT onOther( UINT m, WPARAM w, LPARAM l, bool *handled ) override
	{
		*handled = true;
		if ( m == WM_MEASUREITEM ) { MEASUREITEMSTRUCT *mi = (MEASUREITEMSTRUCT *)l; mi->itemHeight = S( 30 ); return TRUE; }
		if ( m == WM_DRAWITEM ) { drawRow( (DRAWITEMSTRUCT *)l ); return TRUE; }
		*handled = false; return 0;
	}
	void drawRow( DRAWITEMSTRUCT *d )
	{
		if ( d->itemID == (UINT)-1 ) return;
		HDC dc = d->hDC; RECT r = d->rcItem; int row = (int)d->itemID;
		bool sel = ( d->itemState & ODS_SELECTED ) != 0;
		HBRUSH b = CreateSolidBrush( sel ? blend( T.field, T.accent, 22 ) : T.field ); FillRect( dc, &r, b ); DeleteObject( b );
		SetBkMode( dc, TRANSPARENT );
		str m = masters[row];
		SelectObject( dc, F( 12 ) ); SetTextColor( dc, T.text2 );
		RECT sl = { r.left + S( 6 ), r.top, r.left + S( 6 + 92 ), r.bottom };
		DrawTextW( dc, row < 5 ? W( fmt( "Internet %d", row + 1 ) ).c_str( ) : L"Announce only", -1, &sl, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX );
		if ( edit_ && row == editing ) return;
		SelectObject( dc, F( 13, 500 ) ); SetTextColor( dc, T.text );
		RECT ar = { r.left + S( 6 + 92 + 8 ), r.top, r.right - S( 12 + 128 + 14 + 8 ), r.bottom };
		DrawTextW( dc, W( m ).c_str( ), -1, &ar, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX );
		/* status */
		int glyph = -1, role = R_TEXT2; str txt;
		if ( checking.count( m ) ) { glyph = G_REFRESH; txt = "Checking\xE2\x80\xA6"; }
		else if ( answers.count( m ) ) {
			int n = answers[m];
			if ( n < 0 ) { glyph = G_X_CIRCLE; role = R_ORANGE; txt = "No answer"; }
			else { glyph = G_CHECK_CIRCLE; txt = n == 1 ? "Answers \xC2\xB7 1 game" : fmt( "Answers \xC2\xB7 %d games", n ); }
		}
		if ( glyph >= 0 ) {
			RECT tr = { r.right - S( 12 + 128 ), r.top, r.right - S( 12 ), r.bottom };
			SelectObject( dc, F( 11 ) ); SetTextColor( dc, roleColor( role ) );
			DrawTextW( dc, W( txt ).c_str( ), -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX );
			drawGlyph( dc, glyph, r.right - S( 12 + 128 + 16 ), ( r.top + r.bottom - S( 12 ) ) / 2, S( 12 ), glyph == G_CHECK_CIRCLE ? T.green : glyph == G_X_CIRCLE ? T.orange : T.text2 );
		}
	}
};

static LRESULT CALLBACK listDrag( HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR ref )
{
	MastersDlg *d = (MastersDlg *)ref;
	switch ( m ) {
	case WM_LBUTTONDOWN: {
		int r = (int)SendMessageW( h, LB_ITEMFROMPOINT, 0, l );
		if ( !HIWORD( r ) && !( w & ( MK_CONTROL | MK_SHIFT ) ) && LOWORD( r ) < d->masters.size( ) ) { d->dragRow = LOWORD( r ); d->dragStart = { (short)LOWORD( l ), (short)HIWORD( l ) }; d->dragging = false; }
		else d->dragRow = -1;
		break; }
	case WM_MOUSEMOVE:
		if ( d->dragRow >= 0 && ( w & MK_LBUTTON ) ) {
			int dy = (short)HIWORD( l ) - d->dragStart.y;
			if ( !d->dragging && abs( dy ) >= S( 3 ) ) { d->dragging = true; SetCapture( h ); d->endEdit( ); }
			if ( d->dragging ) {
				int r = (int)SendMessageW( h, LB_ITEMFROMPOINT, 0, MAKELPARAM( 5, max( 0, (int)(short)HIWORD( l ) ) ) );
				int to = HIWORD( r ) ? (int)d->masters.size( ) - 1 : (int)LOWORD( r );
				if ( to != d->dragRow && to >= 0 ) { d->moveRows( { d->dragRow }, to > d->dragRow ? to + 1 : to ); d->dragRow = to; }
				return 0;
			}
		}
		break;
	case WM_LBUTTONUP: if ( d->dragging ) { ReleaseCapture( ); d->dragging = false; d->dragRow = -1; return 0; } d->dragRow = -1; break;
	case WM_NCDESTROY: RemoveWindowSubclass( h, listDrag, 1 ); break;
	}
	return DefSubclassProc( h, m, w, l );
}

void showMasters( HWND owner )
{
	MastersDlg d;
	char list[LC_MAX_MASTERS][65]; int n = lc_masters_load( lc_store_shared( ), list );
	for ( int i = 0; i < n; i++ ) d.masters.push_back( list[i] );
	const int W0 = 660;
	d.create( L"Master Servers", W0, 478, WS_POPUP | WS_CAPTION | WS_SYSMENU, 0, owner );
	int y = 18;
	d.caption = d.label( 20, y, W0 - 40, 16, "", 11, 600, R_TEXT2, DT_SINGLELINE, MastersDlg::ID_CAPTION ); y += 16 + 8;
	str hint = "Master servers keep the list of Holomatch games on the internet. The first five are the Join menu's Internet 1 to 5 sources, in this order. When you host an internet server, it announces itself to every one, so add new masters here as the community starts them.";
	int hh = textHeight( W( hint ), 610, F( 11 ) ) * 96 / g_dpi;
	d.label( 20, y, 610, hh + 2, hint.c_str( ), 11, 400, R_TEXT2 ); y += hh + 10;
	d.list = d.ctl_( L"LISTBOX", L"", LBS_OWNERDRAWFIXED | LBS_EXTENDEDSEL | LBS_NOTIFY | LBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, 20, y, W0 - 40, 280, MastersDlg::ID_LIST );
	SetWindowSubclass( d.list, listDrag, 1, (DWORD_PTR)&d );
	SendMessageW( d.list, LB_SETITEMHEIGHT, 0, S( 30 ) );
	y += 280 + 8;
	mkIconButton( d.hwnd, 20, y, 30, 24, MastersDlg::ID_ADD, G_PLUS, "Add a master server" ); d.ids[MastersDlg::ID_ADD] = GetDlgItem( d.hwnd, MastersDlg::ID_ADD );
	mkIconButton( d.hwnd, 54, y, 30, 24, MastersDlg::ID_REMOVE, G_MINUS, "Remove" ); d.ids[MastersDlg::ID_REMOVE] = GetDlgItem( d.hwnd, MastersDlg::ID_REMOVE );
	mkIconButton( d.hwnd, 88, y, 30, 24, MastersDlg::ID_UP, G_UP, "Move up" ); d.ids[MastersDlg::ID_UP] = GetDlgItem( d.hwnd, MastersDlg::ID_UP );
	mkIconButton( d.hwnd, 122, y, 30, 24, MastersDlg::ID_DOWN, G_DOWN, "Move down" ); d.ids[MastersDlg::ID_DOWN] = GetDlgItem( d.hwnd, MastersDlg::ID_DOWN );
	d.label( 164, y + 4, 330, 16, "Drag to reorder, double-click to edit", 11, 400, R_TEXT3, DT_SINGLELINE );
	d.button( W0 - 20 - 90, y, 90, 24, "Check Now", MastersDlg::ID_CHECK, false, 12 );
	setTooltip( d.hwnd, d.ctl( MastersDlg::ID_CHECK ), "Ask each master server for its list of games" );
	y += 24 + 18;
	d.button( 20, y, 190, 28, "Restore Recommended Defaults", MastersDlg::ID_RECOMMENDED, false, 12 );
	str rt = "The live community masters first: "; for ( int i = 0; i < 5; i++ ) { if ( i ) rt += ", "; rt += lc_default_masters[i]; }
	setTooltip( d.hwnd, d.ctl( MastersDlg::ID_RECOMMENDED ), rt.c_str( ) );
	d.button( 216, y, 170, 28, "Restore Original Defaults", MastersDlg::ID_ORIGINAL, false, 12 );
	setTooltip( d.hwnd, d.ctl( MastersDlg::ID_ORIGINAL ), "As the game came: Internet 1 Raven's master (no longer running), Internet 2 efmaster.tjps.eu, 3 to 5 blank" );
	d.button( W0 - 20 - 84 - 8 - 84, y, 84, 28, "Cancel", IDCANCEL );
	d.button( W0 - 20 - 84, y, 84, 28, "Done", IDOK, true );
	d.refill( );
	d.checkAll( false );
	if ( !g_snapPath.empty( ) ) g_snapDelayMs = 4500;
	d.runModal( );
}

}	// namespace wl
