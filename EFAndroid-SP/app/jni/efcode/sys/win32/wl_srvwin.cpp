/*
===========================================================================
Windows launcher: the hosting window of the --host-server instance
(servers.md section 9). The dedicated server is a child process of this
executable; the console parser and state machine are the core's (lc_host.h).

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#include "wl_app.h"
#include <commctrl.h>
#include <deque>

namespace wl {

/* ================================================================ console control */

struct ConLine { str text; int color; };
struct ConData { std::deque<ConLine> lines; std::vector<int> heights; int width = 0; int total = 0; int scroll = 0; size_t chars = 0; };
#define CON_ADD ( WM_APP + 200 )

static void conMeasure( HWND h, ConData *d )
{
	RECT rc; GetClientRect( h, &rc );
	HDC dc = GetDC( h ); HGDIOBJ of = SelectObject( dc, F( 11, 400, true ) );
	d->heights.resize( d->lines.size( ) ); d->total = 0;
	for ( size_t i = 0; i < d->lines.size( ); i++ ) {
		RECT r = { 0, 0, max( 10, (int)rc.right - S( 12 ) ), 0 };
		ws w = W( d->lines[i].text ); if ( w.empty( ) ) w = L" ";
		DrawTextW( dc, w.c_str( ), -1, &r, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EXPANDTABS );
		d->heights[i] = r.bottom; d->total += r.bottom;
	}
	SelectObject( dc, of ); ReleaseDC( h, dc );
	d->width = rc.right;
	SCROLLINFO si = { sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS, 0, max( 0, d->total + S( 8 ) - 1 ), (UINT)rc.bottom, d->scroll, 0 };
	SetScrollInfo( h, SB_VERT, &si, TRUE );
}

static LRESULT CALLBACK conProc( HWND h, UINT m, WPARAM w, LPARAM l )
{
	ConData *d = (ConData *)GetWindowLongPtrW( h, GWLP_USERDATA );
	switch ( m ) {
	case WM_NCCREATE: d = new ConData; SetWindowLongPtrW( h, GWLP_USERDATA, (LONG_PTR)d ); break;
	case WM_NCDESTROY: delete d; break;
	case WM_ERASEBKGND: return 1;
	case WM_SIZE: if ( d ) { conMeasure( h, d ); InvalidateRect( h, 0, FALSE ); } break;
	case CON_ADD: {
		RECT rc; GetClientRect( h, &rc );
		bool atBottom = d->scroll + rc.bottom >= d->total + S( 8 ) - S( 4 );
		d->lines.push_back( ConLine{ str( (const char *)w ), (int)l } ); d->chars += strlen( (const char *)w ) + 1;
		while ( d->chars > 300000 ) { d->chars -= d->lines.front( ).text.size( ) + 1; d->lines.pop_front( ); }
		conMeasure( h, d );
		if ( atBottom ) { d->scroll = max( 0, d->total + S( 8 ) - rc.bottom ); SetScrollPos( h, SB_VERT, d->scroll, TRUE ); }
		InvalidateRect( h, 0, FALSE ); return 0; }
	case WM_VSCROLL: {
		RECT rc; GetClientRect( h, &rc ); int pos = d->scroll;
		switch ( LOWORD( w ) ) { case SB_LINEUP: pos -= S( 16 ); break; case SB_LINEDOWN: pos += S( 16 ); break; case SB_PAGEUP: pos -= rc.bottom; break; case SB_PAGEDOWN: pos += rc.bottom; break;
		case SB_THUMBTRACK: case SB_THUMBPOSITION: { SCROLLINFO si = { sizeof si, SIF_TRACKPOS }; GetScrollInfo( h, SB_VERT, &si ); pos = si.nTrackPos; break; } case SB_TOP: pos = 0; break; case SB_BOTTOM: pos = d->total; break; }
		d->scroll = max( 0, min( pos, max( 0, d->total + S( 8 ) - rc.bottom ) ) ); SetScrollPos( h, SB_VERT, d->scroll, TRUE ); InvalidateRect( h, 0, FALSE ); return 0; }
	case WM_MOUSEWHEEL: SendMessageW( h, WM_VSCROLL, (short)HIWORD( w ) > 0 ? SB_LINEUP : SB_LINEDOWN, 0 ); SendMessageW( h, WM_VSCROLL, (short)HIWORD( w ) > 0 ? SB_LINEUP : SB_LINEDOWN, 0 ); return 0;
	case WM_CONTEXTMENU: {
		HMENU mn = CreatePopupMenu( ); AppendMenuW( mn, MF_STRING, 1, L"Copy All" );
		POINT p = { (short)LOWORD( l ), (short)HIWORD( l ) }; if ( p.x == -1 ) GetCursorPos( &p );
		if ( TrackPopupMenu( mn, TPM_RETURNCMD, p.x, p.y, 0, h, 0 ) == 1 ) { str all; for ( auto &ln : d->lines ) all += ln.text + "\r\n"; copyToClipboard( h, all ); }
		DestroyMenu( mn ); return 0; }
	case WM_PRINTCLIENT: case WM_PAINT: {
		PAINTSTRUCT ps; HDC dc; bool prn = m == WM_PRINTCLIENT; if ( prn ) dc = (HDC)w; else dc = BeginPaint( h, &ps );
		RECT rc; GetClientRect( h, &rc );
		{
			HDC md = CreateCompatibleDC( dc ); HBITMAP mb = CreateCompatibleBitmap( dc, rc.right, rc.bottom ); HGDIOBJ mo = SelectObject( md, mb );
			HBRUSH b = CreateSolidBrush( T.field ); FillRect( md, &rc, b ); DeleteObject( b );
			SetBkMode( md, TRANSPARENT ); SelectObject( md, F( 11, 400, true ) );
			int y = S( 4 ) - d->scroll;
			for ( size_t i = 0; i < d->lines.size( ) && y < rc.bottom; i++ ) {
				if ( y + d->heights[i] > 0 ) {
					static const int role[6] = { R_TEXT, R_ORANGE, R_RED, R_GREEN, R_TEXT2, R_ACCENT };
					SetTextColor( md, roleColor( role[max( 0, min( 5, d->lines[i].color ) )] ) );
					RECT r = { S( 6 ), y, rc.right - S( 6 ), y + d->heights[i] };
					ws w2 = W( d->lines[i].text ); DrawTextW( md, w2.c_str( ), -1, &r, DT_WORDBREAK | DT_NOPREFIX | DT_EXPANDTABS );
				}
				y += d->heights[i];
			}
			BitBlt( dc, 0, 0, rc.right, rc.bottom, md, 0, 0, SRCCOPY ); SelectObject( md, mo ); DeleteObject( mb ); DeleteDC( md );
		}
		if ( !prn ) EndPaint( h, &ps ); return 0; }
	}
	return DefWindowProcW( h, m, w, l );
}

static void registerConsole( )
{
	static bool done; if ( done ) return; done = true;
	WNDCLASSW wc = {}; wc.lpfnWndProc = conProc; wc.hInstance = GetModuleHandleW( 0 ); wc.lpszClassName = L"EFConsole"; wc.hCursor = LoadCursor( 0, IDC_IBEAM );
	RegisterClassW( &wc );
}

/* ================================================================ the server window */

