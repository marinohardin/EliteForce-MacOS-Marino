/*
===========================================================================
Single-player co-op: the connection between a host and its guest.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
It is compiled only in EF_COOP builds (see the macOS CMakeLists.txt), and
does nothing unless the host plays with sp_coop 1 or the guest sets
coop_connect.

Single player runs with the engine's networking off (net_enabled 0), so co-op
has a small UDP protocol of its own on VSP_COOP_PORT (29760), next to the
co-op check (getcoopinfo, sys_osx_coop.m), which shares the host's socket.

Connecting (out-of-band text packets, "\xff\xff\xff\xff" then a word, a
newline and an infostring or a sentence):
  guest -> host  coopConnect   coopProtocol, version, paks (the loaded .pk3
                               files single player reads, as count-hash), name, password,
                               nonce (the guest's, echoed back)
  host -> guest  coopAccept    session, nonce, hostname, map, state
                 coopReject    a sentence for the guest's console
The host takes one guest. The app version, co-op protocol and .pk3 files must
match, and the password too when the host set one (coop_password, or the
launcher's field). A guest asks up to COOP_CONNECT_TRIES times, a second apart.

After that every packet starts with the session number (a long that is never
-1, so never out-of-band) and a message type byte:
  guest -> host  COOP_MSG_INPUT       up to COOP_INPUT_BACKUP of the guest's
                                      newest commands, each with its own number,
                                      so a lost packet costs nothing
                 COOP_MSG_KEEPALIVE   nothing new to send for a second
                 COOP_MSG_DISCONNECT  a sentence
  host -> guest  COOP_MSG_STATUS      an infostring (map, state, players, time)
                                      once a second
                 COOP_MSG_DISCONNECT  a sentence
Either side gives up after COOP_TIMEOUT ms without a packet.

The host's game takes the guest's input once a game frame through
CoopNet_GuestInput (sp/sp_bridge.cpp hands it to the game's co-op code), and
player 2 moves with it in place of following player 1. While the guest's input
is older than COOP_INPUT_STALE ms (the guest is in a menu, the console, or
loading), player 2 holds still; only without a guest does it follow player 1.

Until the guest mirrors the host's world (co-op M1 step 3), a guest that
connects loads the host's map on its own (coop_guestLoadMap 1, the default)
so it has a level to steer from; what it sees there is its own copy.

Console:
  coop_connect <address[:port]>   (a cvar) join that host; "" leaves
  coop_disconnect                 leave the host
  coop_kick                       (host) drop the guest
  coop_status                     what co-op's connection is doing
  coop_password                   (a cvar) the host's password / the one sent
===========================================================================
*/

#include "../client/client.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "sys_coop_net.h"

#ifndef EF_APP_VERSION
#define EF_APP_VERSION	"unknown"
#endif

#define COOP_OOB				"\xff\xff\xff\xff"
#define COOP_CONNECT			"coopConnect"
#define COOP_ACCEPT				"coopAccept"
#define COOP_REJECT				"coopReject"

#define COOP_MSG_INPUT			1		// guest -> host
#define COOP_MSG_KEEPALIVE		2		// guest -> host
#define COOP_MSG_STATUS			3		// host -> guest
#define COOP_MSG_DISCONNECT		4		// both ways

#define COOP_TIMEOUT			10000	// ms without a packet: the other side is gone
#define COOP_CONNECT_TRIES		5		// the guest asks this often, COOP_CONNECT_RETRY apart
#define COOP_CONNECT_RETRY		1000
#define COOP_STATUS_INTERVAL	1000	// the host's status
#define COOP_KEEPALIVE_INTERVAL	1000	// the guest's keepalive, when it sends no input
#define COOP_INPUT_INTERVAL		15		// the guest sends input at most this often (ms)
#define COOP_INPUT_STALE		500		// older guest input doesn't steer player 2
#define COOP_INPUT_BACKUP		4		// commands per input packet

// sp/sp_bridge.cpp, sp/sp_integration.c
extern int			SP_IsActive( void );
extern const char	*SP_CurMap( void );
extern int			SP_MenuPaused( void );
extern int			SP_IsLoading( void );

// one guest command, as it crosses the network
typedef struct {
	int				seq;				// the guest's own command number
	int				angles[3];			// 0..65535 (ANGLE2SHORT)
	int				buttons;
	int				weapon;
	signed char		forwardmove, rightmove, upmove;
} coopCmd_t;

/* ------------------------------------------------------------------------
   Shared
   ------------------------------------------------------------------------ */

static char		coopLaunchPassword[64];
static qboolean	coopCommandsAdded;

void Sys_CoopSetLaunchPassword( const char *password )
{
	Q_strncpyz( coopLaunchPassword, password ? password : "", sizeof( coopLaunchPassword ) );
}

const char *Sys_CoopPassword( void )
{
	const char *cvar = Cvar_VariableString( "coop_password" );
	return cvar[0] ? cvar : coopLaunchPassword;
}

static int CoopIntCompare( const void *a, const void *b )
{
	int x = *(const int *)a, y = *(const int *)b;
	return x < y ? -1 : x > y;
}

