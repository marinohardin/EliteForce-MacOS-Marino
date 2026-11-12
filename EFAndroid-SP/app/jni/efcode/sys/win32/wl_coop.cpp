/*
===========================================================================
Windows launcher: the Co-op sheet (launcher-data-coop.md 6.1). The Co-op...
button is hidden unless lc_coop_ui_enabled().

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#include "wl_app.h"

namespace wl {

static lc_nat *g_coopNat = 0;			/* process-wide: kept when Host is pressed (the game opens its socket on the same port) */
static void coopNatRelease( ) { if ( g_coopNat ) { lc_nat_release( g_coopNat ); g_coopNat = 0; } }

struct CoopDlg : Wnd {
	bool canPlay = false, playNow = false;
	enum { ID_NAME = 10, ID_HOST, ID_PW, ID_BG, ID_ROUTER, ID_INET, ID_INVITE, ID_ADDR, ID_CHECK, ID_JPW, ID_MUNRO, ID_RES_ICON, ID_RES_SPIN, ID_RES_TITLE, ID_RES_TEXT,
		ID_CONNECT, ID_CONNECT_TXT, ID_LAN, ID_NOTE };
	/* NAT state */
	int natState = 0;						/* 0 idle, 1 asking, 2 mapped, 3 failed */
	str natPublic, natWhy; str lanFirst;
	/* check state */
	bool checking = false, found = false, needPw = false; str foundAddr;
	int resGlyph = -1, resRole = R_TEXT;
	struct NatRes { CoopDlg *d; lc_nat_result r; char ip[64]; int ext, err; };
	struct QRes { CoopDlg *d; lc_coop_query q; };
	static const int WM_NAT = WM_APP + 40, WM_QUERY = WM_APP + 41;

