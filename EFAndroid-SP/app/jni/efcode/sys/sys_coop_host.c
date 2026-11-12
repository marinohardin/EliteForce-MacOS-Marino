/*
===========================================================================
Single-player co-op, engine side for Windows and Linux: the host answers co-op
checks on its UDP port, runs the host's side of the connection (sys_coop_net.c)
and keeps the game going in the background. This is the portable port of the
engine part of sys_osx_coop.m (the macOS launcher's Co-op sheet is the launcher
core's lc_coop.c and the platform UI).

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
It is compiled only in EF_COOP builds on Windows and Linux.

The host's answer: single player runs with the engine's networking off
(net_enabled is 0 there), so a co-op host opens a small UDP socket of its own on
VSP_COOP_PORT (29760) while sp_coop is 1 and answers one connectionless query,
polled once a frame from Com_Frame:
  -> "\xff\xff\xff\xffgetcoopinfo [challenge]"
  <- "\xff\xff\xff\xffcoopInfoResponse\n" and an infostring with coopProtocol,
     version, hostname (the computer's name), state (playing / menu), map,
     mission, skill, players, maxPlayers, time, player, needPassword, guest and
     the challenge (up to 32 letters, digits, - or _ of the query).
Every other packet on the port goes to the co-op connection (sys_coop_net.c).
Replies are rate limited as the server's getinfo is: 10 a second per address,
10 per 100 ms in all.

The launcher's password hand-over: the Windows launcher runs in this process and
calls Sys_CoopSetLaunchPassword; the Linux launcher (a separate process) puts it
in the environment variable EF_COOP_PASSWORD for the exec, and it is taken from
there (and removed) the first time Sys_CoopFrame runs.

Test hook: VOYAGERSP_TEST_COOP_PORT=<n> hosts on UDP port n.
===========================================================================
*/

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

#include "sys_coop_net.h"

#ifdef EF_COOP

#ifdef _WIN32
#	include <windows.h>
typedef SOCKET coopSock_t;
#	define COOP_BADSOCK		INVALID_SOCKET
#	define coopClose( s )	closesocket( s )
#	define COOP_ERRNO()		WSAGetLastError()
#	define COOP_STRERROR( e )	va( "error %d", e )
#else
typedef int coopSock_t;
#	define COOP_BADSOCK		(-1)
#	define coopClose( s )	close( s )
#	define COOP_ERRNO()		errno
#	define COOP_STRERROR( e )	strerror( e )
#endif

#define COOP_QUERY		"getcoopinfo"
#define COOP_RESPONSE	"coopInfoResponse"
#define COOP_OOB		"\xff\xff\xff\xff"

#ifndef EF_APP_VERSION
#define EF_APP_VERSION	"unknown"
#endif

// sp/sp_bridge.cpp
extern int SP_IsActive( void );
extern const char *SP_CurMap( void );

typedef struct {
	int		lastTime;
	int		burst;
} coopBucket_t;

typedef struct {
	unsigned int	addr;
	coopBucket_t	bucket;
} coopAddrBucket_t;

static coopSock_t		coopSocket = COOP_BADSOCK;
static int				coopRetryTime;		// a failed bind tries again then
static qboolean			coopBindFailed;		// said so once
static char				coopHostName[64];
static coopBucket_t		coopOutbound;
static coopAddrBucket_t	coopAddrs[32];

// A leaky bucket, as the server's SVC_RateLimit: at most burst events per period ms.
static qboolean CoopRateLimited( coopBucket_t *b, int burst, int period )
{
	int now = Sys_Milliseconds();
	int interval = now - b->lastTime;
	int expired = interval / period;

	if ( expired > b->burst || interval < 0 )
	{
		b->burst = 0;
		b->lastTime = now;
	}
	else
	{
		b->burst -= expired;
		b->lastTime = now - interval % period;
	}
	if ( b->burst < burst )
	{
		b->burst++;
		return qfalse;
	}
	return qtrue;
}