struct SrvWin : Wnd {
	lc_server_settings *settings = 0;
	str basepath, dataFolder; std::vector<str> clientArgs;
	const lc_map_catalog *cat = 0;
	lc_srv *srv = 0; lc_proc *proc = 0;
	lc_srv_state state = LC_SRV_STARTING; str stateDetail;
	str lanAddress; int port = 27960;
	str curMap; std::vector<lc_srv_player> plist; int shownBots = 0;
	bool becameReadyDone = false, hasChild = false, internet = false;
	int stopResponse = IDCANCEL;
	HWND con = 0, table = 0;
	char masters[LC_MAX_MASTERS][65]; int nMasters = 0;
	/* router */
	lc_nat *nat = 0; bool opening = false; str publicAddr, natWhy; int publicPort = 0; bool natHad = false;
	/* listing check */
	bool checking = false; int checkCount = 0; int lstate[LC_MAX_MASTERS] = {}; bool checkEnded = false, listedAny = false, allDown = false;
	lc_nat *lookup = 0; str lookupAddr;
	int rowsY[LC_MAX_MASTERS] = {}; int nListRows = 0, dotX = 0, dotY = 0, inetY = -1, inetGlyph = -1, inetRole = R_TEXT2;
	int stateRole = R_ORANGE; double stopAt = 0;
	bool childExited = false;
	int exitSig = 0, exitCode = 0;
	enum { SID_ADDR = 10, SID_COPY, SID_HINT, SID_INETVAL, SID_INETCOPY, SID_INETHINT, SID_LISTSTAT, SID_LISTCHECK, SID_LISTHINT, SID_MAP, SID_GAME, SID_PLAYERS, SID_NEXT, SID_RESTART, SID_CHANGE, SID_ADDBOT, SID_RMBOTS, SID_MSG,
		SID_KICK, SID_TABLE, SID_CMD, SID_SEND, SID_PLAY, SID_STOP, SID_TITLE, SID_STATE, SID_MASTERLBL = 100 };
	static const int WM_OUT = WM_APP + 50, WM_EXIT = WM_APP + 51, WM_NATRES = WM_APP + 52, WM_LISTRES = WM_APP + 53, WM_LOOKUP = WM_APP + 54;

	/* ---- core callbacks (all on the UI thread) ---- */
	static void cbSend( const char *cmd, void *u ) { SrvWin *s = (SrvWin *)u; if ( s->proc ) { str c = str( cmd ) + "\n"; if ( !lc_proc_write( s->proc, c.c_str( ), c.size( ) ) ) lc_log( "Server: couldn't send \"%s\": write failed", cmd ); } }
	static void cbLog( const char *line, int color, void *u ) { SrvWin *s = (SrvWin *)u; if ( testOn( "VOYAGERSP_TEST_SERVER_START" ) ) fprintf( stderr, "[server] %s\n", line ); SendMessageW( s->con, CON_ADD, (WPARAM)line, color ); }
	static void cbState( lc_srv_state st, const char *detail, void *u ) { SrvWin *s = (SrvWin *)u; s->state = st; s->stateDetail = detail ? detail : ""; s->showState( ); }
	static void cbPlayers( const lc_srv_player *p, int n, void *u ) { SrvWin *s = (SrvWin *)u; s->plist.assign( p, p + n ); s->shownBots = 0; for ( auto &x : s->plist ) if ( x.bot ) s->shownBots++; s->showPlayersTable( ); s->showInfo( ); }
	static void cbInfo( const char *map, int boundPort, void *u ) { SrvWin *s = (SrvWin *)u; s->curMap = map ? map : ""; s->port = boundPort; s->showInfo( ); }
	static void cbReady( int boundPort, void *u ) { SrvWin *s = (SrvWin *)u; s->port = boundPort; s->becameReady( ); }

	/* ---- state texts ---- */
	void showState( )
	{
		str t; int role = R_TEXT2; stateRole = R_TEXT3;
		switch ( state ) {
		case LC_SRV_STARTING: t = "Starting\xE2\x80\xA6"; stateRole = R_ORANGE; break;
		case LC_SRV_RUNNING: t = internet ? "Running \xC2\xB7 on your network and the internet" : "Running \xC2\xB7 on your local network"; stateRole = R_GREEN; break;
		case LC_SRV_IDLE: t = "Running, but no map is loaded"; stateRole = R_ORANGE; break;
		case LC_SRV_STOPPING: t = "Stopping\xE2\x80\xA6"; stateRole = R_ORANGE; break;
		case LC_SRV_STOPPED: t = "Stopped"; stateRole = R_TEXT3; break;
		case LC_SRV_FAILED: t = "Stopped"; stateRole = R_RED; role = R_RED; break;
		}
		if ( !stateDetail.empty( ) ) t += " \xC2\xB7 " + stateDetail;
		setText( ctl( SID_STATE ), t ); setRole( ctl( SID_STATE ), role );
		bool run = state == LC_SRV_RUNNING;
		EnableWindow( ctl( SID_PLAY ), run );
		bool cmd = state == LC_SRV_STARTING || state == LC_SRV_RUNNING || state == LC_SRV_IDLE;
		EnableWindow( ctl( SID_CMD ), cmd ); EnableWindow( ctl( SID_SEND ), cmd );
		bool done = state == LC_SRV_STOPPED || state == LC_SRV_FAILED;
		setText( ctl( SID_STOP ), done ? "Close" : "Stop Server" ); EnableWindow( ctl( SID_STOP ), state != LC_SRV_STOPPING );
		bool ctrl = hasChild && state == LC_SRV_RUNNING;
		for ( int id : { SID_NEXT, SID_RESTART, SID_CHANGE, SID_ADDBOT, SID_RMBOTS, SID_MSG } ) EnableWindow( ctl( id ), ctrl );
		updateKick( );
		showInternet( ); showListing( );
		InvalidateRect( hwnd, 0, TRUE );
	}
	void updateKick( ) { int s = (int)SendMessageW( table, LVM_GETNEXTITEM, -1, LVNI_SELECTED ); EnableWindow( ctl( SID_KICK ), hasChild && state == LC_SRV_RUNNING && s >= 0 && s < (int)plist.size( ) ); }