	static void natCb( lc_nat_result r, const char *ip, int ext, int err, void *u )
	{
		CoopDlg *d = (CoopDlg *)u;
		NatRes *n = new NatRes{ d, r, "", ext, err };
		if ( ip ) lc_strlcpy( n->ip, ip, sizeof n->ip );
		if ( d->hwnd ) PostMessageW( d->hwnd, WM_NAT, 0, (LPARAM)n ); else delete n;
	}
	void natFail( const str &why )
	{
		KillTimer( hwnd, 1 );
		natState = 3; natWhy = why; natPublic.clear();
		showInternet( );
	}
	void onNat( NatRes *n )
	{
		if ( natState == 1 ) {
			switch ( n->r ) {
			case LC_NAT_OK: {
				unsigned a, b, c, d; unsigned long ip = 0; if ( sscanf( n->ip, "%u.%u.%u.%u", &a, &b, &c, &d ) == 4 ) ip = ( a << 24 ) | ( b << 16 ) | ( c << 8 ) | d;
				if ( ip == 0 ) break;			/* no answer yet: the timer gives up */
				if ( lc_ipv4_private( (unsigned)ip ) ) { natFail( fmt( "Your router opened the port, but its own internet address (%s) is private: your provider or another router puts it behind a second NAT, which nobody outside can get through.", n->ip ) ); break; }
				KillTimer( hwnd, 1 );
				natState = 2; natPublic = fmt( "%s:%d", n->ip, n->ext ? n->ext : LC_COOP_PORT );
				lc_log( "Co-op: the router forwards %d to this %s", LC_COOP_PORT, PLATFORM_NOUN );
				showInternet( ); break; }
			case LC_NAT_UNSUPPORTED: natFail( "Your router doesn\xE2\x80\x99t support automatic port opening (NAT-PMP / UPnP), or has it turned off." ); break;
			case LC_NAT_DISABLED: natFail( "Automatic port opening (NAT-PMP / UPnP) is turned off in your router\xE2\x80\x99s settings." ); break;
			case LC_NAT_DOUBLE: natFail( "Your network has two routers in a row (double NAT), so the outer one still blocks the port." ); break;
			case LC_NAT_PRIVATE_EXTERNAL: natFail( fmt( "Your router opened the port, but its own internet address (%s) is private: your provider or another router puts it behind a second NAT, which nobody outside can get through.", n->ip ) ); break;
			case LC_NAT_TIMEOUT: natFail( "Your router didn\xE2\x80\x99t answer the request to open the port: it may not support automatic port opening (NAT-PMP / UPnP), or has it turned off." ); break;
			default: natFail( fmt( "The router didn\xE2\x80\x99t open the port (error %d).", n->err ) );
			}
		}
		delete n;
	}
	void routerToggle( )
	{
		bool on = SendMessageW( ctl( ID_ROUTER ), BM_GETCHECK, 0, 0 ) == BST_CHECKED;
		if ( on ) {
			coopNatRelease( );
			natState = 1; natWhy.clear( ); natPublic.clear( );
			g_coopNat = lc_nat_map_udp( LC_COOP_PORT, natCb, this );
			SetTimer( hwnd, 1, 15000, 0 );
			if ( !g_coopNat ) natFail( "The router didn\xE2\x80\x99t open the port (error 0)." );
		} else {
			KillTimer( hwnd, 1 ); coopNatRelease( ); natState = 0; natPublic.clear( ); natWhy.clear( );
		}
		showInternet( );
	}
	void showInternet( )
	{
		str t; int role = R_TEXT2;
		str ip = lanFirst.empty( ) ? str( "this PC\xE2\x80\x99s address" ) : lanFirst;
		if ( natState == 2 ) { t = fmt( "Port open: a friend on the internet joins %s. Copy Invite and send it to them. The port stays open while Elite Force runs.", natPublic.c_str( ) ); role = R_GREEN; }
		else if ( natState == 1 ) t = "Asking your router to open the port\xE2\x80\xA6";
		else if ( natState == 3 ) { t = fmt( "Port not opened. %s Forward UDP port %d to this PC (%s) in your router\xE2\x80\x99s settings instead, or use Tailscale (see the README).", natWhy.c_str( ), LC_COOP_PORT, ip.c_str( ) ); role = R_ORANGE; }
		else t = "Not needed on the same network. Copy Invite gives your friend a link to this PC\xE2\x80\x99s address.";
		setText( ctl( ID_INET ), t ); setRole( ctl( ID_INET ), role );
		SendMessageW( ctl( ID_ROUTER ), BM_SETCHECK, natState == 1 || natState == 2 ? BST_CHECKED : BST_UNCHECKED, 0 );
		char link[256]; lc_coop_invite_link( natState == 2 ? natPublic.c_str( ) : "", link, sizeof link );
		EnableWindow( ctl( ID_INVITE ), link[0] && natState != 1 );
	}

