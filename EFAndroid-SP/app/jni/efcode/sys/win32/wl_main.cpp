/*
===========================================================================
Windows launcher: the main window and the entry point (Sys_WinLauncher).
Spec: launcher-data-coop.md sections 2, 3, 7, 8.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#include "wl_app.h"
/* win_launcher.h declares this with the engine's qboolean (an enum, int-sized); this file stays free of q_shared.h */
enum { qfalse = 0, qtrue = 1 };
#include <tlhelp32.h>

extern "C" void Sys_CoopSetLaunchPassword( const char *pw ) __attribute__(( weak ));

namespace wl {

Shared G;
str appVersion( ) { return EF_APP_VERSION; }

enum {
	IDC_CARD0 = 200, IDC_COOP = 210, IDC_BROWSE, IDC_CONFIG,
	IDC_QUALITY = 220, IDC_DISPLAY, IDC_WINSIZE, IDC_HIGHDPI, IDC_AA, IDC_PICMIP, IDC_VSYNC, IDC_BRIGHT, IDC_BRIGHTHINT,
	IDC_SRV0 = 230,		/* 230..236 value labels */
	IDC_NAME = 240, IDC_NAMEHINT, IDC_MASTERSUM, IDC_SERVERS, IDC_LOOPBACK, IDC_LOOPHINT, IDC_CMOD, IDC_HUD, IDC_MODLABEL, IDC_NAMELABEL, IDC_INETLABEL,
	IDC_PATH = 260, IDC_STATUS, IDC_EXPANSION, IDC_SHOW, IDC_ADD, IDC_CHANGE, IDC_VERSION,
	IDC_GEAR = 270, IDC_DOWNLOAD, IDC_BADGE1, IDC_BADGE2,
	IDM_ABOUT = 0x8010, IDM_UPDATE, IDM_AUTOUPDATE, IDM_QUIT
};

static const int CW = 564, MX = 28;

bool spawnSelf( const std::vector<str> &args, long *pid, str *err )
{
	std::vector<const char *> av; av.push_back( lc_self_command( ) );
	for ( auto &a : args ) av.push_back( a.c_str( ) );
	av.push_back( 0 );
	char e[256] = "";
	long p = lc_spawn_detached( av.data( ), 0, "VOYAGERSP_TEST_", e, sizeof e );
	if ( p < 0 ) { if ( err ) *err = e; return false; }
	if ( pid ) *pid = p;
	return true;
}

static BOOL CALLBACK findPidWnd( HWND h, LPARAM l )
{
	DWORD pid = 0; GetWindowThreadProcessId( h, &pid );
	auto *a = (std::pair<DWORD, HWND> *)l;
	if ( pid == a->first && IsWindowVisible( h ) && !GetWindow( h, GW_OWNER ) ) { a->second = h; return FALSE; }
	return TRUE;
}
bool activateInstanceWindow( long pid )
{
	std::pair<DWORD, HWND> a( (DWORD)pid, 0 );
	AllowSetForegroundWindow( (DWORD)pid );
	EnumWindows( findPidWnd, (LPARAM)&a );
	if ( !a.second ) return false;
	if ( IsIconic( a.second ) ) ShowWindow( a.second, SW_RESTORE );
	SetForegroundWindow( a.second );
	return true;
}

/* ------------------------------------------------------------------ About */

void showAbout( HWND owner )
{
	struct About : Wnd { void onCommand( int id, int, HWND ) override { if ( id == IDOK || id == IDCANCEL ) end( 1 ); }
		void paintOver( HDC dc ) override { drawAppIcon( dc, S( 22 ), S( 20 ), S( 64 ) ); } } d;
	const int W0 = 520;
	str credits =
		"Made by Marino Hardin (Marino's Build)\r\nSingle player and Holomatch for Windows, macOS and Linux.\r\nNo game data included: requires your own retail copy of the game.\r\n\r\n"
		"Based on VoyagerSP-Android by imjustadudegamer, lilium-voyager by Zack Middleton, ioquake3, id Software's Quake III Arena, the Quake3e Vulkan renderer by Eugene C., and Raven Software's Elite Force single-player game source.\r\n\r\n"
		"Many Holomatch fixes and improvements come from cMod by Noah Metzger (Chomenor), with great thanks for his work and his kind offer to collaborate.\r\n\r\n"
		"Holomatch's widescreen HUD, lag compensation and weapon prediction are cMod's game modules by Noah Metzger, included unchanged with his permission. Thank you, Noah!\r\n\r\n"
		"Star Trek and related marks are trademarks of Paramount. This is an unofficial, non-commercial fan project, not affiliated with or endorsed by Paramount, Raven Software or Activision.";
	str copy = "Made by Marino Hardin. Engine: GPLv2 (ioquake3 / lilium-voyager / Quake3e). Game code: Raven Software Limited Use Software License. Game data not included.";
	d.create( L"About Elite Force", W0, 430, WS_POPUP | WS_CAPTION | WS_SYSMENU, 0, owner );
	d.label( 100, 24, W0 - 120, 30, "Elite Force", 22, 700 );
	d.label( 100, 56, W0 - 120, 18, fmt( "Version %s", appVersion( ).c_str( ) ).c_str( ), 12, 400, R_TEXT2 );
	HWND e = d.edit( 20, 100, W0 - 40, 230, 70, credits.c_str( ), ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL, 12 );
	(void)e;
	d.label( 20, 340, W0 - 40, 40, copy.c_str( ), 11, 400, R_TEXT2 );
	d.button( W0 - 20 - 84, 392, 84, 28, "OK", IDOK, true );
	d.runModal( );
}

/* ------------------------------------------------------------------ HUD sheet */

struct HudDlg : Wnd {
	lc_cmod *c; bool moved = false;
	void sync( ) { EnableWindow( ctl( 33 ), SendMessageW( ctl( 31 ), CB_GETCURSEL, 0, 0 ) == 0 ); }
	void onCommand( int id, int code, HWND ) override
	{
		if ( id == 31 && code == CBN_SELCHANGE ) sync( );
		else if ( id == IDOK ) {
			c->hudAspect = SendMessageW( ctl( 31 ), CB_GETCURSEL, 0, 0 ) == 0 ? 1 : 0;
			if ( moved ) { double v = SendMessageW( ctl( 33 ), TBM_GETPOS, 0, 0 ) / 100.0; snprintf( c->hudCenter, sizeof c->hudCenter, "%g", v ); }
			c->predictWeapons = SendMessageW( ctl( 34 ), BM_GETCHECK, 0, 0 ) == BST_CHECKED;
			end( IDOK );
		}
		else if ( id == IDCANCEL ) end( IDCANCEL );
	}
	LRESULT onOther( UINT m, WPARAM w, LPARAM l, bool *handled ) override
	{
		if ( m == WM_HSCROLL && (HWND)l == ctl( 33 ) ) { moved = true; }
		*handled = false; return 0;
	}
};

static bool hudSheet( HWND owner, lc_cmod *c )
{
	HudDlg d; d.c = c;
	const int W0 = 480;
	d.create( L"HUD", W0, 230, WS_POPUP | WS_CAPTION | WS_SYSMENU, 0, owner );
	d.label( 20, 24, 90, 20, "HUD:", 13, 400, R_TEXT, DT_RIGHT | DT_SINGLELINE );
	HWND cb = d.combo( 120, 22, 340, 31 );
	SendMessageW( cb, CB_ADDSTRING, 0, (LPARAM)W( "Widescreen: sharp, in proportion" ).c_str( ) );
	SendMessageW( cb, CB_ADDSTRING, 0, (LPARAM)W( "Stretched to the screen (original look)" ).c_str( ) );
	SendMessageW( cb, CB_SETCURSEL, c->hudAspect ? 0 : 1, 0 );
	d.label( 20, 66, 90, 20, "Position:", 13, 400, R_TEXT, DT_RIGHT | DT_SINGLELINE );
	d.label( 120, 66, 44, 16, "Edges", 11, 400, R_TEXT2, DT_SINGLELINE );
	HWND tb = d.ctl_( TRACKBAR_CLASSW, L"", TBS_AUTOTICKS | TBS_NOTHUMB * 0 | WS_TABSTOP, 0, 166, 60, 200, 30, 33 );
	SendMessageW( tb, TBM_SETRANGE, TRUE, MAKELONG( 0, 100 ) ); SendMessageW( tb, TBM_SETTICFREQ, 25, 0 );
	char *endp = 0; double v = strtod( c->hudCenter, &endp );
	if ( !( endp && endp != c->hudCenter && !*endp && v >= 0 && v <= 1 ) ) v = 0;
	SendMessageW( tb, TBM_SETPOS, TRUE, (LPARAM)( v * 100 + 0.5 ) );
	d.label( 372, 66, 60, 16, "Middle", 11, 400, R_TEXT2, DT_SINGLELINE );
	d.check( 120, 102, 340, 22, "Show my shots at once on servers that offer it", 34, c->predictWeapons != 0 );
	d.label( 120, 130, 340, 48, "From cMod's game modules by Noah Metzger. Weapon fire prediction needs a server running cMod's game code with it on (cMod servers, and Host a Server's default).", 11, 400, R_TEXT2 );
	d.label( 20, 106, 90, 20, "Weapons:", 13, 400, R_TEXT, DT_RIGHT | DT_SINGLELINE );
	d.button( W0 - 20 - 84 - 8 - 84, 188, 84, 28, "Cancel", IDCANCEL );
	d.button( W0 - 20 - 84, 188, 84, 28, "Done", IDOK, true );
	d.sync( );
	return d.runModal( ) == IDOK;
}

/* ------------------------------------------------------------ the launcher */

struct Launcher : Wnd {
	lc_store *st = 0;
	int mode = 0;
	lc_gfx gfx; int fromConfig = 0;
	lc_cmod cmod;
	char name[64] = "";
	lc_server_settings server;
	str joinAddr, joinPw;
	bool updNewer = false; str updVersion, updUrl;
	bool fetching = false;
	bool visible = false, playPending = false;
	int gGfx = -1, gSrv = -1, gHm = -1, gFiles = -1, gBadge = -1;
	int gfxBoxTop = 0, gfxBoxH = 0, contentH = 0, clientH = 0;
	HWND cards[3] = {};
	str pendingLink;
	/* what the Game Files group shows */
	int stGlyph = G_CHECK_CIRCLE, stRole = R_GREEN, exGlyph = -1, exRole = R_GREEN;
	str folderPathText, statusText, statusTip, expText;
	lc_launch launch; str launchName, launchBase, launchLoop, launchCoopConnect, launchCoopSex, launchCoopName;
	char coopHostPw[64] = "";
	bool test = false;

