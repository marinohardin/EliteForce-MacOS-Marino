// Standalone toolkit smoke test (not part of the game build): wl_ui.cpp + this file.
#include "wl_ui.h"
using namespace wl;

struct T1 : Wnd {
	void onCommand( int id, int code, HWND ) override {
		if ( id == 1 ) end( 1 );
	}
};

int WINAPI wWinMain( HINSTANCE, HINSTANCE, LPWSTR, int )
{
	initToolkit( );
	T1 w;
	w.create( L"Toolkit test", 560, 420, WS_POPUP | WS_CAPTION | WS_SYSMENU, 0, 0 );
	w.addGroup( 20, 100, 520, 160, "GRAPHICS" );
	w.label( 20, 20, 400, 22, "Elite Force", 24, 700 );
	w.label( 20, 50, 400, 18, "Choose how you want to play.", 13, 400, R_TEXT2 );
	HWND card = mkCard( w.hwnd, 20, 280, 160, 100, 20, G_PERSON, "Single Player", "The Voyager campaign, your saves and the Virtual Voyager." );
	cardSetSelected( card, true );
	mkCard( w.hwnd, 190, 280, 160, 100, 21, G_PERSON3, "Holomatch", "Join games on your network." );
	w.label( 34, 112, 100, 20, "Quality:", 13, 400, R_TEXT, DT_RIGHT );
	HWND seg = mkSeg( w.hwnd, 144, 110, 300, 24, 30 );
	segSetItems( seg, { "Performance", "Balanced", "Best", "Custom" } );
	SendMessageW( seg, SEG_SETSEL, 2, 0 ); SendMessageW( seg, SEG_DISABLE, 3, 1 );
	w.label( 34, 146, 100, 20, "Display:", 13, 400, R_TEXT, DT_RIGHT );
	HWND c = w.combo( 144, 144, 160, 31 );
	SendMessageW( c, CB_ADDSTRING, 0, (LPARAM)L"Window" ); SendMessageW( c, CB_SETCURSEL, 0, 0 );
	w.check( 144, 176, 300, 20, "Sync to the display (V-Sync)", 32, true );
	w.edit( 144, 204, 200, 22, 33, "Hello" );
	w.label( 144, 232, 300, 16, "A hint in orange", 11, 400, R_ORANGE );
	mkIconButton( w.hwnd, 440, 110, 28, 24, 34, G_PLUS, "Add" );
	mkIconButton( w.hwnd, 470, 110, 28, 24, 35, G_REFRESH, "Refresh" );
	int x = 380; for ( int g = 0; g < 8; g++ ) { w.label( x, 290 + g * 0, 1, 1, "", 1 ); }
	w.button( 450, 380, 90, 28, "Play", 1, true );
	const char *p = lc_env( "VOYAGERSP_TEST_LAUNCHER_SNAPSHOT" );
	w.show( );
	if ( p ) {
		pumpFor( 500 );
		snapshotWindow( w.hwnd, p );
		return 0;
	}
	Alert( w.hwnd, "Test alert", "Informative text that wraps around a bit more than one line of the dialog.", { "Yes", "No" }, 1, 1 );
	return w.runModal( );
}