	void showInfo( )
	{
		str addr = ( lanAddress.empty( ) ? str( "127.0.0.1" ) : lanAddress ) + fmt( ":%d", port );
		setText( ctl( SID_ADDR ), addr );
		bool disc = port >= 27960 && port <= 27963; str hint; int role = R_TEXT2;
		if ( lanAddress.empty( ) ) hint = fmt( "This %s isn't on a network: only Play on This %s can join.", PLATFORM_NOUN, PLATFORM_NOUN );
		else if ( !disc ) { hint = fmt( "Port %d is outside 27960\xE2\x80\x93" "27963, so Holomatch's Join menu won't list it on your network: players connect to this address.%s", port, internet ? " From the internet: your public address, with the port forwarded." : "" ); role = R_ORANGE; }
		else if ( internet ) hint = "On your network, players find it in Holomatch's Join menu, or connect to this address.";
		else hint = "Players on your network find it in Holomatch's Join menu, or connect to this address.";
		setText( ctl( SID_HINT ), hint ); setRole( ctl( SID_HINT ), role ); setTooltip( hwnd, ctl( SID_HINT ), hint.c_str( ) );
		str map = "\xE2\x80\x94";
		if ( !curMap.empty( ) ) {
			const lc_map_info *mi = cat ? lc_map_find( cat, curMap.c_str( ) ) : 0;
			if ( mi ) { str dn = lc_map_display_name( mi ); map = dn == mi->name ? dn : fmt( "%s (%s)", dn.c_str( ), mi->name ); } else map = curMap;
			if ( settings->rotationCount > 1 ) map += fmt( " \xC2\xB7 %d maps in rotation", settings->rotationCount );
		}
		setText( ctl( SID_MAP ), map );
		str game = lc_gametype_name( settings->gametype ), mods;
		for ( int m = 0; m < LC_MOD_COUNT; m++ ) if ( settings->mods[m] ) { if ( !mods.empty( ) ) mods += ", "; mods += lc_modifier_name( m ); }
		if ( !mods.empty( ) ) game += " \xC2\xB7 " + mods;
		setText( ctl( SID_GAME ), game );
		str pl;
		if ( state == LC_SRV_STARTING || curMap.empty( ) ) pl = fmt( "Up to %d", settings->maxPlayers );
		else { pl = fmt( "%d of %d", (int)plist.size( ), settings->maxPlayers ); if ( shownBots > 0 ) pl += fmt( " \xC2\xB7 %d bot%s", shownBots, shownBots == 1 ? "" : "s" ); }
		setText( ctl( SID_PLAYERS ), pl );
	}
	void showPlayersTable( )
	{
		ListView_SetItemCountEx( table, (int)plist.size( ), LVSICF_NOINVALIDATEALL ); InvalidateRect( table, 0, TRUE ); updateKick( );
	}

	/* ---- the child process ---- */
	struct Out { SrvWin *s; str data; };
	static void procOut( const char *b, size_t n, void *u ) { SrvWin *s = (SrvWin *)u; if ( s->hwnd ) PostMessageW( s->hwnd, WM_OUT, 0, (LPARAM)new str( b, n ) ); }
	static void procExit( int sig, int code, void *u ) { SrvWin *s = (SrvWin *)u; if ( s->hwnd ) PostMessageW( s->hwnd, WM_EXIT, sig, code ); }

	void start( )
	{
		/* vsp_server.cfg (5.1) */
		char *text = 0; char err[256] = "";
		int blank = testOn( "VOYAGERSP_TEST_SERVER_NOMASTER" );
		struct L { static void log( const char *line, void *u ) { lc_log( "%s", line ); (void)u; } };
		if ( !lc_server_cfg_text( settings, cat, masters, nMasters, blank, &text, err, sizeof err, L::log, this ) ) {
			lc_srv_start_failed( srv, err[0] ? err : "Couldn't write the server settings." ); return;
		}
		char path[LC_MAX_PATH]; lc_path_join( path, sizeof path, G.homeBase.c_str( ), "vsp_server.cfg" );
		lc_mkdir_p( G.homeBase.c_str( ) );
		if ( !lc_write_file_atomic( path, text, strlen( text ) ) ) { free( text ); lc_srv_start_failed( srv, fmt( "Couldn't write %s.", path ).c_str( ) ); return; }
		free( text );
		std::vector<str> a = { "+set", "com_holomatch", "1", "+set", "dedicated", settings->internet ? "2" : "1", "+set", "net_port", fmt( "%d", settings->port ) };
		if ( !basepath.empty( ) ) { a.push_back( "+set" ); a.push_back( "fs_basepath" ); a.push_back( basepath ); }
		a.push_back( "+exec" ); a.push_back( "vsp_server.cfg" );
		std::vector<const char *> av; const char *exe = lc_exe_path( ); av.push_back( exe );
		for ( auto &s : a ) av.push_back( s.c_str( ) );
		av.push_back( 0 );
		str joined; for ( auto &s : a ) { if ( !joined.empty( ) ) joined += " "; joined += s; }
		proc = lc_proc_spawn( exe, av.data( ), 0, procOut, procExit, this );
		if ( !proc ) { lc_srv_start_failed( srv, "Couldn't start the server: the process couldn't be created." ); return; }
		hasChild = true;
		lc_log( "Server: starting %s %s", exe, joined.c_str( ) );
		SendMessageW( con, CON_ADD, (WPARAM)( "Starting the server: " + joined ).c_str( ), 4 );
		SetTimer( hwnd, 2, 2000, 0 );
		showState( );
	}

	void stopThen( int response )
	{
		stopResponse = response;
		if ( !hasChild || !proc || !lc_proc_running( proc ) ) { end( response ); return; }
		if ( state == LC_SRV_STOPPING ) return;
		lc_srv_begin_stop( srv );
		stopAt = lc_now( );
		SetTimer( hwnd, 3, 250, 0 );
	}

	void onExit( int sig, int code )
	{
		hasChild = false;
		lc_log( "Server: exited" );
		closeRouterPort( ); stopListCheck( );
		KillTimer( hwnd, 2 ); KillTimer( hwnd, 3 );
		bool wasStopping = state == LC_SRV_STOPPING;
		curMap.clear( ); plist.clear( ); shownBots = 0;
		lc_srv_exited( srv, sig != 0, sig ? sig : code );
		showPlayersTable( ); showInfo( ); showState( );
		if ( wasStopping ) end( stopResponse );
	}

	/* ---- becameReady (9.8) ---- */
	void becameReady( )
	{
		if ( becameReadyDone ) return; becameReadyDone = true;
		lc_log( "Server: ready on %s:%d (map %s)", lanAddress.empty( ) ? "127.0.0.1" : lanAddress.c_str( ), port, curMap.c_str( ) );
		lc_running_register( G.homeRoot.c_str( ), settings->hostname, port );
		if ( internet && settings->openPort ) openRouterPort( );
		if ( internet ) startListCheck( );
		if ( const char *a = testEnv( "VOYAGERSP_TEST_SERVER_COMMAND" ) ) lc_srv_user_command( srv, a );
		if ( const char *a = testEnv( "VOYAGERSP_TEST_SERVER_ACTIONS" ) ) { testActions = a; testIdx = 0; SetTimer( hwnd, 9, 4000, 0 ); }
	}
	str testActions; int testIdx = 0;
	void testAction( )
	{
		size_t pos = 0, i = 0; str a;
		while ( pos <= testActions.size( ) ) { size_t e = testActions.find( ',', pos ); if ( e == str::npos ) e = testActions.size( ); if ( (int)i++ == testIdx ) { a = testActions.substr( pos, e - pos ); break; } pos = e + 1; }
		if ( a.empty( ) ) { KillTimer( hwnd, 9 ); return; }
		testIdx++;
		lc_log( "[test] action %s", a.c_str( ) );
		if ( a == "next" ) lc_srv_next_map( srv ); else if ( a == "restart" ) lc_srv_restart_map( srv ); else if ( a == "addbot" ) lc_srv_add_bot( srv, settings->botSkill );
		else if ( a == "removebots" ) lc_srv_remove_bots( srv ); else if ( !a.compare( 0, 4, "slot" ) ) lc_srv_change_map_slot( srv, atoi( a.c_str( ) + 4 ) );
		else if ( !a.compare( 0, 4, "map:" ) ) lc_srv_change_map( srv, a.c_str( ) + 4 );
		else if ( !a.compare( 0, 4, "kick" ) ) { int r = atoi( a.c_str( ) + 4 ); if ( r >= 0 && r < (int)plist.size( ) ) lc_srv_kick( srv, plist[r].num ); }
	}