	/* ---------------- model ---------------- */
	void loadModel( )
	{
		st = lc_store_shared( );
		mode = lc_store_get_int( st, LC_KEY_MODE, 0 );
		if ( mode < 0 || mode > 2 ) mode = 0;
		if ( const char *m = testEnv( "VOYAGERSP_TEST_LAUNCHER_MODE" ) ) mode = !lc_stricmp( m, "hm" ) ? 1 : !lc_stricmp( m, "host" ) ? 2 : 0;
		RECT wa; SystemParametersInfoW( SPI_GETWORKAREA, 0, &wa, 0 );
		lc_gfx_load( G.homeBase.c_str( ), mode != 0, wa.right - wa.left, wa.bottom - wa.top, &gfx, &fromConfig );
		if ( const char *p = testEnv( "VOYAGERSP_TEST_LAUNCHER_PRESET" ) ) {
			lc_preset pr = !lc_stricmp( p, "performance" ) ? LC_PRESET_PERFORMANCE : !lc_stricmp( p, "balanced" ) ? LC_PRESET_BALANCED : LC_PRESET_BEST;
			lc_gfx_apply_preset( &gfx, pr );
		}
		if ( const char *p = testEnv( "VOYAGERSP_TEST_LAUNCHER_BRIGHTNESS" ) ) {
			for ( int i = 0; i < lc_brightness_count( ); i++ ) if ( !lc_stricmp( p, lc_brightness_test_id( i ) ) ) gfx.brightness = i;
		}
		lc_cfg hm; memset( &hm, 0, sizeof hm );
		char path[LC_MAX_PATH]; lc_config_path( G.homeBase.c_str( ), 1, path, sizeof path );
		lc_cfg_read( path, &hm );
		lc_player_name_from_config( &hm, lc_user_full_name( ), name, sizeof name );
		lc_cmod_load( &hm, st, &cmod );
		lc_cfg_free( &hm );
		serverSettingsLoad( &server );
		if ( const char *p = testEnv( "VOYAGERSP_TEST_MASTERS" ) ) {
			char list[LC_MAX_MASTERS][65]; const char *in[LC_MAX_MASTERS]; int n = 0;
			str s = p; size_t pos = 0;
			static std::vector<str> parts; parts.clear( );
			while ( pos <= s.size( ) ) { size_t e = s.find( ',', pos ); if ( e == str::npos ) e = s.size( ); parts.push_back( s.substr( pos, e - pos ) ); pos = e + 1; }
			for ( auto &x : parts ) if ( n < LC_MAX_MASTERS ) in[n++] = x.c_str( );
			int cnt = lc_clean_masters( in, n, list );
			lc_masters_save( st, list, cnt );
		}
		if ( const char *p = testEnv( "VOYAGERSP_TEST_SERVER_PORT" ) ) server.port = atoi( p );
	}

	/* ---------------- window ---------------- */
	bool buildWindow( )
	{
		RECT wa; SystemParametersInfoW( SPI_GETWORKAREA, 0, &wa, 0 );
		int ch = computeHeight( );
		contentH = ch;
		int availH = (int)( ( wa.bottom - wa.top ) * 96 / g_dpi ) - 70;
		clientH = min( ch, availH );
		if ( !create( L"Elite Force", 620, clientH, WS_POPUP | WS_CAPTION | WS_SYSMENU, 0, 0 ) ) return false;
		SetPropW( hwnd, L"EFLauncherMain", (HANDLE)1 );
		HMENU sm = GetSystemMenu( hwnd, FALSE );
		AppendMenuW( sm, MF_SEPARATOR, 0, 0 );
		AppendMenuW( sm, MF_STRING, IDM_ABOUT, L"About Elite Force" );
		AppendMenuW( sm, MF_STRING, IDM_UPDATE, L"Check for Updates…" );
		AppendMenuW( sm, MF_STRING, IDM_AUTOUPDATE, L"Check for Updates Automatically" );
		build( );
		if ( contentH > clientH ) {
			SCROLLINFO si = { sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS, 0, S( contentH ) - 1, (UINT)S( clientH ), 0, 0 };
			SetWindowLongW( hwnd, GWL_STYLE, GetWindowLongW( hwnd, GWL_STYLE ) | WS_VSCROLL );
			SetScrollInfo( hwnd, SB_VERT, &si, TRUE );
			SetWindowPos( hwnd, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED );
		}
		return true;
	}

	int computeHeight( )
	{
		/* kept in step with build() */
		int graphics = 18 + 256, hm = 18 + 154, files = 18 + 100;
		return 30 + 62 + 20 + 120 + 14 + graphics + 14 + hm + 14 + files + 18 + 30 + 18;
	}