// The .pk3 files single player's game logic reads (the game's own paks, and any that change
// single player: FS_CoopPakChecksums) as "count-hash" (the hash of their sorted checksums): two
// copies with the same files have the same signature, whatever order their search paths are in.
// Holomatch-only packs (maps, bots, cMod) don't count.
static void CoopPakSignature( char *out, int size )
{
	static int	sums[1024];
	const char	*s = FS_CoopPakChecksums();
	int			n = 0;

	while ( *s && n < (int)ARRAY_LEN( sums ) )
	{
		char *end;
		long v = strtol( s, &end, 10 );
		if ( end == s )
			break;
		sums[n++] = (int)v;
		s = end;
	}
	qsort( sums, n, sizeof( sums[0] ), CoopIntCompare );
	Com_sprintf( out, size, "%d-%08x", n, n ? Com_BlockChecksum( sums, n * (int)sizeof( sums[0] ) ) : 0u );
}

// An infostring value: no \ ; " or control characters, at most size - 1 bytes
static void CoopCleanValue( const char *value, char *clean, int size )
{
	int n = 0;

	for ( ; *value && n < size - 1; value++ )
	{
		unsigned char c = (unsigned char)*value;
		clean[n++] = ( c < ' ' || c == 0x7f || c == '\\' || c == ';' || c == '"' ) ? ' ' : (char)c;
	}
	clean[n] = 0;
}

static void CoopSetInfo( char *info, const char *key, const char *value )
{
	char clean[128];

	CoopCleanValue( value, clean, sizeof( clean ) );
	Info_SetValueForKey( info, key, clean );
}

// a value of a received infostring, copied (Info_ValueForKey's own buffers are reused)
static void CoopGetInfo( const char *info, const char *key, char *out, int size )
{
	Q_strncpyz( out, Info_ValueForKey( info, key ), size );
}

// a sentence received from the other side, made printable
static void CoopCleanText( const char *in, char *out, int size )
{
	int n = 0;

	for ( ; *in && n < size - 1; in++ )
	{
		unsigned char c = (unsigned char)*in;
		out[n++] = ( c < ' ' || c == 0x7f || c == '%' ) ? ' ' : (char)c;
	}
	out[n] = 0;
}

static void CoopSendTo( int sock, const struct sockaddr_in *to, const void *data, int len )
{
	if ( sock < 0 || len <= 0 )
		return;
	if ( to )
		sendto( sock, data, len, 0, (const struct sockaddr *)to, sizeof( *to ) );
	else
		send( sock, data, len, 0 );		// a connected socket (the guest's)
}

static void CoopSendOOB( int sock, const struct sockaddr_in *to, const char *word, const char *text )
{
	char	buf[COOP_MAX_PACKET];
	int		n = Com_sprintf( buf, sizeof( buf ), COOP_OOB "%s\n%s", word, text );

	CoopSendTo( sock, to, buf, MIN( n, (int)sizeof( buf ) - 1 ) );
}

static void CoopBeginMessage( msg_t *msg, byte *buf, int size, unsigned session, int type )
{
	MSG_InitOOB( msg, buf, size );
	msg->allowoverflow = qtrue;
	MSG_WriteLong( msg, (int)session );
	MSG_WriteByte( msg, type );
}

static void CoopSendMessage( int sock, const struct sockaddr_in *to, msg_t *msg )
{
	if ( msg->overflowed )
		return;
	CoopSendTo( sock, to, msg->data, msg->cursize );
}

// a received packet as a message to read; *session and *type are its header
static qboolean CoopReadHeader( msg_t *msg, const unsigned char *data, int len, unsigned *session, int *type )
{
	if ( len < 5 || !memcmp( data, COOP_OOB, 4 ) )
		return qfalse;
	MSG_InitOOB( msg, (byte *)data, len );
	msg->cursize = len;
	MSG_BeginReadingOOB( msg );
	*session = (unsigned)MSG_ReadLong( msg );
	*type = MSG_ReadByte( msg );
	return msg->readcount <= msg->cursize;
}

// "\xff\xff\xff\xff<word>\n<rest>": the rest (NUL-terminated in buf), or NULL
static const char *CoopOOBText( const unsigned char *data, int len, const char *word, char *buf, int size )
{
	int wl = (int)strlen( word );

	if ( len < 4 + wl + 1 || memcmp( data, COOP_OOB, 4 ) || memcmp( data + 4, word, wl ) || data[4 + wl] != '\n' )
		return NULL;
	len = MIN( len - ( 5 + wl ), size - 1 );
	memcpy( buf, data + 5 + wl, len );
	buf[len] = 0;
	return buf;
}

static unsigned CoopNewSession( void )
{
	return ( arc4random() % 0x7ffffffeu ) + 1;	// never 0, never -1
}

static const char *CoopAddressString( const struct sockaddr_in *a )
{
	static char	buf[64];
	char		ip[INET_ADDRSTRLEN];

	inet_ntop( AF_INET, &a->sin_addr, ip, sizeof( ip ) );
	Com_sprintf( buf, sizeof( buf ), "%s:%d", ip, ntohs( a->sin_port ) );
	return buf;
}

/* ------------------------------------------------------------------------
   The host
   ------------------------------------------------------------------------ */