	/* ---- router port mapping (9.9) ---- */
	struct NatRes { SrvWin *s; lc_nat_result r; char ip[64]; int ext, err; };
	static void natCb( lc_nat_result r, const char *ip, int ext, int err, void *u )
	{
		SrvWin *s = (SrvWin *)u; NatRes *n = new NatRes{ s, r, "", ext, err }; if ( ip ) lc_strlcpy( n->ip, ip, sizeof n->ip );
		if ( s->hwnd ) PostMessageW( s->hwnd, WM_NATRES, 0, (LPARAM)n ); else delete n;
	}
	void openRouterPort( )
	{
		if ( opening ) return;
		natWhy.clear( ); publicAddr.clear( ); publicPort = 0;
		lc_log( "Server: asking the router to forward UDP %d", port );
		opening = true;
		nat = lc_nat_map_udp( port, natCb, this );
		SetTimer( hwnd, 4, 15000, 0 );
		if ( !nat ) natFailed( "The router didn\xE2\x80\x99t open the port (error 0)." );
		showInternet( );
	}
	void natFailed( const str &why )
	{
		opening = false; KillTimer( hwnd, 4 ); natWhy = why; publicAddr.clear( );
		lc_log( "Server: port not opened: %s", why.c_str( ) );
		SendMessageW( con, CON_ADD, (WPARAM)( "Router: " + why ).c_str( ), 1 );
		showInternet( ); showListing( );
	}
	void closeRouterPort( )
	{
		KillTimer( hwnd, 4 );
		if ( nat ) { lc_nat_release( nat ); nat = 0; lc_log( "Server: router port mapping removed" ); }
		opening = false; publicAddr.clear( );
	}
	void onNatRes( NatRes *n )
	{
		if ( opening ) {
			switch ( n->r ) {
			case LC_NAT_OK: {
				unsigned a, b, c, d; unsigned ip = 0; if ( sscanf( n->ip, "%u.%u.%u.%u", &a, &b, &c, &d ) == 4 ) ip = ( a << 24 ) | ( b << 16 ) | ( c << 8 ) | d;
				if ( !ip ) break;
				if ( lc_ipv4_private( ip ) ) { natFailed( fmt( "Your router opened the port, but its own internet address (%s) is private: your provider or another router puts it behind a second NAT, which nobody outside can get through.", n->ip ) ); break; }
				opening = false; KillTimer( hwnd, 4 );
				publicAddr = n->ip; publicPort = n->ext ? n->ext : port; natHad = true;
				lc_log( "Server: the router forwards %s:%d to this %s", n->ip, publicPort, PLATFORM_NOUN );
				SendMessageW( con, CON_ADD, (WPARAM)fmt( "Router: port opened, players on the internet connect to %s:%d", n->ip, publicPort ).c_str( ), 3 );
				showInternet( ); break; }
			case LC_NAT_UNSUPPORTED: natFailed( "Your router doesn\xE2\x80\x99t support automatic port opening (NAT-PMP / UPnP), or has it turned off." ); break;
			case LC_NAT_DISABLED: natFailed( "Automatic port opening (NAT-PMP / UPnP) is turned off in your router\xE2\x80\x99s settings." ); break;
			case LC_NAT_DOUBLE: natFailed( "Your network has two routers in a row (double NAT), so the outer one still blocks the port." ); break;
			case LC_NAT_PRIVATE_EXTERNAL: natFailed( fmt( "Your router opened the port, but its own internet address (%s) is private: your provider or another router puts it behind a second NAT, which nobody outside can get through.", n->ip ) ); break;
			case LC_NAT_TIMEOUT: natFailed( "Your router didn\xE2\x80\x99t answer the request to open the port: it may not support automatic port opening (NAT-PMP / UPnP), or has it turned off." ); break;
			default: natFailed( fmt( "The router didn\xE2\x80\x99t open the port (error %d).", n->err ) );
			}
		}
		delete n;
	}
	void showInternet( )
	{
		if ( !internet ) return;
		str manual = fmt( "Forward UDP port %d to this %s (%s) in your router\xE2\x80\x99s settings, or use Tailscale (see the README).", port, PLATFORM_NOUN, lanAddress.empty( ) ? ( str( "this " ) + PLATFORM_NOUN + "\xE2\x80\x99s address" ).c_str( ) : lanAddress.c_str( ) );
		str val, hint; int glyph = -1, role = R_TEXT2, hrole = R_TEXT2; bool copy = false;
		if ( !publicAddr.empty( ) ) { val = fmt( "%s:%d", publicAddr.c_str( ), publicPort ); glyph = G_CHECK_CIRCLE; role = R_GREEN; copy = true; hint = "Your router forwards this address to the server while it runs. Friends on the internet connect to it, or find the server in Holomatch's internet list."; }
		else if ( opening ) { val = "Opening the port on your router\xE2\x80\xA6"; glyph = G_REFRESH; }
		else if ( !natWhy.empty( ) ) { val = "Port not opened"; glyph = G_TRIANGLE; role = R_ORANGE; hint = natWhy + " " + manual; hrole = R_ORANGE; }
		else if ( !settings->openPort ) { val = "Your public address"; hint = "Automatic port opening is off. " + manual; }
		else val = hasChild ? "Waiting for the server\xE2\x80\xA6" : "\xE2\x80\x94";
		inetGlyph = glyph; inetRole = role;
		HWND v = ctl( SID_INETVAL ); setText( v, val );
		SendMessageW( v, WM_SETFONT, (WPARAM)( copy ? F( 15, 600, true ) : F( 13 ) ), TRUE );
		setText( ctl( SID_INETHINT ), hint ); setRole( ctl( SID_INETHINT ), hrole ); showCtl( SID_INETHINT, !hint.empty( ) );
		showCtl( SID_INETCOPY, copy );
		InvalidateRect( hwnd, 0, TRUE );
	}

