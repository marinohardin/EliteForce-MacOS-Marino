/*
===========================================================================
Windows launcher: game data. The chooser (LDC 4.2), Add Game Files (4.4),
the copy / unpack progress panel. The work is in the core (lc_data.h).

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#include "wl_app.h"
#include <shobjidl.h>
#include <commdlg.h>

namespace wl {

lc_data g_data;

void dataRefreshLookup( )
{
	G.overridden = lc_data_overridden( G.argc, G.argv ) != 0;
	G.haveData = false; G.missingPatch = false; G.basepath.clear( ); G.dataFolder.clear( );
	memset( &g_data, 0, sizeof g_data );
	if ( G.overridden ) { G.haveData = true; return; }
	if ( lc_data_find( G.argc, G.argv, &g_data ) && g_data.found ) {
		G.haveData = true;
		G.basepath = g_data.basepath; G.dataFolder = g_data.base;
		char names[2][16];
		int n = lc_data_missing_patch_paks( G.homeBase.c_str( ), g_data.base, names );
		G.missingPatch = n > 0;
	}
}

/* ------------------------------------------------------------- file pickers */

static bool pickFolder( HWND owner, const wchar_t *title, str *out )
{
	IFileOpenDialog *d = 0;
	if ( CoCreateInstance( CLSID_FileOpenDialog, 0, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog, (void **)&d ) != S_OK ) return false;
	DWORD opt = 0; d->GetOptions( &opt ); d->SetOptions( opt | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM );
	d->SetTitle( title );
	bool ok = false;
	if ( d->Show( owner ) == S_OK ) {
		IShellItem *it = 0;
		if ( d->GetResult( &it ) == S_OK ) {
			PWSTR p = 0;
			if ( it->GetDisplayName( SIGDN_FILESYSPATH, &p ) == S_OK ) { *out = U( p ); CoTaskMemFree( p ); ok = true; }
			it->Release( );
		}
	}
	d->Release( );
	return ok;
}

static bool pickFiles( HWND owner, const wchar_t *title, bool multi, bool exeOnly, std::vector<str> *out )
{
	IFileOpenDialog *d = 0;
	if ( CoCreateInstance( CLSID_FileOpenDialog, 0, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog, (void **)&d ) != S_OK ) return false;
	DWORD opt = 0; d->GetOptions( &opt ); d->SetOptions( opt | FOS_FORCEFILESYSTEM | ( multi ? FOS_ALLOWMULTISELECT : 0 ) );
	d->SetTitle( title );
	COMDLG_FILTERSPEC f1[] = { { L"GOG installer (*.exe)", L"*.exe" }, { L"All files", L"*.*" } };
	COMDLG_FILTERSPEC f2[] = { { L"Game files (*.pk3, setup .exe)", L"*.pk3;*.exe" }, { L"All files", L"*.*" } };
	if ( exeOnly ) d->SetFileTypes( 2, f1 ); else d->SetFileTypes( 2, f2 );
	bool ok = false;
	if ( d->Show( owner ) == S_OK ) {
		IShellItemArray *arr = 0;
		if ( d->GetResults( &arr ) == S_OK ) {
			DWORD n = 0; arr->GetCount( &n );
			for ( DWORD i = 0; i < n; i++ ) {
				IShellItem *it = 0; PWSTR p = 0;
				if ( arr->GetItemAt( i, &it ) == S_OK ) {
					if ( it->GetDisplayName( SIGDN_FILESYSPATH, &p ) == S_OK ) { out->push_back( U( p ) ); CoTaskMemFree( p ); ok = true; }
					it->Release( );
				}
			}
			arr->Release( );
		}
	}
	d->Release( );
	return ok;
}

/* ------------------------------------------------------------ progress panel */

