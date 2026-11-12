/*
===========================================================================
Windows launcher: the Holomatch Servers browser sheet ("Browse...",
servers.md section 8). The model (sources, merge, filters, sort) is in the
core (lc_net.h); this is the window. Map Info and the 3D preview open the
efservers.com pages in the default browser instead of an embedded web view.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#include "wl_app.h"
#include <commctrl.h>
#include <time.h>

namespace wl {

#define KEY_FAVS "browserfavorites"
#define KEY_ONLYFAV "browseronlyfavorites"
#define KEY_ONLYPEOPLE "browseronlypeople"

struct BrowserDlg : Wnd {
	lc_browser *b = 0;
	const lc_map_catalog *cat = 0;
	char masters[LC_MAX_MASTERS][65]; int nMasters = 0;
	int source = 0, nSources = 0;
	lc_sort_key keys[4]; int nKeys = 0;
	std::vector<lc_browser_row> rows;
	std::vector<str> favs;
	bool onlyFav = false, onlyPeople = false;
	str selAddr, wantAddr;
	HWND seg = 0, table = 0, players = 0, status = 0, spin = 0, ptitle = 0, pnote = 0;
	str joinAddress, joinPassword; bool joined = false;
	std::map<str, str> view3D;				/* map -> url or "none" */
	str checking3D, asking;
	bool reloadPending = false, linkRow = false;
	std::vector<lc_player> shownPlayers; bool tagBots = false;
	int lastSource = -1;
	str snapOut;
	std::set<str> fail3D;
	bool inShow = false;
	enum { ID_SEG = 10, ID_GLOBE, ID_REFRESH, ID_STATUS, ID_SPIN, ID_ONLYFAV, ID_ONLYPEOPLE, ID_TABLE, ID_PTITLE, ID_PNOTE, ID_PREFRESH,
		ID_MAPINFO, ID_MAP3D, ID_PLAYERS, ID_LINKTXT, ID_LINKBTN, ID_ADDR, ID_CONNECT, ID_JOIN = IDOK };
	static const int WM_RELOAD = WM_APP + 30, WM_STATUS = WM_APP + 31, WM_3D = WM_APP + 32;

	/* ---------------- favourites ---------------- */
	void loadFavs( )
	{
		char buf[512][32]; favs.clear( );
		int n = lc_store_get_list( lc_store_shared( ), KEY_FAVS, &buf[0][0], 32, 512 );
		for ( int i = 0; i < n; i++ ) favs.push_back( buf[i] );
		onlyFav = lc_store_get_bool( lc_store_shared( ), KEY_ONLYFAV, 0 ) != 0;
		onlyPeople = lc_store_get_bool( lc_store_shared( ), KEY_ONLYPEOPLE, 0 ) != 0;
	}
	void saveFavs( )
	{
		std::sort( favs.begin( ), favs.end( ) );
		static char buf[512][32]; int n = 0;
		for ( auto &f : favs ) if ( n < 512 ) lc_strlcpy( buf[n++], f.c_str( ), 32 );
		lc_store_set_list( lc_store_shared( ), KEY_FAVS, &buf[0][0], 32, n );
		lc_store_save( lc_store_shared( ) );
	}
	bool isFav( const str &a ) { return std::find( favs.begin( ), favs.end( ), a ) != favs.end( ); }

	/* ---------------- fetching ---------------- */
	static void changedCb( int, void *u ) { BrowserDlg *d = (BrowserDlg *)u; if ( d->hwnd ) PostMessageW( d->hwnd, WM_RELOAD, 0, 0 ); }
	void fetch( int s )
	{
		lc_browser_fetch( b, s, 0, changedCb, this );
		show( );
	}
	void selectSource( int s, bool keepSel = false )
	{
		source = s; (void)keepSel;
		SendMessageW( seg, SEG_SETSEL, s, 0 );
		if ( !lc_browser_fetched( b, s ) ) fetch( s ); else show( );
	}

	/* ---------------- showing ---------------- */
	void show( )
	{
		std::vector<const char *> fp; for ( auto &f : favs ) fp.push_back( f.c_str( ) );
		const lc_browser_row *out = 0;
		int n = lc_browser_rows( b, source, onlyFav, onlyPeople, fp.data( ), (int)fp.size( ), keys, nKeys, &out );
		rows.assign( out, out + n );
		inShow = true;
		ListView_SetItemCountEx( table, n, LVSICF_NOINVALIDATEALL );
		InvalidateRect( table, 0, TRUE );
		int sel = -1;
		for ( int i = 0; i < n; i++ ) if ( rows[i].info.address == selAddr ) sel = i;
		ListView_SetItemState( table, -1, 0, LVIS_SELECTED | LVIS_FOCUSED );
		if ( sel >= 0 ) { ListView_SetItemState( table, sel, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED ); ListView_EnsureVisible( table, sel, FALSE ); }
		else selAddr.clear( );
		inShow = false;
		setText( status, lc_browser_status_text( b, source ) );
		spinnerRun( spin, lc_browser_is_busy( b, source ) != 0 );
		EnableWindow( ctl( ID_JOIN ), !selAddr.empty( ) );
		showPlayers( );
	}

	const lc_browser_row *selectedRow( )
	{
		for ( auto &r : rows ) if ( selAddr == r.info.address ) return &r;
		return 0;
	}

	/* ---------------- players panel (8.6) ---------------- */
	struct StRes { BrowserDlg *d; str addr; int ok; lc_server_status st; };
	static void statusWorker( void *u )
	{
		StRes *r = (StRes *)u;
		r->ok = lc_query_status( r->addr.c_str( ), &r->st );
		if ( r->d->hwnd ) PostMessageW( r->d->hwnd, WM_STATUS, 0, (LPARAM)r );
	}
	void showPlayers( )
	{
		const lc_browser_row *r = selectedRow( );
		/* Map Info / 3D */
		str map = r ? r->info.map : "";
		bool info = r && !map.empty( ) && !lc_map_is_builtin( cat, map.c_str( ) );
		showCtl( ID_MAPINFO, info );
		if ( info ) setTooltip( hwnd, ctl( ID_MAPINFO ), fmt( "See %s on efservers.com: screenshots and its files", map.c_str( ) ).c_str( ) );
		show3D( info ? map : str( ) );
		shownPlayers.clear( );
		auto setPlayers = [&]( ) { ListView_SetItemCountEx( players, (int)shownPlayers.size( ), LVSICF_NOINVALIDATEALL ); InvalidateRect( players, 0, TRUE ); };
		if ( !r ) {
			setText( ptitle, "Players" ); placeNote( ); setText( pnote, "Select a server to see who's playing." ); showCtl( ID_PREFRESH, false ); setPlayers( ); return;
		}
		char nm[128]; lc_strip_colors( r->info.hostname, nm, sizeof nm ); str name = nm; lc_trim( &name[0] ); name = name.c_str( );
		if ( name.empty( ) ) name = r->info.address;
		setText( ptitle, "Players on " + name ); placeNote( );
		lc_server_status st;
		if ( lc_browser_get_status( b, r->info.address, &st ) ) {
			shownPlayers.assign( st.players, st.players + st.playerCount );
			lc_sort_players( shownPlayers.data( ), (int)shownPlayers.size( ) );
			int pinged = 0; for ( auto &p : shownPlayers ) if ( p.ping > 0 ) pinged++;
			int humans = st.humans >= 0 ? st.humans : r->info.humans;
			tagBots = humans < 0 || pinged == humans;
			if ( shownPlayers.empty( ) ) setText( pnote, "No one is playing." );
			else { time_t t = (time_t)st.when; struct tm *tm = localtime( &t ); char tb[64] = ""; if ( tm ) strftime( tb, sizeof tb, "%H:%M", tm ); setText( pnote, fmt( "as of %s", tb ) ); }
			showCtl( ID_PREFRESH, true ); setPlayers( );
			return;
		}
		setText( pnote, "Asking the server\xE2\x80\xA6" ); showCtl( ID_PREFRESH, false ); setPlayers( );
		if ( asking != r->info.address ) {
			asking = r->info.address;
			StRes *res = new StRes{ this, asking, 0, {} };
			lc_thread *t = lc_thread_start( statusWorker, res );
			if ( t ) lc_thread_detach( t ); else { delete res; asking.clear( ); }
		}
	}
	void onStatus( StRes *r )
	{
		if ( asking == r->addr ) asking.clear( );
		if ( r->ok ) { lc_browser_store_status( b, r->addr.c_str( ), &r->st ); show( ); }
		else if ( selAddr == r->addr ) { setText( pnote, "The server didn't answer." ); showCtl( ID_PREFRESH, true ); }
		delete r;
	}

	void placeNote( )
	{
		int tw = min( 300, textWidth( W( getText( ptitle ) ), F( 12, 600 ) ) * 96 / g_dpi + 4 );
		moveCtl( ptitle, 20, ctlY, tw, 16 ); moveCtl( pnote, 20 + tw + 8, ctlY + 1, 330 - tw - 12 + 150, 14 );
	}
	int ctlY = 0;
	/* ---------------- 3D lookup ---------------- */
	struct DRes { BrowserDlg *d; str map, url; int state; };	/* state: 1 found, 0 none, -1 error */
	static void worker3D( void *u )
	{
		DRes *r = (DRes *)u;
		char url[512]; lc_efservers_3d_lookup_url( r->map.c_str( ), url, sizeof url );
		char *body = 0; size_t len = 0; long status = 0; char err[128];
		int rc = lc_http_get( url, 0, 15, &body, &len, &status, err, sizeof err );
		if ( rc == LC_HTTP_OK && body ) { char out[512]; r->state = lc_efservers_parse_3d( body, out, sizeof out ) ? 1 : 0; if ( r->state ) r->url = out; }
		else r->state = -1;
		free( body );
		if ( r->d->hwnd ) PostMessageW( r->d->hwnd, WM_3D, 0, (LPARAM)r );
	}
	void show3D( const str &map )
	{
		showCtl( ID_MAP3D, false );
		if ( map.empty( ) ) return;
		auto i = view3D.find( map );
		if ( i != view3D.end( ) ) {
			if ( i->second != "none" ) { showCtl( ID_MAP3D, true ); setTooltip( hwnd, ctl( ID_MAP3D ), fmt( "Look around %s in 3D, on efservers.com", map.c_str( ) ).c_str( ) ); }
			return;
		}
		if ( checking3D == map || fail3D.count( map ) ) return;
		checking3D = map;
		DRes *r = new DRes{ this, map, "", 0 };
		lc_thread *t = lc_thread_start( worker3D, r );
		if ( t ) lc_thread_detach( t ); else { delete r; checking3D.clear( ); }
	}
	void on3D( DRes *r )
	{
		if ( checking3D == r->map ) checking3D.clear( );
		if ( r->state >= 0 ) view3D[r->map] = r->state == 1 ? r->url : "none"; else fail3D.insert( r->map );
		lc_log( "Browser: 3D view of %s: %s", r->map.c_str( ), r->state > 0 ? r->url.c_str( ) : r->state == 0 ? "none" : "error" );
		const lc_browser_row *sel = selectedRow( );
		if ( sel && r->map == sel->info.map ) showPlayers( );
		delete r;
	}

	/* ---------------- joining (8.8) ---------------- */
	void join( )
	{
		const lc_browser_row *r = selectedRow( ); if ( !r ) return;
		lc_browser_row row = *r;
		char nm[128]; lc_strip_colors( row.info.hostname, nm, sizeof nm ); str name = nm; if ( name.empty( ) ) name = row.info.address;
		if ( lc_map_state( cat, row.info.map ) == LC_MAP_EXPANSION ) {
			str info = fmt( "\xE2\x80\x9C%s\xE2\x80\x9D is running %s, which isn't in your game files. Servers can't send the Expansion Pack's maps, so joining will fail. Add the Expansion Pack's files with Add Game Files\xE2\x80\xA6 in the launcher, or pick another server.", name.c_str( ), row.info.map );
			int a = 0;
			if ( const char *e = testEnv( "VOYAGERSP_TEST_BROWSER_EXPANSION" ) ) { a = !lc_stricmp( e, "anyway" ) ? 1 : 0; lc_log( "[test] expansion warning answer: %s", e ); }
			else a = Alert( hwnd, "This server's map is from the Expansion Pack.", info, { "Cancel", "Join Anyway" }, 1, 0 );
			if ( a != 1 ) return;
		}
		str pw;
		if ( row.info.needPass ) {
			str text;
			int a = PromptText( hwnd, fmt( "\xE2\x80\x9C%s\xE2\x80\x9D needs a password.", name.c_str( ) ), "Ask the person running the server for it.", "", true, { "Join", "Cancel" }, &text );
			if ( a != 0 ) return;
			for ( char c : text ) if ( c != '"' ) pw += c;
		}
		finish( row.info.address, pw );
	}
	void finish( const str &addr, const str &pw )
	{
		lc_browser_cancel_all( b );
		lc_log( "Browser: join %s", addr.c_str( ) );
		joinAddress = addr; joinPassword = pw; joined = true;
		end( IDOK );
	}
	void connectTyped( )
	{
		str a = getText( ctl( ID_ADDR ) ); lc_trim( &a[0] ); a = a.c_str( );
		bool ok = !a.empty( );
		for ( char c : a ) if ( !( isalnum( (unsigned char)c ) || strchr( ".-:[]_", c ) ) ) ok = false;
		if ( !ok ) { MessageBeep( MB_OK ); return; }
		finish( a, "" );
	}

	/* ---------------- table drawing ---------------- */
	static const char *colTitle( int c )
	{ static const char *t[9] = { "", "Server", "", "Map", "Players", "Humans", "Game", "cMod", "Ping" }; return t[c]; }
	void sortClick( int col )
	{
		nKeys = lc_sort_click( keys, nKeys, 4, (lc_column)col );
		show( );
	}

	void drawServerCell( NMLVCUSTOMDRAW *cd )
	{
		HDC dc = cd->nmcd.hdc; RECT rc = cd->nmcd.rc; int row = (int)cd->nmcd.dwItemSpec, col = cd->iSubItem;
		if ( row < 0 || row >= (int)rows.size( ) ) return;
		const lc_browser_row &r = rows[row];
		bool sel = ListView_GetItemState( table, row, LVIS_SELECTED ) != 0;
		HBRUSH bb = CreateSolidBrush( sel ? blend( T.field, T.accent, 24 ) : T.field ); FillRect( dc, &rc, bb ); DeleteObject( bb );
		SetBkMode( dc, TRANSPARENT );
		RECT tr = rc; tr.left += S( 4 ); tr.right -= S( 4 );
		auto text = [&]( const str &t, COLORREF c, UINT al = DT_LEFT, int px = 13 ) {
			SelectObject( dc, F( px ) ); SetTextColor( dc, c );
			DrawTextW( dc, W( t ).c_str( ), -1, &tr, al | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX );
		};
		switch ( col ) {
		case LC_COL_FAV: { bool f = r.favorite != 0; drawGlyph( dc, f ? G_STAR_FILL : G_STAR, ( rc.left + rc.right - S( 12 ) ) / 2, ( rc.top + rc.bottom - S( 12 ) ) / 2, S( 12 ), f ? T.yellow : T.text3 ); break; }
		case LC_COL_NAME: {
			char plain[160]; unsigned char ci[160]; lc_decode_colors( r.info.hostname, plain, sizeof plain, ci );
			size_t n = strlen( plain ); int x = tr.left; SelectObject( dc, F( 13 ) );
			if ( n == 0 ) { str a = r.info.name[0] ? r.info.name : r.info.address; text( a, T.text ); break; }
			size_t i = 0; while ( i < n && isspace( (unsigned char)plain[i] ) ) i++;
			size_t e = n; while ( e > i && isspace( (unsigned char)plain[e - 1] ) ) e--;
			while ( i < e ) {
				size_t j = i; while ( j < e && ci[j] == ci[i] ) j++;
				ws w = W( str( plain + i, j - i ) ); SIZE sz; GetTextExtentPoint32W( dc, w.c_str( ), (int)w.size( ), &sz );
				static const int map[8] = { R_TEXT2, R_RED, R_GREEN, R_YELLOW, R_BLUE, R_TEAL, R_PINK, R_TEXT };
				SetTextColor( dc, roleColor( map[ci[i] & 7] ) );
				RECT sr = { x, tr.top, tr.right, tr.bottom }; DrawTextW( dc, w.c_str( ), -1, &sr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX );
				x += sz.cx; i = j; if ( x > tr.right ) break;
			}
			if ( r.thisComputer ) { SetTextColor( dc, T.text ); ws w = W( " (this " PLATFORM_NOUN ")" ); RECT sr = { x, tr.top, tr.right, tr.bottom }; DrawTextW( dc, w.c_str( ), -1, &sr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX ); }
			break; }
		case LC_COL_LOCK: if ( r.info.needPass ) drawGlyph( dc, G_LOCK, ( rc.left + rc.right - S( 11 ) ) / 2, ( rc.top + rc.bottom - S( 11 ) ) / 2, S( 11 ), T.text2 ); break;
		case LC_COL_MAP: {
			int st = lc_map_state( cat, r.info.map );
			text( r.info.map, st == LC_MAP_EXPANSION ? T.purple : st == LC_MAP_MISSING ? T.amber : T.text ); break; }
		case LC_COL_PLAYERS: text( r.info.maxPlayers > 0 ? fmt( "%d/%d", r.info.players, r.info.maxPlayers ) : fmt( "%d", r.info.players ), r.info.players == 0 ? T.text2 : T.text, DT_RIGHT ); break;
		case LC_COL_HUMANS: text( r.info.humans < 0 ? str( "?" ) : fmt( "%d", r.info.humans ), r.info.humans <= 0 ? T.text2 : T.text, DT_RIGHT ); break;
		case LC_COL_TYPE: { char buf[48]; text( lc_browser_gametype_name( r.info.gametype, buf, sizeof buf ), T.text ); break; }
		case LC_COL_CMOD: if ( r.isCmod ) { char num[32]; str t = lc_cmod_number( r.cmodVersion, num, sizeof num ) ? str( num ) : str( "\xE2\x9C\x93" ); text( t, T.text, DT_CENTER ); } break;
		case LC_COL_PING: text( fmt( "%d", r.info.ping ), r.info.ping < 80 ? T.green : r.info.ping < 160 ? T.text : T.orange, DT_RIGHT ); break;
		}
	}

	void drawPlayerCell( NMLVCUSTOMDRAW *cd )
	{
		HDC dc = cd->nmcd.hdc; RECT rc = cd->nmcd.rc; int row = (int)cd->nmcd.dwItemSpec, col = cd->iSubItem;
		if ( row < 0 || row >= (int)shownPlayers.size( ) ) return;
		const lc_player &p = shownPlayers[row];
		HBRUSH bb = CreateSolidBrush( T.field ); FillRect( dc, &rc, bb ); DeleteObject( bb );
		SetBkMode( dc, TRANSPARENT );
		RECT tr = rc; tr.left += S( 4 ); tr.right -= S( 4 );
		SelectObject( dc, F( 11 ) );
		if ( col == 0 ) {
			char plain[96]; unsigned char ci[96]; lc_decode_colors( p.name, plain, sizeof plain, ci );
			size_t n = strlen( plain ), i = 0, e = n; while ( i < n && isspace( (unsigned char)plain[i] ) ) i++; while ( e > i && isspace( (unsigned char)plain[e - 1] ) ) e--;
			int x = tr.left;
			while ( i < e ) {
				size_t j = i; while ( j < e && ci[j] == ci[i] ) j++;
				ws w = W( str( plain + i, j - i ) ); SIZE sz; GetTextExtentPoint32W( dc, w.c_str( ), (int)w.size( ), &sz );
				static const int map[8] = { R_TEXT2, R_RED, R_GREEN, R_YELLOW, R_BLUE, R_TEAL, R_PINK, R_TEXT };
				SetTextColor( dc, roleColor( map[ci[i] & 7] ) );
				RECT sr = { x, tr.top, tr.right, tr.bottom }; DrawTextW( dc, w.c_str( ), -1, &sr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX );
				x += sz.cx; i = j; if ( x > tr.right ) break;
			}
		} else if ( col == 1 ) { SetTextColor( dc, T.text ); DrawTextW( dc, W( fmt( "%d", p.score ) ).c_str( ), -1, &tr, DT_RIGHT | DT_SINGLELINE | DT_VCENTER ); }
		else if ( col == 2 ) { SetTextColor( dc, p.ping == 0 ? T.text3 : T.text ); DrawTextW( dc, W( fmt( "%d", p.ping ) ).c_str( ), -1, &tr, DT_RIGHT | DT_SINGLELINE | DT_VCENTER ); }
		else if ( col == 3 && tagBots && p.ping == 0 ) { SetTextColor( dc, T.text2 ); DrawTextW( dc, L"bot", -1, &tr, DT_RIGHT | DT_SINGLELINE | DT_VCENTER ); }
	}

	LRESULT onNotify( NMHDR *h ) override
	{
		if ( h->hwndFrom == table ) {
			switch ( h->code ) {
			case NM_CUSTOMDRAW: {
				NMLVCUSTOMDRAW *cd = (NMLVCUSTOMDRAW *)h;
				if ( cd->nmcd.dwDrawStage == CDDS_PREPAINT ) return CDRF_NOTIFYITEMDRAW;
				if ( cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT ) return CDRF_NOTIFYSUBITEMDRAW;
				if ( cd->nmcd.dwDrawStage == ( CDDS_ITEMPREPAINT | CDDS_SUBITEM ) ) { drawServerCell( cd ); return CDRF_SKIPDEFAULT; }
				return CDRF_DODEFAULT; }
			case LVN_COLUMNCLICK: { int c = ( (NMLISTVIEW *)h )->iSubItem; if ( c >= 0 && c <= LC_COL_PING ) sortClick( c ); return 0; }
			case LVN_ITEMCHANGED: {
				NMLISTVIEW *n = (NMLISTVIEW *)h; if ( inShow ) return 0;
				if ( ( n->uChanged & LVIF_STATE ) && ( n->uNewState & LVIS_SELECTED ) && !( n->uOldState & LVIS_SELECTED ) && n->iItem >= 0 && n->iItem < (int)rows.size( ) ) {
					if ( selAddr != rows[n->iItem].info.address ) { fail3D.clear( ); selAddr = rows[n->iItem].info.address; EnableWindow( ctl( ID_JOIN ), TRUE ); showPlayers( ); }
				}
				return 0; }
			case NM_CLICK: {
				NMITEMACTIVATE *ia = (NMITEMACTIVATE *)h;
				LVHITTESTINFO hi = {}; hi.pt = ia->ptAction; ListView_SubItemHitTest( table, &hi );
				if ( hi.iItem >= 0 && hi.iSubItem == LC_COL_FAV && hi.iItem < (int)rows.size( ) ) {
					str a = rows[hi.iItem].info.address;
					auto f = std::find( favs.begin( ), favs.end( ), a );
					if ( f == favs.end( ) ) favs.push_back( a ); else favs.erase( f );
					saveFavs( ); show( );
				}
				return 0; }
			case NM_DBLCLK: { NMITEMACTIVATE *ia = (NMITEMACTIVATE *)h; if ( ia->iItem >= 0 ) join( ); return 0; }
			}
		} else if ( h->hwndFrom == players ) {
			if ( h->code == NM_CUSTOMDRAW ) {
				NMLVCUSTOMDRAW *cd = (NMLVCUSTOMDRAW *)h;
				if ( cd->nmcd.dwDrawStage == CDDS_PREPAINT ) return CDRF_NOTIFYITEMDRAW;
				if ( cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT ) return CDRF_NOTIFYSUBITEMDRAW;
				if ( cd->nmcd.dwDrawStage == ( CDDS_ITEMPREPAINT | CDDS_SUBITEM ) ) { drawPlayerCell( cd ); return CDRF_SKIPDEFAULT; }
			}
		}
		return 0;
	}

	void onUser( UINT m, WPARAM, LPARAM l ) override
	{
		if ( m == WM_RELOAD ) { if ( !reloadPending ) { reloadPending = true; SetTimer( hwnd, 1, 100, 0 ); } }
		else if ( m == WM_STATUS ) onStatus( (StRes *)l );
		else if ( m == WM_3D ) on3D( (DRes *)l );
	}
	void onTimer( int id ) override
	{
		if ( id == 1 ) { KillTimer( hwnd, 1 ); reloadPending = false; show( ); }
		else if ( id == 2 ) testStep( );
	}

	bool onClose( ) override { lc_browser_cancel_all( b ); lc_log( "Browser: cancelled" ); end( IDCANCEL ); return true; }

	void onCommand( int id, int code, HWND ) override
	{
		switch ( id ) {
		case ID_SEG: if ( code == 1 ) selectSource( (int)SendMessageW( seg, SEG_GETSEL, 0, 0 ) ); break;
		case ID_GLOBE: lc_open_url( "https://efservers.com/" ); break;
		case ID_REFRESH: fetch( source ); break;
		case ID_ONLYFAV: onlyFav = SendMessageW( ctl( id ), BM_GETCHECK, 0, 0 ) == BST_CHECKED; lc_store_set_int( lc_store_shared( ), KEY_ONLYFAV, onlyFav ); lc_store_save( lc_store_shared( ) ); show( ); break;
		case ID_ONLYPEOPLE: onlyPeople = SendMessageW( ctl( id ), BM_GETCHECK, 0, 0 ) == BST_CHECKED; lc_store_set_int( lc_store_shared( ), KEY_ONLYPEOPLE, onlyPeople ); lc_store_save( lc_store_shared( ) ); show( ); break;
		case ID_PREFRESH: { const lc_browser_row *r = selectedRow( ); if ( r ) { lc_browser_forget_status( b, r->info.address ); asking.clear( ); showPlayers( ); } break; }
		case ID_MAPINFO: { const lc_browser_row *r = selectedRow( ); if ( r ) { char u[512]; lc_efservers_info_url( r->info.map, u, sizeof u ); lc_log( "Browser: map preview %s", u ); lc_open_url( u ); } break; }
		case ID_MAP3D: { const lc_browser_row *r = selectedRow( ); if ( r ) { auto i = view3D.find( r->info.map ); if ( i != view3D.end( ) && i->second != "none" ) { lc_log( "Browser: map preview %s", i->second.c_str( ) ); lc_open_url( i->second.c_str( ) ); } } break; }
		case ID_LINKBTN: { char err[256]; int ok = lc_link_register( err, sizeof err ); lc_log( "Browser: open stvef:// links with this app: %s", ok ? "done" : err ); setLinkRow( ); break; }
		case ID_ADDR: if ( code == EN_CHANGE ) { str a = getText( ctl( ID_ADDR ) ); lc_trim( &a[0] ); EnableWindow( ctl( ID_CONNECT ), !str( a.c_str( ) ).empty( ) ); } break;
		case ID_CONNECT: connectTyped( ); break;
		case IDOK: if ( GetFocus( ) == ctl( ID_ADDR ) ) connectTyped( ); else join( ); break;
		case IDCANCEL: onClose( ); break;
		}
	}
	bool preTranslate( MSG *m ) override
	{
		if ( m->message == WM_KEYDOWN && ( m->wParam == VK_F5 || ( m->wParam == 'R' && ( GetKeyState( VK_CONTROL ) & 0x8000 ) ) ) ) { fetch( source ); return true; }
		return false;
	}

	void setLinkRow( )
	{
		char other[256] = ""; int st = lc_link_handler_state( other, sizeof other );
		bool show_ = st == 2;
		if ( ctl( ID_LINKTXT ) ) {
			showCtl( ID_LINKTXT, show_ ); showCtl( ID_LINKBTN, show_ );
			if ( show_ ) setText( ctl( ID_LINKTXT ), fmt( "Join links on websites (stvef://) open in %s.", other ) );
		}
		lc_log( "Browser: stvef:// links open in %s", st == 2 ? other : st == 1 ? "this app" : "no app" );
	}

	/* ---------------- test hooks (8.10) ---------------- */
	int testPhase = 0, testWaits = 0;
	void testStep( )
	{
		if ( lc_browser_is_busy( b, source ) || !checking3D.empty( ) || !asking.empty( ) ) { if ( ++testWaits < 160 ) return; }
		KillTimer( hwnd, 2 );
		for ( auto &r : rows ) {
			char nm[128], cm[48] = "-"; lc_strip_colors( r.info.hostname, nm, sizeof nm );
			if ( r.isCmod ) { char num[32]; lc_cmod_number( r.cmodVersion, num, sizeof num ); snprintf( cm, sizeof cm, "%s (%s)", num, r.cmodVersion ); }
			lc_log( "[test] row: %s | %s | %s | cMod %s", r.info.address, nm, r.info.map, cm );
		}
		if ( const char *s = testEnv( "VOYAGERSP_TEST_BROWSER_SELECT" ) ) {
			int i = atoi( s );
			if ( i >= 0 && i < (int)rows.size( ) ) { ListView_SetItemState( table, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED ); pumpFor( 400 ); for ( int k = 0; k < 40 && ( !asking.empty( ) || !checking3D.empty( ) ); k++ ) pumpFor( 100 ); }
		}
		if ( const char *j = testEnv( "VOYAGERSP_TEST_BROWSER_JOIN" ) ) {
			int i = atoi( j );
			if ( i >= 0 && i < (int)rows.size( ) ) { ListView_SetItemState( table, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED ); pumpFor( 100 ); join( ); return; }
		}
		if ( !snapOut.empty( ) ) {
			str p = snapOut; snapOut.clear( ); snapshotWindow( hwnd, p.c_str( ) );
			if ( const char *hs = testEnv( "VOYAGERSP_TEST_BROWSER_HOLD" ) ) pumpFor( (int)( atof( hs ) * 1000 ) );
			onClose( );
		}
	}
};