static qboolean CoopAddressLimited( unsigned int addr )
{
	coopAddrBucket_t *slot = NULL, *oldest = &coopAddrs[0];

	for ( int i = 0; i < (int)ARRAY_LEN( coopAddrs ); i++ )
	{
		if ( coopAddrs[i].addr == addr )
		{
			slot = &coopAddrs[i];
			break;
		}
		if ( coopAddrs[i].bucket.lastTime < oldest->bucket.lastTime )
			oldest = &coopAddrs[i];
	}
	if ( !slot )
	{
		slot = oldest;
		memset( slot, 0, sizeof( *slot ) );
		slot->addr = addr;
		slot->bucket.lastTime = Sys_Milliseconds();
	}
	return CoopRateLimited( &slot->bucket, 10, 1000 );
}

// An infostring value: no \ ; " or control characters, at most size - 1 bytes,
// never ending inside a UTF-8 character.
static void CoopCleanValue( const char *in, char *out, int size )
{
	int n = 0, i;

	for ( ; *in && n < size - 1; in++ )
	{
		unsigned char c = (unsigned char)*in;
		out[n++] = ( c < ' ' || c == 0x7f || c == '\\' || c == ';' || c == '"' ) ? ' ' : (char)c;
	}
	out[n] = 0;
	if ( n && ( (unsigned char)out[n - 1] & 0x80 ) )
	{
		unsigned char lead;
		int need;
		for ( i = n - 1; i > 0 && ( (unsigned char)out[i] & 0xc0 ) == 0x80; i-- )
			;
		lead = (unsigned char)out[i];
		need = lead >= 0xf0 ? 4 : lead >= 0xe0 ? 3 : lead >= 0xc0 ? 2 : 1;
		if ( n - i < need )
			out[i] = 0;
	}
}

static void CoopSetInfo( char *info, const char *key, const char *value )
{
	char clean[128];

	CoopCleanValue( value, clean, sizeof( clean ) );
	Info_SetValueForKey( info, key, clean );
}

// the port this host listens on (VOYAGERSP_TEST_COOP_PORT: see the top of this file)
static int CoopHostPort( void )
{
	const char *test = getenv( "VOYAGERSP_TEST_COOP_PORT" );
	int port = test ? atoi( test ) : 0;

	return ( port > 0 && port < 65536 ) ? port : VSP_COOP_PORT;
}

// the computer's name, for the co-op check's "hostname" and a player without coop_name
void Sys_CoopMacName( char *out, int size )
{
#ifdef _WIN32
	wchar_t w[256];
	DWORD n = (DWORD)ARRAY_LEN( w );
	char utf8[512];

	if ( GetComputerNameExW( ComputerNameDnsHostname, w, &n ) && n &&
		WideCharToMultiByte( CP_UTF8, 0, w, -1, utf8, sizeof( utf8 ), NULL, NULL ) > 0 )
		Q_strncpyz( out, utf8, size );
	else
		Q_strncpyz( out, "PC", size );
#else
	char name[256];

	if ( gethostname( name, sizeof( name ) ) == 0 && name[0] )
	{
		name[sizeof( name ) - 1] = 0;
		Q_strncpyz( out, name, size );
	}
	else
		Q_strncpyz( out, "Computer", size );
#endif
}

static void CoopOpenSocket( void )
{
	struct sockaddr_in sa;
	coopSock_t fd;
	int err = 0, failed;

	if ( coopRetryTime && Sys_Milliseconds() - coopRetryTime < 0 )
		return;
	fd = socket( AF_INET, SOCK_DGRAM, 0 );
	if ( fd == COOP_BADSOCK )
	{
		coopRetryTime = Sys_Milliseconds() + 5000;
		return;
	}
	memset( &sa, 0, sizeof( sa ) );
	sa.sin_family = AF_INET;
	sa.sin_port = htons( (unsigned short)CoopHostPort() );
	sa.sin_addr.s_addr = htonl( INADDR_ANY );
	failed = bind( fd, (struct sockaddr *)&sa, sizeof( sa ) ) != 0;
	if ( failed )
		err = COOP_ERRNO();
	else
	{
#ifdef _WIN32
		u_long on = 1;
		failed = ioctlsocket( fd, FIONBIO, &on ) != 0;
#else
		failed = fcntl( fd, F_SETFL, O_NONBLOCK ) < 0;
#endif
		if ( failed )
			err = COOP_ERRNO();
	}
	if ( failed )
	{
		if ( !coopBindFailed )
			Com_Printf( "co-op: can't answer co-op checks, UDP port %d is in use (%s); trying again every 5 s\n",
				CoopHostPort(), COOP_STRERROR( err ) );
		coopBindFailed = qtrue;
		coopClose( fd );
		coopRetryTime = Sys_Milliseconds() + 5000;
		return;
	}
	coopSocket = fd;
	coopRetryTime = 0;
	coopBindFailed = qfalse;
	Sys_CoopMacName( coopHostName, sizeof( coopHostName ) );
	Com_Printf( "co-op: answering co-op checks on UDP port %d\n", CoopHostPort() );
}

