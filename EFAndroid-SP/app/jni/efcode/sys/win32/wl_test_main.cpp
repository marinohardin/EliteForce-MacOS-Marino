// Standalone launcher test driver (not part of the game build): runs Sys_WinLauncher and prints
// the command line it produced, like the engine's main() would use it.
extern "C" int Sys_WinLauncher( int argc, char **argv, char *commandLine, int size );
#include <stdio.h>
#include <string.h>

int main( int argc, char **argv )
{
	char cmd[1024] = "+set com_homepath \"EliteForce-Marino\" ";
	int r = Sys_WinLauncher( argc, argv, cmd, sizeof cmd );
	fprintf( stderr, "[wltest] launcher returned %d\ncommandLine: %s\n", (int)r, cmd );
	fflush( stderr );
	return 0;
}