static struct {
	qboolean			active;
	struct sockaddr_in	addr;
	unsigned			session;
	char				name[32];
	char				nonce[24];
	int					lastHeard;		// Sys_Milliseconds of the guest's last packet
	int					lastStatus;
	int					connectWindow;	// connect requests: at most 5 a second, from anyone
	int					connectCount;

	// the guest's input
	qboolean			haveCmd;
	int					cmdHeard;		// when the newest command arrived
	coopCmd_t			cmd;			// the newest command
	int					heldButtons;	// every command's buttons since the game last took the input
} coopHost;

static int coopHostSock = -1;			// for coop_kick (the socket is sys_osx_coop.m's)

static qboolean CoopSameAddress( const struct sockaddr_in *a, const struct sockaddr_in *b )
{
	return a->sin_addr.s_addr == b->sin_addr.s_addr && a->sin_port == b->sin_port;
}

static void CoopHostStateInfo( char *info )
{
	qboolean playing = SP_IsActive() && SP_CurMap()[0];

	CoopSetInfo( info, "state", playing ? "playing" : "menu" );
	if ( playing )
	{
		CoopSetInfo( info, "map", SP_CurMap() );
		CoopSetInfo( info, "time", va( "%d", Cvar_VariableIntegerValue( "coop_time" ) ) );
	}
	CoopSetInfo( info, "players", va( "%d", playing ? MAX( 1, Cvar_VariableIntegerValue( "coop_players" ) ) : 1 ) );
}

static void CoopHostDrop( int sock, const char *reason, qboolean tell )
{
	if ( !coopHost.active )
		return;
	if ( tell )
	{
		byte	buf[COOP_MAX_PACKET];
		msg_t	msg;

		CoopBeginMessage( &msg, buf, sizeof( buf ), coopHost.session, COOP_MSG_DISCONNECT );
		MSG_WriteString( &msg, reason );
		CoopSendMessage( sock, &coopHost.addr, &msg );
	}
	Com_Printf( "co-op: %s left (%s)\n", coopHost.name, reason );
	memset( &coopHost.addr, 0, sizeof( coopHost.addr ) );
	coopHost.active = qfalse;
	coopHost.haveCmd = qfalse;
	coopHost.heldButtons = 0;
	coopHost.name[0] = 0;
}

static void CoopHostReject( int sock, const struct sockaddr_in *from, const char *reason )
{
	Com_Printf( "co-op: refused a player 2 from %s: %s\n", CoopAddressString( from ), reason );
	CoopSendOOB( sock, from, COOP_REJECT, reason );
}

static void CoopHostConnect( int sock, const char *request, const struct sockaddr_in *from )
{
	char	protocol[16], version[64], paks[64], ourPaks[64], name[32], password[128], ourPassword[128], nonce[24];
	char	info[MAX_INFO_STRING];
	int		now = Sys_Milliseconds();

	// at most 5 requests a second from anyone (a guest asks once a second)
	if ( now - coopHost.connectWindow >= 1000 || now - coopHost.connectWindow < 0 )
	{
		coopHost.connectWindow = now;
		coopHost.connectCount = 0;
	}
	if ( ++coopHost.connectCount > 5 )
		return;

	CoopGetInfo( request, "coopProtocol", protocol, sizeof( protocol ) );
	CoopGetInfo( request, "version", version, sizeof( version ) );
	CoopGetInfo( request, "paks", paks, sizeof( paks ) );
	CoopGetInfo( request, "name", name, sizeof( name ) );
	CoopGetInfo( request, "password", password, sizeof( password ) );
	CoopGetInfo( request, "nonce", nonce, sizeof( nonce ) );
	if ( !name[0] )
		Q_strncpyz( name, "Player 2", sizeof( name ) );

	// the same guest asking again (its first answer got lost): the same answer
	if ( coopHost.active && CoopSameAddress( from, &coopHost.addr ) && !strcmp( nonce, coopHost.nonce ) )
	{
		info[0] = 0;
		CoopSetInfo( info, "session", va( "%u", coopHost.session ) );
		CoopSetInfo( info, "nonce", coopHost.nonce );
		CoopHostStateInfo( info );
		CoopSendOOB( sock, from, COOP_ACCEPT, info );
		return;
	}

	if ( atoi( protocol ) != VSP_COOP_PROTOCOL || strcmp( version, EF_APP_VERSION ) )
	{
		CoopHostReject( sock, from, va( "The host runs Elite Force for Mac %s (co-op version %d), and you run %s "
			"(co-op version %s). Both need the same version.", EF_APP_VERSION, VSP_COOP_PROTOCOL,
			version[0] ? version : "?", protocol[0] ? protocol : "?" ) );
		return;
	}
	if ( coopHost.active && !CoopSameAddress( from, &coopHost.addr ) )
	{
		CoopHostReject( sock, from, va( "The game already has a player 2 (%s).", coopHost.name ) );
		return;
	}
	CoopPakSignature( ourPaks, sizeof( ourPaks ) );
	if ( strcmp( paks, ourPaks ) )
	{
		CoopHostReject( sock, from, va( "Your game files differ from the host's (host: %d single-player .pk3 files, "
			"you: %d). Both need the same pak0-pak3, and the same packs that change single player "
			"(Holomatch map packs don't matter).", atoi( ourPaks ), atoi( paks ) ) );
		return;
	}
	// compared as it crossed the network (CoopSetInfo cleans the guest's)
	CoopCleanValue( Sys_CoopPassword(), ourPassword, sizeof( ourPassword ) );
	if ( ourPassword[0] && strcmp( password, ourPassword ) )
	{
		CoopHostReject( sock, from, password[0] ? "Wrong password." : "This game needs a password." );
		return;
	}
	if ( coopHost.active )
	{
		CoopHostDrop( sock, "reconnecting", qfalse );	// the guest started over (a new nonce, same place)
	}

	memset( &coopHost.addr, 0, sizeof( coopHost.addr ) );
	coopHost.addr = *from;
	coopHost.active = qtrue;
	coopHost.session = CoopNewSession();
	Q_strncpyz( coopHost.name, name, sizeof( coopHost.name ) );
	Q_strncpyz( coopHost.nonce, nonce, sizeof( coopHost.nonce ) );
	coopHost.lastHeard = now;
	coopHost.lastStatus = now;
	coopHost.haveCmd = qfalse;
	coopHost.heldButtons = 0;
	memset( &coopHost.cmd, 0, sizeof( coopHost.cmd ) );

	info[0] = 0;
	CoopSetInfo( info, "session", va( "%u", coopHost.session ) );
	CoopSetInfo( info, "nonce", nonce );
	CoopHostStateInfo( info );
	CoopSendOOB( sock, from, COOP_ACCEPT, info );
	Com_Printf( "co-op: %s joined from %s as player 2\n", coopHost.name, CoopAddressString( from ) );
}