static void CoopCloseSocket( void )
{
	if ( coopSocket == COOP_BADSOCK )
		return;
	CoopNet_HostShutdown( (int)coopSocket, "the host stopped co-op" );
	coopClose( coopSocket );
	coopSocket = COOP_BADSOCK;
	Com_Printf( "co-op: no longer answering co-op checks\n" );
}

// A co-op check: answer it (unless rate limited) and return qtrue; qfalse for any other packet.
static qboolean CoopAnswer( const char *packet, int len, const struct sockaddr_in *from )
{
	char cmd[64], challenge[33], info[MAX_INFO_STRING], reply[MAX_INFO_STRING + 32];
	const char *p;
	int n = 0;
	qboolean playing;

	// "\xff\xff\xff\xffgetcoopinfo" and an optional challenge; anything else is dropped
	if ( len < 4 + (int)strlen( COOP_QUERY ) || memcmp( packet, COOP_OOB, 4 ) )
		return qfalse;
	len = len - 4 < (int)sizeof( cmd ) - 1 ? len - 4 : (int)sizeof( cmd ) - 1;
	memcpy( cmd, packet + 4, len );
	cmd[len] = 0;
	if ( strncmp( cmd, COOP_QUERY, strlen( COOP_QUERY ) ) )
		return qfalse;
	p = cmd + strlen( COOP_QUERY );
	if ( *p && *p != ' ' && *p != '\n' )
		return qfalse;
	while ( *p == ' ' )
		p++;
	for ( ; n < (int)sizeof( challenge ) - 1 && ( isalnum( (unsigned char)*p ) || *p == '-' || *p == '_' ); p++ )
		challenge[n++] = *p;
	challenge[n] = 0;

	if ( CoopAddressLimited( (unsigned int)from->sin_addr.s_addr ) || CoopRateLimited( &coopOutbound, 10, 100 ) )
		return qtrue;

	playing = SP_IsActive() && SP_CurMap()[0];
	info[0] = 0;
	CoopSetInfo( info, "coopProtocol", va( "%d", VSP_COOP_PROTOCOL ) );
	CoopSetInfo( info, "version", EF_APP_VERSION );
	CoopSetInfo( info, "hostname", coopHostName );
	if ( Cvar_VariableString( "coop_name" )[0] )
		CoopSetInfo( info, "player", Cvar_VariableString( "coop_name" ) );
	CoopSetInfo( info, "state", playing ? "playing" : "menu" );
	if ( playing )
	{
		CoopSetInfo( info, "map", SP_CurMap() );
		CoopSetInfo( info, "mission", Cvar_VariableString( "coop_mission" ) );
		CoopSetInfo( info, "time", va( "%d", Cvar_VariableIntegerValue( "coop_time" ) ) );
	}
	CoopSetInfo( info, "skill", va( "%d", Cvar_VariableIntegerValue( "g_spskill" ) ) );
	CoopSetInfo( info, "players", va( "%d", playing ? ( Cvar_VariableIntegerValue( "coop_players" ) > 1 ? Cvar_VariableIntegerValue( "coop_players" ) : 1 ) : 1 ) );
	CoopSetInfo( info, "maxPlayers", "2" );
	CoopSetInfo( info, "needPassword", CoopNet_HostNeedsPassword() ? "1" : "0" );
	CoopSetInfo( info, "guest", CoopNet_HostHasGuest() ? "1" : "0" );
	if ( challenge[0] )
		CoopSetInfo( info, "challenge", challenge );

	n = Com_sprintf( reply, sizeof( reply ), COOP_OOB COOP_RESPONSE "\n%s", info );
	sendto( coopSocket, reply, n, 0, (const struct sockaddr *)from, sizeof( *from ) );
	return qtrue;
}