struct CopyDlg : Wnd {
	lc_copyjob *job; volatile unsigned long long progress = 0; volatile signed char cancel = 0;
	volatile int finished = 0; int rc = 0; char err[512] = ""; lc_thread *th = 0; unsigned long long total = 0;
	HWND bar = 0, lbl = 0; str homeBase; bool isUnpack = false;
	static void worker( void *u )
	{
		CopyDlg *d = (CopyDlg *)u;
		d->rc = lc_copyjob_run( d->job, &d->progress, &d->cancel, d->err, sizeof d->err );
		d->finished = 1;
	}
	void onCommand( int id, int code, HWND ) override { if ( id == IDCANCEL ) cancel = 1; }
	bool onClose( ) override { cancel = 1; return true; }
	void update( )
	{
		unsigned long long p = progress;
		SendMessageW( bar, PBM_SETPOS, total ? (WPARAM)( p * 1000 / total ) : 0, 0 );
		char t[512]; lc_copyjob_progress_text( job, p, t, sizeof t );
		str cur = getText( lbl ); if ( cur != t ) setText( lbl, t );
	}
	void onTimer( int id ) override
	{
		if ( id != 1 ) return;
		update( );
		if ( finished ) { KillTimer( hwnd, 1 ); end( rc ); }
	}
};

bool dataCopyJobRun( HWND owner, lc_copyjob *job, const str &homeBase )
{
	CopyDlg d; d.job = job; d.total = lc_copyjob_total( job ); d.homeBase = homeBase; d.isUnpack = lc_copyjob_is_unpack( job ) != 0;
	char title[128]; lc_copyjob_title( job, title, sizeof title );
	const int W0 = 440;
	d.create( W( title ).c_str( ), W0, 130, WS_POPUP | WS_CAPTION, 0, owner );
	d.lbl = d.label( 20, 18, W0 - 40, 36, "", 12, 400, R_TEXT, DT_WORDBREAK | DT_PATH_ELLIPSIS );
	d.bar = d.ctl_( PROGRESS_CLASSW, L"", PBS_SMOOTH, 0, 20, 62, W0 - 40, 14, 60 );
	SendMessageW( d.bar, PBM_SETRANGE32, 0, 1000 );
	d.button( W0 - 20 - 84, 90, 84, 28, "Cancel", IDCANCEL, true );
	d.update( );
	d.th = lc_thread_start( CopyDlg::worker, &d );
	SetTimer( d.hwnd, 1, 20, 0 );
	if ( !d.th ) { d.rc = 0; strcpy( d.err, "Couldn't start the copy." ); d.finished = 1; }
	int rc = d.runModal( );
	if ( d.th ) lc_thread_join( d.th );
	bool ok = d.rc == 1;
	if ( d.rc == 0 ) {
		lc_log( "Copy failed: %s", d.err );
		if ( d.isUnpack )
			Alert( owner, "Unpacking the GOG installer failed",
				fmt( "%s.\n\nThe partly unpacked files were removed. If the download is damaged, download it again from GOG; otherwise: %s", d.err, MSG_USE_BASEEF ), { "OK" }, 1 );
		else
			Alert( owner, "Copying the game files failed", fmt( "%s\n\nThe partial copy was removed.", d.err ), { "OK" }, 1 );
	} else if ( d.rc < 0 ) lc_log( "Copy cancelled" );
	(void)rc;
	lc_copyjob_free( job );
	return ok;
}

static bool checkSpace( HWND owner, unsigned long long total, const str &homeBase, const char *title, const str &text )
{
	lc_mkdir_p( homeBase.c_str( ) );
	unsigned long long freeB = lc_free_space( homeBase.c_str( ) );
	if ( freeB && freeB < total + ( 64ull << 20 ) ) {
		Alert( owner, title, text, { "OK" }, 1 );
		return false;
	}
	return true;
}
static str bytes( unsigned long long n ) { char b[64]; lc_format_bytes( n, b, sizeof b ); return b; }

/* ------------------------------------------------------------ copy and unpack */