static void CoopHostInput( msg_t *msg )
{
	int count = MSG_ReadByte( msg );

	if ( count < 1 || count > COOP_INPUT_BACKUP )
		return;
	for ( int i = 0; i < count; i++ )
	{
		coopCmd_t c;

		c.seq = MSG_ReadLong( msg );
		for ( int j = 0; j < 3; j++ )
			c.angles[j] = MSG_ReadShort( msg ) & 0xffff;
		c.buttons = MSG_ReadLong( msg );
		c.weapon = MSG_ReadByte( msg );
		c.forwardmove = (signed char)MSG_ReadByte( msg );
		c.rightmove = (signed char)MSG_ReadByte( msg );
		c.upmove = (signed char)MSG_ReadByte( msg );
		if ( msg->readcount > msg->cursize )
			return;
		// oldest first; only commands newer than the newest one seen count
		if ( coopHost.haveCmd && c.seq - coopHost.cmd.seq <= 0 )
			continue;
		coopHost.cmd = c;
		coopHost.heldButtons |= c.buttons;
		coopHost.haveCmd = qtrue;
		coopHost.cmdHeard = Sys_Milliseconds();
	}
}

void CoopNet_HostPacket( int sock, const unsigned char *data, int len, const struct sockaddr_in *from )
{
	char		text[COOP_MAX_PACKET];
	const char	*request;
	msg_t		msg;
	unsigned	session;
	int			type;

	coopHostSock = sock;
	if ( ( request = CoopOOBText( data, len, COOP_CONNECT, text, sizeof( text ) ) ) != NULL )
	{
		CoopHostConnect( sock, request, from );
		return;
	}
	if ( !coopHost.active || !CoopSameAddress( from, &coopHost.addr ) )
		return;
	if ( !CoopReadHeader( &msg, data, len, &session, &type ) || session != coopHost.session )
		return;

	coopHost.lastHeard = Sys_Milliseconds();
	switch ( type )
	{
	case COOP_MSG_INPUT:
		CoopHostInput( &msg );
		break;
	case COOP_MSG_KEEPALIVE:
		break;
	case COOP_MSG_DISCONNECT:
		{
			char reason[128];
			CoopCleanText( MSG_ReadString( &msg ), reason, sizeof( reason ) );
			CoopHostDrop( sock, reason[0] ? reason : "left the game", qfalse );
		}
		break;
	default:
		break;
	}
}

void CoopNet_HostFrame( int sock )
{
	int now = Sys_Milliseconds();

	coopHostSock = sock;
	if ( !coopHost.active )
		return;
	if ( now - coopHost.lastHeard > COOP_TIMEOUT )
	{
		CoopHostDrop( sock, "timed out", qtrue );
		return;
	}
	if ( now - coopHost.lastStatus >= COOP_STATUS_INTERVAL )
	{
		byte	buf[COOP_MAX_PACKET];
		char	info[MAX_INFO_STRING];
		msg_t	msg;

		coopHost.lastStatus = now;
		info[0] = 0;
		CoopHostStateInfo( info );
		CoopBeginMessage( &msg, buf, sizeof( buf ), coopHost.session, COOP_MSG_STATUS );
		MSG_WriteString( &msg, info );
		CoopSendMessage( sock, &coopHost.addr, &msg );
	}
}

void CoopNet_HostShutdown( int sock, const char *reason )
{
	CoopHostDrop( sock, reason, qtrue );
	coopHostSock = -1;
}