	void build( )
	{
		for ( auto &i : ids ) (void)i;
		groups.clear( );
		int y = 30;
		/* header */
		label( 98, y + 0, 330, 16, "Star Trek: Voyager", 12, 600, R_TEXT2, DT_SINGLELINE );
		label( 98, y + 16, 330, 30, "Elite Force", 24, 700, R_TEXT, DT_SINGLELINE );
		label( 98, y + 46, 330, 17, "Choose how you want to play.", 13, 400, R_TEXT2, DT_SINGLELINE );
		HWND gear = mkIconButton( hwnd, 620 - MX - 28, y + 17, 28, 26, IDC_GEAR, G_MORE, "More: About, Check for Updates\xE2\x80\xA6" );
		(void)gear;
		/* update badge: built hidden */
		gBadge = addGroup( 620 - MX - 28 - 8 - 260, y + 8, 260, 46, "", 9 );
		groups[gBadge].kind = 1; groups[gBadge].visible = false;
		label( 620 - MX - 28 - 8 - 260 + 38, y + 13, 150, 16, "Update available", 12, 600, R_TEXT, DT_SINGLELINE, IDC_BADGE1 );
		label( 620 - MX - 28 - 8 - 260 + 38, y + 29, 150, 16, "", 11, 400, R_TEXT2, DT_SINGLELINE, IDC_BADGE2 );
		button( 620 - MX - 28 - 8 - 84, y + 16, 76, 22, "Download\xE2\x80\xA6", IDC_DOWNLOAD, false, 12 );
		setTooltip( hwnd, ctl( IDC_DOWNLOAD ), "Open the release page on GitHub" );
		y += 62 + 20;
		/* cards */
		const int cw = ( CW - 20 ) / 3, chh = 120;
		struct { int glyph; const char *title, *detail; } cd[3] = {
			{ G_PERSON, "Single Player", "The Voyager campaign, your saves and the Virtual Voyager." },
			{ G_PERSON3, "Holomatch", "Join games on your network or the internet, or play with bots." },
			{ G_SERVER, "Host a Server", "" } };
		str hostDetail = fmt( "Run a Holomatch server on this %s for friends.", PLATFORM_NOUN );
		cd[2].detail = hostDetail.c_str( );
		for ( int i = 0; i < 3; i++ )
			cards[i] = mkCard( hwnd, MX + i * ( cw + 10 ), y, i == 2 ? CW - 2 * ( cw + 10 ) : cw, chh, IDC_CARD0 + i, cd[i].glyph, cd[i].title, cd[i].detail );
		ids[IDC_CARD0] = cards[0]; ids[IDC_CARD0 + 1] = cards[1]; ids[IDC_CARD0 + 2] = cards[2];
		int by = y + chh - 11 - 22;
		if ( lc_coop_ui_enabled( ) ) {
			button( MX + 14, by, 72, 22, "Co-op\xE2\x80\xA6", IDC_COOP, false, 12 );
			setTooltip( hwnd, ctl( IDC_COOP ), "Play the campaign with a friend: host a co-op game, or check for one to join (experimental)" );
		}
		button( MX + ( cw + 10 ) + 14, by, 76, 22, "Browse\xE2\x80\xA6", IDC_BROWSE, false, 12 );
		setTooltip( hwnd, ctl( IDC_BROWSE ), "See the games on your network and the internet, and join one" );
		button( MX + 2 * ( cw + 10 ) + 14, by, 76, 22, "Config\xE2\x80\xA6", IDC_CONFIG, false, 12 );
		setTooltip( hwnd, ctl( IDC_CONFIG ), "Maps, game type, bots and the server's name, password and network" );
		y += chh + 14;

		/* graphics / server group */
		const int lx = MX + 14, lw = 112, cx = lx + lw + 10, cwid = 190;
		gfxBoxTop = y + 18; gfxBoxH = 256;
		gGfx = addGroup( MX, gfxBoxTop, CW, gfxBoxH, "GRAPHICS" );
		gSrv = addGroup( MX, gfxBoxTop, CW, gfxBoxH, "SERVER" );
		groups[gSrv].visible = false;
		{
			int gy = gfxBoxTop + 10;
			int labN = 0;
			auto lab = [&]( int row_y, const char *t ) { label( lx, row_y + 3, lw, 18, t, 13, 400, R_TEXT, DT_RIGHT | DT_SINGLELINE, 300 + labN++ ); };
			lab( gy, "Quality:" );
			HWND q = mkSeg( hwnd, cx, gy, 330, 24, IDC_QUALITY ); ids[IDC_QUALITY] = q;
			segSetItems( q, { "Performance", "Balanced", "Best", "Custom" } ); SendMessageW( q, SEG_DISABLE, 3, 1 );
			gy += 24 + 5;
			lab( gy, "Display:" );
			HWND c = combo( cx, gy, cwid, IDC_DISPLAY );
			SendMessageW( c, CB_ADDSTRING, 0, (LPARAM)L"Window" ); SendMessageW( c, CB_ADDSTRING, 0, (LPARAM)L"Full Screen" );
			gy += 22 + 5;
			lab( gy, "Window size:" );
			combo( cx, gy, cwid, IDC_WINSIZE );
			gy += 22 + 5;
			check( cx, gy, 340, 18, "Full resolution (high DPI)", IDC_HIGHDPI );
			gy += 18 + 5;
			lab( gy, "Anti-aliasing:" );
			c = combo( cx, gy, cwid, IDC_AA );
			for ( const wchar_t *s : { L"Off", L"2×", L"4×", L"8×" } ) SendMessageW( c, CB_ADDSTRING, 0, (LPARAM)s );
			gy += 22 + 5;
			lab( gy, "Texture detail:" );
			c = combo( cx, gy, cwid, IDC_PICMIP );
			for ( const wchar_t *s : { L"High", L"Medium", L"Low" } ) SendMessageW( c, CB_ADDSTRING, 0, (LPARAM)s );
			gy += 22 + 5;
			check( cx, gy, 340, 18, "Sync to the display (V-Sync)", IDC_VSYNC );
			gy += 18 + 5;
			lab( gy, "Brightness:" );
			c = combo( cx, gy, cwid, IDC_BRIGHT );
			for ( int i = 0; i < lc_brightness_count( ); i++ ) SendMessageW( c, CB_ADDSTRING, 0, (LPARAM)W( lc_brightness_title( i ) ).c_str( ) );
			SendMessageW( c, CB_ADDSTRING, 0, (LPARAM)L"Custom" );
			gy += 22 + 5;
			label( cx, gy, 400, 30, "", 11, 400, R_TEXT2, DT_WORDBREAK, IDC_BRIGHTHINT );
			/* server summary grid (same box) */
			int sy = gfxBoxTop + 10;
			const char *sl[6] = { "Server name:", "Game:", "Maps:", "Players:", "Network:", "Address:" };
			for ( int i = 0; i < 6; i++ ) {
				label( lx, sy + 3, lw, 18, sl[i], 13, 400, R_TEXT, DT_RIGHT | DT_SINGLELINE, IDC_SRV0 + 20 + i );
				label( cx, sy + 3, CW - 14 - lw - 10 - 14, 18, "", 13, i == 5 ? 500 : 400, R_TEXT, DT_SINGLELINE | DT_END_ELLIPSIS, IDC_SRV0 + i );
				sy += 24;
			}
			sy += 6;
			str tip = fmt( "Friends connect to this address, or find the server in Holomatch's Join menu. It runs in its own window, where Play on This %s joins it with your Holomatch graphics settings.", PLATFORM_NOUN );
			label( cx, sy, 400, 44, tip.c_str( ), 11, 400, R_TEXT2, DT_WORDBREAK, 320 );
		}
		y = gfxBoxTop + gfxBoxH + 14;

		/* holomatch group */
		int hbTop = y + 18, hbH = 154;
		gHm = addGroup( MX, hbTop, CW, hbH, "HOLOMATCH" );
		{
			int gy = hbTop + 10;
			label( lx, gy + 3, lw, 18, "Player name:", 13, 400, R_TEXT, DT_RIGHT | DT_SINGLELINE, IDC_NAMELABEL );
			HWND ne = edit( cx, gy, 220, 22, IDC_NAME, "" );
			SendMessageW( ne, EM_SETCUEBANNER, TRUE, (LPARAM)L"UnnamedPlayer" );
			SendMessageW( ne, EM_LIMITTEXT, 63, 0 );
			setText( ne, name );
			gy += 22 + 3;
			label( cx, gy, 400, 14, "", 11, 400, R_TEXT2, DT_SINGLELINE, IDC_NAMEHINT );
			gy += 14 + 8;
			label( lx, gy + 3, lw, 18, "Internet games:", 13, 400, R_TEXT, DT_RIGHT | DT_SINGLELINE, IDC_INETLABEL );
			label( cx, gy + 3, 300, 18, "", 13, 400, R_TEXT, DT_SINGLELINE | DT_END_ELLIPSIS, IDC_MASTERSUM );
			button( MX + CW - 14 - 84, gy, 84, 22, "Servers\xE2\x80\xA6", IDC_SERVERS, false, 12 );
			setTooltip( hwnd, ctl( IDC_SERVERS ), "The master servers that list internet games, and that a server you host announces itself to" );
			gy += 22 + 4;
			HWND lb = check( cx, gy, 300, 18, "Loopback Host Fix", IDC_LOOPBACK, loopbackFix( ) );
			str lt = fmt( "Most routers don't route this %s back to its own public address, so a server hosted here wouldn't answer in your own internet lists. This lists it as this %s (127.0.0.1) instead.", PLATFORM_NOUN, PLATFORM_NOUN );
			setTooltip( hwnd, lb, lt.c_str( ) );
			gy += 18 + 3;
			str lh = fmt( "Shows a server you host on this %s in your own internet lists.", PLATFORM_NOUN );
			label( cx, gy, 400, 14, lh.c_str( ), 11, 400, R_TEXT2, DT_SINGLELINE, IDC_LOOPHINT );
			gy += 14 + 8;
			label( lx, gy + 3, lw, 18, "Game modules:", 13, 400, R_TEXT, DT_RIGHT | DT_SINGLELINE, IDC_MODLABEL );
			HWND cm = check( cx, gy, 340, 18, "Use cMod's game modules, by Noah Metzger", IDC_CMOD, cmod.on != 0 );
			setTooltip( hwnd, cm, "cMod's cgame and ui in place of the original ones (not on pure servers, and never in place of a mod's own): a widescreen HUD and menus, weapon fire prediction and many fixes." );
			button( MX + CW - 14 - 64, gy - 2, 64, 22, "HUD\xE2\x80\xA6", IDC_HUD, false, 12 );
			setTooltip( hwnd, ctl( IDC_HUD ), "cMod's widescreen HUD and weapon fire prediction" );
		}
		y = hbTop + hbH + 14;

		/* game files group */
		int fTop = y + 18, fH = 100;
		gFiles = addGroup( MX, fTop, CW, fH, "GAME FILES" );
		{
			int gy = fTop + 10;
			label( lx + 46, gy, CW - 28 - 46, 16, "", 12, 500, R_TEXT, DT_SINGLELINE | DT_PATH_ELLIPSIS, IDC_PATH );
			label( lx + 46 + 18, gy + 19, CW - 28 - 46 - 18, 14, "", 11, 400, R_TEXT2, DT_SINGLELINE | DT_END_ELLIPSIS, IDC_STATUS );
			label( lx + 46 + 18, gy + 19 + 17, CW - 28 - 46 - 18, 14, "", 11, 400, R_TEXT2, DT_SINGLELINE | DT_END_ELLIPSIS, IDC_EXPANSION );
			gy += 46 + 8;
			int x = MX + CW - 14;
			x -= 90; button( x, gy, 90, 24, "Change\xE2\x80\xA6", IDC_CHANGE, false, 12 ); x -= 8;
			x -= 130; button( x, gy, 130, 24, "Add Game Files\xE2\x80\xA6", IDC_ADD, false, 12 ); x -= 8;
			x -= 130; button( x, gy, 130, 24, "Show in File Explorer", IDC_SHOW, false, 12 );
			setTooltip( hwnd, ctl( IDC_ADD ), "Copy more .pk3 files into the game, such as the Expansion Pack's or extra maps" );
		}
		y = fTop + fH + 18;
		/* footer */
		str ver = appVersion( ).empty( ) ? str( "Made by Marino Hardin" ) : fmt( "Version %s \xC2\xB7 Made by Marino Hardin", appVersion( ).c_str( ) );
		label( MX, y + 8, 330, 16, ver.c_str( ), 11, 400, R_TEXT3, DT_SINGLELINE, IDC_VERSION );
		button( 620 - MX - 110 - 10 - 80, y, 80, 30, "Quit", IDCANCEL, false, 13 );
		button( 620 - MX - 110, y, 110, 30, "Play", IDOK, true, 13 );
		SendMessageW( ctl( IDOK ), WM_SETFONT, (WPARAM)F( 13, 600 ), TRUE );
	}