static bool copyGameFiles( HWND owner, const str &srcBase )
{
	int pk3s = 0, saves = 0, kept = 0;
	lc_copyjob *job = lc_plan_copy_game_files( srcBase.c_str( ), G.homeBase.c_str( ), &pk3s, &saves, &kept );
	if ( !job ) { Alert( owner, "Copying the game files failed", "Nothing to copy was found in that folder.", { "OK" }, 1 ); return false; }
	unsigned long long total = lc_copyjob_total( job );
	lc_log( "Copying %d .pk3 files and %d saves (%d existing saves kept) from \"%s\" to \"%s\": %s", pk3s, saves, kept,
		srcBase.c_str( ), G.homeBase.c_str( ), bytes( total ).c_str( ) );
	if ( !checkSpace( owner, total, G.homeBase, "Not enough disk space",
		fmt( "Copying the game files needs %s, but only %s is free on the disk containing\n%s.\n\nFree up some space, or tick \"Use BaseEF files in place rather than copy\" to use the game folder directly.",
			bytes( total + ( 64ull << 20 ) ).c_str( ), bytes( lc_free_space( G.homeBase.c_str( ) ) ).c_str( ), G.homeBase.c_str( ) ) ) ) {
		lc_copyjob_free( job ); return false;
	}
	if ( !dataCopyJobRun( owner, job, G.homeBase ) ) return false;
	char p0[LC_MAX_PATH];
	if ( !lc_child_ci( G.homeBase.c_str( ), "pak0.pk3", p0, sizeof p0 ) ) {
		Alert( owner, "Copying the game files failed", "pak0.pk3 did not end up in the destination folder.", { "OK" }, 1 );
		return false;
	}
	lc_log( "Copied %s of game files to \"%s\"", bytes( total ).c_str( ), G.homeBase.c_str( ) );
	return true;
}

static str externalBase( )
{
	const char *f = lc_store_get( lc_store_shared( ), LC_KEY_GAMEFOLDER );
	char b[LC_MAX_PATH];
	if ( f && *f && lc_child_ci( f, LC_BASEGAME, b, sizeof b ) ) return b;
	return str( );
}

/* returns the number of files unpacked, 0 for nothing to do, -1 for a failure (already shown) */
static int unpackInstaller( HWND owner, const str &exe, bool onlyNew )
{
	lc_log( "GOG installer: reading \"%s\"", exe.c_str( ) );
	char err[256] = ""; int present = 0;
	str ext = externalBase( );
	lc_copyjob *job = lc_plan_unpack_installer( exe.c_str( ), onlyNew ? 1 : 0, G.homeBase.c_str( ), ext.empty( ) ? 0 : ext.c_str( ), &present, err, sizeof err );
	if ( !job && err[0] ) {
		lc_log( "GOG installer: %s", err );
		Alert( owner, "This installer can't be used",
			fmt( "%s\n\nThis app can't unpack it: %s.\n\n%s", lc_path_basename( exe.c_str( ) ), err, MSG_USE_BASEEF ), { "OK" }, 1 );
		return -1;
	}
	if ( !job || lc_copyjob_count( job ) == 0 ) {
		if ( job ) lc_copyjob_free( job );
		Alert( owner, "Nothing to add", "The game already has all of this installer's .pk3 files.", { "OK" }, 0 );
		return 0;
	}
	unsigned long long total = lc_copyjob_total( job );
	int count = lc_copyjob_count( job );
	lc_log( "GOG installer: %d .pk3 files (%s to unpack), %d already present", count, bytes( total ).c_str( ), present );
	if ( !checkSpace( owner, total, G.homeBase, "Not enough disk space",
		fmt( "Unpacking the game files needs %s, but only %s is free.", bytes( total + ( 64ull << 20 ) ).c_str( ), bytes( lc_free_space( G.homeBase.c_str( ) ) ).c_str( ) ) ) ) {
		lc_copyjob_free( job ); return -1;
	}
	if ( !dataCopyJobRun( owner, job, G.homeBase ) ) return -1;
	lc_log( "GOG installer: unpacked %d files (%s) to \"%s\"", count, bytes( total ).c_str( ), G.homeBase.c_str( ) );
	return count;
}

/* ---------------------------------------------------------------- chooser */

struct ChooserDlg : Wnd {
	str path; bool inPlace = false; HWND chk = 0, status = 0;
	void sync( )
	{
		inPlace = SendMessageW( chk, BM_GETCHECK, 0, 0 ) == BST_CHECKED;
		setText( status, inPlace ? "Files will be read from this folder; nothing is copied." :
			"Files will be copied into the app's data folder (about 600 MB for a standard install)." );
	}
	void onCommand( int id, int code, HWND ) override
	{
		if ( id == 11 ) sync( );
		else if ( id == 12 && code == BN_CLICKED ) { str p; if ( pickFolder( hwnd, L"Choose", &p ) ) { path = p; end( 1 ); } }
		else if ( id == 13 && code == BN_CLICKED ) { std::vector<str> f; if ( pickFiles( hwnd, L"Choose GOG's installer", false, true, &f ) && !f.empty( ) ) { path = f[0]; end( 2 ); } }
		else if ( id == IDCANCEL ) end( 0 );
	}
};

