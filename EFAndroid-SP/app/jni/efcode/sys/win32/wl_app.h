/*
===========================================================================
Windows launcher: declarations shared by the launcher's windows.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#ifndef WL_APP_H
#define WL_APP_H

#include "wl_ui.h"

extern "C" {
#include "../../launcher/lc_settings.h"
#include "../../launcher/lc_launch.h"
#include "../../launcher/lc_data.h"
#include "../../launcher/lc_update.h"
#include "../../launcher/lc_links.h"
#include "../../launcher/lc_server.h"
#include "../../launcher/lc_net.h"
#include "../../launcher/lc_host.h"
#include "../../launcher/lc_nat.h"
#include "../../launcher/lc_coop.h"
}

#ifndef EF_APP_VERSION
#define EF_APP_VERSION "1.4.0"
#endif

namespace wl {

#define PLATFORM_NOUN LC_PLATFORM_NOUN
#define MSG_USE_BASEEF "Install the game from GOG (or from the CD, with the 1.2 patch), then choose the folder it installed to."

/* shared state of the launcher process */
struct Shared {
	str homeBase, homeRoot;
	str dataFolder;						/* the BaseEF folder in use, "" if none */
	str basepath;						/* fs_basepath to pass, "" if none */
	bool haveData = false, overridden = false, missingPatch = false;
	bool canPlay( ) const { return haveData && !missingPatch; }
	str publicIP;						/* from the router, "" if unknown */
	bool publicLookupRunning = false;
	int argc = 0; char **argv = 0;
};
extern Shared G;

/* wl_data.cpp */
bool dataRunChooser( HWND owner );				/* §4.2; true = a game folder was chosen */
int dataAddGameFiles( HWND owner );				/* §4.4; number of files added */
bool dataCopyJobRun( HWND owner, lc_copyjob *job, const str &homeBase );		/* progress panel; frees the job */
void dataRefreshLookup( );						/* lc_data_find into G */
extern lc_data g_data;

/* wl_server.cpp */
void showServerConfig( HWND owner, lc_server_settings *settings, const str &dataFolder );
void showMasters( HWND owner );
str masterSummary( int *count, bool *orange, str *tip );
void serverSettingsLoad( lc_server_settings *s );
void publicAddressStart( );
bool loopbackFix( );
void setLoopbackFix( bool on );
bool snapshotConfigOffscreen( );

/* wl_browser.cpp */
bool showBrowser( HWND owner, const str &dataFolder, str *address, str *password );

/* wl_coop.cpp */
void showCoopSheet( HWND owner, bool canPlay, const str &prefillAddress, bool autoCheck, bool *playNow );

/* wl_srvwin.cpp: the --host-server instance */
void runServerWindow( lc_server_settings *settings, const str &basepath, const str &dataFolder,
	const std::vector<str> &clientArgs );

/* wl_main.cpp */
str appVersion( );
str publicAddress( );
bool publicAddressPending( );
str logHome( );
void showAbout( HWND owner );
bool spawnSelf( const std::vector<str> &args, long *pid, str *err );
bool activateInstanceWindow( long pid );

}	// namespace wl

#endif