int CoopNet_HostNeedsPassword( void )
{
	return Sys_CoopPassword()[0] != 0;
}

int CoopNet_HostHasGuest( void )
{
	return coopHost.active;
}

/*
=================
CoopNet_GuestInput

The game's co-op code, once a game frame on the host (through sp/sp_bridge.cpp): the
guest's newest command, with the buttons of every command since the last call, so a click
between two game frames isn't lost. Returns 0 when there is no guest or its input is older
than COOP_INPUT_STALE ms; player 2 then holds still (or, with no guest, follows player 1).
=================
*/
int CoopNet_GuestInput( int *angles, int *buttons, int *weapon, signed char *forwardmove,
	signed char *rightmove, signed char *upmove )
{
	if ( !coopHost.active || !coopHost.haveCmd || Sys_Milliseconds() - coopHost.cmdHeard > COOP_INPUT_STALE )
	{
		coopHost.heldButtons = 0;
		return 0;
	}
	angles[0] = coopHost.cmd.angles[0];
	angles[1] = coopHost.cmd.angles[1];
	angles[2] = coopHost.cmd.angles[2];
	*buttons = coopHost.cmd.buttons | coopHost.heldButtons;
	*weapon = coopHost.cmd.weapon;
	*forwardmove = coopHost.cmd.forwardmove;
	*rightmove = coopHost.cmd.rightmove;
	*upmove = coopHost.cmd.upmove;
	coopHost.heldButtons = 0;
	return 1;
}

// the guest's name, "" without a guest
const char *CoopNet_GuestName( void )
{
	return coopHost.active ? coopHost.name : "";
}

/* ------------------------------------------------------------------------
   The guest
   ------------------------------------------------------------------------ */

typedef enum {
	COOP_GUEST_IDLE,
	COOP_GUEST_CONNECTING,
	COOP_GUEST_CONNECTED
} coopGuestState_t;

static struct {
	coopGuestState_t	state;
	int					sock;
	struct sockaddr_in	host;
	char				address[128];	// coop_connect when this connection started
	char				nonce[24];
	unsigned			session;
	int					tries;
	int					lastSend;		// the last connect request, input or keepalive
	int					lastHeard;		// the host's last packet
	qboolean			refused;		// the host's Mac said nothing listens on the port

	// input
	int					lastCmdNumber;	// cl.cmdNumber last taken
	int					seq;
	coopCmd_t			cmds[COOP_INPUT_BACKUP];	// the newest last
	int					numCmds;
	qboolean			unsent;			// a command not sent yet

	// the host's state
	char				hostMap[MAX_QPATH];
	char				hostState[16];
	char				requestedMap[MAX_QPATH];	// coop_guestLoadMap: the map this guest last loaded for it
} coopGuest = { COOP_GUEST_IDLE, -1 };

int CoopNet_GuestActive( void )
{
	return Cvar_VariableString( "coop_connect" )[0] != 0;
}

// "host" or "host:port" -> *out; a sentence in err otherwise
static qboolean CoopResolve( const char *address, struct sockaddr_in *out, char *err, int errSize )
{
	char			host[128];
	const char		*colon = strchr( address, ':' );
	int				port = VSP_COOP_PORT;
	struct addrinfo	hints, *res = NULL;

	Q_strncpyz( host, address, sizeof( host ) );
	if ( colon )
	{
		if ( strchr( colon + 1, ':' ) )
		{
			Q_strncpyz( err, "IPv6 addresses aren't supported yet; use the host's IPv4 address.", errSize );
			return qfalse;
		}
		host[MIN( (int)( colon - address ), (int)sizeof( host ) - 1 )] = 0;
		port = atoi( colon + 1 );
		if ( port < 1 || port > 65535 )
		{
			Com_sprintf( err, errSize, "\"%s\" isn't a port.", colon + 1 );
			return qfalse;
		}
	}
	if ( !host[0] )
	{
		Q_strncpyz( err, "no host address given.", errSize );
		return qfalse;
	}
	memset( &hints, 0, sizeof( hints ) );
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	if ( getaddrinfo( host, NULL, &hints, &res ) != 0 || !res )
	{
		Com_sprintf( err, errSize, "\"%s\" couldn't be found.", host );
		return qfalse;
	}
	memset( out, 0, sizeof( *out ) );
	memcpy( out, res->ai_addr, MIN( (int)sizeof( *out ), (int)res->ai_addrlen ) );
	freeaddrinfo( res );
	out->sin_family = AF_INET;
	out->sin_port = htons( port );
	return qtrue;
}

// leave: tell the host (when connected), close, and clear coop_connect so it isn't retried
static void CoopGuestStop( const char *why, qboolean tell )
{
	if ( coopGuest.state == COOP_GUEST_CONNECTED && tell )
	{
		byte	buf[COOP_MAX_PACKET];
		msg_t	msg;

		CoopBeginMessage( &msg, buf, sizeof( buf ), coopGuest.session, COOP_MSG_DISCONNECT );
		MSG_WriteString( &msg, "left the game" );
		CoopSendMessage( coopGuest.sock, NULL, &msg );
	}
	if ( coopGuest.sock >= 0 )
		close( coopGuest.sock );
	if ( why )
		Com_Printf( "co-op: %s\n", why );
	memset( &coopGuest, 0, sizeof( coopGuest ) );
	coopGuest.state = COOP_GUEST_IDLE;
	coopGuest.sock = -1;
	if ( Cvar_VariableString( "coop_connect" )[0] )
		Cvar_Set( "coop_connect", "" );
}