	/* ---- check ---- */
	static void queryWorker( void *u )
	{
		QRes *r = (QRes *)u;
		lc_coop_query_run( r->q.address, 2.0, &r->q );
		if ( r->d->hwnd ) PostMessageW( r->d->hwnd, WM_QUERY, 0, (LPARAM)r ); else delete r;
	}
	void doCheck( )
	{
		if ( checking ) return;
		str a = getText( ctl( ID_ADDR ) ); lc_trim( &a[0] ); a = a.c_str( );
		checking = true; EnableWindow( ctl( ID_CHECK ), FALSE );
		found = false; EnableWindow( ctl( ID_CONNECT ), FALSE ); setTooltip( hwnd, ctl( ID_CONNECT ), "Check the address first" );
		showResult( -1, R_TEXT, fmt( "Checking %s\xE2\x80\xA6", a.empty( ) ? "the address" : a.c_str( ) ), "", true );
		QRes *r = new QRes; r->d = this; memset( &r->q, 0, sizeof r->q ); lc_strlcpy( r->q.address, a.c_str( ), sizeof r->q.address );
		lc_thread *t = lc_thread_start( queryWorker, r );
		if ( t ) lc_thread_detach( t ); else { delete r; checking = false; EnableWindow( ctl( ID_CHECK ), TRUE ); }
	}
	void showResult( int glyph, int role, const str &title, const str &text, bool spin = false )
	{
		resGlyph = glyph; resRole = role;
		showCtl( ID_RES_TITLE, true );
		setText( ctl( ID_RES_TITLE ), title ); setText( ctl( ID_RES_TEXT ), text ); showCtl( ID_RES_TEXT, !text.empty( ) );
		spinnerRun( ctl( ID_RES_SPIN ), spin );
		InvalidateRect( hwnd, 0, FALSE );
	}
	void onQuery( QRes *r )
	{
		checking = false; EnableWindow( ctl( ID_CHECK ), TRUE );
		lc_coop_query &q = r->q;
		char host[128]; lc_coop_info_value( q.info, "hostname", host, sizeof host );
		char desc[2048]; desc[0] = 0; if ( !strcmp( q.status, "ok" ) || !strcmp( q.status, "mismatch" ) ) lc_coop_describe( q.info, desc, sizeof desc );
		lc_log( "Co-op: check %s -> %s", q.address, q.status );
		found = false;
		if ( !strcmp( q.status, "ok" ) ) {
			found = true; foundAddr = q.address;
			char np[16]; lc_coop_info_value( q.info, "needPassword", np, sizeof np ); needPw = !strcmp( np, "1" );
			str title = fmt( "Co-op game found%s (%s)", host[0] ? fmt( " on \xE2\x80\x9C%s\xE2\x80\x9D", host ).c_str( ) : "", q.address );
			showResult( G_CHECK_CIRCLE, R_GREEN, title, desc );
		} else if ( !strcmp( q.status, "mismatch" ) ) {
			str title = fmt( "Version mismatch: the co-op game%s (%s) can\xE2\x80\x99t be joined from this %s", host[0] ? fmt( " on \xE2\x80\x9C%s\xE2\x80\x9D", host ).c_str( ) : "", q.address, PLATFORM_NOUN );
			showResult( G_TRIANGLE, R_ORANGE, title, str( q.error ) + "\n\n" + desc );
		} else if ( !strcmp( q.status, "bad" ) ) showResult( G_EXCL_CIRCLE, R_RED, "This address can\xE2\x80\x99t be used", q.error );
		else showResult( G_X_CIRCLE, R_TEXT2, fmt( "No co-op game found at %s", q.address ),
			q.error[0] ? str( q.error ) : str( "Nothing answered within 2 seconds. Check the address, that the host pressed Host and is running co-op, and that both PCs are on the same network." ) );
		EnableWindow( ctl( ID_CONNECT ), found && canPlay );
		setTooltip( hwnd, ctl( ID_CONNECT ), found ? fmt( "Join %s as player 2", foundAddr.c_str( ) ).c_str( ) : "Check the address first" );
		delete r;
	}

