/*
===========================================================================
Windows launcher: the Server Settings sheet ("Config...", servers.md 4).

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#include "wl_app.h"
#include <commctrl.h>

namespace wl {

struct CfgDlg : Wnd {
	lc_server_settings *orig, ed;
	str dataFolder;
	const lc_map_catalog *cat = 0;
	std::vector<const lc_map_info *> shown;			/* the filtered map list */
	std::vector<HWND> page[4];
	int curPage = 0;
	str search;
	std::map<str, HBITMAP> thumbs;
	HWND seg = 0, mapList = 0, rotList = 0;
	/* drag and drop */
	int dragSrc = -1; bool dragging = false; POINT dragStart = {}; std::vector<int> dragRows; int dropAt = -1;

	enum { ID_SEG = 10, ID_SEARCH = 100, ID_MAPS, ID_ADDROT, ID_ROT, ID_REMOVE, ID_UP, ID_DOWN, ID_MAPCAP, ID_ROTCAP,
		ID_GAMETYPE = 120, ID_GTHINT, ID_MOD0 = 122 /* 122..126 */, ID_MODHINT0 = 140 /* ..144 */,
		ID_FRAG = 130, ID_CAP = 132, ID_TIME = 134, ID_FF = 136, ID_BOTS = 137, ID_SKILL = 139, ID_BOTHINT = 145,
		ID_NAME = 150, ID_PW = 151, ID_MAXP = 152, ID_LOCAL = 154, ID_INTERNET = 155, ID_OPENPORT = 156, ID_PORT = 157,
		ID_NETHINT = 158, ID_DL = 159, ID_PURE = 160, ID_TICK = 161,
		ID_CMOD = 170, ID_CMODHINT = 171, ID_UNLAG = 172, ID_SMOOTH = 173, ID_PREDICT = 174, ID_RESTORE = 180 };

	/* ---- helpers ---- */
	HWND reg( int pg, HWND h ) { page[pg].push_back( h ); return h; }
	HWND numField( int pg, int x, int y, int w, int id, int lo, int hi, bool stepper, const char *unit )
	{
		HWND e = reg( pg, edit( x, y, w, 22, id, "", ES_RIGHT | ES_NUMBER ) );
		SendMessageW( e, EM_LIMITTEXT, 5, 0 );
		if ( stepper ) {
			HWND u = ctl_( UPDOWN_CLASSW, L"", UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_NOTHOUSANDS | UDS_AUTOBUDDY, 0, 0, 0, 0, 0, id + 1 );
			SendMessageW( u, UDM_SETBUDDY, (WPARAM)e, 0 );
			SendMessageW( u, UDM_SETRANGE32, lo, hi );
			reg( pg, u );
		}
		if ( unit && *unit ) reg( pg, label( x + w + ( stepper ? 4 : 4 ), y + 3, 120, 16, unit, 13, 400, R_TEXT2, DT_SINGLELINE ) );
		return e;
	}
	void setNum( int id, int v ) { HWND e = ctl( id ); wchar_t b[16]; _snwprintf( b, 16, L"%d", v ); SetWindowTextW( e, b ); }
	int *numTarget( int id )
	{
		switch ( id ) { case ID_FRAG: return &ed.fraglimit; case ID_CAP: return &ed.capturelimit; case ID_TIME: return &ed.timelimit;
		case ID_BOTS: return &ed.bots; case ID_MAXP: return &ed.maxPlayers; case ID_PORT: return &ed.port; }
		return 0;
	}
	void numRange( int id, int *lo, int *hi )
	{
		switch ( id ) { case ID_FRAG: case ID_TIME: *lo = 0; *hi = 999; break; case ID_CAP: *lo = 0; *hi = 99; break; case ID_BOTS: *lo = 0; *hi = 31; break;
		case ID_MAXP: *lo = 2; *hi = 32; break; default: *lo = 1024; *hi = 65535; }
	}
	bool inRefresh = false;

	const lc_map_info *findMap( const char *name ) { return cat ? lc_map_find( cat, name ) : 0; }
	str mapTitle( const char *name ) { const lc_map_info *m = findMap( name ); return m ? lc_map_display_name( m ) : name; }

	/* ---- building ---- */
	void build( )
	{
		const int W0 = 700;
		seg = mkSeg( hwnd, ( W0 - 400 ) / 2, 18, 400, 26, ID_SEG ); ids[ID_SEG] = seg;
		segSetItems( seg, { "Maps", "Game", "Server", "cMod" } );
		addGroup( 20, 56, W0 - 40, 480, "", 10 );
		buildMaps( ); buildGame( ); buildServer( ); buildCmod( );
		button( 20, 550, 140, 28, "Restore Defaults", ID_RESTORE, false, 12 );
		button( W0 - 20 - 84 - 8 - 84, 550, 84, 28, "Cancel", IDCANCEL );
		button( W0 - 20 - 84, 550, 84, 28, "Done", IDOK, true );
	}

	HWND mkList( int pg, int x, int y, int w, int h, int id )
	{
		HWND l = reg( pg, ctl_( L"LISTBOX", L"", LBS_OWNERDRAWFIXED | LBS_EXTENDEDSEL | LBS_NOTIFY | LBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, x, y, w, h, id ) );
		SendMessageW( l, LB_SETITEMHEIGHT, 0, S( 50 ) );
		return l;
	}

	void buildMaps( )
	{
		int cw = 304, lx = 36, rx = 36 + cw + 20, y = 56 + 14;
		reg( 0, label( lx, y, cw, 14, "", 11, 600, R_TEXT2, DT_SINGLELINE, ID_MAPCAP ) );
		HWND se = reg( 0, edit( lx, y + 18, cw, 22, ID_SEARCH, "" ) );
		SendMessageW( se, EM_SETCUEBANNER, TRUE, (LPARAM)L"Search maps" );
		mapList = mkList( 0, lx, y + 46, cw, 480 - 14 - 14 - 46 - 34, ID_MAPS );
		reg( 0, button( lx, y + 46 + 480 - 14 - 14 - 46 - 34 + 6, 150, 26, "+  Add to Rotation", ID_ADDROT, false, 12 ) );
		reg( 0, label( rx, y, cw, 14, "", 11, 600, R_TEXT2, DT_SINGLELINE, ID_ROTCAP ) );
		reg( 0, label( rx, y + 18, cw, 28, "The server plays these in order, then starts over.", 11, 400, R_TEXT2 ) );
		rotList = mkList( 0, rx, y + 46, cw, 480 - 14 - 14 - 46 - 34, ID_ROT );
		int by = y + 46 + 480 - 14 - 14 - 46 - 34 + 6;
		reg( 0, mkIconButton( hwnd, rx, by, 30, 26, ID_REMOVE, G_MINUS, "Remove from the rotation" ) ); ids[ID_REMOVE] = GetDlgItem( hwnd, ID_REMOVE );
		reg( 0, mkIconButton( hwnd, rx + 34, by, 30, 26, ID_UP, G_UP, "Play earlier" ) ); ids[ID_UP] = GetDlgItem( hwnd, ID_UP );
		reg( 0, mkIconButton( hwnd, rx + 68, by, 30, 26, ID_DOWN, G_DOWN, "Play later" ) ); ids[ID_DOWN] = GetDlgItem( hwnd, ID_DOWN );
		reg( 0, label( rx + 110, by + 5, cw - 110, 16, "Drag to reorder", 11, 400, R_TEXT3, DT_SINGLELINE | DT_RIGHT ) );
		ids[ID_MAPS] = mapList; ids[ID_ROT] = rotList;
	}

	void buildGame( )
	{
		int lx = 36, lw = 112, cx = lx + lw + 10, y = 56 + 18;
		auto lab = [&]( int yy, const char *t ) { reg( 1, label( lx, yy + 3, lw, 18, t, 13, 400, R_TEXT, DT_RIGHT | DT_SINGLELINE ) ); };
		lab( y, "Game type:" );
		HWND c = reg( 1, combo( cx, y, 220, ID_GAMETYPE ) );
		for ( int g : { 0, 1, 3, 4 } ) SendMessageW( c, CB_ADDSTRING, 0, (LPARAM)W( lc_gametype_name( g ) ).c_str( ) );
		y += 26;
		reg( 1, label( cx, y, 420, 30, "", 11, 400, R_TEXT2, DT_WORDBREAK, ID_GTHINT ) ); y += 32;
		lab( y + 4, "Modifiers:" );
		const char *hints[5] = { "Killed players join the enemy team, the Borg. Team Holomatch only.", "Players choose a class, each with its own weapons and abilities.",
			"One direct hit causes instant death.", "Last one standing wins. Not with Capture the Flag.", "One random player gets all the weapons and armor. Free For All and Tournament only." };
		for ( int m = 0; m < LC_MOD_COUNT; m++ ) {
			HWND k = reg( 1, check( cx, y + 2, 130, 20, lc_modifier_name( m ), ID_MOD0 + m ) );
			setTooltip( hwnd, k, hints[m] );
			reg( 1, label( cx + 138, y + 4, 370, 16, hints[m], 11, 400, R_TEXT2, DT_SINGLELINE | DT_END_ELLIPSIS, ID_MODHINT0 + m ) );
			y += 24;
		}
		reg( 1, label( cx, y + 2, 420, 16, "Some modifiers exclude others, as in the game's own Create Server menu.", 11, 400, R_TEXT2, DT_SINGLELINE ) ); y += 28;
		lab( y, "Frag limit:" ); numField( 1, cx, y, 66, ID_FRAG, 0, 999, true, "points" ); y += 28;
		lab( y, "Capture limit:" ); numField( 1, cx, y, 66, ID_CAP, 0, 99, true, "flags" ); y += 28;
		lab( y, "Time limit:" ); numField( 1, cx, y, 66, ID_TIME, 0, 999, true, "minutes" ); y += 28;
		reg( 1, check( cx, y, 420, 20, "Friendly fire: teammates can hurt each other", ID_FF ) ); y += 32;
		lab( y, "Bots:" ); numField( 1, cx, y, 66, ID_BOTS, 0, 31, true, "" );
		HWND sk = reg( 1, combo( cx + 66 + 14, y, 150, ID_SKILL ) );
		for ( int i = 1; i <= 5; i++ ) SendMessageW( sk, CB_ADDSTRING, 0, (LPARAM)W( fmt( "%d \xC2\xB7 %s", i, lc_skill_name( i ) ) ).c_str( ) );
		y += 28;
		reg( 1, label( cx, y, 420, 30, "", 11, 400, R_TEXT2, DT_WORDBREAK, ID_BOTHINT ) );
	}

	void buildServer( )
	{
		int lx = 36, lw = 112, cx = lx + lw + 10, y = 56 + 18;
		auto lab = [&]( int yy, const char *t ) { reg( 2, label( lx, yy + 3, lw, 18, t, 13, 400, R_TEXT, DT_RIGHT | DT_SINGLELINE ) ); };
		lab( y, "Server name:" ); reg( 2, edit( cx, y, 300, 22, ID_NAME, "" ) ); y += 28;
		lab( y, "Password:" ); HWND pw = reg( 2, edit( cx, y, 200, 22, ID_PW, "" ) ); SendMessageW( pw, EM_SETCUEBANNER, TRUE, (LPARAM)L"None" ); y += 26;
		reg( 2, label( cx, y, 420, 16, "Players type it in Holomatch's Join menu. Leave it empty for an open server.", 11, 400, R_TEXT2, DT_SINGLELINE ) ); y += 28;
		lab( y, "Max players:" ); numField( 2, cx, y, 66, ID_MAXP, 2, 32, true, "including bots" ); y += 32;
		lab( y, "Network:" ); reg( 2, radio( cx, y, 300, 20, "Local network only", ID_LOCAL, true ) ); y += 24;
		reg( 2, radio( cx, y, 400, 20, "Internet: also list it on the master server", ID_INTERNET ) ); y += 24;
		HWND op = reg( 2, check( cx + 18, y, 400, 20, "Open the port on my router automatically", ID_OPENPORT ) );
		setTooltip( hwnd, op, "Asks your router (NAT-PMP / UPnP) to forward the port while the server runs, so you don't have to set up port forwarding." ); y += 30;
		lab( y, "Port:" ); numField( 2, cx, y, 64, ID_PORT, 1024, 65535, false, "" );
		reg( 2, label( cx + 64 + 8, y + 4, 330, 16, "UDP \xC2\xB7 27960\xE2\x80\x93" "27963 show up in the Join menu", 11, 400, R_TEXT2, DT_SINGLELINE ) ); y += 28;
		reg( 2, label( cx, y, 420, 30, "", 11, 400, R_TEXT2, DT_WORDBREAK, ID_NETHINT ) ); y += 36;
		lab( y, "Downloads:" ); reg( 2, check( cx, y, 400, 20, "Let players download maps they don't have", ID_DL ) ); y += 24;
		reg( 2, label( cx + 18, y, 400, 16, "Add-on maps only: the retail and Expansion Pack files are never sent.", 11, 400, R_TEXT2, DT_SINGLELINE ) ); y += 28;
		lab( y, "Files:" ); reg( 2, check( cx, y, 420, 20, "Pure server: players must use the same game files", ID_PURE ) ); y += 32;
		lab( y, "Tick Rate:" ); HWND tk = reg( 2, combo( cx, y, 160, ID_TICK ) );
		SendMessageW( tk, CB_ADDSTRING, 0, (LPARAM)L"Standard (20)" ); SendMessageW( tk, CB_ADDSTRING, 0, (LPARAM)L"High (40)" );
		setTooltip( hwnd, tk, "How many times a second the server updates the game (sv_fps). High uses more upload bandwidth." ); y += 26;
		reg( 2, label( cx, y, 420, 16, "Updates per second. High uses more upload bandwidth.", 11, 400, R_TEXT2, DT_SINGLELINE ) );
	}

	void buildCmod( )
	{
		int lx = 36, lw = 128, cx = lx + lw + 10, y = 56 + 18;
		reg( 3, label( lx, y + 2, lw, 18, "Game code:", 13, 400, R_TEXT, DT_RIGHT | DT_SINGLELINE ) );
		HWND c = reg( 3, check( cx, y, 400, 20, "Run cMod's game code", ID_CMOD ) );
		setTooltip( hwnd, c, "cMod's server game module (qagame) by Noah Metzger, in place of the original one (sv_cmodGame)." ); y += 26;
		reg( 3, label( cx, y, 420, 32, "", 11, 400, R_TEXT2, DT_WORDBREAK, ID_CMODHINT ) ); y += 44;
		reg( 3, label( lx - 20, y + 3, lw + 20, 18, "Lag compensation:", 13, 400, R_TEXT, DT_RIGHT | DT_SINGLELINE ) );
		HWND u = reg( 3, combo( cx, y, 220, ID_UNLAG ) );
		for ( const wchar_t *s : { L"Off", L"Instant-hit weapons", L"All weapons" } ) SendMessageW( u, CB_ADDSTRING, 0, (LPARAM)s );
		setTooltip( hwnd, u, "g_unlagged: the server checks each shot against where the shooter saw the other players." ); y += 28;
		reg( 3, label( cx, y, 420, 62, "Shots hit what the shooter saw on screen, so players with a high ping can aim normally. Works for every player, whatever game they run. Instant-hit weapons is the common choice; All weapons also covers projectiles.", 11, 400, R_TEXT2 ) ); y += 68;
		HWND s = reg( 3, check( cx, y, 420, 20, "Smooth other players' movement", ID_SMOOTH ) ); setTooltip( hwnd, s, "g_smoothClients" ); y += 26;
		HWND p = reg( 3, check( cx, y, 420, 20, "Let cMod players see their own shots at once", ID_PREDICT ) );
		setTooltip( hwnd, p, "g_unlaggedPredict: players with cMod's cgame (this app's Holomatch, cMod) predict their weapon fire." ); y += 36;
		reg( 3, label( cx, y, 420, 32, "cMod is by Noah Metzger (Chomenor). Players never download it from your server.", 11, 400, R_TEXT2 ) );
	}

	/* ---- showing state ---- */
	void showPage( int p )
	{
		curPage = p;
		for ( int i = 0; i < 4; i++ ) for ( HWND h : page[i] ) ShowWindow( h, i == p ? SW_SHOWNA : SW_HIDE );
		SendMessageW( seg, SEG_SETSEL, p, 0 );
		InvalidateRect( hwnd, 0, TRUE );
	}

	void reloadMaps( )
	{
		shown.clear( );
		int total = cat ? lc_map_catalog_count( cat ) : 0;
		std::wstring q = W( search );
		SendMessageW( mapList, WM_SETREDRAW, FALSE, 0 );
		SendMessageW( mapList, LB_RESETCONTENT, 0, 0 );
		for ( int i = 0; i < total; i++ ) {
			const lc_map_info *m = lc_map_catalog_at( cat, i );
			if ( !q.empty( ) ) {
				ws a = W( lc_map_display_name( m ) ), b = W( m->name ), qq = q;
				auto low = []( ws s ) { for ( auto &c : s ) c = (wchar_t)towlower( c ); return s; };
				if ( low( a ).find( low( qq ) ) == ws::npos && low( b ).find( low( qq ) ) == ws::npos ) continue;
			}
			shown.push_back( m );
			SendMessageW( mapList, LB_ADDSTRING, 0, (LPARAM)L"" );
		}
		SendMessageW( mapList, WM_SETREDRAW, TRUE, 0 );
		InvalidateRect( mapList, 0, TRUE );
		setText( ctl( ID_MAPCAP ), q.empty( ) ? fmt( "MAPS \xC2\xB7 %d", total ) : fmt( "MAPS \xC2\xB7 %d OF %d", (int)shown.size( ), total ) );
		EnableWindow( ctl( ID_ADDROT ), SendMessageW( mapList, LB_GETSELCOUNT, 0, 0 ) > 0 );
	}
	void reloadRotation( const std::vector<int> &selRows = {} )
	{
		SendMessageW( rotList, WM_SETREDRAW, FALSE, 0 );
		SendMessageW( rotList, LB_RESETCONTENT, 0, 0 );
		for ( int i = 0; i < ed.rotationCount; i++ ) SendMessageW( rotList, LB_ADDSTRING, 0, (LPARAM)L"" );
		for ( int r : selRows ) SendMessageW( rotList, LB_SETSEL, TRUE, r );
		SendMessageW( rotList, WM_SETREDRAW, TRUE, 0 );
		InvalidateRect( rotList, 0, TRUE );
		setText( ctl( ID_ROTCAP ), ed.rotationCount == 1 ? "ROTATION \xC2\xB7 1 MAP" : fmt( "ROTATION \xC2\xB7 %d MAPS", ed.rotationCount ) );
		rotButtons( );
		showGameHint( );
	}
	std::vector<int> selOf( HWND l )
	{
		std::vector<int> r; int n = (int)SendMessageW( l, LB_GETSELCOUNT, 0, 0 );
		if ( n <= 0 ) return r;
		r.resize( n ); SendMessageW( l, LB_GETSELITEMS, n, (LPARAM)r.data( ) );
		return r;
	}
	void rotButtons( )
	{
		auto s = selOf( rotList );
		EnableWindow( ctl( ID_REMOVE ), !s.empty( ) );
		EnableWindow( ctl( ID_UP ), !s.empty( ) && s.front( ) > 0 );
		EnableWindow( ctl( ID_DOWN ), !s.empty( ) && s.back( ) < ed.rotationCount - 1 );
		EnableWindow( ctl( ID_ADDROT ), SendMessageW( mapList, LB_GETSELCOUNT, 0, 0 ) > 0 );
	}

	void showGameHint( )
	{
		int total = 0, made = 0;
		for ( int i = 0; i < ed.rotationCount; i++ ) { const lc_map_info *m = findMap( ed.rotation[i] ); if ( !m ) continue; total++; if ( lc_map_supports_gametype( m, ed.gametype ) ) made++; }
		str t; int role = R_TEXT2;
		if ( total == 0 ) { t = "Add maps to the rotation on the Maps tab."; role = R_ORANGE; }
		else if ( made == total ) t = total == 1 ? "The map in the rotation is made for it." : fmt( "All %d maps in the rotation are made for it.", total );
		else { t = fmt( "%d of the %d maps in the rotation are made for it; the others may lack spawn points or flags.", made, total ); role = R_ORANGE; }
		setText( ctl( ID_GTHINT ), t ); setRole( ctl( ID_GTHINT ), role );
	}

	void showLimits( )
	{
		EnableWindow( ctl( ID_FRAG ), ed.gametype != 4 ); EnableWindow( ctl( ID_FRAG + 1 ), ed.gametype != 4 );
		EnableWindow( ctl( ID_CAP ), ed.gametype == 4 ); EnableWindow( ctl( ID_CAP + 1 ), ed.gametype == 4 );
		EnableWindow( ctl( ID_FF ), ed.gametype >= 3 );
		EnableWindow( ctl( ID_SKILL ), ed.bots > 0 );
		str h = ed.bots == 0 ? "No bots." : ed.gametype >= 3 ? fmt( "Bots keep each team at %d players; they leave as people join.", ed.bots ) : fmt( "Bots keep the game at %d players; they leave as people join.", ed.bots );
		setText( ctl( ID_BOTHINT ), h );
	}

	void showServerTab( )
	{
		SendMessageW( ctl( ID_LOCAL ), BM_SETCHECK, ed.internet ? BST_UNCHECKED : BST_CHECKED, 0 );
		SendMessageW( ctl( ID_INTERNET ), BM_SETCHECK, ed.internet ? BST_CHECKED : BST_UNCHECKED, 0 );
		SendMessageW( ctl( ID_OPENPORT ), BM_SETCHECK, ed.openPort ? BST_CHECKED : BST_UNCHECKED, 0 );
		EnableWindow( ctl( ID_OPENPORT ), ed.internet );
		bool disc = ed.port >= 27960 && ed.port <= 27963; str h; int role = R_TEXT2;
		if ( !disc ) { h = "Outside 27960\xE2\x80\x93" "27963, Holomatch's Join menu won't find the server on your network: players have to connect by address."; role = R_ORANGE; }
		else if ( ed.internet && ed.openPort ) h = fmt( "The server window shows your public address once the router has opened UDP port %d. If it can't, forward that port to this %s by hand.", ed.port, PLATFORM_NOUN );
		else if ( ed.internet ) h = fmt( "Players outside your network need UDP port %d forwarded to this %s on your router.", ed.port, PLATFORM_NOUN );
		else h = "Players on your network find it in Holomatch's Join menu.";
		setText( ctl( ID_NETHINT ), h ); setRole( ctl( ID_NETHINT ), role );
	}

	void showCmod( )
	{
		bool have = cmodAvailable( ), on = have && ed.cmodGame;
		SendMessageW( ctl( ID_CMOD ), BM_SETCHECK, ed.cmodGame ? BST_CHECKED : BST_UNCHECKED, 0 );
		EnableWindow( ctl( ID_CMOD ), have );
		setText( ctl( ID_CMODHINT ), have ? "cMod's version of the Holomatch game: the original rules and modes, many bug fixes, and lag compensation." : "cMod's game modules aren't in this copy of the app." );
		setRole( ctl( ID_CMODHINT ), have ? R_TEXT2 : R_ORANGE );
		SendMessageW( ctl( ID_UNLAG ), CB_SETCURSEL, ed.unlagged, 0 );
		SendMessageW( ctl( ID_SMOOTH ), BM_SETCHECK, ed.smoothClients ? BST_CHECKED : BST_UNCHECKED, 0 );
		SendMessageW( ctl( ID_PREDICT ), BM_SETCHECK, ed.unlaggedPredict ? BST_CHECKED : BST_UNCHECKED, 0 );
		EnableWindow( ctl( ID_UNLAG ), on ); EnableWindow( ctl( ID_SMOOTH ), on ); EnableWindow( ctl( ID_PREDICT ), on && ed.unlagged > 0 );
	}
	static bool cmodAvailable( ) { lc_cmod c; memset( &c, 0, sizeof c ); char p[LC_MAX_PATH]; return lc_cmod_pak_path( p, sizeof p ) != 0; }

	void showAll( )
	{
		inRefresh = true;
		SendMessageW( ctl( ID_GAMETYPE ), CB_SETCURSEL, ed.gametype == 1 ? 1 : ed.gametype == 3 ? 2 : ed.gametype == 4 ? 3 : 0, 0 );
		for ( int m = 0; m < LC_MOD_COUNT; m++ ) {
			SendMessageW( ctl( ID_MOD0 + m ), BM_SETCHECK, ed.mods[m] ? BST_CHECKED : BST_UNCHECKED, 0 );
			EnableWindow( ctl( ID_MOD0 + m ), lc_server_settings_modifier_allowed( &ed, m ) );
		}
		setNum( ID_FRAG, ed.fraglimit ); setNum( ID_CAP, ed.capturelimit ); setNum( ID_TIME, ed.timelimit ); setNum( ID_BOTS, ed.bots );
		setNum( ID_MAXP, ed.maxPlayers ); setNum( ID_PORT, ed.port );
		SendMessageW( ctl( ID_FF ), BM_SETCHECK, ed.friendlyFire ? BST_CHECKED : BST_UNCHECKED, 0 );
		SendMessageW( ctl( ID_SKILL ), CB_SETCURSEL, max( 1, min( 5, ed.botSkill ) ) - 1, 0 );
		SetWindowTextW( ctl( ID_NAME ), W( ed.hostname ).c_str( ) ); SetWindowTextW( ctl( ID_PW ), W( ed.password ).c_str( ) );
		SendMessageW( ctl( ID_DL ), BM_SETCHECK, ed.allowDownload ? BST_CHECKED : BST_UNCHECKED, 0 );
		SendMessageW( ctl( ID_PURE ), BM_SETCHECK, ed.pure ? BST_CHECKED : BST_UNCHECKED, 0 );
		SendMessageW( ctl( ID_TICK ), CB_SETCURSEL, ed.tickRate == 40 ? 1 : 0, 0 );
		inRefresh = false;
		showLimits( ); showServerTab( ); showCmod( );
		search.clear( ); SetWindowTextW( ctl( ID_SEARCH ), L"" );
		reloadMaps( ); reloadRotation( );
		showPage( 0 );
	}

	/* ---- map cells ---- */
	HBITMAP thumb( const lc_map_info *m )
	{
		str key = m->name; auto i = thumbs.find( key );
		if ( i != thumbs.end( ) ) return i->second;
		HBITMAP out = 0;
		size_t len = 0; int tga = 0;
		unsigned char *b = lc_map_levelshot_bytes( m, &len, &tga );
		if ( b ) {
			int w = 0, h = 0; HBITMAP src = decodeImage( b, len, tga != 0, &w, &h );
			free( b );
			if ( src ) { out = scaledBitmap( src, S( 56 ), S( 42 ) ); DeleteObject( src ); }
		}
		thumbs[key] = out;
		return out;
	}
	void drawCell( DRAWITEMSTRUCT *d, const lc_map_info *m, const char *rawName, int index )
	{
		HDC dc = d->hDC; RECT r = d->rcItem;
		bool sel = ( d->itemState & ODS_SELECTED ) != 0;
		HBRUSH b = CreateSolidBrush( sel ? blend( T.field, T.accent, 22 ) : T.field ); FillRect( dc, &r, b ); DeleteObject( b );
		int tx = r.left + S( 4 ), ty = r.top + ( r.bottom - r.top - S( 42 ) ) / 2;
		HBITMAP th = m ? thumb( m ) : 0;
		if ( th ) {
			HDC md = CreateCompatibleDC( dc ); HGDIOBJ o = SelectObject( md, th );
			BitBlt( dc, tx, ty, S( 56 ), S( 42 ), md, 0, 0, SRCCOPY ); SelectObject( md, o ); DeleteDC( md );
		} else {
			RECT pr = { tx, ty, tx + S( 56 ), ty + S( 42 ) };
			fillRoundRect( dc, pr, S( 4 ), blend( T.field, T.text, 8 ), CLR_NONE, 0 );
			drawGlyph( dc, m ? G_MAP : G_QUESTION, tx + S( 19 ), ty + S( 12 ), S( 18 ), T.text3 );
		}
		int x = tx + S( 56 + 9 ), mid = ( r.top + r.bottom ) / 2;
		SetBkMode( dc, TRANSPARENT );
		str title = m ? lc_map_display_name( m ) : rawName;
		if ( index >= 0 ) title = fmt( "%d.  %s", index + 1, title.c_str( ) );
		SelectObject( dc, F( 13, 500 ) ); SetTextColor( dc, T.text );
		RECT tr = { x, mid - S( 18 ), r.right - S( 6 ), mid }; DrawTextW( dc, W( title ).c_str( ), -1, &tr, DT_SINGLELINE | DT_BOTTOM | DT_END_ELLIPSIS | DT_NOPREFIX );
		/* detail parts */
		struct Part { str t; int role; bool bold; }; std::vector<Part> parts;
		if ( !m ) { parts.push_back( { rawName, R_TEXT2, false } ); parts.push_back( { "Missing from your game files", R_RED, true } ); }
		else {
			parts.push_back( { m->name, R_TEXT2, false } );
			if ( m->expansion ) parts.push_back( { "Expansion Pack", R_PURPLE, true } ); else if ( !m->retail ) parts.push_back( { "Add-on", R_BLUE, true } );
			if ( !lc_map_supports_gametype( m, ed.gametype ) ) parts.push_back( { fmt( "Not made for %s", lc_gametype_name( ed.gametype ) ), R_ORANGE, true } );
			else { char td[128]; lc_map_types_description( m, td, sizeof td ); if ( td[0] ) parts.push_back( { td, R_TEXT2, false } ); }
		}
		int px = x;
		for ( size_t i = 0; i < parts.size( ); i++ ) {
			str t = ( i ? " \xC2\xB7 " : "" ) + parts[i].t;
			SelectObject( dc, F( 11, parts[i].bold ? 600 : 400 ) );
			SetTextColor( dc, i && parts[i].role == R_TEXT2 ? T.text2 : roleColor( parts[i].role ) );
			ws w = W( t ); SIZE sz; GetTextExtentPoint32W( dc, w.c_str( ), (int)w.size( ), &sz );
			RECT pr = { px, mid + S( 1 ), r.right - S( 6 ), mid + S( 17 ) };
			DrawTextW( dc, w.c_str( ), -1, &pr, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX );
			px += sz.cx; if ( px > r.right ) break;
		}
		if ( d->itemState & ODS_FOCUS ) {}
	}

	LRESULT onOther( UINT m, WPARAM w, LPARAM l, bool *handled ) override
	{
		*handled = true;
		if ( m == WM_MEASUREITEM ) { ( (MEASUREITEMSTRUCT *)l )->itemHeight = S( 50 ); return TRUE; }
		if ( m == WM_DRAWITEM ) {
			DRAWITEMSTRUCT *d = (DRAWITEMSTRUCT *)l;
			if ( d->itemID == (UINT)-1 ) return TRUE;
			if ( d->CtlID == ID_MAPS ) { if ( d->itemID < shown.size( ) ) drawCell( d, shown[d->itemID], shown[d->itemID]->name, -1 ); }
			else if ( d->CtlID == ID_ROT ) { if ( (int)d->itemID < ed.rotationCount ) drawCell( d, findMap( ed.rotation[d->itemID] ), ed.rotation[d->itemID], (int)d->itemID ); }
			return TRUE;
		}
		*handled = false; return 0;
	}

	/* ---- editing ---- */
	void addMaps( const std::vector<str> &names, int at = -1 )
	{
		if ( at < 0 || at > ed.rotationCount ) at = ed.rotationCount;
		int n = (int)names.size( ), room = 64 - ed.rotationCount; if ( n > room ) n = room;
		if ( n <= 0 ) return;
		memmove( ed.rotation[at + n], ed.rotation[at], (size_t)( ed.rotationCount - at ) * 64 );
		for ( int i = 0; i < n; i++ ) lc_strlcpy( ed.rotation[at + i], names[i].c_str( ), 64 );
		ed.rotationCount += n;
		std::vector<int> s; for ( int i = 0; i < n; i++ ) s.push_back( at + i );
		reloadRotation( s );
		if ( !s.empty( ) ) SendMessageW( rotList, LB_SETTOPINDEX, max( 0, s.back( ) - 3 ), 0 );
	}
	void removeRot( std::vector<int> rows )
	{
		std::sort( rows.begin( ), rows.end( ) );
		for ( int i = (int)rows.size( ) - 1; i >= 0; i-- ) {
			int r = rows[i]; if ( r < 0 || r >= ed.rotationCount ) continue;
			memmove( ed.rotation[r], ed.rotation[r + 1], (size_t)( ed.rotationCount - r - 1 ) * 64 ); ed.rotationCount--;
		}
	}
	void moveRot( std::vector<int> rows, int toAbove )
	{
		std::sort( rows.begin( ), rows.end( ) );
		std::vector<str> names; int above = 0;
		for ( int r : rows ) { names.push_back( ed.rotation[r] ); if ( r < toAbove ) above++; }
		removeRot( rows );
		int at = max( 0, min( toAbove - above, ed.rotationCount ) );
		addMaps( names, at );
	}
	void doRemove( )
	{
		auto s = selOf( rotList ); if ( s.empty( ) ) return;
		removeRot( s );
		reloadRotation( ed.rotationCount ? std::vector<int>{ min( s.front( ), ed.rotationCount - 1 ) } : std::vector<int>{} );
	}
	void doAddSelected( )
	{
		std::vector<str> n; for ( int i : selOf( mapList ) ) if ( i < (int)shown.size( ) ) n.push_back( shown[i]->name );
		addMaps( n );
	}

	void onNum( int id )
	{
		if ( inRefresh ) return;
		int *t = numTarget( id ); if ( !t ) return;
		str s = getText( ctl( id ) ); if ( s.empty( ) ) return;
		int lo, hi; numRange( id, &lo, &hi );
		long v = atol( s.c_str( ) ); v = max( (long)lo, min( v, (long)hi ) );
		*t = (int)v;
		if ( id == ID_FRAG || id == ID_CAP || id == ID_TIME ) showLimits( );
		if ( id == ID_BOTS ) showLimits( );
		if ( id == ID_PORT ) showServerTab( );
	}

	void onCommand( int id, int code, HWND ) override
	{
		if ( inRefresh ) return;
		if ( id == ID_SEG && code == 1 ) { showPage( (int)SendMessageW( seg, SEG_GETSEL, 0, 0 ) ); return; }
		if ( code == EN_CHANGE ) {
			if ( id == ID_SEARCH ) { search = getText( ctl( ID_SEARCH ) ); lc_trim( &search[0] ); search = str( search.c_str( ) ); reloadMaps( ); }
			else if ( id == ID_NAME ) { char o[64]; lc_clean_cvar_text( getText( ctl( id ) ).c_str( ), 48, o, sizeof o ); lc_strlcpy( ed.hostname, o, sizeof ed.hostname ); }
			else if ( id == ID_PW ) { char o[64]; lc_clean_cvar_text( getText( ctl( id ) ).c_str( ), 32, o, sizeof o ); lc_strlcpy( ed.password, o, sizeof ed.password ); }
			else onNum( id );
			return;
		}
		if ( code == EN_KILLFOCUS ) { int *t = numTarget( id ); if ( t ) { inRefresh = true; setNum( id, *t ); inRefresh = false; } return; }
		switch ( id ) {
		case ID_MAPS:
			if ( code == LBN_SELCHANGE ) rotButtons( );
			else if ( code == LBN_DBLCLK ) { POINT p; GetCursorPos( &p ); ScreenToClient( mapList, &p ); int r = (int)SendMessageW( mapList, LB_ITEMFROMPOINT, 0, MAKELPARAM( p.x, p.y ) ); if ( !HIWORD( r ) && LOWORD( r ) < shown.size( ) ) addMaps( { shown[LOWORD( r )]->name } ); }
			break;
		case ID_ROT: if ( code == LBN_SELCHANGE ) rotButtons( ); break;
		case ID_ADDROT: doAddSelected( ); break;
		case ID_REMOVE: doRemove( ); break;
		case ID_UP: { auto s = selOf( rotList ); if ( !s.empty( ) ) moveRot( s, s.front( ) - 1 ); break; }
		case ID_DOWN: { auto s = selOf( rotList ); if ( !s.empty( ) ) moveRot( s, s.back( ) + 2 ); break; }
		case ID_GAMETYPE:
			if ( code == CBN_SELCHANGE ) {
				static const int g[4] = { 0, 1, 3, 4 };
				ed.gametype = g[max( 0, (int)SendMessageW( ctl( id ), CB_GETCURSEL, 0, 0 ) )];
				lc_server_settings_apply_modifier_rules( &ed, -1 ); showAll( ); showPage( 1 );
			}
			break;
		case ID_SKILL: if ( code == CBN_SELCHANGE ) ed.botSkill = (int)SendMessageW( ctl( id ), CB_GETCURSEL, 0, 0 ) + 1; break;
		case ID_FF: ed.friendlyFire = SendMessageW( ctl( id ), BM_GETCHECK, 0, 0 ) == BST_CHECKED; break;
		case ID_LOCAL: case ID_INTERNET: ed.internet = id == ID_INTERNET; showServerTab( ); break;
		case ID_OPENPORT: ed.openPort = SendMessageW( ctl( id ), BM_GETCHECK, 0, 0 ) == BST_CHECKED; showServerTab( ); break;
		case ID_DL: ed.allowDownload = SendMessageW( ctl( id ), BM_GETCHECK, 0, 0 ) == BST_CHECKED; break;
		case ID_PURE: ed.pure = SendMessageW( ctl( id ), BM_GETCHECK, 0, 0 ) == BST_CHECKED; break;
		case ID_TICK: if ( code == CBN_SELCHANGE ) ed.tickRate = SendMessageW( ctl( id ), CB_GETCURSEL, 0, 0 ) == 1 ? 40 : 20; break;
		case ID_CMOD: ed.cmodGame = SendMessageW( ctl( id ), BM_GETCHECK, 0, 0 ) == BST_CHECKED; showCmod( ); break;
		case ID_UNLAG: if ( code == CBN_SELCHANGE ) { ed.unlagged = (int)SendMessageW( ctl( id ), CB_GETCURSEL, 0, 0 ); showCmod( ); } break;
		case ID_SMOOTH: ed.smoothClients = SendMessageW( ctl( id ), BM_GETCHECK, 0, 0 ) == BST_CHECKED; break;
		case ID_PREDICT: ed.unlaggedPredict = SendMessageW( ctl( id ), BM_GETCHECK, 0, 0 ) == BST_CHECKED; break;
		case ID_RESTORE: { lc_server_settings d; lc_server_settings_defaults( &d, lc_user_full_name( ) ); ed = d; showAll( ); break; }
		case IDOK:
			*orig = ed;
			lc_server_settings_save( orig, lc_store_shared( ) ); lc_store_save( lc_store_shared( ) );
			lc_log( "Server settings saved: %s, %s, %d maps", orig->hostname, lc_gametype_name( orig->gametype ), orig->rotationCount );
			end( IDOK ); break;
		case IDCANCEL: end( IDCANCEL ); break;
		default:
			if ( id >= ID_MOD0 && id < ID_MOD0 + LC_MOD_COUNT && code == BN_CLICKED ) {
				int m = id - ID_MOD0; ed.mods[m] = SendMessageW( ctl( id ), BM_GETCHECK, 0, 0 ) == BST_CHECKED;
				lc_server_settings_apply_modifier_rules( &ed, ed.mods[m] ? m : -1 );
				for ( int k = 0; k < LC_MOD_COUNT; k++ ) { SendMessageW( ctl( ID_MOD0 + k ), BM_SETCHECK, ed.mods[k] ? BST_CHECKED : BST_UNCHECKED, 0 ); EnableWindow( ctl( ID_MOD0 + k ), lc_server_settings_modifier_allowed( &ed, k ) ); }
			}
		}
	}

	/* ---- drag and drop between the lists ---- */
	void dragBegin( int src, LPARAM l, WPARAM w )
	{
		HWND list = src == 0 ? mapList : rotList;
		std::vector<int> pre = selOf( list );
		int r = (int)SendMessageW( list, LB_ITEMFROMPOINT, 0, l );
		dragSrc = -1;
		if ( HIWORD( r ) ) return;
		int item = LOWORD( r );
		if ( std::find( pre.begin( ), pre.end( ), item ) != pre.end( ) && !( w & ( MK_CONTROL | MK_SHIFT ) ) ) dragRows = pre; else dragRows = { item };
		dragSrc = src; dragStart = { (short)LOWORD( l ), (short)HIWORD( l ) }; dragging = false; dropAt = -1;
	}
	void dragMove( HWND list, LPARAM l )
	{
		if ( dragSrc < 0 ) return;
		POINT p = { (short)LOWORD( l ), (short)HIWORD( l ) };
		if ( !dragging && ( abs( p.x - dragStart.x ) > S( 5 ) || abs( p.y - dragStart.y ) > S( 5 ) ) ) { dragging = true; SetCapture( list ); }
		if ( !dragging ) return;
		POINT sp = p; ClientToScreen( list, &sp );
		POINT rp = sp; ScreenToClient( rotList, &rp );
		RECT rc; GetClientRect( rotList, &rc );
		int old = dropAt; dropAt = -1;
		if ( PtInRect( &rc, rp ) ) {
			int r = (int)SendMessageW( rotList, LB_ITEMFROMPOINT, 0, MAKELPARAM( rp.x, rp.y ) );
			dropAt = HIWORD( r ) ? ed.rotationCount : min( (int)LOWORD( r ), ed.rotationCount );
			if ( dropAt >= ed.rotationCount ) dropAt = ed.rotationCount;
		}
		if ( old != dropAt || true ) {
			InvalidateRect( rotList, 0, FALSE ); UpdateWindow( rotList );
			if ( dropAt >= 0 ) {
				HDC dc = GetDC( rotList );
				int y = dropAt * S( 50 ) - (int)SendMessageW( rotList, LB_GETTOPINDEX, 0, 0 ) * S( 50 );
				RECT lr = { 0, y - S( 1 ), rc.right, y + S( 1 ) }; HBRUSH b = CreateSolidBrush( T.accent ); FillRect( dc, &lr, b ); DeleteObject( b );
				ReleaseDC( rotList, dc );
			}
		}
	}
	void dragEnd( )
	{
		if ( dragSrc < 0 ) return;
		bool was = dragging; int src = dragSrc, at = dropAt;
		dragging = false; dragSrc = -1; dropAt = -1;
		ReleaseCapture();
		if ( !was ) return;
		InvalidateRect( rotList, 0, TRUE );
		if ( at < 0 ) return;
		if ( src == 0 ) { std::vector<str> n; for ( int i : dragRows ) if ( i < (int)shown.size( ) ) n.push_back( shown[i]->name ); addMaps( n, at ); }
		else moveRot( dragRows, at );
	}
};