static void CoopGuestSendConnect( void )
{
	char	info[MAX_INFO_STRING], paks[64], name[64];

	CoopPakSignature( paks, sizeof( paks ) );
	Q_strncpyz( name, Cvar_VariableString( "coop_name" ), sizeof( name ) );
	if ( !name[0] && gethostname( name, sizeof( name ) ) == 0 )
	{
		char *dot = strstr( name, ".local" );
		if ( dot )
			*dot = 0;
	}
	name[sizeof( name ) - 1] = 0;
	info[0] = 0;
	CoopSetInfo( info, "coopProtocol", va( "%d", VSP_COOP_PROTOCOL ) );
	CoopSetInfo( info, "version", EF_APP_VERSION );
	CoopSetInfo( info, "paks", paks );
	CoopSetInfo( info, "name", name[0] ? name : "Player 2" );
	CoopSetInfo( info, "nonce", coopGuest.nonce );
	if ( Sys_CoopPassword()[0] )
		CoopSetInfo( info, "password", Sys_CoopPassword() );
	CoopSendOOB( coopGuest.sock, NULL, COOP_CONNECT, info );
	coopGuest.tries++;
	coopGuest.lastSend = Sys_Milliseconds();
}

static void CoopGuestStart( const char *address )
{
	char	err[128];
	int		fd;

	memset( &coopGuest, 0, sizeof( coopGuest ) );
	coopGuest.sock = -1;
	Q_strncpyz( coopGuest.address, address, sizeof( coopGuest.address ) );
	if ( Com_Holomatch() || Com_DedicatedServer() )
	{
		CoopGuestStop( "co-op is for single player; coop_connect is ignored in Holomatch", qfalse );
		return;
	}
	if ( !CoopResolve( address, &coopGuest.host, err, sizeof( err ) ) )
	{
		CoopGuestStop( va( "can't join %s: %s", address, err ), qfalse );
		return;
	}
	// a connected UDP socket: only the host's packets arrive, and "nothing listens there" shows
	fd = socket( AF_INET, SOCK_DGRAM, 0 );
	if ( fd < 0 || fcntl( fd, F_SETFL, O_NONBLOCK ) < 0 ||
		connect( fd, (const struct sockaddr *)&coopGuest.host, sizeof( coopGuest.host ) ) < 0 )
	{
		if ( fd >= 0 )
			close( fd );
		CoopGuestStop( va( "can't join %s: no network socket (%s)", address, strerror( errno ) ), qfalse );
		return;
	}
	coopGuest.sock = fd;
	coopGuest.state = COOP_GUEST_CONNECTING;
	Com_sprintf( coopGuest.nonce, sizeof( coopGuest.nonce ), "%08x%08x", arc4random(), arc4random() );
	Com_Printf( "co-op: joining %s...\n", CoopAddressString( &coopGuest.host ) );
	CoopGuestSendConnect();
}

// the host's state (from its accept or status): coop_guestLoadMap loads the host's map
static void CoopGuestHostState( const char *info )
{
	char map[MAX_QPATH];

	CoopGetInfo( info, "state", coopGuest.hostState, sizeof( coopGuest.hostState ) );
	CoopGetInfo( info, "map", map, sizeof( map ) );
	for ( char *p = map; *p; p++ )
	{
		if ( !isalnum( (unsigned char)*p ) && *p != '_' && *p != '-' && *p != '/' )
		{
			map[0] = 0;		// not a map name
			break;
		}
	}
	Q_strncpyz( coopGuest.hostMap, map, sizeof( coopGuest.hostMap ) );

	if ( !Cvar_VariableIntegerValue( "coop_guestLoadMap" ) || strcmp( coopGuest.hostState, "playing" ) || !map[0] )
		return;
	if ( !Q_stricmp( coopGuest.requestedMap, map ) || SP_IsLoading() )
		return;
	if ( SP_IsActive() && !Q_stricmp( SP_CurMap(), map ) )
	{
		Q_strncpyz( coopGuest.requestedMap, map, sizeof( coopGuest.requestedMap ) );
		return;
	}
	Q_strncpyz( coopGuest.requestedMap, map, sizeof( coopGuest.requestedMap ) );
	Com_Printf( "co-op: the host is on %s; loading it here (until co-op mirrors the host's world, "
		"this is your own copy of the level, and your moves steer player 2 in the host's)\n", map );
	Cbuf_AddText( va( "map %s\n", map ) );
}