	/* ---- actions ---- */
	str takeName( )
	{
		char o[64]; lc_coop_safe_name( getText( ctl( ID_NAME ) ).c_str( ), o, sizeof o );
		lc_store *st = lc_store_shared( ); lc_store_set( st, LC_KEY_COOP_NAME, o ); lc_store_save( st );
		return o;
	}
	void doHost( )
	{
		if ( !canPlay ) return;
		bool bg = SendMessageW( ctl( ID_BG ), BM_GETCHECK, 0, 0 ) == BST_CHECKED;
		lc_store *st = lc_store_shared (); lc_store_set_int( st, LC_KEY_COOP_BACKGROUND, bg ); lc_store_save( st );
		str n = takeName( ); str pw = getText( ctl( ID_PW ) );
		lc_coop_choose_host( bg, n.c_str( ), pw.c_str( ) );
		lc_log( "Co-op: hosting (single player with sp_coop 1) as %s%s", n.c_str( ), pw.empty( ) ? "" : ", with a password" );
		playNow = true; end( IDOK );
	}
	void doConnect( )
	{
		if ( !found || !canPlay ) return;
		str pw = getText( ctl( ID_JPW ) );
		if ( needPw && pw.empty( ) ) {
			showResult( G_LOCK, R_ORANGE, "This game needs a password", "Type the password the host gave you, then press Connect." );
			SetFocus( ctl( ID_JPW ) ); return;
		}
		coopNatRelease( ); natState = 0;
		str n = takeName( );
		bool female = SendMessageW( ctl( ID_MUNRO ), SEG_GETSEL, 0, 0 ) == 1;
		lc_store *st = lc_store_shared( ); lc_store_set( st, LC_KEY_COOP_MUNRO, female ? "female" : "male" ); lc_store_save( st );
		lc_coop_choose_join( foundAddr.c_str( ), female ? "female" : "male", n.c_str( ), pw.c_str( ) );
		lc_log( "Co-op: joining %s as player 2 (single player with coop_connect), %s, %s Munro", foundAddr.c_str( ), n.c_str( ), female ? "female" : "male" );
		playNow = true; end( IDOK );
	}
	void onCommand( int id, int code, HWND ) override
	{
		switch ( id ) {
		case ID_HOST: doHost( ); break;
		case ID_ROUTER: routerToggle( ); break;
		case ID_INVITE: {
			char link[256]; lc_coop_invite_link( natState == 2 ? natPublic.c_str( ) : "", link, sizeof link );
			if ( link[0] ) { copyToClipboard( hwnd, link ); setText( ctl( ID_INET ), fmt( "Copied %s. Your friend opens it (Elite Force for Windows with co-op) to check your game and join.", link ) ); setRole( ctl( ID_INET ), R_TEXT2 ); lc_log( "Co-op: invite %s", link ); }
			break; }
		case ID_CHECK: doCheck( ); break;
		case ID_ADDR: if ( code == EN_CHANGE && found ) { found = false; EnableWindow( ctl( ID_CONNECT ), FALSE ); setTooltip( hwnd, ctl( ID_CONNECT ), "Check the address first" ); } break;
		case ID_CONNECT: doConnect( ); break;
		case IDOK: if ( GetFocus( ) == ctl( ID_ADDR ) ) doCheck( ); break;
		case IDCANCEL: coopNatRelease( ); end( IDCANCEL ); break;
		}
	}
	bool onClose( ) override { coopNatRelease( ); end( IDCANCEL ); return true; }
	void onTimer( int id ) override { if ( id == 1 && natState == 1 ) natFail( "Your router didn\xE2\x80\x99t answer the request to open the port: it may not support automatic port opening (NAT-PMP / UPnP), or has it turned off." ); }
	void onUser( UINT m, WPARAM, LPARAM l ) override { if ( m == WM_NAT ) onNat( (NatRes *)l ); else if ( m == WM_QUERY ) onQuery( (QRes *)l ); }
	void paintOver( HDC dc ) override
	{
		if ( resGlyph >= 0 ) {
			RECT r; GetWindowRect( ctl( ID_RES_ICON ), &r ); POINT p = { r.left, r.top }; ScreenToClient( hwnd, &p );
			drawGlyph( dc, resGlyph, p.x, p.y, S( 20 ), roleColor( resRole == R_TEXT2 ? R_TEXT2 : resRole ) );
		}
	}
};