static LRESULT CALLBACK cfgListSub( HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR ref )
{
	CfgDlg *d = (CfgDlg *)ref; int src = (int)id;
	switch ( m ) {
	case WM_LBUTTONDOWN: { d->dragBegin( src, l, w );
		std::vector<int> keep = d->dragRows; bool multi = d->dragSrc >= 0 && keep.size( ) > 1;
		LRESULT r = DefSubclassProc( h, m, w, l );
		if ( multi ) { SendMessageW( h, LB_SETSEL, FALSE, -1 ); for ( int k : keep ) SendMessageW( h, LB_SETSEL, TRUE, k ); }
		return r; }
	case WM_MOUSEMOVE: if ( w & MK_LBUTTON ) d->dragMove( h, l ); break;
	case WM_LBUTTONUP: d->dragEnd( ); break;
	case WM_KEYDOWN: if ( src == 1 && ( w == VK_DELETE || w == VK_BACK ) ) { d->doRemove( ); return 0; } break;
	case WM_NCDESTROY: RemoveWindowSubclass( h, cfgListSub, id ); break;
	}
	return DefSubclassProc( h, m, w, l );
}

void showServerConfig( HWND owner, lc_server_settings *settings, const str &dataFolder )
{
	CfgDlg d; d.orig = settings; d.ed = *settings; d.dataFolder = dataFolder;
	d.cat = lc_map_catalog_get( dataFolder.c_str( ), lc_homebase( ) );
	d.create( L"Server Settings", 700, 604, WS_POPUP | WS_CAPTION | WS_SYSMENU, 0, owner );
	d.build( );
	SetWindowSubclass( d.mapList, cfgListSub, 0, (DWORD_PTR)&d );
	SetWindowSubclass( d.rotList, cfgListSub, 1, (DWORD_PTR)&d );
	d.showAll( );
	if ( const char *t = testEnv( "VOYAGERSP_TEST_SERVER_CONFIG_TAB" ) ) {
		int p = !lc_stricmp( t, "game" ) ? 1 : !lc_stricmp( t, "server" ) ? 2 : !lc_stricmp( t, "cmod" ) ? 3 : 0;
		d.showPage( p );
	}
	d.runModal( );
}

}	// namespace wl