static void CoopGuestReceive( void )
{
	unsigned char	data[COOP_MAX_PACKET];
	char			text[COOP_MAX_PACKET];

	for ( int i = 0; coopGuest.sock >= 0 && i < 16; i++ )
	{
		ssize_t		n = recv( coopGuest.sock, data, sizeof( data ), 0 );
		const char	*rest;
		msg_t		msg;
		unsigned	session;
		int			type;

		if ( n < 0 )
		{
			if ( errno == ECONNREFUSED )
				coopGuest.refused = qtrue;		// the host's Mac: nothing on that port
			return;
		}
		if ( n == 0 )
			continue;

		if ( ( rest = CoopOOBText( data, (int)n, COOP_ACCEPT, text, sizeof( text ) ) ) != NULL )
		{
			char nonce[24], sessionText[16];

			if ( coopGuest.state != COOP_GUEST_CONNECTING )
				continue;
			CoopGetInfo( rest, "nonce", nonce, sizeof( nonce ) );
			CoopGetInfo( rest, "session", sessionText, sizeof( sessionText ) );
			if ( strcmp( nonce, coopGuest.nonce ) || !sessionText[0] )
				continue;
			coopGuest.session = (unsigned)strtoul( sessionText, NULL, 10 );
			coopGuest.state = COOP_GUEST_CONNECTED;
			coopGuest.lastHeard = Sys_Milliseconds();
			coopGuest.lastCmdNumber = cl.cmdNumber;
			Com_Printf( "co-op: joined %s as player 2\n", CoopAddressString( &coopGuest.host ) );
			CoopGuestHostState( rest );
			continue;
		}
		if ( ( rest = CoopOOBText( data, (int)n, COOP_REJECT, text, sizeof( text ) ) ) != NULL )
		{
			char reason[256];

			if ( coopGuest.state != COOP_GUEST_CONNECTING )
				continue;
			CoopCleanText( rest, reason, sizeof( reason ) );
			CoopGuestStop( va( "the host refused: %s", reason ), qfalse );
			return;
		}
		if ( coopGuest.state != COOP_GUEST_CONNECTED ||
			!CoopReadHeader( &msg, data, (int)n, &session, &type ) || session != coopGuest.session )
			continue;

		coopGuest.lastHeard = Sys_Milliseconds();
		if ( type == COOP_MSG_STATUS )
		{
			char info[MAX_INFO_STRING];
			Q_strncpyz( info, MSG_ReadString( &msg ), sizeof( info ) );
			CoopGuestHostState( info );
		}
		else if ( type == COOP_MSG_DISCONNECT )
		{
			char reason[128];
			CoopCleanText( MSG_ReadString( &msg ), reason, sizeof( reason ) );
			CoopGuestStop( va( "the host ended the game (%s)", reason[0] ? reason : "no reason given" ), qfalse );
			return;
		}
	}
}

// this player's input steers player 2 while it plays a level (not in a menu, the console or a load)
static qboolean CoopGuestHasInput( void )
{
	return SP_IsActive() && clc.state == CA_ACTIVE && !SP_MenuPaused() && !SP_IsLoading();
}

static void CoopGuestTakeInput( void )
{
	usercmd_t	*uc;
	coopCmd_t	*c;

	if ( !CoopGuestHasInput() )
	{
		coopGuest.lastCmdNumber = cl.cmdNumber;
		return;
	}
	if ( cl.cmdNumber == coopGuest.lastCmdNumber )
		return;
	coopGuest.lastCmdNumber = cl.cmdNumber;
	uc = &cl.cmds[cl.cmdNumber & CMD_MASK];

	if ( coopGuest.numCmds == COOP_INPUT_BACKUP )
	{
		memmove( &coopGuest.cmds[0], &coopGuest.cmds[1], sizeof( coopGuest.cmds[0] ) * ( COOP_INPUT_BACKUP - 1 ) );
		coopGuest.numCmds--;
	}
	c = &coopGuest.cmds[coopGuest.numCmds++];
	c->seq = ++coopGuest.seq;
	for ( int j = 0; j < 3; j++ )
		c->angles[j] = uc->angles[j] & 0xffff;
	c->buttons = uc->buttons;
	c->weapon = uc->weapon;
	c->forwardmove = uc->forwardmove;
	c->rightmove = uc->rightmove;
	c->upmove = uc->upmove;
	coopGuest.unsent = qtrue;
}

static void CoopGuestSend( void )
{
	byte	buf[COOP_MAX_PACKET];
	msg_t	msg;
	int		now = Sys_Milliseconds();

	if ( coopGuest.unsent && coopGuest.numCmds && now - coopGuest.lastSend >= COOP_INPUT_INTERVAL )
	{
		CoopBeginMessage( &msg, buf, sizeof( buf ), coopGuest.session, COOP_MSG_INPUT );
		MSG_WriteByte( &msg, coopGuest.numCmds );
		for ( int i = 0; i < coopGuest.numCmds; i++ )
		{
			const coopCmd_t *c = &coopGuest.cmds[i];
			MSG_WriteLong( &msg, c->seq );
			for ( int j = 0; j < 3; j++ )
				MSG_WriteShort( &msg, c->angles[j] );
			MSG_WriteLong( &msg, c->buttons );
			MSG_WriteByte( &msg, c->weapon );
			MSG_WriteByte( &msg, (byte)c->forwardmove );
			MSG_WriteByte( &msg, (byte)c->rightmove );
			MSG_WriteByte( &msg, (byte)c->upmove );
		}
		CoopSendMessage( coopGuest.sock, NULL, &msg );
		coopGuest.unsent = qfalse;
		coopGuest.lastSend = now;
	}
	else if ( now - coopGuest.lastSend >= COOP_KEEPALIVE_INTERVAL )
	{
		CoopBeginMessage( &msg, buf, sizeof( buf ), coopGuest.session, COOP_MSG_KEEPALIVE );
		CoopSendMessage( coopGuest.sock, NULL, &msg );
		coopGuest.lastSend = now;
	}
}