/*
=================
Keeping a co-op game going in the background

With coop_backgroundPlay 1 (the default; the launcher's "Keep playing in the
background"), while this copy hosts (single player with sp_coop 1) or is a
connected guest, the unfocused / minimized frame-rate caps (com_maxfpsUnfocused,
com_maxfpsMinimized) don't apply (Com_Frame asks Sys_CoopBackgroundPlay).
Keys and the mouse still go only to the window in front.
=================
*/
static cvar_t		*coop_backgroundPlay;

int Sys_CoopBackgroundPlay( void )
{
	if ( !coop_backgroundPlay || !coop_backgroundPlay->integer || Com_Holomatch() || Com_DedicatedServer() )
		return 0;
	if ( CoopNet_GuestActive() )
		return 1;		// a guest (connecting or connected)
	return coopSocket != COOP_BADSOCK && Cvar_VariableIntegerValue( "sp_coop" ) == 1;	// a host
}

static void CoopBackgroundFrame( void )
{
	static qboolean was;
	qboolean want = Sys_CoopBackgroundPlay() != 0;

	if ( want == was )
		return;
	was = want;
	if ( want )
		Com_Printf( "co-op: the game keeps playing in the background\n" );
}

/*
=================
Sys_CoopFrame

Once a frame (Com_Frame, EF_COOP builds): a guest (coop_connect set) runs
its connection; while single player runs with sp_coop 1 (and this copy isn't a
guest), answer co-op checks on VSP_COOP_PORT and run the host's side of the
connection. Does nothing else otherwise.
=================
*/
void Sys_CoopFrame( void )
{
	static qboolean registered;
	char packet[COOP_MAX_PACKET];

	if ( !registered && !Com_Holomatch() && !Com_DedicatedServer() )
	{
		const char *pw;

		registered = qtrue;
#ifdef _WIN32
		{
			WSADATA wsa;
			WSAStartup( MAKEWORD( 2, 2 ), &wsa );
		}
#endif
		// the Linux launcher hands the password over in the environment (never on the command line)
		pw = getenv( "EF_COOP_PASSWORD" );
		if ( pw )
		{
			if ( pw[0] )
				Sys_CoopSetLaunchPassword( pw );
#ifdef _WIN32
			_putenv( "EF_COOP_PASSWORD=" );
#else
			unsetenv( "EF_COOP_PASSWORD" );
#endif
		}
		// so coop_connect, coop_password and coop_status work from the console
		Cvar_Get( "coop_connect", "", CVAR_TEMP );
		Cvar_Get( "coop_password", "", CVAR_TEMP );
		Cvar_Get( "coop_name", "", CVAR_TEMP );		// "Your name" (empty: this computer's name)
		Cvar_Get( "coop_sex", "", CVAR_TEMP );		// a guest's Munro: male / female (empty: like the host's)
		coop_backgroundPlay = Cvar_Get( "coop_backgroundPlay", "1", CVAR_ARCHIVE );
		CoopNet_AddCommands();
	}
	CoopNet_GuestFrame();
	if ( Com_Holomatch() || Com_DedicatedServer() || Cvar_VariableIntegerValue( "sp_coop" ) != 1 || CoopNet_GuestActive() )
	{
		CoopCloseSocket();
		CoopBackgroundFrame();
		return;
	}
	if ( coopSocket == COOP_BADSOCK )
		CoopOpenSocket();
	for ( int i = 0; coopSocket != COOP_BADSOCK && i < 16; i++ )
	{
		struct sockaddr_in from;
#ifdef _WIN32
		int fromLen = sizeof( from );
#else
		socklen_t fromLen = sizeof( from );
#endif
		int n = (int)recvfrom( coopSocket, packet, sizeof( packet ), 0, (struct sockaddr *)&from, &fromLen );

		if ( n < 0 )
		{
#ifdef _WIN32
			// an ICMP "port unreachable" for an answer we sent shows up here; keep reading
			if ( WSAGetLastError() == WSAECONNRESET )
				continue;
#endif
			break;
		}
		if ( n == 0 )
			break;
		if ( fromLen == (int)sizeof( from ) && from.sin_family == AF_INET && !CoopAnswer( packet, n, &from ) )
			CoopNet_HostPacket( (int)coopSocket, (const unsigned char *)packet, n, &from );
	}
	if ( coopSocket != COOP_BADSOCK )
		CoopNet_HostFrame( (int)coopSocket );
	CoopBackgroundFrame();
}

#endif // EF_COOP