void showCoopSheet( HWND owner, bool canPlay, const str &prefillAddress, bool autoCheck, bool *playNow )
{
	CoopDlg d; d.canPlay = canPlay;
	const int W0 = 560;
	lc_store *st = lc_store_shared( );
	char lan[8][16], ifn[8][64]; int nlan = lc_coop_lan_addresses( lan, ifn, 8 );
	if ( nlan > 0 ) d.lanFirst = lan[0];
	int y = 18;
	const int H = 770;
	d.create( L"Co-op", W0, H, WS_POPUP | WS_CAPTION | WS_SYSMENU, 0, owner );
	d.label( 20, y, 90, 24, "Co-op", 17, 700, R_TEXT, DT_SINGLELINE );
	d.label( 100, y + 6, 120, 14, "EXPERIMENTAL", 10, 600, R_ORANGE, DT_SINGLELINE );
	y += 24 + 4;
	d.label( 20, y, 520, 34, "Play the campaign with a friend. The host is player 1 and plays the story; the friend joins as player 2.", 12, 400, R_TEXT2 ); y += 36 + 6;
	/* name */
	d.label( 20, y + 3, 80, 16, "Your name", 12, 400, R_TEXT, DT_SINGLELINE );
	char saved[64], nm[64]; const char *s = lc_store_get( st, LC_KEY_COOP_NAME ); lc_coop_safe_name( s ? s : "", saved, sizeof saved );
	lc_coop_safe_name( lc_computer_name( ), nm, sizeof nm ); if ( !nm[0] ) lc_strlcpy( nm, PLATFORM_NOUN, sizeof nm );
	HWND ne = d.edit( 106, y, 220, 22, CoopDlg::ID_NAME, saved[0] ? saved : nm );
	SendMessageW( ne, EM_SETCUEBANNER, TRUE, (LPARAM)W( nm ).c_str( ) ); SendMessageW( ne, EM_LIMITTEXT, 31, 0 );
	setTooltip( d.hwnd, ne, "Shown to the other player in co-op messages, such as \xE2\x80\x9CHold Use to revive \xE2\x80\xA6\xE2\x80\x9D" );
	d.label( 334, y + 4, 150, 14, "up to 31 letters", 11, 400, R_TEXT2, DT_SINGLELINE );
	y += 22 + 22;
	/* host group */
	int hTop = y, hH = 296;
	d.addGroup( 20, hTop, W0 - 40, hH, "HOST" );
	int ty = hTop + 14; const int tx = 36, tw = 400;
	str intro = fmt( "Starts single player with co-op on: choose New Game or Load as usual. Your friend then joins as player 2 with this %s\xE2\x80\x99s address:", PLATFORM_NOUN );
	d.label( tx, ty, tw, 32, intro.c_str( ), 12, 400, R_TEXT ); ty += 34;
	if ( nlan == 0 ) { d.label( tx, ty, tw, 16, "No network found. Connect this PC to Wi-Fi or Ethernet first.", 12, 400, R_ORANGE, DT_SINGLELINE ); ty += 20; }
	else for ( int i = 0; i < nlan && i < 4; i++ ) { d.label( tx, ty, tw, 18, fmt( "%s    (%s)", lan[i], ifn[i] ).c_str( ), 13, 500, R_TEXT, DT_SINGLELINE ); ty += 18; }
	ty += 8;
	d.label( tx, ty + 3, 130, 16, "Password (optional)", 12, 400, R_TEXT, DT_SINGLELINE );
	HWND pw = d.edit( tx + 136, ty, 160, 22, CoopDlg::ID_PW, "", ES_PASSWORD ); SendMessageW( pw, EM_SETCUEBANNER, TRUE, (LPARAM)L"none" ); ty += 28;
	HWND bg = d.check( tx, ty, 360, 20, "Keep playing in the background", CoopDlg::ID_BG, lc_store_get_bool( st, LC_KEY_COOP_BACKGROUND, 1 ) != 0, 12 );
	setTooltip( d.hwnd, bg, "The game goes on while its window isn\xE2\x80\x99t in front, e.g. while you click into a second copy on this PC (keys and the mouse still go only to the window in front)" ); ty += 24;
	d.check( tx, ty, 380, 20, "Open the port on my router (for a friend on the internet)", CoopDlg::ID_ROUTER, false, 12 ); ty += 24;
	d.label( tx, ty, tw, 48, "", 11, 400, R_TEXT2, DT_WORDBREAK, CoopDlg::ID_INET ); ty += 52;
	HWND inv = d.button( tx, ty, 100, 22, "Copy Invite", CoopDlg::ID_INVITE, false, 12 );
	setTooltip( d.hwnd, inv, "Copy a stvef://coop/ link for your friend: opening it shows Co-op with your address filled in" ); ty += 28;
	d.label( tx, ty, W0 - 72, 46, fmt( "Co-op uses UDP port %d. Windows may ask whether Elite Force can communicate on your network: allow it. Player 2 follows you until your friend joins; then your friend plays player 2 and sees your game from player 2\xE2\x80\x99s eyes.", LC_COOP_PORT ).c_str( ), 11, 400, R_TEXT2, DT_WORDBREAK, CoopDlg::ID_NOTE );
	HWND hb = d.button( W0 - 40 - 14 - 80, hTop + 14, 80, 28, "Host", CoopDlg::ID_HOST, false, 13 );
	EnableWindow( hb, canPlay );
	setTooltip( d.hwnd, hb, canPlay ? "Play single player with co-op on" : "Add the game files first" );
	y = hTop + hH + 22;
	/* join group */
	int jTop = y, jH = 218;
	d.addGroup( 20, jTop, W0 - 40, jH, "JOIN" );
	int jy = jTop + 14;
	d.label( tx, jy + 3, 90, 16, "Host\xE2\x80\x99s address", 12, 400, R_TEXT, DT_SINGLELINE );
	HWND ae = d.edit( tx + 96, jy, 260, 22, CoopDlg::ID_ADDR, "" );
	SendMessageW( ae, EM_SETCUEBANNER, TRUE, (LPARAM)L"192.168.1.20, or address:port (default 29760)" );
	HWND cb = d.button( tx + 96 + 266, jy - 1, 80, 24, "Check", CoopDlg::ID_CHECK, false, 12 );
	setTooltip( d.hwnd, cb, "Ask that address whether a co-op game runs there" );
	jy += 22 + 10;
	d.label( tx, jy + 3, 60, 16, "Password", 12, 400, R_TEXT, DT_SINGLELINE );
	HWND jp = d.edit( tx + 66, jy, 140, 22, CoopDlg::ID_JPW, "", ES_PASSWORD ); SendMessageW( jp, EM_SETCUEBANNER, TRUE, (LPARAM)L"if the host set one" );
	d.label( tx + 66 + 140 + 20, jy + 3, 44, 16, "Munro", 12, 400, R_TEXT, DT_SINGLELINE );
	HWND mg = mkSeg( d.hwnd, tx + 66 + 140 + 20 + 50, jy, 120, 22, CoopDlg::ID_MUNRO ); d.ids[CoopDlg::ID_MUNRO] = mg;
	segSetItems( mg, { "Male", "Female" } );
	const char *mu = lc_store_get( st, LC_KEY_COOP_MUNRO ); SendMessageW( mg, SEG_SETSEL, mu && !strcmp( mu, "female" ) ? 1 : 0, 0 );
	setTooltip( d.hwnd, mg, "How player 2 looks on both screens. The story still speaks to the host\xE2\x80\x99s Munro." );
	jy += 22 + 12;
	d.ctl_( L"STATIC", L"", SS_NOTIFY, 0, tx, jy, 26, 26, CoopDlg::ID_RES_ICON ); ShowWindow( d.ctl( CoopDlg::ID_RES_ICON ), SW_HIDE );
	d.ids[CoopDlg::ID_RES_SPIN] = mkSpinner( d.hwnd, tx + 4, jy + 4, 16, CoopDlg::ID_RES_SPIN );
	d.label( tx + 30, jy, W0 - 40 - 14 - 30 - 16, 34, "", 13, 600, R_TEXT, DT_WORDBREAK, CoopDlg::ID_RES_TITLE ); d.showCtl( CoopDlg::ID_RES_TITLE, false );
	d.label( tx + 30, jy + 36, W0 - 40 - 14 - 30 - 16, 48, "", 12, 400, R_TEXT2, DT_WORDBREAK, CoopDlg::ID_RES_TEXT ); d.showCtl( CoopDlg::ID_RES_TEXT, false );
	jy += 92;
	HWND cn = d.button( tx, jy, 80, 28, "Connect", CoopDlg::ID_CONNECT, false, 13 );
	EnableWindow( cn, FALSE ); setTooltip( d.hwnd, cn, "Check the address first" );
	d.label( tx + 92, jy, W0 - 40 - 14 - 92 - 16, 46, "Connect starts single player and joins the host\xE2\x80\x99s game as player 2: you see it from player 2\xE2\x80\x99s eyes and play player 2 while the host plays the story.", 11, 400, R_TEXT2 );
	d.button( W0 - 20 - 80, H - 20 - 28, 80, 28, "Cancel", IDCANCEL );
	d.showInternet( );
	if ( !prefillAddress.empty( ) ) SetWindowTextW( ae, W( prefillAddress ).c_str( ) );
	SetFocus( ae );
	if ( autoCheck ) PostMessageW( d.hwnd, WM_COMMAND, MAKEWPARAM( CoopDlg::ID_CHECK, BN_CLICKED ), (LPARAM)cb );
	d.runModal( );
	if ( playNow ) *playNow = d.playNow;
}

}	// namespace wl