	/* ---- listing check (9.10, 9.11) ---- */
	struct ListRes { SrvWin *s; lc_list_result r; };
	str masterLabel( int i )
	{
		char cur[LC_MAX_MASTERS][65]; int n = lc_masters_load( lc_store_shared( ), cur );
		(void)n; return i < 5 ? fmt( "Internet %d", i + 1 ) : str( cur[i] );
	}
	void startListCheck( )
	{
		stopListCheck( );
		checkCount = 0; checking = true; checkEnded = false; listedAny = false; allDown = false;
		for ( int i = 0; i < LC_MAX_MASTERS; i++ ) lstate[i] = 0;
		if ( !lookup ) lookup = lc_nat_public_address( []( lc_nat_result r, const char *ip, int, void *u ) {
			SrvWin *s = (SrvWin *)u; if ( r == LC_NAT_OK && ip && s->hwnd ) PostMessageW( s->hwnd, WM_LOOKUP, 0, (LPARAM)new str( ip ) ); }, this );
		SetTimer( hwnd, 5, testOn( "VOYAGERSP_TEST_LIST_FAST" ) ? 2000 : 8000, 0 );
		showListing( );
	}
	void stopListCheck( ) { KillTimer( hwnd, 5 ); checking = false; showListing( ); }
	static void listWorker( void *u )
	{
		ListRes *r = (ListRes *)u; SrvWin *s = r->s;
		char cur[LC_MAX_MASTERS][65]; int n = lc_masters_load( lc_store_shared( ), cur );
		str ip = !s->publicAddr.empty( ) ? s->publicAddr : s->lookupAddr;
		str want = ip.empty( ) ? str( ) : fmt( "%s:%d", ip.c_str( ), !s->publicAddr.empty( ) ? s->publicPort : s->port );
		memset( &r->r, 0, sizeof r->r );
		lc_list_check( cur, n, want.empty( ) ? 0 : want.c_str( ), &r->r );
		if ( s->hwnd ) PostMessageW( s->hwnd, WM_LISTRES, 0, (LPARAM)r ); else delete r;
	}
	void checkList( )
	{
		if ( !hasChild || !checking ) return;
		checkCount++;
		ListRes *r = new ListRes{ this, {} };
		lc_thread *t = lc_thread_start( listWorker, r );
		if ( t ) lc_thread_detach( t ); else { delete r; }
	}
	void onListRes( ListRes *r )
	{
		if ( !hasChild || !checking ) { delete r; return; }
		char cur[LC_MAX_MASTERS][65]; int n = lc_masters_load( lc_store_shared( ), cur );
		str L, M, D; int nl = 0, nm = 0, nd = 0; bool newly = false; str newLabels;
		for ( int i = 0; i < n; i++ ) {
			int ns = r->r.listed[i] ? 1 : r->r.missing[i] ? 2 : r->r.down[i] ? 3 : 0;
			str lab = masterLabel( i ) + ( i < 5 ? fmt( " (%s)", cur[i] ) : "" );
			if ( ns == 1 ) { if ( lstate[i] != 1 ) { newly = true; newLabels += ( newLabels.empty( ) ? "" : ", " ) + masterLabel( i ); } L += ( nl++ ? ", " : "" ) + lab; }
			else if ( ns == 2 ) M += ( nm++ ? ", " : "" ) + lab; else if ( ns == 3 ) D += ( nd++ ? ", " : "" ) + lab;
			lstate[i] = ns;
		}
		lc_log( "Server: internet list check %d: listed on [%s], not on [%s], no answer from [%s]", checkCount, L.c_str( ), M.c_str( ), D.c_str( ) );
		if ( newly ) SendMessageW( con, CON_ADD, (WPARAM)fmt( "Internet: listed on %s, so players anywhere can reach it.", newLabels.c_str( ) ).c_str( ), 3 );
		listedAny = nl > 0; allDown = nl == 0 && nm == 0;
		bool fast = testOn( "VOYAGERSP_TEST_LIST_FAST" );
		int maxChecks = fast ? 2 : 9;
		if ( ( listedAny && nm == 0 ) || checkCount >= maxChecks ) {
			checking = false; checkEnded = true;
			if ( !listedAny ) SendMessageW( con, CON_ADD, (WPARAM)( allDown ? "Internet: no master server answered, so the listing couldn't be checked." : "Internet: not on the server list. The master server couldn't reach this server from the internet." ), 1 );
			else if ( nm > 0 ) SendMessageW( con, CON_ADD, (WPARAM)fmt( "Internet: not listed on %s (yet).", M.c_str( ) ).c_str( ), 4 );
		} else SetTimer( hwnd, 5, fast ? 2000 : 20000, 0 );
		showListing( );
		delete r;
	}
	void showListing( )
	{
		if ( !internet || !hwnd ) return;
		str st;
		if ( checking && checkCount == 0 ) st = "Checking the masters\xE2\x80\xA6"; else if ( checking ) st = fmt( "Checking again\xE2\x80\xA6 (%d of 9)", checkCount );
		else if ( !checkEnded && checkCount == 0 ) st = hasChild ? "Waiting for the server\xE2\x80\xA6" : "";
		setText( ctl( SID_LISTSTAT ), st ); showCtl( SID_LISTSTAT, !st.empty( ) );
		str hint;
		if ( checkEnded && !listedAny && !allDown ) hint = fmt( "No master could reach your server, so players outside your network can't either%s. Forward UDP %d to %s in your router's settings, or use Tailscale (see the README).",
			natHad && !publicAddr.empty( ) ? " (your router accepted the port request but doesn't forward it)" : "", port, lanAddress.empty( ) ? ( str( "this " ) + PLATFORM_NOUN ).c_str( ) : lanAddress.c_str( ) );
		setText( ctl( SID_LISTHINT ), hint ); showCtl( SID_LISTHINT, !hint.empty( ) );
		bool listedAll = true; for ( int i = 0; i < nListRows; i++ ) if ( lstate[i] == 2 || lstate[i] == 0 ) listedAll = false;
		showCtl( SID_LISTCHECK, hasChild && !checking && !listedAll );
		InvalidateRect( hwnd, 0, TRUE );
	}

	/* ---- commands ---- */
	void doCmd( )
	{
		str c = getText( ctl( SID_CMD ) ); lc_trim( &c[0] ); c = c.c_str( );
		if ( c.empty( ) || !hasChild ) return;
		SetWindowTextW( ctl( SID_CMD ), L"" );
		if ( !lc_stricmp( c.c_str( ), "quit" ) ) { stopThen( IDCANCEL ); return; }
		lc_srv_user_command( srv, c.c_str( ) );
	}
	void changeMapMenu( )
	{
		HMENU m = CreatePopupMenu( ); std::vector<str> cmd;
		bool rot = settings->rotationCount > 0;
		if ( rot ) {
			AppendMenuW( m, MF_STRING | MF_GRAYED, 0, L"Rotation" );
			for ( int i = 0; i < settings->rotationCount; i++ ) {
				const lc_map_info *mi = cat ? lc_map_find( cat, settings->rotation[i] ) : 0;
				str t = mi ? fmt( "%s (%s)", lc_map_display_name( mi ), mi->name ) : str( settings->rotation[i] );
				cmd.push_back( fmt( "slot %d", i + 1 ) );
				AppendMenuW( m, MF_STRING | ( curMap == settings->rotation[i] ? MF_CHECKED : 0 ), 1000 + (int)cmd.size( ) - 1, W( "   " + t ).c_str( ) );
			}
		}
		std::vector<const lc_map_info *> others;
		int total = cat ? lc_map_catalog_count( cat ) : 0;
		for ( int i = 0; i < total; i++ ) {
			const lc_map_info *mi = lc_map_catalog_at( cat, i ); bool inRot = false;
			for ( int k = 0; k < settings->rotationCount; k++ ) if ( !lc_stricmp( settings->rotation[k], mi->name ) ) inRot = true;
			if ( !inRot && lc_map_supports_gametype( mi, settings->gametype ) ) others.push_back( mi );
		}
		if ( !others.empty( ) ) {
			if ( rot ) AppendMenuW( m, MF_SEPARATOR, 0, 0 );
			AppendMenuW( m, MF_STRING | MF_GRAYED, 0, L"Other maps" );
			for ( auto mi : others ) {
				str t = fmt( "%s (%s)", lc_map_display_name( mi ), mi->name ); if ( mi->expansion ) t += " \xC2\xB7 Expansion Pack";
				cmd.push_back( str( "map " ) + mi->name );
				AppendMenuW( m, MF_STRING | ( curMap == mi->name ? MF_CHECKED : 0 ), 1000 + (int)cmd.size( ) - 1, W( "   " + t ).c_str( ) );
			}
		}
		RECT r; GetWindowRect( ctl( SID_CHANGE ), &r );
		int c = TrackPopupMenu( m, TPM_RETURNCMD, r.left, r.bottom, 0, hwnd, 0 );
		DestroyMenu( m );
		if ( c >= 1000 && c - 1000 < (int)cmd.size( ) ) {
			const str &x = cmd[c - 1000];
			if ( !x.compare( 0, 5, "slot " ) ) lc_srv_change_map_slot( srv, atoi( x.c_str( ) + 5 ) ); else lc_srv_change_map( srv, x.c_str( ) + 4 );
		}
	}
	void doKick( int row )
	{
		if ( !hasChild || row < 0 || row >= (int)plist.size( ) ) return;
		const lc_srv_player &p = plist[row];
		if ( !p.bot ) {
			if ( Alert( hwnd, fmt( "Kick %s?", p.name ), "They're disconnected from the server, but can join again.", { "Kick", "Cancel" }, 1, 1 ) != 0 ) return;
		}
		lc_srv_kick( srv, p.num );
	}
	void doMessage( )
	{
		str text; int r = PromptText( hwnd, "Message everyone on the server", "Shown in the game's chat area as coming from the server.", "Message", false, { "Send", "Cancel" }, &text );
		if ( r == 0 ) lc_srv_say( srv, text.c_str( ) );
	}
	void playHere( )
	{
		std::vector<str> a = clientArgs;
		if ( settings->password[0] ) { a.push_back( "+set" ); a.push_back( "password" ); a.push_back( settings->password ); }
		a.push_back( "+connect" ); a.push_back( fmt( "127.0.0.1:%d", port ) );
		str j; for ( auto &s : a ) { if ( &s != &a[0] ) j += " "; j += s; }
		lc_log( "Server: Play on This %s: %s", PLATFORM_NOUN, j.c_str( ) );
		SendMessageW( con, CON_ADD, (WPARAM)"Starting Elite Force to join this server\xE2\x80\xA6", 4 );
		long pid = 0; str err;
		if ( !spawnSelf( a, &pid, &err ) ) SendMessageW( con, CON_ADD, (WPARAM)( "Couldn't start the game: " + err ).c_str( ), 2 );
		else lc_log( "Server: Play on This %s started pid %ld", PLATFORM_NOUN, pid );
	}