bool showBrowser( HWND owner, const str &dataFolder, str *address, str *password )
{
	BrowserDlg d;
	d.cat = lc_map_catalog_get( dataFolder.c_str( ), lc_homebase( ) );
	d.nMasters = lc_masters_load( lc_store_shared( ), d.masters );
	d.b = lc_browser_new( d.masters, d.nMasters, d.cat );
	lc_browser_set_loopback_fix( d.b, loopbackFix( ) );
	d.nSources = lc_browser_source_count( d.b );
	lc_browser_default_sort( d.keys ); d.nKeys = 4;
	d.loadFavs( );
	char other[256] = ""; d.linkRow = lc_link_handler_state( other, sizeof other ) == 2;
	const int W0 = 780, TH = 280, PH = 112;
	int H = 18 + 26 + 8 + 20 + 8 + TH + 12 + 20 + 4 + PH + 18 + ( d.linkRow ? 22 + 18 : 0 ) + 28 + 20;
	d.create( L"Holomatch Servers", W0, H, WS_POPUP | WS_CAPTION | WS_SYSMENU, 0, owner );
	int y = 18;
	/* source tabs */
	std::vector<str> labels;
	for ( int i = 0; i < d.nSources; i++ ) { char l[32]; lc_browser_source_label( d.b, i, l, sizeof l ); labels.push_back( l ); }
	int sw = min( d.nSources * 86, W0 - 40 - 80 );
	d.seg = mkSeg( d.hwnd, 20, y, sw, 26, BrowserDlg::ID_SEG ); d.ids[BrowserDlg::ID_SEG] = d.seg;
	segSetItems( d.seg, labels );
	setTooltip( d.hwnd, d.seg, "Local: this network. Combined: every master server's list. Internet 1, 2\xE2\x80\xA6: one master server each, in the order set in Servers\xE2\x80\xA6" );
	mkIconButton( d.hwnd, W0 - 20 - 62, y, 28, 26, BrowserDlg::ID_GLOBE, G_GLOBE, "Open efservers.com: who's playing where, server and map history" ); d.ids[BrowserDlg::ID_GLOBE] = GetDlgItem( d.hwnd, BrowserDlg::ID_GLOBE );
	mkIconButton( d.hwnd, W0 - 20 - 28, y, 28, 26, BrowserDlg::ID_REFRESH, G_REFRESH, "Ask again" ); d.ids[BrowserDlg::ID_REFRESH] = GetDlgItem( d.hwnd, BrowserDlg::ID_REFRESH );
	y += 26 + 8;
	d.spin = mkSpinner( d.hwnd, 22, y + 3, 14, BrowserDlg::ID_SPIN );
	d.status = d.label( 44, y + 2, 380, 16, "", 11, 400, R_TEXT2, DT_SINGLELINE | DT_END_ELLIPSIS, BrowserDlg::ID_STATUS );
	HWND c1 = d.check( W0 - 20 - 190, y, 190, 18, "Only servers with people", BrowserDlg::ID_ONLYPEOPLE, d.onlyPeople, 12 );
	setTooltip( d.hwnd, c1, "Hide servers with only bots, or no one. Servers that don't say (?) stay when they have players" );
	HWND c2 = d.check( W0 - 20 - 190 - 14 - 110, y, 110, 18, "Only favorites", BrowserDlg::ID_ONLYFAV, d.onlyFav, 12 );
	setTooltip( d.hwnd, c2, "Star servers in the list to make them favorites" );
	y += 20 + 8;
	/* server table */
	d.table = d.ctl_( WC_LISTVIEWW, L"", LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP | WS_BORDER, 0, 20, y, W0 - 40, TH, BrowserDlg::ID_TABLE );
	ListView_SetExtendedListViewStyle( d.table, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER );
	ListView_SetBkColor( d.table, T.field );
	static const int wdt[9] = { 22, 208, 22, 110, 74, 68, 120, 48, 48 };
	static const int fmtc[9] = { LVCFMT_CENTER, LVCFMT_LEFT, LVCFMT_CENTER, LVCFMT_LEFT, LVCFMT_RIGHT, LVCFMT_RIGHT, LVCFMT_LEFT, LVCFMT_CENTER, LVCFMT_RIGHT };
	for ( int i = 0; i < 9; i++ ) {
		ws t = W( BrowserDlg::colTitle( i ) );
		LVCOLUMNW c = {}; c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT; c.fmt = fmtc[i]; c.cx = S( wdt[i] ); c.pszText = (LPWSTR)t.c_str( );
		SendMessageW( d.table, LVM_INSERTCOLUMNW, i, (LPARAM)&c );
	}
	y += TH + 12;
	/* players header */
	d.ctlY = y + 2;
	d.ptitle = d.label( 20, y + 2, 300, 16, "Players", 12, 600, R_TEXT, DT_SINGLELINE | DT_END_ELLIPSIS, BrowserDlg::ID_PTITLE );
	d.pnote = d.label( 20, y + 3, 10, 14, "", 11, 400, R_TEXT2, DT_SINGLELINE | DT_END_ELLIPSIS, BrowserDlg::ID_PNOTE );
	mkIconButton( d.hwnd, 330, y, 22, 20, BrowserDlg::ID_PREFRESH, G_REFRESH, "Ask the server again", 10 ); d.ids[BrowserDlg::ID_PREFRESH] = GetDlgItem( d.hwnd, BrowserDlg::ID_PREFRESH );
	d.button( W0 - 20 - 130, y - 2, 130, 24, "3D Map Preview", BrowserDlg::ID_MAP3D, false, 12 );
	d.button( W0 - 20 - 130 - 8 - 90, y - 2, 90, 24, "Map Info", BrowserDlg::ID_MAPINFO, false, 12 );
	y += 20 + 4;
	d.players = d.ctl_( WC_LISTVIEWW, L"", LVS_REPORT | LVS_OWNERDATA | LVS_NOSORTHEADER | WS_BORDER, 0, 20, y, W0 - 40, PH, BrowserDlg::ID_PLAYERS );
	ListView_SetBkColor( d.players, T.field );
	static const char *pt[4] = { "Name", "Score", "Ping", "" }; static const int pw[4] = { 300, 50, 44, 34 };
	for ( int i = 0; i < 4; i++ ) {
		ws t = W( pt[i] ); LVCOLUMNW c = {}; c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT; c.fmt = i ? LVCFMT_RIGHT : LVCFMT_LEFT; c.cx = S( i == 0 ? W0 - 40 - 50 - 44 - 34 - 20 : pw[i] ); c.pszText = (LPWSTR)t.c_str( );
		SendMessageW( d.players, LVM_INSERTCOLUMNW, i, (LPARAM)&c );
	}
	y += PH + 18;
	if ( d.linkRow ) {
		d.label( 20, y + 3, 480, 16, fmt( "Join links on websites (stvef://) open in %s.", other ).c_str( ), 11, 400, R_TEXT2, DT_SINGLELINE, BrowserDlg::ID_LINKTXT );
		d.button( W0 - 20 - 230, y, 230, 22, "Open Join Links with Elite Force", BrowserDlg::ID_LINKBTN, false, 12 );
		y += 22 + 18;
	}
	HWND ae = d.edit( 20, y, 260, 24, BrowserDlg::ID_ADDR, "" );
	SendMessageW( ae, EM_SETCUEBANNER, TRUE, (LPARAM)L"Or an address, like 203.0.113.5:27960" );
	d.button( 288, y - 1, 90, 26, "Connect", BrowserDlg::ID_CONNECT );
	d.button( W0 - 20 - 84 - 8 - 84, y - 1, 84, 26, "Cancel", IDCANCEL );
	d.button( W0 - 20 - 84, y - 1, 84, 26, "Join", IDOK, true );
	EnableWindow( d.ctl( BrowserDlg::ID_CONNECT ), FALSE ); EnableWindow( d.ctl( BrowserDlg::ID_JOIN ), FALSE );
	d.showCtl( BrowserDlg::ID_MAPINFO, false ); d.showCtl( BrowserDlg::ID_MAP3D, false ); d.showCtl( BrowserDlg::ID_PREFRESH, false );
	/* the note sits right of the title: position it from the title's text width after it is known */
	applyTheme( d.hwnd );
	d.setLinkRow( );
	int initial = d.nSources > 1 ? 1 : 0;
	if ( const char *t = testEnv( "VOYAGERSP_TEST_BROWSER_TAB" ) ) {
		if ( !lc_stricmp( t, "local" ) ) initial = 0;
		else if ( !lc_stricmp( t, "combined" ) ) initial = d.nSources > 1 ? 1 : 0;
		else { int n = atoi( t ); initial = max( 0, min( d.nSources - 1, n + 1 ) ); }
	}
	d.selectSource( initial );
	d.snapOut = g_snapPath; g_snapPath.clear( );
	if ( !d.snapOut.empty( ) || testEnv( "VOYAGERSP_TEST_BROWSER_JOIN" ) ) SetTimer( d.hwnd, 2, 250, 0 );
	int r = d.runModal( );
	lc_browser_free( d.b );
	if ( r == IDOK && d.joined ) { *address = d.joinAddress; *password = d.joinPassword; return true; }
	return false;
}

}	// namespace wl