	/* ---------------- state to controls ---------------- */
	int bright( ) { return gfx.brightness; }

	void syncGfx( )
	{
		lc_preset p = lc_gfx_match_preset( &gfx );
		SendMessageW( ctl( IDC_QUALITY ), SEG_SETSEL, (WPARAM)p, 0 );
		SendMessageW( ctl( IDC_DISPLAY ), CB_SETCURSEL, gfx.fullscreen ? 1 : 0, 0 );
		/* window sizes */
		HWND sz = ctl( IDC_WINSIZE );
		SendMessageW( sz, CB_RESETCONTENT, 0, 0 );
		RECT wa; SystemParametersInfoW( SPI_GETWORKAREA, 0, &wa, 0 );
		int sizes[16][2];
		int n = lc_window_sizes( wa.right - wa.left, wa.bottom - wa.top - GetSystemMetrics( SM_CYCAPTION ), gfx.width, gfx.height, sizes, 16 );
		int sel = 0;
		for ( int i = 0; i < n; i++ ) {
			int idx = (int)SendMessageW( sz, CB_ADDSTRING, 0, (LPARAM)W( fmt( "%d \xC3\x97 %d", sizes[i][0], sizes[i][1] ) ).c_str( ) );
			SendMessageW( sz, CB_SETITEMDATA, idx, sizes[i][0] * 10000 + sizes[i][1] );
			if ( sizes[i][0] == gfx.width && sizes[i][1] == gfx.height ) sel = idx;
		}
		SendMessageW( sz, CB_SETCURSEL, sel, 0 );
		EnableWindow( sz, !gfx.fullscreen );
		SendMessageW( ctl( IDC_HIGHDPI ), BM_SETCHECK, gfx.highdpi ? BST_CHECKED : BST_UNCHECKED, 0 );
		int aa = gfx.msaa == 0 ? 0 : gfx.msaa == 2 ? 1 : gfx.msaa == 4 ? 2 : 3;
		SendMessageW( ctl( IDC_AA ), CB_SETCURSEL, aa, 0 );
		SendMessageW( ctl( IDC_PICMIP ), CB_SETCURSEL, max( 0, min( 2, gfx.picmip ) ), 0 );
		SendMessageW( ctl( IDC_VSYNC ), BM_SETCHECK, gfx.vsync ? BST_CHECKED : BST_UNCHECKED, 0 );
		int nb = lc_brightness_count( );
		SendMessageW( ctl( IDC_BRIGHT ), CB_SETCURSEL, gfx.brightness == LC_BRIGHTNESS_CUSTOM ? nb : gfx.brightness, 0 );
		setText( ctl( IDC_BRIGHTHINT ), lc_brightness_help( gfx.brightness ) );
	}

	void readGfx( )
	{
		gfx.fullscreen = (int)SendMessageW( ctl( IDC_DISPLAY ), CB_GETCURSEL, 0, 0 ) == 1;
		int i = (int)SendMessageW( ctl( IDC_WINSIZE ), CB_GETCURSEL, 0, 0 );
		if ( i >= 0 ) { int d = (int)SendMessageW( ctl( IDC_WINSIZE ), CB_GETITEMDATA, i, 0 ); gfx.width = d / 10000; gfx.height = d % 10000; }
		gfx.highdpi = SendMessageW( ctl( IDC_HIGHDPI ), BM_GETCHECK, 0, 0 ) == BST_CHECKED;
		static const int aa[4] = { 0, 2, 4, 8 };
		int a = (int)SendMessageW( ctl( IDC_AA ), CB_GETCURSEL, 0, 0 ); gfx.msaa = aa[max( 0, min( 3, a ) )];
		gfx.picmip = max( 0, (int)SendMessageW( ctl( IDC_PICMIP ), CB_GETCURSEL, 0, 0 ) );
		gfx.vsync = SendMessageW( ctl( IDC_VSYNC ), BM_GETCHECK, 0, 0 ) == BST_CHECKED;
		int b = (int)SendMessageW( ctl( IDC_BRIGHT ), CB_GETCURSEL, 0, 0 );
		gfx.brightness = ( b < 0 || b >= lc_brightness_count( ) ) ? LC_BRIGHTNESS_CUSTOM : b;
	}

	void syncMode( )
	{
		for ( int i = 0; i < 3; i++ ) cardSetSelected( cards[i], i == mode );
		bool hm = mode != 0;
		EnableWindow( ctl( IDC_NAME ), hm );
		setRole( ctl( IDC_NAMELABEL ), hm ? R_TEXT : R_TEXT3 );
		bool cm = hm && cmod.available;
		EnableWindow( ctl( IDC_CMOD ), cm );
		setRole( ctl( IDC_MODLABEL ), cm ? R_TEXT : R_TEXT3 );
		syncHud( );
		str hint = mode == 1 ? "Shown to other players in Holomatch." : mode == 2 ? fmt( "Your name when you use Play on This %s.", PLATFORM_NOUN ) : "Used when you play Holomatch.";
		setText( ctl( IDC_NAMEHINT ), hint );
		setText( ctl( IDOK ), mode == 0 ? "Play" : mode == 1 ? "Play Holomatch" : "Start Server" );
		groups[gGfx].visible = mode != 2; groups[gSrv].visible = mode == 2;
		for ( int id : { IDC_QUALITY, IDC_DISPLAY, IDC_WINSIZE, IDC_HIGHDPI, IDC_AA, IDC_PICMIP, IDC_VSYNC, IDC_BRIGHT, IDC_BRIGHTHINT } ) showCtl( id, mode != 2 );
		showGfxLabels( mode != 2 );
		for ( int i = 0; i < 6; i++ ) { showCtl( IDC_SRV0 + i, mode == 2 ); showCtl( IDC_SRV0 + 20 + i, mode == 2 ); }
		if ( mode == 2 ) syncServer( );
		InvalidateRect( hwnd, 0, TRUE );
	}
	void showGfxLabels( bool on )
	{
		for ( int i = 0; i < 8; i++ ) showCtl( 300 + i, on );
		showCtl( 320, !on );
	}

	void syncHud( )
	{
		bool cm = mode != 0 && cmod.available && SendMessageW( ctl( IDC_CMOD ), BM_GETCHECK, 0, 0 ) == BST_CHECKED;
		EnableWindow( ctl( IDC_HUD ), cm );
	}