	/* ---- messages ---- */
	void onCommand( int id, int code, HWND ) override
	{
		switch ( id ) {
		case SID_COPY: copyToClipboard( hwnd, getText( ctl( SID_ADDR ) ) ); break;
		case SID_INETCOPY: copyToClipboard( hwnd, getText( ctl( SID_INETVAL ) ) ); break;
		case SID_LISTCHECK: if ( hasChild ) { lc_srv_heartbeat( srv ); startListCheck( ); } break;
		case SID_NEXT: lc_srv_next_map( srv ); break;
		case SID_RESTART: lc_srv_restart_map( srv ); break;
		case SID_CHANGE: changeMapMenu( ); break;
		case SID_ADDBOT: lc_srv_add_bot( srv, settings->botSkill ); break;
		case SID_RMBOTS: lc_srv_remove_bots( srv ); break;
		case SID_MSG: doMessage( ); break;
		case SID_KICK: doKick( (int)SendMessageW( table, LVM_GETNEXTITEM, -1, LVNI_SELECTED ) ); break;
		case SID_SEND: doCmd( ); break;
		case SID_PLAY: playHere( ); break;
		case SID_STOP: stopThen( IDCANCEL ); break;
		case IDOK: if ( GetFocus( ) == ctl( SID_CMD ) ) doCmd( ); break;
		case IDCANCEL: break;
		}
	}
	bool onClose( ) override { stopThen( IDCANCEL ); return true; }
	LRESULT onNotify( NMHDR *h ) override
	{
		if ( h->hwndFrom != table ) return 0;
		if ( h->code == NM_CUSTOMDRAW ) {
			NMLVCUSTOMDRAW *cd = (NMLVCUSTOMDRAW *)h;
			if ( cd->nmcd.dwDrawStage == CDDS_PREPAINT ) return CDRF_NOTIFYITEMDRAW;
			if ( cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT ) return CDRF_NOTIFYSUBITEMDRAW;
			if ( cd->nmcd.dwDrawStage == ( CDDS_ITEMPREPAINT | CDDS_SUBITEM ) ) {
				HDC dc = cd->nmcd.hdc; RECT rc = cd->nmcd.rc; int row = (int)cd->nmcd.dwItemSpec, col = cd->iSubItem;
				if ( row >= (int)plist.size( ) ) return CDRF_SKIPDEFAULT;
				bool sel = ListView_GetItemState( table, row, LVIS_SELECTED ) != 0;
				HBRUSH b = CreateSolidBrush( sel ? blend( T.field, T.accent, 24 ) : T.field ); FillRect( dc, &rc, b ); DeleteObject( b );
				SetBkMode( dc, TRANSPARENT ); SetTextColor( dc, T.text ); SelectObject( dc, F( 12, 400, col != 0 ) );
				RECT tr = rc; tr.left += S( 4 ); tr.right -= S( 4 );
				const lc_srv_player &p = plist[row];
				ws t = W( col == 0 ? p.name : col == 1 ? p.score : p.ping );
				DrawTextW( dc, t.c_str( ), -1, &tr, ( col ? DT_RIGHT : DT_LEFT ) | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX );
				return CDRF_SKIPDEFAULT;
			}
		} else if ( h->code == LVN_ITEMCHANGED ) updateKick( );
		else if ( h->code == NM_RCLICK ) {
			NMITEMACTIVATE *ia = (NMITEMACTIVATE *)h; if ( ia->iItem >= 0 ) {
				HMENU m = CreatePopupMenu( ); AppendMenuW( m, MF_STRING | ( hasChild && state == LC_SRV_RUNNING ? 0 : MF_GRAYED ), 1, L"Kick" );
				POINT p; GetCursorPos( &p ); int c = TrackPopupMenu( m, TPM_RETURNCMD, p.x, p.y, 0, hwnd, 0 ); DestroyMenu( m ); if ( c == 1 ) doKick( ia->iItem );
			}
		}
		return 0;
	}
	void onUser( UINT m, WPARAM w, LPARAM l ) override
	{
		if ( m == WM_OUT ) { str *s = (str *)l; if ( srv ) lc_srv_feed( srv, s->data( ), s->size( ) ); delete s; }
		else if ( m == WM_EXIT ) onExit( (int)w, (int)l );
		else if ( m == WM_NATRES ) onNatRes( (NatRes *)l );
		else if ( m == WM_LISTRES ) onListRes( (ListRes *)l );
		else if ( m == WM_LOOKUP ) { str *s = (str *)l; lookupAddr = *s; delete s; if ( lookup ) { lc_nat_release( lookup ); lookup = 0; } }
	}
	void onTimer( int id ) override
	{
		if ( id == 2 ) { if ( hasChild ) lc_srv_poll_tick( srv, lc_now( ) ); }
		else if ( id == 3 ) {
			int r = lc_srv_stop_tick( srv, lc_now( ) );
			if ( r == 1 && proc ) lc_proc_terminate( proc ); else if ( r == 2 && proc ) lc_proc_kill( proc );
		}
		else if ( id == 4 ) { if ( opening ) natFailed( "Your router didn\xE2\x80\x99t answer the request to open the port: it may not support automatic port opening (NAT-PMP / UPnP), or has it turned off." ); }
		else if ( id == 5 ) { KillTimer( hwnd, 5 ); checkList( ); }
		else if ( id == 9 ) testAction( );
		else if ( id == 20 ) { KillTimer( hwnd, 20 ); testSnapshot( ); }
		else if ( id == 21 ) { KillTimer( hwnd, 21 ); playHere( ); }
		else if ( id == 22 ) { KillTimer( hwnd, 22 ); stopThen( IDCANCEL ); }
	}
	void testSnapshot( )
	{
		if ( !snapOut.empty( ) ) { snapshotWindow( hwnd, snapOut.c_str( ) ); snapOut.clear( ); }
		if ( !testEnv( "VOYAGERSP_TEST_SERVER_STOP_AFTER" ) ) stopThen( IDCANCEL );
	}
	str snapOut;
	LRESULT onOther( UINT m, WPARAM w, LPARAM l, bool *handled ) override
	{
		*handled = true;
		if ( m == WM_COMMAND && LOWORD( w ) == IDCANCEL && !l ) return 0;	// Esc must not stop the server
		if ( m == WM_QUERYENDSESSION ) return TRUE;
		if ( m == WM_ENDSESSION && w ) { if ( proc ) lc_proc_kill( proc ); return 0; }
		*handled = false; return 0;
	}
	void paintOver( HDC dc ) override
	{
		drawAppIcon( dc, S( 28 ), S( 20 ), S( 52 ) );
		RECT r; if ( ctl( SID_STATE ) ) { GetWindowRect( ctl( SID_STATE ), &r ); POINT p = { r.left, r.top }; ScreenToClient( hwnd, &p );
			drawGlyph( dc, G_DOT, p.x - S( 16 ), p.y + S( 4 ), S( 10 ), roleColor( stateRole ) ); }
		if ( internet && inetGlyph >= 0 && ctl( SID_INETVAL ) ) { GetWindowRect( ctl( SID_INETVAL ), &r ); POINT p = { r.left, r.top }; ScreenToClient( hwnd, &p );
			drawGlyph( dc, inetGlyph, p.x - S( 20 ), p.y + S( 2 ), S( 14 ), roleColor( inetRole ) ); }
		if ( internet ) for ( int i = 0; i < nListRows; i++ ) {
			int gl = lstate[i] == 1 ? G_CHECK_CIRCLE : lstate[i] == 2 ? G_X_CIRCLE : lstate[i] == 3 ? G_EXCL_CIRCLE : G_DOTTED;
			COLORREF c = lstate[i] == 1 ? T.green : lstate[i] == 2 ? T.red : lstate[i] == 3 ? T.amber : T.text3;
			drawGlyph( dc, gl, S( 128 ), S( rowsY[i] ) + S( 2 ), S( 13 ), c );
		}
	}
};

