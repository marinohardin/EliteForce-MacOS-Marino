// bink_stub.c — no-op Bink backend for builds without FFmpeg (EF_BINK=OFF).
// cl_cin.c treats a failed Bink_Open as "no cinematic" and moves on.
#include <string.h>
#include "bink_ff.h"

int Bink_IsBinkFile( const char *name ) {
	size_t n = name ? strlen( name ) : 0;
	return n > 4 && !strcasecmp( name + n - 4, ".bik" );
}
int  Bink_Open( int handle, const char *name, int *outW, int *outH ) { return 0; }
int  Bink_Update( int handle, int startMs, int nowMs, int silent, unsigned char **outBuf ) { return 0; }
void Bink_Restart( int handle ) { }
void Bink_Stop( int handle ) { }