	void syncServer( )
	{
		const lc_map_catalog *cat = lc_map_catalog_get( G.dataFolder.c_str( ), G.homeBase.c_str( ) );
		auto set = [&]( int i, const str &t, int role = R_TEXT ) { setText( ctl( IDC_SRV0 + i ), t ); setRole( ctl( IDC_SRV0 + i ), role ); };
		set( 0, server.hostname );
		str game = lc_gametype_name( server.gametype );
		str mods; for ( int m = 0; m < LC_MOD_COUNT; m++ ) if ( server.mods[m] ) { if ( !mods.empty( ) ) mods += ", "; mods += lc_modifier_name( m ); }
		if ( !mods.empty( ) ) game += " \xC2\xB7 " + mods;
		set( 1, game );
		/* maps */
		int missing = 0; std::vector<str> found, names;
		for ( int i = 0; i < server.rotationCount; i++ ) {
			const lc_map_info *mi = cat ? lc_map_find( cat, server.rotation[i] ) : 0;
			if ( mi ) found.push_back( lc_map_display_name( mi ) ); else missing++;
			names.push_back( server.rotation[i] );
		}
		str mt; int mr = R_TEXT;
		if ( server.rotationCount == 0 ) { mt = "None chosen yet"; mr = R_ORANGE; }
		else if ( server.rotationCount == 1 ) { if ( !found.empty( ) ) mt = found[0]; else { mt = "1 missing map"; mr = R_ORANGE; } }
		else {
			mt = fmt( "%d in rotation: ", server.rotationCount );
			for ( size_t i = 0; i < found.size( ); i++ ) { if ( i ) mt += ", "; mt += found[i]; }
			if ( missing ) { mt += fmt( " (%d missing)", missing ); mr = R_ORANGE; }
		}
		set( 2, mt, mr );
		str tip; for ( size_t i = 0; i < names.size( ); i++ ) { if ( i ) tip += " \xE2\x86\x92 "; tip += names[i]; }
		setTooltip( hwnd, ctl( IDC_SRV0 + 2 ), tip.c_str( ) );
		/* players */
		str pl = fmt( "Up to %d", server.maxPlayers );
		if ( server.bots > 0 ) pl += fmt( " \xC2\xB7 bots fill to %d%s (%s)", server.bots, server.gametype >= 3 ? " per team" : "", lc_skill_name( server.botSkill ) );
		else pl += " \xC2\xB7 no bots";
		set( 3, pl );
		bool disc = server.port >= 27960 && server.port <= 27963;
		str net = server.internet ? ( server.openPort ? "Internet, port opened on the router" : "Internet" ) : "Local network only";
		net += fmt( " \xC2\xB7 port %d", server.port );
		if ( !disc ) net += " (not listed in the Join menu)";
		if ( server.password[0] ) net += " \xC2\xB7 password";
		set( 4, net, disc ? R_TEXT : R_ORANGE );
		char lan[64] = ""; if ( !lc_lan_address( lan, sizeof lan ) || !lan[0] ) strcpy( lan, "127.0.0.1" );
		set( 5, fmt( "%s:%d", lan, server.port ) );
	}

	void syncMasters( )
	{
		int n = 0; bool orange = false; str tip;
		str t = masterSummary( &n, &orange, &tip );
		setText( ctl( IDC_MASTERSUM ), t ); setRole( ctl( IDC_MASTERSUM ), orange ? R_ORANGE : R_TEXT );
		setTooltip( hwnd, ctl( IDC_MASTERSUM ), tip.c_str( ) );
	}

	void syncData( )
	{
		dataRefreshLookup( );
		char names[2][16]; int nMissing = 0;
		if ( G.haveData && !G.overridden ) nMissing = lc_data_missing_patch_paks( G.homeBase.c_str( ), G.dataFolder.c_str( ), names );
		if ( G.overridden ) {
			folderPathText = "Set on the command line";
			statusText = "fs_basepath / fs_homepath given on the command line"; statusTip.clear( );
			stGlyph = G_TERMINAL; stRole = R_TEXT2; exGlyph = -1;
		} else if ( G.haveData ) {
			folderPathText = expandHome( G.dataFolder );
			if ( nMissing > 0 ) {
				str nm = names[0]; if ( nMissing > 1 ) nm += str( " and " ) + names[1];
				statusText = fmt( "Patch 1.2 missing \xC2\xB7 add %s with Add Game Files\xE2\x80\xA6", nm.c_str( ) );
				statusTip = fmt( "These game files are from the original CD (version 1.0). The game also needs %s from the 1.1 and 1.2 patches: copy them from a patched installation (GOG, Steam, or a PC with patch 1.2) with Add Game Files\xE2\x80\xA6.", nm.c_str( ) );
				stGlyph = G_TRIANGLE; stRole = R_ORANGE;
				lc_log( "Launcher: patch files missing (%s) in \"%s\"", nm.c_str( ), G.dataFolder.c_str( ) );
			} else {
				statusText = G.basepath.empty( ) ? "Ready \xC2\xB7 copied into the app's own folder" : "Ready \xC2\xB7 using the files in this folder";
				statusTip.clear( ); stGlyph = G_CHECK_CIRCLE; stRole = R_GREEN;
			}
			bool exp = lc_data_has_expansion( G.homeBase.c_str( ), G.dataFolder.c_str( ) ) != 0;
			exGlyph = exp ? G_PUZZLE_FILL : G_PUZZLE; exRole = exp ? R_GREEN : R_TEXT3;
			expText = exp ? "Expansion Pack found \xC2\xB7 Virtual Voyager and its Holomatch maps" : "No Expansion Pack \xC2\xB7 add its files to play Virtual Voyager and its maps";
		} else {
			folderPathText = "No game files yet";
			statusText = "Choose the Elite Force folder (the one with BaseEF), or the GOG setup .exe.";
			statusTip.clear(); stGlyph = G_TRIANGLE; stRole = R_ORANGE; exGlyph = -1;
		}
		setText( ctl( IDC_PATH ), folderPathText ); setTooltip( hwnd, ctl( IDC_PATH ), G.dataFolder.c_str( ) );
		setText( ctl( IDC_STATUS ), statusText ); setRole( ctl( IDC_STATUS ), stRole == R_GREEN ? R_TEXT2 : stRole == R_ORANGE ? R_ORANGE : R_TEXT2 );
		setTooltip( hwnd, ctl( IDC_STATUS ), statusTip.c_str( ) );
		showCtl( IDC_EXPANSION, G.haveData && !G.overridden && exGlyph >= 0 );
		setText( ctl( IDC_EXPANSION ), expText );
		showCtl( IDC_SHOW, !G.dataFolder.empty( ) );
		showCtl( IDC_ADD, G.haveData && !G.overridden );
		EnableWindow( ctl( IDC_CHANGE ), !G.overridden );
		setText( ctl( IDC_CHANGE ), G.haveData ? "Change\xE2\x80\xA6" : "Choose\xE2\x80\xA6" );
		bool can = G.canPlay( );
		EnableWindow( ctl( IDOK ), can );
		setTooltip( hwnd, ctl( IDOK ), G.missingPatch ? "Add the 1.2 patch files first (see Game Files)" : "" );
		RECT r = { 0, S( 0 ), S( 620 ), S( contentH ) };
		InvalidateRect( hwnd, &r, TRUE );
	}

	void paintOver( HDC dc ) override
	{
		drawAppIcon( dc, S( MX ), S( 30 ) - scrollY + S( 3 ), S( 56 ) );
		if ( gFiles >= 0 ) {
			Group &g = groups[gFiles];
			int gx = g.r.left + S( 14 ), gy = g.r.top - scrollY + S( 10 );
			drawGlyph( dc, G_FOLDER, gx + S( 4 ), gy + S( 3 ), S( 28 ), T.accent );
			drawGlyph( dc, stGlyph, gx + S( 46 ), gy + S( 19 ) + S( 1 ), S( 13 ), roleColor( stRole ) );
			if ( exGlyph >= 0 && G.haveData && !G.overridden ) drawGlyph( dc, exGlyph, gx + S( 46 ), gy + S( 36 ) + S( 1 ), S( 13 ), roleColor( exRole ) );
		}
		if ( gBadge >= 0 && groups[gBadge].visible ) {
			Group &g = groups[gBadge];
			drawGlyph( dc, G_DOWN_CIRCLE, g.r.left + S( 10 ), g.r.top - scrollY + S( 13 ), S( 20 ), T.accent );
		}
	}

	void syncUpdate( )
	{
		bool show = updNewer;
		groups[gBadge].visible = show;
		for ( int id : { IDC_BADGE1, IDC_BADGE2, IDC_DOWNLOAD } ) showCtl( id, show );
		if ( show ) {
			setText( ctl( IDC_BADGE2 ), fmt( "Version %s \xC2\xB7 you have %s", updVersion.c_str( ), appVersion( ).c_str( ) ) );
			setTooltip( hwnd, ctl( IDC_BADGE2 ), fmt( "Elite Force for Windows %s is on GitHub", updVersion.c_str( ) ).c_str( ) );
		}
		InvalidateRect( hwnd, 0, TRUE );
	}

	/* ---------------- actions ---------------- */
	void selectMode( int m )
	{
		if ( m < 0 || m > 2 ) return;
		mode = m; syncMode( );
	}

	void readControls( )
	{
		readGfx( );
		str n = getText( ctl( IDC_NAME ) );
		lc_strlcpy( name, n.c_str( ), sizeof name );
		cmod.on = SendMessageW( ctl( IDC_CMOD ), BM_GETCHECK, 0, 0 ) == BST_CHECKED;
	}