static bool choosePath( HWND owner, str *path, bool *inPlace )
{
	if ( const char *t = testEnv( "VOYAGERSP_TEST_CHOOSE" ) ) {
		*path = t; *inPlace = testOn( "VOYAGERSP_TEST_INPLACE" );
		return true;
	}
	ChooserDlg d;
	const int W0 = 520;
	str explain = "Select the folder where Star Trek: Voyager \xE2\x80\x93 Elite Force is installed (the one containing BaseEF with pak0.pk3), the BaseEF folder itself, or GOG's Windows installer (the setup .exe). By default the game files are copied into the app's own data folder, so the original folder or CD is no longer needed.";
	int eh = textHeight( W( explain ), W0 - 40, F( 12 ) ) * 96 / g_dpi;
	int H = 20 + 22 + 8 + eh + 14 + 22 + 6 + 18 + 22 + 30 + 20;
	d.create( W( "Choose your Elite Force folder, or GOG's installer" ).c_str( ), W0, H, WS_POPUP | WS_CAPTION | WS_SYSMENU, 0, owner );
	int y = 20;
	d.label( 20, y, W0 - 40, 22, "Choose your Elite Force folder, or GOG's installer", 15, 700 ); y += 30;
	d.label( 20, y, W0 - 40, eh + 2, explain.c_str( ), 12, 400, R_TEXT2 ); y += eh + 14;
	d.chk = d.check( 20, y, W0 - 40, 22, "Use BaseEF files in place rather than copy", 11, false ); y += 26;
	d.status = d.label( 40, y, W0 - 60, 18, "", 11, 400, R_TEXT2 ); y += 18 + 22;
	d.sync( );
	int x = W0 - 20;
	x -= 84; d.button( x, y, 84, 28, "Cancel", IDCANCEL ); x -= 8;
	x -= 190; d.button( x, y, 190, 28, "Choose GOG Installer\xE2\x80\xA6", 13 ); x -= 8;
	x -= 150; d.button( x, y, 150, 28, "Choose Folder\xE2\x80\xA6", 12, true );
	int r = d.runModal( );
	if ( r == 0 ) return false;
	*path = d.path; *inPlace = d.inPlace;
	return true;
}

static str parentDir( const str &p ) { char b[LC_MAX_PATH]; lc_path_dirname( b, sizeof b, p.c_str( ) ); return b; }

bool dataRunChooser( HWND owner )
{
	bool test = lc_env( "VOYAGERSP_TEST_CHOOSE" ) != 0;
	for ( ;; ) {
		str path; bool inPlace = false;
		if ( !choosePath( owner, &path, &inPlace ) ) { lc_log( "Game folder selection cancelled" ); return false; }
		if ( lc_is_installer( path.c_str( ) ) ) {
			int n = unpackInstaller( owner, path, false );
			char p0[LC_MAX_PATH];
			if ( n > 0 && lc_child_ci( G.homeBase.c_str( ), "pak0.pk3", p0, sizeof p0 ) ) { lc_remember_game_folder( 0 ); return true; }
			if ( test ) return false;
			continue;
		}
		char base[LC_MAX_PATH], problem[512] = "";
		if ( lc_validate_choice( path.c_str( ), inPlace ? 1 : 0, base, sizeof base, problem, sizeof problem ) != 0 ) {
			lc_log( "Rejected \"%s\": %s", path.c_str( ), problem );
			Alert( owner, "That folder can't be used", path + "\n\n" + problem, { "OK" }, 1 );
			if ( test ) return false;
			continue;
		}
		if ( inPlace ) {
			str folder = parentDir( base );
			lc_remember_game_folder( folder.c_str( ) );
			lc_log( "Using game files in place from \"%s\" (remembered)", base );
			return true;
		}
		if ( copyGameFiles( owner, base ) ) { lc_remember_game_folder( 0 ); return true; }
		if ( test ) return false;
	}
}

