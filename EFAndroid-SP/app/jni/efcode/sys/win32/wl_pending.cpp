#include "wl_app.h"
namespace wl {
bool showBrowser( HWND, const str &, str *, str * ) { return false; }
void showCoopSheet( HWND, bool, const str &, bool, bool * ) {}
void runServerWindow( lc_server_settings *, const str &, const str &, const std::vector<str> & ) {}
}