	void doPlay( )
	{
		if ( !G.canPlay( ) ) return;
		if ( mode == 2 ) { startServer( ); return; }
		readControls( );
		playPending = true;
		memset( &launch, 0, sizeof launch );
		launch.mode = mode == 0 ? LC_MODE_SP : LC_MODE_HOLOMATCH;
		launch.gfx = gfx;
		launchName = name; launch.name = launchName.c_str( );
		launch.cmod = cmod;
		launchBase = G.basepath; launch.basepath = launchBase.empty( ) ? 0 : launchBase.c_str( );
		if ( mode == 1 ) {
			char list[LC_MAX_MASTERS][65];
			int n = lc_masters_load( st, list );
			launch.mastersCfgWritten = lc_masters_write_cfg( G.homeBase.c_str( ), list, n, 0 ) ? 1 : 0;
			launchLoop = loopbackArgs( );
			launch.loopbackIP = launchLoop.empty( ) ? 0 : launchLoop.c_str( );
			if ( !joinAddr.empty( ) ) { launch.joinAddress = joinAddr.c_str( ); launch.joinPassword = joinPw.empty( ) ? 0 : joinPw.c_str( ); }
		} else {
			struct lc_launch_coop_fields cf; lc_coop_launch_fields( &cf );
			launch.coopHost = cf.coopHost; launch.coopBackground = cf.coopBackground;
			launchCoopConnect = cf.coopConnect ? cf.coopConnect : ""; launchCoopSex = cf.coopSex ? cf.coopSex : ""; launchCoopName = cf.coopName ? cf.coopName : "";
			launch.coopConnect = launchCoopConnect.empty( ) ? 0 : launchCoopConnect.c_str( );
			launch.coopSex = launchCoopSex.empty( ) ? 0 : launchCoopSex.c_str( );
			launch.coopName = launchCoopName.empty( ) ? 0 : launchCoopName.c_str( );
			if ( lc_coop_choice( ) && Sys_CoopSetLaunchPassword ) Sys_CoopSetLaunchPassword( lc_coop_password( ) );
		}
		lc_store_set_int( st, LC_KEY_MODE, mode ); lc_store_save( st );
		end( IDOK );
	}

	str loopbackArgs( )
	{
		if ( !loopbackFix( ) ) return str( );
		int waited = 0;
		while ( publicAddress( ).empty( ) && publicAddressPending( ) && waited < 1500 ) { pumpFor( 50 ); waited += 50; }
		return publicAddress( );
	}

	void startServer( )
	{
		readControls( );
		lc_store_set_int( st, LC_KEY_MODE, 2 );
		lc_server_settings_save( &server, st ); lc_store_save( st );
		struct lc_running run[16];
		int nr = lc_running_list( G.homeRoot.c_str( ), run, 16 );
		if ( nr > 0 ) {
			str msg = nr == 1 ? fmt( "A server is already running on this %s.", PLATFORM_NOUN ) : fmt( "%d servers are already running on this %s.", nr, PLATFORM_NOUN );
			struct lc_running &last = run[nr - 1];
			str info = fmt( "\xE2\x80\x9C%s\xE2\x80\x9D is running on port %d. Another server would start on the next free port, with the settings in Config\xE2\x80\xA6; players would see both in the server list.",
				last.name[0] ? last.name : "Server", last.port );
			int r = 1;
			if ( const char *a = testEnv( "VOYAGERSP_TEST_SERVER_RUNNING" ) ) { r = !lc_stricmp( a, "show" ) ? 0 : !lc_stricmp( a, "another" ) ? 1 : 2; lc_log( "[test] server running answer: %s", a ); }
			else r = Alert( hwnd, msg, info, { "Show Running Server", "Start Another", "Cancel" }, 0, 2 );
			if ( r == 0 ) { activateInstanceWindow( last.pid ); lc_log( "Launcher: showing the running server (pid %d)", last.pid ); return; }
			if ( r == 2 ) return;
		}
		lc_log( "Launcher: starting the server in its own window" );
		long pid = 0; str err;
		if ( !spawnSelf( { LC_HOST_SERVER_ARG }, &pid, &err ) ) {
			Alert( hwnd, "The server couldn't be started.", err, { "OK" }, 1 );
			return;
		}
		lc_log( "Launcher: server window started, pid %ld", pid );
	}

	void doBrowse( )
	{
		selectMode( 1 );
		str addr, pw;
		if ( showBrowser( hwnd, G.dataFolder, &addr, &pw ) ) joinAndPlay( addr, pw );
	}

	void joinAndPlay( const str &addr, const str &pw )
	{
		joinAddr = addr; joinPw = pw;
		selectMode( 1 );
		doPlay( );
	}

	void doConfig( )
	{
		selectMode( 2 );
		showServerConfig( hwnd, &server, G.dataFolder );
		syncServer( );
	}

	void doChange( )
	{
		if ( dataRunChooser( hwnd ) ) lc_log( "Launcher: game folder changed" );
		syncData( ); show( ); SetForegroundWindow( hwnd );
	}

	void doAdd( )
	{
		int n = dataAddGameFiles( hwnd );
		if ( n > 0 ) lc_log( "Launcher: game files added" );
		syncData( ); SetForegroundWindow( hwnd );
	}

	/* updates */
	struct UpdJob { Launcher *l; bool manual; lc_update_result r; };
	static void updWorker( void *u )
	{
		UpdJob *j = (UpdJob *)u;
		memset( &j->r, 0, sizeof j->r );
		lc_update_fetch( EF_APP_VERSION, &j->r );
		PostMessageW( j->l->hwnd, WM_APP + 10, 0, (LPARAM)j );
	}
	void startUpdateCheck( bool manual )
	{
		if ( fetching ) return;
		fetching = true;
		UpdJob *j = new UpdJob{ this, manual, {} };
		lc_thread *t = lc_thread_start( updWorker, j );
		if ( t ) lc_thread_detach( t ); else { fetching = false; delete j; }
	}
	void autoUpdate( )
	{
		if ( !lc_store_get_bool( st, LC_KEY_CHECK_UPDATES, 1 ) ) return;
		char sv[64] = "", su[512] = "";
		int due = lc_update_auto_state( st, sv, sizeof sv, su, sizeof su );
		if ( sv[0] && lc_version_is_newer( sv, EF_APP_VERSION ) ) { updNewer = true; updVersion = sv; updUrl = su; syncUpdate( ); }
		if ( due || testEnv( "VOYAGERSP_TEST_UPDATE" ) ) startUpdateCheck( false );
	}
	void onUpdateResult( UpdJob *j )
	{
		fetching = false;
		lc_update_result &r = j->r;
		if ( j->manual ) {
			if ( r.ok ) { lc_update_save( st, &r, EF_APP_VERSION ); lc_store_save( st ); applyUpdate( r ); }
			char msg[512], info[1024]; int newer = 0;
			lc_update_alert_texts( &r, EF_APP_VERSION, msg, sizeof msg, info, sizeof info, &newer );
			lc_log( "[test] Check for Updates: \"%s\" \"%s\"", msg, info );
			if ( newer ) {
				if ( Alert( hwnd, msg, info, { "Download\xE2\x80\xA6", "Later" }, 0, 1 ) == 0 ) lc_open_url( r.url );
			} else Alert( hwnd, msg, info, { "OK" }, r.ok ? 0 : 1 );
		} else {
			if ( !r.ok ) lc_log( "Updates: couldn't check (%s)", r.problem );
			else { lc_update_save( st, &r, EF_APP_VERSION ); lc_store_save( st ); applyUpdate( r ); }
		}
		delete j;
	}
	void applyUpdate( const lc_update_result &r )
	{
		updNewer = r.version[0] && lc_version_is_newer( r.version, EF_APP_VERSION );
		updVersion = r.version; updUrl = r.url;
		syncUpdate( );
	}

	void toggleAutoUpdate( )
	{
		bool on = !lc_store_get_bool( st, LC_KEY_CHECK_UPDATES, 1 );
		lc_store_set_int( st, LC_KEY_CHECK_UPDATES, on ); lc_store_save( st );
		lc_log( "Updates: automatic check %s", on ? "on" : "off" );
		if ( on ) autoUpdate( ); else { updNewer = false; syncUpdate( ); }
	}

	void gearMenu( )
	{
		HMENU m = CreatePopupMenu( );
		AppendMenuW( m, MF_STRING, IDM_ABOUT, L"About Elite Force" );
		AppendMenuW( m, MF_SEPARATOR, 0, 0 );
		AppendMenuW( m, MF_STRING, IDM_UPDATE, L"Check for Updates…" );
		AppendMenuW( m, MF_STRING | ( lc_store_get_bool( st, LC_KEY_CHECK_UPDATES, 1 ) ? MF_CHECKED : 0 ), IDM_AUTOUPDATE, L"Check for Updates Automatically" );
		AppendMenuW( m, MF_SEPARATOR, 0, 0 );
		AppendMenuW( m, MF_STRING, IDM_QUIT, L"Quit\tEsc" );
		RECT r; GetWindowRect( ctl( IDC_GEAR ), &r );
		int c = TrackPopupMenu( m, TPM_RETURNCMD | TPM_RIGHTALIGN, r.right, r.bottom, 0, hwnd, 0 );
		DestroyMenu( m );
		if ( c ) command( c );
	}

	void command( int c )
	{
		switch ( c ) {
		case IDM_ABOUT: showAbout( hwnd ); break;
		case IDM_UPDATE: startUpdateCheck( true ); break;
		case IDM_AUTOUPDATE: toggleAutoUpdate( ); break;
		case IDM_QUIT: quit( ); break;
		}
	}

	void quit( ) { lc_log( "Launcher: quit" ); end( IDCANCEL ); }