/* --------------------------------------------------------- Add Game Files... */

struct AddDlg : Wnd {
	std::vector<str> picks;
	void onCommand( int id, int code, HWND ) override
	{
		if ( code != BN_CLICKED && id != IDCANCEL ) return;
		if ( id == 12 ) { std::vector<str> f; if ( pickFiles( hwnd, L"Add", true, false, &f ) ) { picks = f; end( 1 ); } }
		else if ( id == 13 ) { str p; if ( pickFolder( hwnd, L"Add", &p ) ) { picks.push_back( p ); end( 1 ); } }
		else if ( id == IDCANCEL ) end( 0 );
	}
};

int dataAddGameFiles( HWND owner )
{
	std::vector<str> picks;
	if ( const char *t = testEnv( "VOYAGERSP_TEST_ADD" ) ) picks.push_back( t );
	else {
		AddDlg d; const int W0 = 520;
		str msg = "Choose .pk3 files, a folder that contains them (such as BaseEF of the Expansion Pack or its CD), or GOG's installer (setup_\xE2\x80\xA6.exe)";
		int eh = textHeight( W( msg ), W0 - 40, F( 13 ) ) * 96 / g_dpi;
		d.create( L"Add Game Files", W0, 20 + eh + 22 + 30 + 20, WS_POPUP | WS_CAPTION | WS_SYSMENU, 0, owner );
		d.label( 20, 20, W0 - 40, eh + 2, msg.c_str( ), 13, 400 );
		int y = 20 + eh + 22, x = W0 - 20;
		x -= 84; d.button( x, y, 84, 28, "Cancel", IDCANCEL ); x -= 8;
		x -= 130; d.button( x, y, 130, 28, "Choose Folder\xE2\x80\xA6", 13 ); x -= 8;
		x -= 130; d.button( x, y, 130, 28, "Choose Files\xE2\x80\xA6", 12, true );
		if ( d.runModal( ) != 1 ) return 0;
		picks = d.picks;
	}
	str ext = externalBase( );
	int total = 0; bool anyOther = false;
	std::vector<str> others;
	for ( auto &p : picks ) {
		if ( lc_is_installer( p.c_str( ) ) ) { int n = unpackInstaller( owner, p, true ); if ( n > 0 ) total += n; }
		else { others.push_back( p ); anyOther = true; }
	}
	if ( !anyOther ) return total;
	std::vector<const char *> cp; for ( auto &s : others ) cp.push_back( s.c_str( ) );
	int present = 0;
	lc_copyjob *job = lc_plan_add_files( cp.data( ), (int)cp.size( ), G.homeBase.c_str( ), ext.empty( ) ? 0 : ext.c_str( ), &present );
	if ( !job || lc_copyjob_count( job ) == 0 ) {
		if ( job ) lc_copyjob_free( job );
		lc_log( "Add game files: 0 new .pk3, %d already present", present );
		Alert( owner, "Nothing to add", present == 1 ? "The game already has that .pk3 file." :
			present > 1 ? "The game already has all of those .pk3 files." :
			"No .pk3 files were found there. Choose .pk3 files, or the BaseEF folder that contains them.", { "OK" }, 0 );
		return total;
	}
	unsigned long long size = lc_copyjob_total( job ); int n = lc_copyjob_count( job );
	lc_log( "Add game files: %d new .pk3 (%s), %d already present", n, bytes( size ).c_str( ), present );
	if ( !checkSpace( owner, size, G.homeBase, "Not enough disk space",
		fmt( "Adding the files needs %s, but only %s is free.", bytes( size ).c_str( ), bytes( lc_free_space( G.homeBase.c_str( ) ) ).c_str( ) ) ) ) {
		lc_copyjob_free( job ); return total;
	}
	if ( dataCopyJobRun( owner, job, G.homeBase ) ) {
		lc_log( "Added %d files (%s) to \"%s\"", n, bytes( size ).c_str( ), G.homeBase.c_str( ) );
		total += n;
	}
	return total;
}

}	// namespace wl