static void CoopStatus_f( void );
static void CoopDisconnect_f( void );
static void CoopKick_f( void );

static void CoopAddCommands( void )
{
	if ( coopCommandsAdded )
		return;
	coopCommandsAdded = qtrue;
	Cvar_Get( "coop_guestLoadMap", "1", CVAR_TEMP );
	Cmd_AddCommand( "coop_status", CoopStatus_f );
	Cmd_AddCommand( "coop_disconnect", CoopDisconnect_f );
	Cmd_AddCommand( "coop_kick", CoopKick_f );
}

void CoopNet_GuestFrame( void )
{
	const char	*want = Cvar_VariableString( "coop_connect" );
	int			now;

	if ( !want[0] )
	{
		if ( coopGuest.state != COOP_GUEST_IDLE )
			CoopGuestStop( "left the host", qtrue );
		return;
	}
	CoopAddCommands();
	if ( coopGuest.state != COOP_GUEST_IDLE && strcmp( want, coopGuest.address ) )
	{
		char next[128];
		Q_strncpyz( next, want, sizeof( next ) );
		CoopGuestStop( "leaving the host for another one", qtrue );
		Cvar_Set( "coop_connect", next );
		want = Cvar_VariableString( "coop_connect" );
	}
	if ( coopGuest.state == COOP_GUEST_IDLE )
	{
		CoopGuestStart( want );
		if ( coopGuest.state == COOP_GUEST_IDLE )
			return;
	}

	CoopGuestReceive();
	if ( coopGuest.state == COOP_GUEST_IDLE )
		return;

	now = Sys_Milliseconds();
	if ( coopGuest.state == COOP_GUEST_CONNECTING )
	{
		if ( now - coopGuest.lastSend < COOP_CONNECT_RETRY )
			return;
		if ( coopGuest.tries >= COOP_CONNECT_TRIES )
		{
			CoopGuestStop( va( "no answer from %s%s", CoopAddressString( &coopGuest.host ), coopGuest.refused ?
				": nothing is listening on that port (is the host running co-op?)" :
				". Check the address, that the host is in co-op, and that both Macs are on the same network." ), qfalse );
			return;
		}
		CoopGuestSendConnect();
		return;
	}

	if ( now - coopGuest.lastHeard > COOP_TIMEOUT )
	{
		CoopGuestStop( "lost the connection to the host (nothing heard for 10 seconds)", qtrue );
		return;
	}
	CoopGuestTakeInput();
	CoopGuestSend();
}

/* ------------------------------------------------------------------------
   Console
   ------------------------------------------------------------------------ */

static void CoopStatus_f( void )
{
	if ( coopGuest.state == COOP_GUEST_CONNECTING )
		Com_Printf( "co-op: joining %s (asked %d times)\n", CoopAddressString( &coopGuest.host ), coopGuest.tries );
	else if ( coopGuest.state == COOP_GUEST_CONNECTED )
		Com_Printf( "co-op: player 2 in %s's game (%s%s%s), last heard %d ms ago, %d commands sent\n",
			CoopAddressString( &coopGuest.host ), coopGuest.hostState, coopGuest.hostMap[0] ? " on " : "",
			coopGuest.hostMap, Sys_Milliseconds() - coopGuest.lastHeard, coopGuest.seq );
	else if ( coopHost.active )
		Com_Printf( "co-op: hosting; player 2 is %s at %s, last heard %d ms ago, %s\n", coopHost.name,
			CoopAddressString( &coopHost.addr ), Sys_Milliseconds() - coopHost.lastHeard,
			coopHost.haveCmd && Sys_Milliseconds() - coopHost.cmdHeard <= COOP_INPUT_STALE ? "steering" : "holding player 2 still (no fresh input)" );
	else if ( coopHostSock >= 0 )
		Com_Printf( "co-op: hosting; no player 2 has joined\n" );
	else
		Com_Printf( "co-op: not connected (coop_connect <address> joins a host; sp_coop 1 hosts)\n" );
}

static void CoopDisconnect_f( void )
{
	if ( coopGuest.state != COOP_GUEST_IDLE )
		CoopGuestStop( "left the host", qtrue );
	else
		Cvar_Set( "coop_connect", "" );
}

static void CoopKick_f( void )
{
	if ( !coopHost.active )
	{
		Com_Printf( "co-op: no player 2 to drop\n" );
		return;
	}
	CoopHostDrop( coopHostSock, "dropped by the host", qtrue );
}

// the host registers the commands too (sys_osx_coop.m opens the host's socket)
void CoopNet_AddCommands( void )
{
	CoopAddCommands();
}