	/* links (7.1) */
	void handleLink( const str &link )
	{
		if ( !visible && !test ) { pendingLink = link; return; }
		char addr[256] = "", pw[256] = "";
		if ( lc_coop_ui_enabled( ) ) {
			int cr = lc_coop_link_address( link.c_str( ), addr, sizeof addr );
			if ( cr >= 0 ) {
				if ( cr == 0 ) { Alert( hwnd, "Join co-op", fmt( "The co-op invite %s has no address in it.", link.c_str( ) ), { "OK" }, 0 ); return; }
				closeSheets( ); selectMode( 0 );
				bool playNow = false;
				showCoopSheet( hwnd, G.canPlay( ), addr, true, &playNow );
				if ( playNow ) { playPending = false; doPlay( ); }
				return;
			}
		}
		int ok = lc_link_address( link.c_str( ), addr, sizeof addr, pw, sizeof pw );
		lc_log( "Link: %s -> %s", link.c_str( ), ok ? addr : "not a server address" );
		if ( !ok ) { Alert( hwnd, "This link can't be joined", link + "\n\nIt doesn't hold a server address.", { "OK" }, 1 ); return; }
		closeSheets( );
		joinAndPlay( addr, "" );
	}
	void closeSheets( ) { for ( Wnd *w : std::vector<Wnd *>( g_sheets ) ) if ( w != this ) w->end( IDCANCEL ); }

	/* ---------------- message handling ---------------- */
	bool onClose( ) override { quit( ); return true; }

	bool preTranslate( MSG *m ) override
	{
		if ( m->message == WM_KEYDOWN && m->wParam == 'Q' && ( GetKeyState( VK_CONTROL ) & 0x8000 ) ) { quit( ); return true; }
		return false;
	}

	void onCommand( int id, int code, HWND ) override
	{
		if ( id >= IDC_CARD0 && id <= IDC_CARD0 + 2 ) {
			int i = id - IDC_CARD0;
			if ( code == BN_CLICKED ) selectMode( i );
			else if ( code == BN_DOUBLECLICKED ) { selectMode( i ); doPlay( ); }
			else if ( code == 100 ) { selectMode( ( i + 2 ) % 3 ); SetFocus( cards[mode] ); }
			else if ( code == 101 ) { selectMode( ( i + 1 ) % 3 ); SetFocus( cards[mode] ); }
			return;
		}
		switch ( id ) {
		case IDOK: if ( code == BN_CLICKED || code == 0 ) doPlay( ); break;
		case IDCANCEL: quit( ); break;
		case IDC_COOP: { bool playNow = false; selectMode( 0 ); showCoopSheet( hwnd, G.canPlay( ), "", false, &playNow ); if ( playNow ) doPlay( ); break; }
		case IDC_BROWSE: doBrowse( ); break;
		case IDC_CONFIG: doConfig( ); break;
		case IDC_QUALITY: {
			readGfx( );
			int s = (int)SendMessageW( ctl( IDC_QUALITY ), SEG_GETSEL, 0, 0 );
			if ( s >= 0 && s <= 2 ) { lc_gfx_apply_preset( &gfx, (lc_preset)s ); }
			syncGfx( ); break; }
		case IDC_DISPLAY: case IDC_WINSIZE: case IDC_AA: case IDC_PICMIP: case IDC_BRIGHT:
			if ( code == CBN_SELCHANGE ) { readGfx( ); syncGfx( ); }
			break;
		case IDC_HIGHDPI: case IDC_VSYNC: readGfx( ); syncGfx( ); break;
		case IDC_SERVERS: showMasters( hwnd ); syncMasters( ); break;
		case IDC_LOOPBACK: setLoopbackFix( SendMessageW( ctl( IDC_LOOPBACK ), BM_GETCHECK, 0, 0 ) == BST_CHECKED ); break;
		case IDC_CMOD: syncHud( ); break;
		case IDC_HUD:
			if ( hudSheet( hwnd, &cmod ) ) lc_cmod_save_store( &cmod, st ), lc_store_save( st );
			break;
		case IDC_SHOW: lc_open_folder( G.dataFolder.c_str( ) ); break;
		case IDC_ADD: doAdd( ); break;
		case IDC_CHANGE: doChange( ); break;
		case IDC_GEAR: gearMenu( ); break;
		case IDC_DOWNLOAD: if ( !updUrl.empty( ) ) lc_open_url( updUrl.c_str( ) ); break;
		case IDM_ABOUT: case IDM_UPDATE: case IDM_AUTOUPDATE: case IDM_QUIT: command( id ); break;
		}
	}

	void scrollTo( int pos )
	{
		int maxp = max( 0, S( contentH ) - S( clientH ) );
		pos = max( 0, min( pos, maxp ) );
		if ( pos == scrollY ) return;
		ScrollWindowEx( hwnd, 0, scrollY - pos, 0, 0, 0, 0, SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE );
		scrollY = pos;
		SetScrollPos( hwnd, SB_VERT, pos, TRUE );
		InvalidateRect( hwnd, 0, TRUE );
	}

	LRESULT onOther( UINT m, WPARAM w, LPARAM l, bool *handled ) override
	{
		*handled = true;
		switch ( m ) {
		case WM_VSCROLL: {
			int pos = scrollY;
			switch ( LOWORD( w ) ) {
			case SB_LINEUP: pos -= S( 30 ); break; case SB_LINEDOWN: pos += S( 30 ); break;
			case SB_PAGEUP: pos -= S( clientH ) - S( 40 ); break; case SB_PAGEDOWN: pos += S( clientH ) - S( 40 ); break;
			case SB_THUMBTRACK: case SB_THUMBPOSITION: { SCROLLINFO si = { sizeof si, SIF_TRACKPOS }; GetScrollInfo( hwnd, SB_VERT, &si ); pos = si.nTrackPos; break; }
			}
			scrollTo( pos ); return 0; }
		case WM_MOUSEWHEEL: if ( contentH > clientH ) { scrollTo( scrollY - (short)HIWORD( w ) * S( 60 ) / 120 ); return 0; } break;
		case WM_COPYDATA: {
			COPYDATASTRUCT *cd = (COPYDATASTRUCT *)l;
			if ( cd && cd->dwData == 0x4C4B4645 && cd->lpData ) { handleLink( str( (const char *)cd->lpData, cd->cbData ? strnlen( (const char *)cd->lpData, cd->cbData ) : 0 ) ); return TRUE; }
			break; }
		case WM_SYSCOMMAND: if ( ( w & 0xFFF0 ) >= IDM_ABOUT && ( w & 0xFFF0 ) <= IDM_AUTOUPDATE ) { command( (int)( w & 0xFFF0 ) ); return 0; } break;
		case WM_INITMENUPOPUP: {
			HMENU sm = GetSystemMenu( hwnd, FALSE );
			if ( (HMENU)w == sm ) CheckMenuItem( sm, IDM_AUTOUPDATE, MF_BYCOMMAND | ( lc_store_get_bool( st, LC_KEY_CHECK_UPDATES, 1 ) ? MF_CHECKED : MF_UNCHECKED ) );
			break; }
		case WM_APP + 10: onUpdateResult( (UpdJob *)l ); return 0;
		case WM_GETMINMAXINFO: break;
		}
		*handled = false; return 0;
	}

	void onDpiChanged( ) override
	{
		/* rebuild the controls at the new scale */
		readControls( );
		for ( HWND c = GetWindow( hwnd, GW_CHILD ), n; c; c = n ) { n = GetWindow( c, GW_HWNDNEXT ); DestroyWindow( c ); }
		ids.clear( ); scrollY = 0;
		build( ); applyTheme( hwnd );
		syncGfx( ); syncMode( ); syncMasters( ); syncData( ); syncUpdate( );
		InvalidateRect( hwnd, 0, TRUE );
	}

	void onTimer( int id ) override { if ( id == 5 ) { KillTimer( hwnd, 5 ); runTestHooks( ); } }