void runServerWindow( lc_server_settings *settings, const str &basepath, const str &dataFolder, const std::vector<str> &clientArgs )
{
	registerConsole( );
	SrvWin w; w.settings = settings; w.basepath = basepath; w.dataFolder = dataFolder; w.clientArgs = clientArgs;
	w.cat = lc_map_catalog_get( dataFolder.c_str( ), G.homeBase.c_str( ) );
	w.internet = settings->internet != 0;
	if ( testOn( "VOYAGERSP_TEST_SERVER_INTERNET" ) ) { settings->internet = settings->openPort = 1; w.internet = true; }
	if ( const char *p = testEnv( "VOYAGERSP_TEST_SERVER_PORT" ) ) settings->port = atoi( p );
	w.port = settings->port;
	char lan[64] = ""; if ( lc_lan_address( lan, sizeof lan ) ) w.lanAddress = lan;
	w.nMasters = lc_masters_load( lc_store_shared( ), w.masters );
	const int W0 = 720;
	str title = fmt( "%s \xC2\xB7 Elite Force Server", settings->hostname );
	w.create( W( title ).c_str( ), W0, 900, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, 0, 0 );
	int y = 20;
	w.ids[SrvWin::SID_TITLE] = w.label( 92, y + 2, W0 - 92 - 28, 30, settings->hostname, 22, 700, R_TEXT, DT_SINGLELINE | DT_END_ELLIPSIS, SrvWin::SID_TITLE );
	w.label( 108, y + 36, W0 - 92 - 28, 18, "", 13, 400, R_TEXT2, DT_SINGLELINE, SrvWin::SID_STATE );
	y += 52 + 16 + 18;
	/* server group */
	int gTop = y; const int lx = 44, vx = 44 + 80 + 8;
	int gy = gTop + 12;
	w.label( lx, gy + 2, 80, 18, "Address:", 13, 400, R_TEXT, DT_RIGHT | DT_SINGLELINE );
	w.label( vx, gy, 300, 22, "", 15, 600, R_TEXT, DT_SINGLELINE, SrvWin::SID_ADDR );
	w.button( vx + 300, gy, 60, 22, "Copy", SrvWin::SID_COPY, false, 12 ); gy += 26;
	w.label( vx, gy, 520, 30, "", 11, 400, R_TEXT2, DT_WORDBREAK | DT_END_ELLIPSIS, SrvWin::SID_HINT ); gy += 32;
	if ( w.internet ) {
		w.label( lx, gy + 2, 80, 18, "Internet:", 13, 400, R_TEXT, DT_RIGHT | DT_SINGLELINE );
		w.label( vx + 20, gy, 300, 22, "", 13, 400, R_TEXT, DT_SINGLELINE, SrvWin::SID_INETVAL );
		w.button( vx + 330, gy, 60, 22, "Copy", SrvWin::SID_INETCOPY, false, 12 ); gy += 26;
		w.label( vx + 20, gy, 500, 44, "", 11, 400, R_TEXT2, DT_WORDBREAK, SrvWin::SID_INETHINT ); gy += 48;
		w.label( lx, gy + 2, 80, 18, "Listed:", 13, 400, R_TEXT, DT_RIGHT | DT_SINGLELINE );
		w.nListRows = w.nMasters;
		if ( w.nMasters == 0 ) { w.label( vx, gy, 400, 16, "No master servers (see Servers\xE2\x80\xA6 in the launcher)", 12, 400, R_ORANGE, DT_SINGLELINE ); gy += 20; }
		for ( int i = 0; i < w.nMasters; i++ ) {
			str lab = w.masterLabel( i ), host = w.masters[i];
			bool same = lab == host;
			w.rowsY[i] = gy;
			if ( !same ) { w.label( vx + 28, gy + 1, 90, 16, lab.c_str( ), 13, 400, R_TEXT, DT_SINGLELINE, SrvWin::SID_MASTERLBL + i ); w.label( vx + 28 + 92, gy + 2, 300, 16, host.c_str( ), 12, 400, R_TEXT2, DT_SINGLELINE ); }
			else w.label( vx + 28, gy + 1, 300, 16, host.c_str( ), 13, 400, R_TEXT, DT_SINGLELINE );
			gy += 20;
		}
		w.label( vx, gy + 2, 300, 16, "", 11, 400, R_TEXT2, DT_SINGLELINE, SrvWin::SID_LISTSTAT );
		w.button( vx + 300, gy, 100, 22, "Check Again", SrvWin::SID_LISTCHECK, false, 12 ); gy += 26;
		w.label( vx, gy, 520, 44, "", 11, 400, R_ORANGE, DT_WORDBREAK, SrvWin::SID_LISTHINT ); gy += 48;
	}
	w.label( lx, gy + 2, 80, 18, "Map:", 13, 400, R_TEXT, DT_RIGHT | DT_SINGLELINE ); w.label( vx, gy + 2, 520, 18, "", 13, 400, R_TEXT, DT_SINGLELINE | DT_END_ELLIPSIS, SrvWin::SID_MAP ); gy += 22;
	w.label( lx, gy + 2, 80, 18, "Game:", 13, 400, R_TEXT, DT_RIGHT | DT_SINGLELINE ); w.label( vx, gy + 2, 520, 18, "", 13, 400, R_TEXT, DT_SINGLELINE | DT_END_ELLIPSIS, SrvWin::SID_GAME ); gy += 22;
	w.label( lx, gy + 2, 80, 18, "Players:", 13, 400, R_TEXT, DT_RIGHT | DT_SINGLELINE ); w.label( vx, gy + 2, 520, 18, "", 13, 400, R_TEXT, DT_SINGLELINE | DT_END_ELLIPSIS, SrvWin::SID_PLAYERS ); gy += 22;
	gy += 10;
	w.addGroup( 28, gTop, W0 - 56, gy - gTop, "SERVER" );
	y = gy + 16;
	/* controls */
	int bw = ( W0 - 56 - 16 ) / 3;
	struct { int id; const char *t, *tip; } bt[6] = {
		{ SrvWin::SID_NEXT, "Next Map", "Go to the next map in the rotation" }, { SrvWin::SID_RESTART, "Restart Map", "Restart the current map; scores start over" },
		{ SrvWin::SID_CHANGE, "Change Map \xE2\x96\xBE", "Switch to another map now" }, { SrvWin::SID_ADDBOT, "Add Bot", "Add one bot at the skill set in Config\xE2\x80\xA6" },
		{ SrvWin::SID_RMBOTS, "Remove Bots", "Kick all bots (and stop filling the game with bots)" }, { SrvWin::SID_MSG, "Message\xE2\x80\xA6", "Send a message to everyone on the server" } };
	for ( int i = 0; i < 6; i++ ) { w.button( 28 + ( i % 3 ) * ( bw + 8 ), y + ( i / 3 ) * 36, bw, 28, bt[i].t, bt[i].id, false, 13 ); setTooltip( w.hwnd, w.ctl( bt[i].id ), bt[i].tip ); }
	y += 72 + 10;
	/* players + console */
	int areaH = 200;
	w.label( 28, y, 120, 14, "PLAYERS", 11, 600, R_TEXT2, DT_SINGLELINE ); w.button( 28 + 230 - 60, y - 4, 60, 22, "Kick", SrvWin::SID_KICK, false, 12 );
	setTooltip( w.hwnd, w.ctl( SrvWin::SID_KICK ), "Kick the selected player or bot" );
	w.label( 28 + 230 + 14, y, 120, 14, "CONSOLE", 11, 600, R_TEXT2, DT_SINGLELINE );
	y += 20;
	w.table = w.ctl_( WC_LISTVIEWW, L"", LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER | WS_TABSTOP | WS_BORDER, 0, 28, y, 230, areaH, SrvWin::SID_TABLE );
	ListView_SetBkColor( w.table, T.field ); w.ids[SrvWin::SID_TABLE] = w.table;
	{ static const char *pt[3] = { "Player", "Score", "Ping" }; static const int pw[3] = { 112, 54, 46 };
	  for ( int i = 0; i < 3; i++ ) { ws t = W( pt[i] ); LVCOLUMNW c = {}; c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT; c.fmt = i ? LVCFMT_RIGHT : LVCFMT_LEFT; c.cx = S( pw[i] ); c.pszText = (LPWSTR)t.c_str( ); SendMessageW( w.table, LVM_INSERTCOLUMNW, i, (LPARAM)&c ); } }
	w.con = w.ctl_( L"EFConsole", L"", WS_VSCROLL | WS_BORDER, 0, 28 + 230 + 14, y, W0 - 56 - 230 - 14, areaH, 60 ); w.ids[60] = w.con;
	conMeasure( w.con, (ConData *)GetWindowLongPtrW( w.con, GWLP_USERDATA ) );
	y += areaH + 10;
	HWND cmd = w.edit( 28, y, W0 - 56 - 8 - 70, 24, SrvWin::SID_CMD, "" );
	SendMessageW( cmd, EM_SETCUEBANNER, TRUE, (LPARAM)L"Console command, such as say Hello, map hm_borg1 or kick <name>" );
	w.button( W0 - 28 - 70, y - 1, 70, 26, "Send", SrvWin::SID_SEND, false, 13 );
	y += 24 + 18;
	str pt = fmt( "Play on This %s", PLATFORM_NOUN );
	w.button( 28, y, 190, 32, ( "\xE2\x96\xB6  " + pt ).c_str( ), SrvWin::SID_PLAY, false, 13 ); setTooltip( w.hwnd, w.ctl( SrvWin::SID_PLAY ), "Start Elite Force in Holomatch and join this server" );
	w.button( W0 - 28 - 120, y, 120, 32, "Stop Server", SrvWin::SID_STOP, true, 13 );
	y += 32 + 22;
	/* resize to fit */
	{ RECT r = { 0, 0, S( W0 ), S( y ) }; AdjustWindowRectEx( &r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0 ); SetWindowPos( w.hwnd, 0, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER ); }
	centerOn( w.hwnd, 0 );
	lc_srv_callbacks cb = {}; cb.send = SrvWin::cbSend; cb.log = SrvWin::cbLog; cb.state = SrvWin::cbState; cb.players = SrvWin::cbPlayers; cb.info = SrvWin::cbInfo; cb.ready = SrvWin::cbReady; cb.user = &w;
	w.srv = lc_srv_new( &cb, settings->port );
	lc_running_register( G.homeRoot.c_str( ), settings->hostname, settings->port );
	w.showInfo( ); w.showState( ); w.showListing( ); w.showInternet( );
	if ( const char *p = testEnv( "VOYAGERSP_TEST_SERVER_SNAPSHOT" ) ) { w.snapOut = p; const char *a = testEnv( "VOYAGERSP_TEST_SERVER_SNAPSHOT_AFTER" ); SetTimer( w.hwnd, 20, (UINT)( ( a ? atof( a ) : 15.0 ) * 1000 ), 0 ); }
	if ( const char *p = testEnv( "VOYAGERSP_TEST_SERVER_PLAY_AFTER" ) ) SetTimer( w.hwnd, 21, (UINT)( atof( p ) * 1000 ), 0 );
	if ( const char *p = testEnv( "VOYAGERSP_TEST_SERVER_STOP_AFTER" ) ) SetTimer( w.hwnd, 22, (UINT)( atof( p ) * 1000 ), 0 );
	/* the window is modal-less: its own loop, ended by stopThen */
	PostMessageW( w.hwnd, WM_APP + 99, 0, 0 );
	w.start( );
	w.owner = 0;
	w.runModal( );
	lc_running_unregister( G.homeRoot.c_str( ) );
	lc_log( "Server: window closed" );
	if ( w.proc ) lc_proc_free( w.proc );
	lc_srv_free( w.srv );
}

}	// namespace wl