	/* ---------------- test hooks (9) ---------------- */
	void exitTest( ) { fflush( stderr ); ExitProcess( 0 ); }
	void runTestHooks( )
	{
		const char *p;
		if ( ( p = testEnv( "VOYAGERSP_TEST_MASTERS_SNAPSHOT" ) ) ) { g_snapPath = p; showMasters( hwnd ); }
		if ( ( p = testEnv( "VOYAGERSP_TEST_LAUNCHER_HUD" ) ) ) { g_snapPath = p; hudSheet( hwnd, &cmod ); }
		if ( ( p = testEnv( "VOYAGERSP_TEST_LAUNCHER_ABOUT" ) ) ) { g_snapPath = p; showAbout( hwnd ); }
		if ( ( p = testEnv( "VOYAGERSP_TEST_UPDATE_ALERT" ) ) ) { g_alertSnapshotPath = p; startUpdateCheck( true ); pumpFor( 6000 ); }
		if ( ( p = testEnv( "VOYAGERSP_TEST_SERVER_CONFIG_SNAPSHOT" ) ) ) { g_snapPath = p; showServerConfig( hwnd, &server, G.dataFolder ); }
		if ( ( p = testEnv( "VOYAGERSP_TEST_BROWSER_SNAPSHOT" ) ) ) { str a, pw; g_snapPath = p; if ( showBrowser( hwnd, G.dataFolder, &a, &pw ) ) joinAndPlay( a, pw ); }
		if ( ( p = testEnv( "VOYAGERSP_TEST_COOP_SHEET" ) ) && lc_coop_ui_enabled( ) ) { g_snapPath = p; bool pn = false; showCoopSheet( hwnd, G.canPlay( ), testEnv( "VOYAGERSP_TEST_COOP_QUERY" ) ? testEnv( "VOYAGERSP_TEST_COOP_QUERY" ) : "", testEnv( "VOYAGERSP_TEST_COOP_QUERY" ) != 0, &pn ); }
		if ( testOn( "VOYAGERSP_TEST_LAUNCHER_BROWSE_CANCEL" ) ) { g_snapPath.clear( ); str a, pw; showBrowser( hwnd, G.dataFolder, &a, &pw ); lc_log( "[test] Browse cancel: launcher %s", IsWindow( hwnd ) ? "stayed" : "gone" ); exitTest( ); }
		if ( ( p = testEnv( "VOYAGERSP_TEST_LINK" ) ) ) { handleLink( p ); }
		if ( ( p = testEnv( "VOYAGERSP_TEST_LAUNCHER_SNAPSHOT" ) ) ) {
			snapshotWindow( hwnd, p );
			if ( !testOn( "VOYAGERSP_TEST_LAUNCHER_PLAY" ) && !testEnv( "VOYAGERSP_TEST_SERVER_START" ) ) exitTest( );
		}
		if ( ( testEnv( "VOYAGERSP_TEST_MASTERS_SNAPSHOT" ) || testEnv( "VOYAGERSP_TEST_LAUNCHER_HUD" ) || testEnv( "VOYAGERSP_TEST_LAUNCHER_ABOUT" ) ||
			testEnv( "VOYAGERSP_TEST_UPDATE_ALERT" ) || testEnv( "VOYAGERSP_TEST_SERVER_CONFIG_SNAPSHOT" ) || testEnv( "VOYAGERSP_TEST_BROWSER_SNAPSHOT" ) ||
			testEnv( "VOYAGERSP_TEST_COOP_SHEET" ) ) && !testEnv( "VOYAGERSP_TEST_LAUNCHER_PLAY" ) )
			exitTest( );
		if ( ( p = testEnv( "VOYAGERSP_TEST_LAUNCHER_AUTOPLAY" ) ) ) { SetTimer( hwnd, 6, (UINT)( atof( p ) * 1000 ), 0 ); }
	}
};

/* ------------------------------------------------------------------ run */

}	// namespace wl

using namespace wl;

extern "C" int Sys_WinLauncher( int argc, char **argv, char *commandLine, int size )
{
	for ( int i = 1; i < argc; i++ )
		if ( !lc_stricmp( argv[i], "com_holomatch" ) ) return qfalse;
	bool hostOnly = false, chooseFolder = false, forceLauncher = false;
	str link;
	for ( int i = 1; i < argc; i++ ) {
		if ( !strcmp( argv[i], LC_HOST_SERVER_ARG ) ) hostOnly = true;
		else if ( !strcmp( argv[i], LC_CHOOSE_FOLDER_ARG ) ) chooseFolder = true;
		else if ( !strcmp( argv[i], LC_LAUNCHER_ARG ) ) forceLauncher = true;
		else if ( !strcmp( argv[i], "--uri" ) && i + 1 < argc ) link = argv[i + 1];
		else if ( link.empty( ) && lc_is_link_arg( argv[i] ) ) link = argv[i];
	}
	(void)forceLauncher;
	if ( testEnv( "VOYAGERSP_TEST_SERVER_START" ) ) hostOnly = true;

	/* a link, and a launcher already up: hand it over */
	if ( !link.empty( ) && !hostOnly ) {
		struct F { static BOOL CALLBACK cb( HWND h, LPARAM l ) { if ( GetPropW( h, L"EFLauncherMain" ) ) { *(HWND *)l = h; return FALSE; } return TRUE; } };
		HWND other = 0; EnumWindows( F::cb, (LPARAM)&other );
		if ( other ) {
			COPYDATASTRUCT cd = { 0x4C4B4645, (DWORD)link.size( ) + 1, (void *)link.c_str( ) };
			DWORD pid = 0; GetWindowThreadProcessId( other, &pid ); AllowSetForegroundWindow( pid );
			SendMessageW( other, WM_COPYDATA, 0, (LPARAM)&cd );
			if ( IsIconic( other ) ) ShowWindow( other, SW_RESTORE );
			SetForegroundWindow( other );
			exit( 0 );
		}
	}

	initToolkit( );
	G.argc = argc; G.argv = argv;
	G.homeBase = lc_homebase( ); G.homeRoot = lc_homepath( );
	Launcher *L = new Launcher;
	L->test = testOn( "VOYAGERSP_TEST_BACKGROUND" ) || testOn( "VOYAGERSP_TEST_NODIALOG" );
	L->loadModel( );
	publicAddressStart( );
	dataRefreshLookup( );

	if ( hostOnly ) {
		/* the server window only */
		lc_launch o; memset( &o, 0, sizeof o );
		o.mode = LC_MODE_HOLOMATCH; o.gfx = L->gfx; o.cmod = L->cmod;
		str nm = L->name; o.name = nm.c_str( );
		str bp = G.basepath; o.basepath = bp.empty( ) ? 0 : bp.c_str( );
		char list[LC_MAX_MASTERS][65]; int nm2 = lc_masters_load( L->st, list );
		o.mastersCfgWritten = lc_masters_write_cfg( G.homeBase.c_str( ), list, nm2, 0 ) ? 1 : 0;
		str lb; { if ( loopbackFix( ) ) { int w = 0; while ( publicAddress( ).empty( ) && publicAddressPending( ) && w < 1500 ) { pumpFor( 50 ); w += 50; } lb = publicAddress( ); } }
		o.loopbackIP = lb.empty( ) ? 0 : lb.c_str( );
		int ac = 0; char **av = lc_launch_client_argv( &o, G.homeBase.c_str( ), lc_self_command( ), &ac );
		std::vector<str> ca; for ( int i = 1; i < ac; i++ ) ca.push_back( av[i] );
		if ( av ) lc_argv_free( av );
		if ( !G.haveData ) lc_log( "Server: no game files found" );
		runServerWindow( &L->server, G.basepath, G.dataFolder, ca );
		exit( 0 );
	}

	/* the launcher */
	{
		char err[256];
		if ( lc_link_handler_state( 0, 0 ) == 0 ) { if ( !lc_link_register( err, sizeof err ) ) lc_log( "Link: couldn't register the stvef:// handler: %s", err ); }
	}
	if ( !L->buildWindow( ) ) { lc_log( "Launcher: couldn't create the window" ); return qfalse; }
	L->syncGfx( ); L->syncMode( ); L->syncMasters( ); L->syncData( ); L->syncUpdate( );
	if ( !link.empty( ) ) L->pendingLink = link;

	if ( testOn( "VOYAGERSP_TEST_LAUNCHER_PLAY" ) && !testEnv( "VOYAGERSP_TEST_LAUNCHER_SNAPSHOT" ) ) {
		L->doPlay( );
	} else {
		L->visible = true;
		L->autoUpdate( );
		L->show( );
		SetForegroundWindow( L->hwnd );
		SetTimer( L->hwnd, 5, 400, 0 );
		if ( chooseFolder ) L->doChange( );
		if ( !L->pendingLink.empty( ) ) { str pl = L->pendingLink; L->pendingLink.clear( ); PostMessageW( L->hwnd, WM_NULL, 0, 0 ); L->handleLink( pl ); }
		if ( !L->done ) {
			/* the modal loop of the launcher */
			L->done = false;
			while ( !L->done ) {
				MSG m; BOOL r = GetMessageW( &m, 0, 0, 0 );
				if ( r <= 0 ) { if ( r == 0 ) L->done = true; break; }
				if ( m.message == WM_TIMER && m.hwnd == L->hwnd && m.wParam == 6 ) { KillTimer( L->hwnd, 6 ); L->doPlay( ); continue; }
				if ( L->preTranslate( &m ) ) continue;
				if ( IsDialogMessageW( L->hwnd, &m ) ) continue;
				TranslateMessage( &m ); DispatchMessageW( &m );
			}
		}
	}
	bool play = L->playPending && L->done;
	if ( L->hwnd ) { ShowWindow( L->hwnd, SW_HIDE ); DestroyWindow( L->hwnd ); L->hwnd = 0; }
	if ( !play ) { lc_log( "Launcher: quit" ); exit( 0 ); }
	lc_set_from_launcher( 1 );
	lc_launch_append_args( &L->launch, G.homeBase.c_str( ), commandLine, size );
	return qtrue;
}

