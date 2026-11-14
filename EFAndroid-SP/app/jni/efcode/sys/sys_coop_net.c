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
  guest -> host  coopConnect   coopProtocol, version, paks (the game's own .pk3
                               files loaded: FS_CoopPaks), name, password,
                               nonce (the guest's, echoed back), and optionally sex
                               (male / female: how player 2 looks; without it, like
                               player 1) and look (a character for player 2 from the
                               game's coop_characters.h; without it, Munro)
  host -> guest  coopAccept    session, nonce, paks (the host's), hostName (the host player's name), map, state
                 coopReject    a sentence for the guest's console
The host takes one guest. The app version, co-op protocol and the game's own
pak0-pak2 must match (CoopNet_ComparePaks: the Expansion Pack's pak3 may
differ, with a note on both sides; other packs don't count), and the password
too when the host set one (coop_password, or the launcher's field). A guest asks up to COOP_CONNECT_TRIES times, a second apart.

After that every packet starts with the session number (a long that is never
-1, so never out-of-band) and a message type byte:
  guest -> host  COOP_MSG_INPUT       up to COOP_INPUT_BACKUP of the guest's
                                      newest commands (one per host frame it
                                      took in), each with its own number, so a
                                      lost packet costs nothing
                 COOP_MSG_KEEPALIVE   nothing new to send for a second, or
                                      the newest replica frame it decoded
                 COOP_MSG_COMMAND     a console command for player 2 (the
                                      cheats god, noclip, give, notarget and
                                      undying), numbered, sent a few times;
                                      the host's game runs it for player 2
                                      when the host allows cheats
                 COOP_MSG_DISCONNECT  a sentence
  host -> guest  COOP_MSG_STATUS      an infostring (map, state, players, time)
                                      once a second
                 COOP_MSG_FRAME       a fragment of a replica frame
                 COOP_MSG_DISCONNECT  a sentence
Either side gives up after COOP_TIMEOUT ms without a packet. Input and
keepalive packets end with the newest replica frame the guest has (its ack).

The host's game takes the guest's input once a game frame through
CoopNet_GuestInput (sp/sp_bridge.cpp hands it to the game's co-op code), and
player 2 moves with it in place of following player 1: one command a game frame, in
order, and each replica frame names the last one applied, so the guest's cgame
can predict its own movement from there. While the guest's input
is older than COOP_INPUT_STALE ms (the guest is in a menu, the console, or
loading), player 2 holds still; only without a guest does it follow player 1.

The replica: after every game frame the host describes its world as
COOP_NUM_SLOTS slots (each entity, each configstring, the level's globals and
the newest server commands; sp_bridge.cpp fills them through the game's
COOP_GetReplica) and sends the slots that differ from the newest frame the
guest acked, compressed and cut into fragments. A guest that has acked nothing
(it just joined, or fell behind the COOP_FRAME_BACKUP frames kept) gets every
slot, a "whole" frame, at most once per COOP_FULL_RETRY ms. Lost
fragments cost a frame, never a resend: the next frame is a change from the
guest's ack again. The guest keeps the same ring, rebuilds each frame from its
base, and loads the host's map with sp_coop 2 when the frames are of a level it
hasn't loaded; that level then shows the host's world instead of running its
own (see docs/coop-m1-design.md).

Console:
  coop_connect <address[:port]>   (a cvar) join that host; "" leaves
  coop_disconnect                 leave the host
  coop_kick                       (host) drop the guest
  coop_status                     what co-op's connection is doing
  coop_password                   (a cvar) the host's password / the one sent
  coop_netstats 1 / 2             bytes and time every 5 s / and what changes most
  coop_simlatency <ms>, coop_simloss <percent>   (cvars, tests) a slow or lossy network
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
#include <sys/time.h>

#include <compression.h>		// the system's Compression library: the replica frames

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
#define COOP_MSG_FRAME			5		// host -> guest: a fragment of a replica frame
#define COOP_MSG_COMMAND		6		// guest -> host: a console command for player 2
#define COOP_COMMAND_QUEUE		4		// guest commands the host's game hasn't run yet
#define COOP_COMMAND_LEN		128
#define COOP_COMMAND_SENDS		3		// each is sent this often (they're few, and a lost one is missed)

#define COOP_TIMEOUT			10000	// ms without a packet: the other side is gone
#define COOP_CONNECT_TRIES		5		// the guest asks this often, COOP_CONNECT_RETRY apart
#define COOP_CONNECT_RETRY		1000
#define COOP_STATUS_INTERVAL	1000	// the host's status
#define COOP_KEEPALIVE_INTERVAL	1000	// the guest's keepalive, when it sends no input
#define COOP_INPUT_STALE		500		// older guest input doesn't steer player 2
#define COOP_INPUT_BACKUP		4		// commands per input packet
#define COOP_CMD_QUEUE			3		// the host's queue of guest commands (one is applied per game frame)

// Replica frames: once a game frame the host's world as slots of bytes (the game's entity and
// globals images, the configstrings, the newest server commands), sent as the changes against
// the last frame the guest acknowledged.
#define COOP_FRAME_BACKUP		32		// frames kept on both sides (a delta's base must be among them)
#define COOP_FRAGMENT			1200	// the most frame bytes in one packet
#define COOP_MAX_FRAGMENTS		2048
#define COOP_FULL_RETRY			1000	// a whole-world frame the guest hasn't acknowledged: at most once this often
#define COOP_ASSEMBLY			4		// frames being put together from their fragments on the guest
#define COOP_HOST_STALL			400		// ms without a new frame: the guest shows it's waiting for the host

// sp/sp_bridge.cpp, sp/sp_integration.c
extern int			SP_IsActive( void );
extern const char	*SP_CurMap( void );
extern int			SP_MenuPaused( void );
extern int			SP_IsLoading( void );
extern void			SP_AbortToMenu( void );
extern int			SP_CoopCreditsUp( void );
extern void			SP_CoopCreditsDone( void );

// one guest command, as it crosses the network
typedef struct {
	int				seq;				// the guest's own command number
	int				angles[3];			// 0..65535 (ANGLE2SHORT)
	int				buttons;
	int				weapon;
	signed char		forwardmove, rightmove, upmove;
	int				viewTime;			// the host level time the guest's screen showed (its shots)
} coopCmd_t;

typedef struct {
	int				num;				// frame number (0: unused)
	int				levelId;			// the host's level (a new one at every level start)
	int				time;				// the host's level time
	int				ackCmd;				// the newest guest command the host had applied
	char			map[MAX_QPATH];
	byte			*data;				// the slots' bytes, back to back
	int				dataLen, dataSize;
	int				ofs[COOP_NUM_SLOTS];
	int				len[COOP_NUM_SLOTS];	// 0: empty
} coopFrame_t;

/* ------------------------------------------------------------------------
   Shared
   ------------------------------------------------------------------------ */

static void		CoopNetStats( void );

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

// A FS_CoopPaks list ("pak0.pk3:<checksum> ...") as the checksum of each of the game's own paks
// (pak0-pak3), 0 for one that isn't there
static void CoopParsePaks( const char *list, unsigned int sums[4] )
{
	memset( sums, 0, 4 * sizeof( sums[0] ) );
	while ( list && *list )
	{
		while ( *list == ' ' )
			list++;
		if ( !Q_stricmpn( list, "pak", 3 ) && list[3] >= '0' && list[3] <= '3' && !Q_stricmpn( list + 4, ".pk3:", 5 ) )
			sums[list[3] - '0'] = (unsigned int)strtoul( list + 9, NULL, 10 );
		while ( *list && *list != ' ' )
			list++;
	}
}

static void CoopPakListAdd( char *list, int size, int pak )
{
	Q_strcat( list, size, va( "%spak%d.pk3", list[0] ? ", " : "", pak ) );
}

/*
CoopNet_ComparePaks: do a host's and a guest's game files allow co-op? Only the game's own paks
count, so community packs, Holomatch maps and mods never stop a co-op game:
  - pak0.pk3-pak2.pk3 (the game and its patches) must be the same on both: COOP_PAKS_DIFFER,
    the host refuses the guest;
  - pak3.pk3 (the Expansion Pack's) may differ: COOP_PAKS_EXPANSION, a warning. It replaces a
    few single-player files (NPC, weapon and item tables, some texts, the tricorder, two
    scripts), but the host runs the game, so on the guest's screen the difference is mostly
    cosmetic.
text gets sentences naming each pack that differs, for the host to read (forHost) or the guest.
*/
int CoopNet_ComparePaks( const char *hostPaks, const char *guestPaks, qboolean forHost, char *text, int size )
{
	unsigned int	host[4], guest[4];
	char			hostOnly[64] = "", guestOnly[64] = "", differ[64] = "";

	CoopParsePaks( hostPaks, host );
	CoopParsePaks( guestPaks, guest );
	text[0] = 0;
	for ( int i = 0; i < 3; i++ )
	{
		if ( host[i] == guest[i] )
			continue;
		if ( !guest[i] )
			CoopPakListAdd( hostOnly, sizeof( hostOnly ), i );
		else if ( !host[i] )
			CoopPakListAdd( guestOnly, sizeof( guestOnly ), i );
		else
			CoopPakListAdd( differ, sizeof( differ ), i );
	}
	if ( hostOnly[0] || guestOnly[0] || differ[0] )
	{
		if ( hostOnly[0] )
			Q_strcat( text, size, va( forHost ? "Player 2 is missing %s (you have it). " :
				"You're missing %s (the host has it). ", hostOnly ) );
		if ( guestOnly[0] )
			Q_strcat( text, size, va( forHost ? "You're missing %s (player 2 has it). " :
				"The host is missing %s (you have it). ", guestOnly ) );
		if ( differ[0] )
			Q_strcat( text, size, va( forHost ? "Player 2's %s differs from yours. " :
				"Your %s differs from the host's. ", differ ) );
		Q_strcat( text, size, "Both need the game's original pak0.pk3, pak1.pk3 and pak2.pk3 (other packs don't matter)." );
		return COOP_PAKS_DIFFER;
	}
	if ( host[3] != guest[3] )
	{
		if ( !guest[3] )
			Q_strcat( text, size, forHost ? "Player 2 doesn't have the Expansion Pack (pak3.pk3); you do." :
				"You don't have the Expansion Pack (pak3.pk3); the host does." );
		else if ( !host[3] )
			Q_strcat( text, size, forHost ? "Player 2 has the Expansion Pack (pak3.pk3); you don't." :
				"The host doesn't have the Expansion Pack (pak3.pk3); you do." );
		else
			Q_strcat( text, size, forHost ? "Player 2's Expansion Pack (pak3.pk3) differs from yours." :
				"Your Expansion Pack (pak3.pk3) differs from the host's." );
		Q_strcat( text, size, " You can still play together: the host runs the game, so the difference is mostly "
			"cosmetic on player 2's screen (some texts, the tricorder, a few weapon and NPC details)." );
		return COOP_PAKS_EXPANSION;
	}
	return COOP_PAKS_SAME;
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

// A player's name as the game shows it: printable ASCII without \ ; " or %, no spaces at
// either end, at most size - 1 (COOP_NAME_MAX) characters
static void CoopCleanName( const char *in, char *out, int size )
{
	int n = 0;

	for ( ; *in && n < size - 1; in++ )
	{
		unsigned char c = (unsigned char)*in;
		if ( c < ' ' || c > '~' || c == '\\' || c == ';' || c == '"' || c == '%' )
			continue;
		if ( c == ' ' && ( !n || out[n - 1] == ' ' ) )
			continue;
		out[n++] = (char)c;
	}
	while ( n && out[n - 1] == ' ' )
		n--;
	out[n] = 0;
}

// this player's name for the other side: coop_name, else the Mac's name
static void CoopOwnName( char *out, int size )
{
	char raw[128];

	Q_strncpyz( raw, Cvar_VariableString( "coop_name" ), sizeof( raw ) );
	CoopCleanName( raw, out, size );
	if ( !out[0] )
	{
		Sys_CoopMacName( raw, sizeof( raw ) );
		CoopCleanName( raw, out, size );
	}
}

// "male" / "female" from what a player chose (coop_sex), "" for anything else
static const char *CoopSexValue( const char *s )
{
	if ( !Q_stricmp( s, "female" ) || !Q_stricmp( s, "f" ) )
		return "female";
	if ( !Q_stricmp( s, "male" ) || !Q_stricmp( s, "m" ) )
		return "male";
	return "";
}

// a character id (coop_look: lower case letters, digits and _), "" for anything else; the host's
// game takes only the ones it knows
static void CoopLookValue( const char *in, char *out, int size )
{
	int n = 0;

	for ( ; *in && n < size - 1; in++ )
	{
		char c = *in;
		if ( ( c >= 'A' && c <= 'Z' ) )
			c += 'a' - 'A';
		if ( !( ( c >= 'a' && c <= 'z' ) || ( c >= '0' && c <= '9' ) || c == '_' ) )
		{
			n = 0;
			break;
		}
		out[n++] = c;
	}
	if ( *in )
		n = 0;		// too long or not an id: Munro
	out[n] = 0;
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

static void CoopSendNow( int sock, const struct sockaddr_in *to, const void *data, int len )
{
	if ( to )
		sendto( sock, data, len, 0, (const struct sockaddr *)to, sizeof( *to ) );
	else
		send( sock, data, len, 0 );		// a connected socket (the guest's)
}

/*
A bad network, for tests: coop_simlatency <ms> holds every packet this copy sends that long
(set it on both copies for a round trip of twice that), and coop_simloss <percent> drops that
share of them. Both are 0 (off) unless set.
*/
#define COOP_SIM_QUEUE	1024

typedef struct {
	int					due;			// Sys_Milliseconds
	int					sock;
	qboolean			haveTo;
	struct sockaddr_in	to;
	int					len;
	byte				data[COOP_MAX_PACKET];
} coopSimPacket_t;

static coopSimPacket_t	*coopSim;		// a ring, allocated when first used
static int				coopSimHead, coopSimCount;

static void CoopSimFlush( void )
{
	int now = Sys_Milliseconds();

	while ( coopSimCount )
	{
		coopSimPacket_t *p = &coopSim[coopSimHead];
		if ( p->due - now > 0 )
			break;
		CoopSendNow( p->sock, p->haveTo ? &p->to : NULL, p->data, p->len );
		coopSimHead = ( coopSimHead + 1 ) % COOP_SIM_QUEUE;
		coopSimCount--;
	}
}

static void CoopSendTo( int sock, const struct sockaddr_in *to, const void *data, int len )
{
	int delay, loss;

	if ( sock < 0 || len <= 0 )
		return;
	delay = Cvar_VariableIntegerValue( "coop_simlatency" );
	loss = Cvar_VariableIntegerValue( "coop_simloss" );
	if ( loss > 0 && (int)( arc4random() % 100 ) < loss )
		return;
	if ( delay <= 0 || len > COOP_MAX_PACKET )
	{
		CoopSendNow( sock, to, data, len );
		return;
	}
	if ( !coopSim && !( coopSim = calloc( COOP_SIM_QUEUE, sizeof( *coopSim ) ) ) )
		return;
	if ( coopSimCount == COOP_SIM_QUEUE )
		return;		// a full queue loses the packet, as a full router would
	{
		coopSimPacket_t *p = &coopSim[( coopSimHead + coopSimCount ) % COOP_SIM_QUEUE];
		p->due = Sys_Milliseconds() + MIN( delay, 2000 );
		p->sock = sock;
		p->haveTo = to != NULL;
		if ( to )
			p->to = *to;
		p->len = len;
		memcpy( p->data, data, len );
		coopSimCount++;
	}
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
   Replica frames (both sides)

   A frame on the wire, compressed (the system's zlib-format encoder), then cut
   into fragments of up to COOP_FRAGMENT bytes:
     frame, base (0: from nothing), levelId, level time, ackCmd, map
     then the slots that differ from the base frame's, each:
       slot (2 bytes), kind: 0 now empty
                             1 whole: length, bytes
                             2 changed: new length, then runs of (skip, length, bytes)
                               counted from the end of the last run, a length of 0 ends them
     slot 0xffff ends the frame.
   Lengths and skips are variable-length numbers (7 bits a byte).
   ------------------------------------------------------------------------ */

typedef struct {
	byte	*data;
	int		len, size;
} coopBuf_t;

static void CoopBufNeed( coopBuf_t *b, int more )
{
	if ( b->len + more <= b->size )
		return;
	b->size = MAX( b->size * 2, b->len + more + 65536 );
	b->data = realloc( b->data, b->size );
	if ( !b->data )
		Com_Error( ERR_FATAL, "co-op: out of memory for replica frames" );
}

static void CoopBufPut( coopBuf_t *b, const void *data, int len )
{
	CoopBufNeed( b, len );
	memcpy( b->data + b->len, data, len );
	b->len += len;
}

static void CoopBufByte( coopBuf_t *b, int v )
{
	byte c = (byte)v;
	CoopBufPut( b, &c, 1 );
}

static void CoopBufVar( coopBuf_t *b, unsigned v )
{
	while ( v >= 0x80 )
	{
		CoopBufByte( b, ( v & 0x7f ) | 0x80 );
		v >>= 7;
	}
	CoopBufByte( b, v );
}

static void CoopBufLong( coopBuf_t *b, int v )
{
	byte c[4] = { v & 0xff, ( v >> 8 ) & 0xff, ( v >> 16 ) & 0xff, ( v >> 24 ) & 0xff };
	CoopBufPut( b, c, 4 );
}

// reading a decompressed frame
typedef struct {
	const byte	*p, *end;
	qboolean	bad;
} coopReader_t;

static int CoopReadByteR( coopReader_t *r )
{
	if ( r->p >= r->end ) { r->bad = qtrue; return 0; }
	return *r->p++;
}

static unsigned CoopReadVar( coopReader_t *r )
{
	unsigned v = 0;
	for ( int shift = 0; shift < 35; shift += 7 )
	{
		int c = CoopReadByteR( r );
		v |= (unsigned)( c & 0x7f ) << shift;
		if ( !( c & 0x80 ) )
			return v;
	}
	r->bad = qtrue;
	return 0;
}

static int CoopReadLongR( coopReader_t *r )
{
	int a = CoopReadByteR( r ), b = CoopReadByteR( r ), c = CoopReadByteR( r ), d = CoopReadByteR( r );
	return a | ( b << 8 ) | ( c << 16 ) | ( (unsigned)d << 24 );
}

static const byte *CoopReadBytes( coopReader_t *r, int len )
{
	const byte *p = r->p;
	if ( len < 0 || r->end - r->p < len ) { r->bad = qtrue; return NULL; }
	r->p += len;
	return p;
}

static coopFrame_t *CoopRingAlloc( void )
{
	coopFrame_t *ring = calloc( COOP_FRAME_BACKUP, sizeof( coopFrame_t ) );
	if ( !ring )
		Com_Error( ERR_FATAL, "co-op: out of memory for replica frames" );
	return ring;
}

static void CoopRingClear( coopFrame_t *ring )
{
	if ( !ring )
		return;
	for ( int i = 0; i < COOP_FRAME_BACKUP; i++ )
		ring[i].num = 0;
}

static void CoopRingFree( coopFrame_t **ring )
{
	if ( !*ring )
		return;
	for ( int i = 0; i < COOP_FRAME_BACKUP; i++ )
		free( (*ring)[i].data );
	free( *ring );
	*ring = NULL;
}

static coopFrame_t *CoopRingFind( coopFrame_t *ring, int num )
{
	coopFrame_t *f;
	if ( !ring || num <= 0 )
		return NULL;
	f = &ring[num % COOP_FRAME_BACKUP];
	return f->num == num ? f : NULL;
}

static void CoopFrameReset( coopFrame_t *f, int num )
{
	f->num = num;
	f->dataLen = 0;
	memset( f->len, 0, sizeof( f->len ) );
}

static void CoopFrameAdd( coopFrame_t *f, int slot, const void *data, int len )
{
	if ( slot < 0 || slot >= COOP_NUM_SLOTS || len <= 0 )
		return;
	if ( f->dataLen + len > f->dataSize )
	{
		f->dataSize = MAX( f->dataSize * 2, f->dataLen + len + 262144 );
		f->data = realloc( f->data, f->dataSize );
		if ( !f->data )
			Com_Error( ERR_FATAL, "co-op: out of memory for replica frames" );
	}
	memcpy( f->data + f->dataLen, data, len );
	f->ofs[slot] = f->dataLen;
	f->len[slot] = len;
	f->dataLen += len;
}

// f's slots that differ from base (NULL: from nothing) into out
static void CoopEncodeFrame( coopBuf_t *out, const coopFrame_t *f, const coopFrame_t *base )
{
	CoopBufLong( out, f->num );
	CoopBufLong( out, base ? base->num : 0 );
	CoopBufLong( out, f->levelId );
	CoopBufLong( out, f->time );
	CoopBufLong( out, f->ackCmd );
	CoopBufPut( out, f->map, (int)strlen( f->map ) + 1 );

	for ( int slot = 0; slot < COOP_NUM_SLOTS; slot++ )
	{
		const byte	*nb = f->data + f->ofs[slot], *bb = base ? base->data + base->ofs[slot] : NULL;
		int			nl = f->len[slot], bl = base ? base->len[slot] : 0;

		if ( nl == bl && ( !nl || !memcmp( nb, bb, nl ) ) )
			continue;
		CoopBufByte( out, slot & 0xff );
		CoopBufByte( out, slot >> 8 );
		if ( !nl )
		{
			CoopBufByte( out, 0 );
			continue;
		}
		if ( !bl )
		{
			CoopBufByte( out, 1 );
			CoopBufVar( out, nl );
			CoopBufPut( out, nb, nl );
			continue;
		}
		CoopBufByte( out, 2 );
		CoopBufVar( out, nl );
		{
			int pos = 0, last = 0;
			while ( pos < nl )
			{
				int start, end, same;

				while ( pos < nl && pos < bl && nb[pos] == bb[pos] )
					pos++;
				if ( pos >= nl )
					break;
				start = pos;
				// the run ends at 8 equal bytes in a row (a shorter gap is cheaper sent along)
				for ( end = pos, same = 0; end < nl && same < 8; end++ )
					same = ( end < bl && nb[end] == bb[end] ) ? same + 1 : 0;
				end -= same;
				CoopBufVar( out, start - last );
				CoopBufVar( out, end - start );
				CoopBufPut( out, nb + start, end - start );
				pos = last = end;
			}
			CoopBufVar( out, 0 );
			CoopBufVar( out, 0 );
		}
	}
	CoopBufByte( out, 0xff );
	CoopBufByte( out, 0xff );
}

/*
=================
CoopChangeStats

coop_netstats 2 (host): which parts of the images change from one frame to the next, to
find what costs bytes. An entity's or the globals' image is the game's chunks back to back
([id][length][data], the save format); each changed 4-byte word is counted under its chunk and
offset (the struct's own offset: the chunks are the structs the save code writes). Printed
with the other numbers every 5 seconds, the 24 most often changed words first.
=================
*/
#define COOP_STAT_CHUNKS	12
#define COOP_STAT_WORDS		8192
static unsigned	coopStatIds[COOP_STAT_CHUNKS];
static int		(*coopStatWord)[COOP_STAT_WORDS];		// [COOP_STAT_CHUNKS], allocated when first used
static int		coopStatChunkBytes[COOP_STAT_CHUNKS + 1];	// + other slots (configstrings, commands)

static void CoopChangeStats( const coopFrame_t *f, const coopFrame_t *prev )
{
	if ( !coopStatWord && !( coopStatWord = calloc( COOP_STAT_CHUNKS, sizeof( *coopStatWord ) ) ) )
		return;
	for ( int slot = 0; slot < COOP_NUM_SLOTS; slot++ )
	{
		const byte	*nb = f->data + f->ofs[slot], *pb = prev->data + prev->ofs[slot];
		int			nl = f->len[slot], pl = prev->len[slot], pos = 0;

		if ( !nl || !pl || ( nl == pl && !memcmp( nb, pb, nl ) ) )
			continue;
		if ( slot >= COOP_SLOT_CS && slot != COOP_SLOT_GLOBALS )
		{
			coopStatChunkBytes[COOP_STAT_CHUNKS] += nl;
			continue;
		}
		while ( pos + 8 <= nl && pos + 8 <= pl )
		{
			unsigned	id, pid;
			int			len, plen, c;

			memcpy( &id, nb + pos, 4 );
			memcpy( &len, nb + pos + 4, 4 );
			memcpy( &pid, pb + pos, 4 );
			memcpy( &plen, pb + pos + 4, 4 );
			if ( id != pid || len != plen || len < 0 || pos + 8 + len > nl || pos + 8 + len > pl )
				break;		// the two images have different chunks from here on
			for ( c = 0; c < COOP_STAT_CHUNKS && coopStatIds[c] && coopStatIds[c] != id; c++ )
				;
			if ( c < COOP_STAT_CHUNKS )
			{
				coopStatIds[c] = id;
				for ( int w = 0; w < len && w / 4 < COOP_STAT_WORDS; w += 4 )
				{
					int n = MIN( 4, len - w );
					if ( memcmp( nb + pos + 8 + w, pb + pos + 8 + w, n ) )
					{
						coopStatWord[c][w / 4]++;
						coopStatChunkBytes[c] += n;
					}
				}
			}
			pos += 8 + len;
		}
	}
}

static void CoopChangeStatsPrint( void )
{
	char	name[5];
	int		best[24][2], nbest = 0;

	if ( !coopStatWord )
		return;
	for ( int c = 0; c < COOP_STAT_CHUNKS && coopStatIds[c]; c++ )
	{
		for ( int k = 0; k < 4; k++ )
			name[k] = (char)( coopStatIds[c] >> ( 24 - 8 * k ) );
		name[4] = 0;
		Com_Printf( "co-op changes: %s %d bytes\n", name, coopStatChunkBytes[c] );
		for ( int w = 0; w < COOP_STAT_WORDS; w++ )
		{
			int n = coopStatWord[c][w], i;
			if ( !n )
				continue;
			for ( i = nbest; i > 0 && coopStatWord[best[i - 1][0]][best[i - 1][1]] < n; i-- )
				if ( i < 24 )
					memcpy( best[i], best[i - 1], sizeof( best[i] ) );
			if ( i < 24 )
			{
				best[i][0] = c;
				best[i][1] = w;
				if ( nbest < 24 )
					nbest++;
			}
		}
	}
	Com_Printf( "co-op changes: configstrings and commands %d bytes\n", coopStatChunkBytes[COOP_STAT_CHUNKS] );
	for ( int i = 0; i < nbest; i++ )
	{
		unsigned id = coopStatIds[best[i][0]];
		for ( int k = 0; k < 4; k++ )
			name[k] = (char)( id >> ( 24 - 8 * k ) );
		name[4] = 0;
		Com_Printf( "co-op changes: %s +%d: %d\n", name, best[i][1] * 4, coopStatWord[best[i][0]][best[i][1]] );
	}
	memset( coopStatWord, 0, COOP_STAT_CHUNKS * sizeof( *coopStatWord ) );
	memset( coopStatChunkBytes, 0, sizeof( coopStatChunkBytes ) );
}

// a decompressed frame into f (base: the frame it's a delta from, NULL when from nothing);
// qfalse when it doesn't parse
static qboolean CoopDecodeFrame( const byte *data, int len, coopFrame_t *f, const coopFrame_t *base )
{
	static const byte	*changed[COOP_NUM_SLOTS];
	static byte			kind[COOP_NUM_SLOTS];
	coopReader_t		r = { data, data + len, qfalse };
	const char			*map;
	int					n;

	memset( kind, 0xff, sizeof( kind ) );
	r.p += 8;	// frame, base (the caller read them)
	f->levelId = CoopReadLongR( &r );
	f->time = CoopReadLongR( &r );
	f->ackCmd = CoopReadLongR( &r );
	map = (const char *)r.p;
	for ( n = 0; r.p < r.end && *r.p; r.p++, n++ )
		;
	if ( r.p >= r.end || n >= MAX_QPATH )
		return qfalse;
	memcpy( f->map, map, n );
	f->map[n] = 0;
	r.p++;

	for ( ;; )
	{
		int slot = CoopReadByteR( &r );
		slot |= CoopReadByteR( &r ) << 8;
		if ( r.bad )
			return qfalse;
		if ( slot == 0xffff )
			break;
		if ( slot >= COOP_NUM_SLOTS )
			return qfalse;
		kind[slot] = CoopReadByteR( &r );
		changed[slot] = r.p;
		if ( kind[slot] == 1 )
		{
			int nl = CoopReadVar( &r );
			CoopReadBytes( &r, nl );
		}
		else if ( kind[slot] == 2 )
		{
			CoopReadVar( &r );
			for ( ;; )
			{
				CoopReadVar( &r );
				int rl = CoopReadVar( &r );
				if ( r.bad || !rl )
					break;
				CoopReadBytes( &r, rl );
			}
			if ( !base )
				return qfalse;
		}
		else if ( kind[slot] != 0 )
			return qfalse;
		if ( r.bad )
			return qfalse;
	}

	f->dataLen = 0;
	memset( f->len, 0, sizeof( f->len ) );
	for ( int slot = 0; slot < COOP_NUM_SLOTS; slot++ )
	{
		const byte	*bb = base ? base->data + base->ofs[slot] : NULL;
		int			bl = base ? base->len[slot] : 0;

		if ( kind[slot] == 0xff )
		{
			CoopFrameAdd( f, slot, bb, bl );
			continue;
		}
		if ( kind[slot] == 0 )
			continue;
		r.p = changed[slot];
		r.end = data + len;
		if ( kind[slot] == 1 )
		{
			int nl = CoopReadVar( &r );
			CoopFrameAdd( f, slot, CoopReadBytes( &r, nl ), nl );
			continue;
		}
		{
			static coopBuf_t	tmp;
			int					nl = CoopReadVar( &r ), pos = 0;

			if ( nl <= 0 )
				return qfalse;
			// the base's bytes (zeros past their end), then the runs over them
			tmp.len = 0;
			CoopBufNeed( &tmp, nl );
			memset( tmp.data, 0, nl );
			memcpy( tmp.data, bb, MIN( bl, nl ) );
			for ( ;; )
			{
				int skip = CoopReadVar( &r ), rl = CoopReadVar( &r );
				const byte *run;
				if ( r.bad )
					return qfalse;
				if ( !rl )
					break;
				pos += skip;
				if ( pos < 0 || rl < 0 || pos + rl > nl || !( run = CoopReadBytes( &r, rl ) ) )
					return qfalse;
				memcpy( tmp.data + pos, run, rl );
				pos += rl;
			}
			CoopFrameAdd( f, slot, tmp.data, nl );
		}
	}
	return qtrue;
}

/* ------------------------------------------------------------------------
   The host
   ------------------------------------------------------------------------ */

static struct {
	qboolean			active;
	struct sockaddr_in	addr;
	unsigned			session;
	char				name[32];
	char				sex[8];			// "male", "female", or "" (look like player 1)
	char				look[COOP_LOOK_MAX];	// the guest's character, "" for Munro (the game checks it)
	char				nonce[24];
	int					lastHeard;		// Sys_Milliseconds of the guest's last packet
	int					lastStatus;
	int					pausedSent;		// the paused state the guest was last told
	int					connectWindow;	// connect requests: at most 5 a second, from anyone
	int					connectCount;

	// the guest's input
	qboolean			haveCmd;
	int					cmdHeard;		// when the newest command arrived
	coopCmd_t			cmd;			// the newest command
	coopCmd_t			queue[COOP_CMD_QUEUE];	// not applied yet, oldest first
	int					queued;
	int					appliedSeq;		// the newest command the game applied (the frames' ackCmd)
	int					heldButtons;	// buttons of commands dropped from a full queue
	int					viewTime;		// the applied command's (CoopNet_GuestViewTime)

	// the guest's console commands (CoopNet_HostTakeCommand)
	int					textSeq;		// the newest one's number
	char				text[COOP_COMMAND_QUEUE][COOP_COMMAND_LEN];
	int					textQueued;

	// replica frames
	coopFrame_t			*ring;
	coopFrame_t			*building;		// between CoopNet_HostFrameBegin and End
	int					frameNum;		// the newest frame's number
	int					ackFrame;		// the newest frame the guest has
	int					guestLevel;		// the host level the guest has up (CoopNet_HostGuestLevel)
	int					fullFrame;		// the last whole-world frame sent, and when
	int					fullSentAt;
	int					sndbufSet;

	// what it costs (coop_status, coop_netstats)
	int					statStart;		// Sys_Milliseconds the counts below started
	int					statBytes, statPackets, statFrames, statFulls, statSkipped;
	int					statBuildUsec, statSendUsec;	// building the frame (the game) and encoding + sending it
	int					statMaxBytes;	// the biggest frame
} coopHost;

static int coopHostSock = -1;			// for coop_kick (the socket is sys_osx_coop.m's)

static qboolean CoopSameAddress( const struct sockaddr_in *a, const struct sockaddr_in *b )
{
	return a->sin_addr.s_addr == b->sin_addr.s_addr && a->sin_port == b->sin_port;
}

static void CoopHostStateInfo( char *info )
{
	qboolean	playing = SP_IsActive() && SP_CurMap()[0];
	char		hostName[COOP_NAME_MAX + 1];

	CoopOwnName( hostName, sizeof( hostName ) );
	CoopSetInfo( info, "hostName", hostName );

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
	coopHost.queued = 0;
	coopHost.appliedSeq = 0;
	coopHost.viewTime = 0;
	coopHost.textSeq = 0;
	coopHost.textQueued = 0;
	coopHost.name[0] = 0;
	coopHost.sex[0] = 0;
	coopHost.look[0] = 0;
	CoopRingFree( &coopHost.ring );
	coopHost.building = NULL;
}

static void CoopHostReject( int sock, const struct sockaddr_in *from, const char *reason )
{
	Com_Printf( "co-op: refused a player 2 from %s: %s\n", CoopAddressString( from ), reason );
	CoopSendOOB( sock, from, COOP_REJECT, reason );
}

static void CoopHostConnect( int sock, const char *request, const struct sockaddr_in *from )
{
	char	protocol[16], version[64], paks[256], rawName[128], name[32], password[128], ourPassword[128], nonce[24];
	char	sex[16], look[64], why[512], hostWhy[512];
	int		pakMatch;
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
	CoopGetInfo( request, "name", rawName, sizeof( rawName ) );
	CoopCleanName( rawName, name, sizeof( name ) );
	CoopGetInfo( request, "sex", sex, sizeof( sex ) );		// (optional: a guest without it looks like player 1)
	CoopGetInfo( request, "look", look, sizeof( look ) );	// (optional: Munro without it)
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
		CoopSetInfo( info, "paks", FS_CoopPaks() );
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
	// only the game's own paks count: pak0-pak2 must match, the Expansion Pack's pak3 only warns
	pakMatch = CoopNet_ComparePaks( FS_CoopPaks(), paks, qfalse, why, sizeof( why ) );
	CoopNet_ComparePaks( FS_CoopPaks(), paks, qtrue, hostWhy, sizeof( hostWhy ) );
	if ( pakMatch == COOP_PAKS_DIFFER )
	{
		Com_Printf( "co-op: refused %s from %s: %s\n", name, CoopAddressString( from ), hostWhy );
		CoopSendOOB( sock, from, COOP_REJECT, va( "Your game files differ from the host's. %s", why ) );
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
	Q_strncpyz( coopHost.sex, CoopSexValue( sex ), sizeof( coopHost.sex ) );
	CoopLookValue( look, coopHost.look, sizeof( coopHost.look ) );
	Q_strncpyz( coopHost.nonce, nonce, sizeof( coopHost.nonce ) );
	coopHost.lastHeard = now;
	coopHost.lastStatus = now;
	coopHost.pausedSent = 0;
	coopHost.haveCmd = qfalse;
	coopHost.heldButtons = 0;
	coopHost.queued = 0;
	coopHost.appliedSeq = 0;
	coopHost.viewTime = 0;
	coopHost.textSeq = 0;
	coopHost.textQueued = 0;
	memset( &coopHost.cmd, 0, sizeof( coopHost.cmd ) );
	CoopRingClear( coopHost.ring );
	coopHost.ackFrame = 0;
	coopHost.guestLevel = 0;
	coopHost.fullFrame = 0;
	coopHost.fullSentAt = 0;
	coopHost.statStart = now;
	coopHost.statBytes = coopHost.statPackets = coopHost.statFrames = coopHost.statFulls = coopHost.statSkipped = 0;
	coopHost.statBuildUsec = coopHost.statSendUsec = coopHost.statMaxBytes = 0;

	info[0] = 0;
	CoopSetInfo( info, "session", va( "%u", coopHost.session ) );
	CoopSetInfo( info, "nonce", nonce );
	CoopSetInfo( info, "paks", FS_CoopPaks() );
	CoopHostStateInfo( info );
	CoopSendOOB( sock, from, COOP_ACCEPT, info );
	Com_Printf( "co-op: %s joined from %s as player 2%s%s\n", coopHost.name, CoopAddressString( from ),
		coopHost.sex[0] ? va( " (%s)", coopHost.sex ) : "", coopHost.look[0] ? va( " as %s", coopHost.look ) : "" );
	if ( pakMatch == COOP_PAKS_EXPANSION )
		Com_Printf( "co-op: note: %s\n", hostWhy );
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
		c.viewTime = MSG_ReadLong( msg );
		if ( msg->readcount > msg->cursize )
			return;
		// oldest first; only commands newer than the newest one seen count
		if ( coopHost.haveCmd && c.seq - coopHost.cmd.seq <= 0 )
			continue;
		coopHost.cmd = c;
		if ( coopHost.queued == COOP_CMD_QUEUE )
		{
			// the guest got ahead: drop the oldest, keeping its buttons
			coopHost.heldButtons |= coopHost.queue[0].buttons;
			memmove( &coopHost.queue[0], &coopHost.queue[1], sizeof( coopHost.queue[0] ) * ( COOP_CMD_QUEUE - 1 ) );
			coopHost.queued--;
		}
		coopHost.queue[coopHost.queued++] = c;
		coopHost.haveCmd = qtrue;
		coopHost.cmdHeard = Sys_Milliseconds();
	}
}

// the newest replica frame the guest has put together (at the end of its input and keepalives)
static void CoopHostReadAck( msg_t *msg )
{
	int ack;

	if ( msg->readcount + 4 > msg->cursize )
		return;
	ack = MSG_ReadLong( msg );
	if ( ack > coopHost.ackFrame && ack <= coopHost.frameNum )
		coopHost.ackFrame = ack;
	if ( msg->readcount + 4 <= msg->cursize )
		coopHost.guestLevel = MSG_ReadLong( msg );
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
		CoopHostReadAck( &msg );
		break;
	case COOP_MSG_KEEPALIVE:
		CoopHostReadAck( &msg );
		break;
	case COOP_MSG_COMMAND:
		{
			int seq = MSG_ReadLong( &msg );
			char text[COOP_COMMAND_LEN];
			CoopCleanText( MSG_ReadString( &msg ), text, sizeof( text ) );
			if ( msg.readcount > msg.cursize || seq <= coopHost.textSeq )
				break;		// (a copy of one already taken)
			coopHost.textSeq = seq;
			if ( text[0] && coopHost.textQueued < COOP_COMMAND_QUEUE )
				Q_strncpyz( coopHost.text[coopHost.textQueued++], text, COOP_COMMAND_LEN );
		}
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
	int paused;

	coopHostSock = sock;
	if ( !coopHost.active )
	{
		SP_CoopCreditsDone();
		return;
	}
	// the closing credits are over (this game went back to the menu after them): so is the guest's game
	if ( SP_CoopCreditsUp() && ( !SP_IsActive() || clc.state != CA_ACTIVE ) && !SP_IsLoading() )
	{
		SP_CoopCreditsDone();
		CoopHostDrop( sock, "the game is over", qtrue );
		return;
	}
	CoopNetStats();
	if ( now - coopHost.lastHeard > COOP_TIMEOUT )
	{
		CoopHostDrop( sock, "timed out", qtrue );
		return;
	}
	// the guest hears at once when this game pauses or goes on (and once a second anyway)
	paused = ( SP_IsActive() && SP_MenuPaused() ) ? 1 : 0;
	if ( now - coopHost.lastStatus >= COOP_STATUS_INTERVAL || paused != coopHost.pausedSent )
	{
		byte	buf[COOP_MAX_PACKET];
		char	info[MAX_INFO_STRING];
		msg_t	msg;

		coopHost.lastStatus = now;
		coopHost.pausedSent = paused;
		info[0] = 0;
		CoopHostStateInfo( info );
		// paused: the game is stopped (menu, console); frame: the newest frame sent before it
		CoopSetInfo( info, "paused", paused ? "1" : "0" );
		CoopSetInfo( info, "frame", va( "%d", coopHost.frameNum ) );
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

/*
=================
CoopNet_HostTakeCommand

The bridge on the host, before a game frame: the guest's oldest console command not run
yet (for player 2), into out. 0 when there is none.
=================
*/
int CoopNet_HostTakeCommand( char *out, int size )
{
	if ( !coopHost.active || !coopHost.textQueued )
		return 0;
	Q_strncpyz( out, coopHost.text[0], size );
	coopHost.textQueued--;
	memmove( &coopHost.text[0], &coopHost.text[1], sizeof( coopHost.text[0] ) * coopHost.textQueued );
	return 1;
}

int CoopNet_HostNeedsPassword( void )
{
	return Sys_CoopPassword()[0] != 0;
}

int CoopNet_HostHasGuest( void )
{
	return coopHost.active;
}

int CoopNet_HostGuestLevel( void )
{
	return coopHost.active ? coopHost.guestLevel : 0;
}

/*
=================
CoopNet_GuestInput

The game's co-op code, once a game frame on the host (through sp/sp_bridge.cpp): the
guest's next command. The guest makes one command per host frame it gets (each 50 ms
of its play), so the game applies them one a frame, in order, and every replica frame
says which was the last (ackCmd): the guest's prediction replays the ones after it.
With none waiting this frame, player 2 stands with the newest command's angles. Returns
0 when there is no guest or its input is older than COOP_INPUT_STALE ms; player 2 then
holds still (or, with no guest, follows player 1).
=================
*/
int CoopNet_GuestInput( int *angles, int *buttons, int *weapon, signed char *forwardmove,
	signed char *rightmove, signed char *upmove )
{
	coopCmd_t c;

	if ( !coopHost.active || !coopHost.haveCmd || Sys_Milliseconds() - coopHost.cmdHeard > COOP_INPUT_STALE )
	{
		coopHost.heldButtons = 0;
		coopHost.queued = 0;
		return 0;
	}
	if ( coopHost.queued )
	{
		c = coopHost.queue[0];
		memmove( &coopHost.queue[0], &coopHost.queue[1], sizeof( coopHost.queue[0] ) * ( COOP_CMD_QUEUE - 1 ) );
		coopHost.queued--;
		coopHost.appliedSeq = c.seq;
		coopHost.viewTime = c.viewTime;
	}
	else
	{
		c = coopHost.cmd;
		c.buttons = 0;
		c.forwardmove = c.rightmove = c.upmove = 0;
	}
	angles[0] = c.angles[0];
	angles[1] = c.angles[1];
	angles[2] = c.angles[2];
	*buttons = c.buttons | coopHost.heldButtons;
	*weapon = c.weapon;
	*forwardmove = c.forwardmove;
	*rightmove = c.rightmove;
	*upmove = c.upmove;
	coopHost.heldButtons = 0;
	return 1;
}

int CoopNet_GuestViewTime( void )
{
	return coopHost.active ? coopHost.viewTime : 0;
}

// the guest's name, "" without a guest
const char *CoopNet_GuestName( void )
{
	return coopHost.active ? coopHost.name : "";
}

// how the guest wants player 2 to look: "male", "female", or "" (like player 1, or no guest)
const char *CoopNet_GuestSex( void )
{
	return coopHost.active ? coopHost.sex : "";
}

// the character the guest chose, "" for the default Munro or without a guest
const char *CoopNet_GuestLook( void )
{
	return coopHost.active ? coopHost.look : "";
}

// the copy of qpath the game would load is in one of the game's own pak0-pak2 (pak3, other
// packs and loose files don't count: a guest's pak0-pak2 match the host's, nothing else does)
qboolean CoopNet_CorePakFile( const char *qpath )
{
	int source = FS_FileSource( qpath ), pak;		// the pack's checksum (a folder's is another number)

	if ( !source )
		return qfalse;
	pak = FS_CoopCorePakIndex( (unsigned int)source );
	return ( pak >= 0 && pak <= 2 ) ? qtrue : qfalse;
}

/*
=================
CoopNet_HostFrameBegin / Slot / End

The bridge, after every game frame while a guest is connected: the world as slots
(the game's images, the configstrings, the newest server commands). End sends the
changes against the newest frame the guest has, compressed and in fragments; with
no such frame (the guest just joined, or fell more than COOP_FRAME_BACKUP frames
behind, or a new level started), the whole world, at most once every
COOP_FULL_RETRY ms until the guest has it.
=================
*/
static long long CoopUsec( void )
{
	struct timeval tv;
	gettimeofday( &tv, NULL );
	return (long long)tv.tv_sec * 1000000 + tv.tv_usec;
}

static long long coopHostBuildStart;

int CoopNet_HostFrameBegin( int levelId, int levelTime, const char *map )
{
	coopFrame_t *f;

	coopHost.building = NULL;
	if ( !coopHost.active || coopHostSock < 0 )
		return 0;
	if ( !coopHost.ring )
		coopHost.ring = CoopRingAlloc();
	coopHost.frameNum++;
	f = &coopHost.ring[coopHost.frameNum % COOP_FRAME_BACKUP];
	CoopFrameReset( f, coopHost.frameNum );
	f->levelId = levelId;
	f->time = levelTime;
	f->ackCmd = coopHost.appliedSeq;
	Q_strncpyz( f->map, map ? map : "", sizeof( f->map ) );
	coopHost.building = f;
	coopHostBuildStart = CoopUsec();
	return 1;
}

void CoopNet_HostFrameSlot( int slot, const void *data, int len )
{
	if ( coopHost.building )
		CoopFrameAdd( coopHost.building, slot, data, len );
}

qboolean CoopNet_HostFrameSlotKeep( int slot )
{
	coopFrame_t *f = coopHost.building, *prev;

	if ( !f || slot < 0 || slot >= COOP_NUM_SLOTS || !( prev = CoopRingFind( coopHost.ring, f->num - 1 ) ) ||
		prev->levelId != f->levelId || !prev->len[slot] )
		return qfalse;
	CoopFrameAdd( f, slot, prev->data + prev->ofs[slot], prev->len[slot] );
	return qtrue;
}

void CoopNet_HostFrameEnd( void )
{
	static coopBuf_t	enc, zbuf;
	coopFrame_t			*f = coopHost.building, *base;
	long long			t0 = CoopUsec();
	int					now = Sys_Milliseconds(), zlen, count;

	coopHost.building = NULL;
	if ( !f || !coopHost.active )
		return;
	coopHost.statBuildUsec += (int)( t0 - coopHostBuildStart );
	coopHost.statFrames++;
	if ( Cvar_VariableIntegerValue( "coop_netstats" ) == 2 && ( base = CoopRingFind( coopHost.ring, f->num - 1 ) ) != NULL &&
		base->levelId == f->levelId )
		CoopChangeStats( f, base );

	base = CoopRingFind( coopHost.ring, coopHost.ackFrame );
	if ( base && ( base == f || base->levelId != f->levelId ) )
		base = NULL;
	if ( !base )
	{
		// the guest hasn't got the last whole frame yet: don't send another so soon
		if ( coopHost.fullFrame && coopHost.ackFrame < coopHost.fullFrame && now - coopHost.fullSentAt < COOP_FULL_RETRY )
		{
			coopHost.statSkipped++;
			return;
		}
		coopHost.fullFrame = f->num;
		coopHost.fullSentAt = now;
		coopHost.statFulls++;
	}

	enc.len = 0;
	CoopEncodeFrame( &enc, f, base );
	zbuf.len = 0;
	CoopBufNeed( &zbuf, enc.len + 4096 );
	zlen = (int)compression_encode_buffer( zbuf.data, zbuf.size, enc.data, enc.len, NULL, COMPRESSION_ZLIB );
	if ( zlen <= 0 )
	{
		Com_Printf( "co-op: couldn't compress replica frame %d (%d bytes)\n", f->num, enc.len );
		return;
	}
	count = ( zlen + COOP_FRAGMENT - 1 ) / COOP_FRAGMENT;
	if ( count > COOP_MAX_FRAGMENTS )
	{
		Com_Printf( "co-op: replica frame %d is too big (%d bytes)\n", f->num, zlen );
		return;
	}
	if ( !coopHost.sndbufSet )
	{
		int size = 4 * 1024 * 1024;
		setsockopt( coopHostSock, SOL_SOCKET, SO_SNDBUF, &size, sizeof( size ) );
		coopHost.sndbufSet = 1;
	}
	for ( int i = 0; i < count; i++ )
	{
		byte	buf[COOP_MAX_PACKET];
		msg_t	msg;
		int		n = MIN( COOP_FRAGMENT, zlen - i * COOP_FRAGMENT );

		CoopBeginMessage( &msg, buf, sizeof( buf ), coopHost.session, COOP_MSG_FRAME );
		MSG_WriteLong( &msg, f->num );
		MSG_WriteShort( &msg, i );
		MSG_WriteShort( &msg, count );
		MSG_WriteLong( &msg, zlen );
		MSG_WriteLong( &msg, enc.len );
		MSG_WriteData( &msg, zbuf.data + i * COOP_FRAGMENT, n );
		CoopSendMessage( coopHostSock, &coopHost.addr, &msg );
		coopHost.statBytes += msg.cursize + 28;		// + the UDP and IPv4 headers
		coopHost.statPackets++;
	}
	coopHost.statMaxBytes = MAX( coopHost.statMaxBytes, zlen );
	coopHost.statSendUsec += (int)( CoopUsec() - t0 );
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
	int					seq;
	int					textSeq;		// the last console command sent (CoopNet_GuestSendCommand)
	coopCmd_t			cmds[COOP_INPUT_BACKUP];	// the newest last
	int					numCmds;
	qboolean			unsent;			// a command not sent yet

	// the host's state
	char				hostMap[MAX_QPATH];
	char				hostState[16];
	qboolean			hostPaused;		// the host's game is paused...
	int					hostPausedFrame;	// ...after this frame

	// replica frames
	coopFrame_t			*ring;
	int					newestFrame;	// the newest frame put together
	int					newestFrameAt;	// and when (Sys_Milliseconds)
	int					ackSent;		// the newest frame the host was told about
	int					loadLevelId;	// the host level this guest loaded the map for (0: none yet)
	int					readyLevel;		// ...and the one it has up (CoopNet_GuestLevelReady)
	struct {
		int		frame;					// 0: free
		int		count, got, zlen, rawlen;
		byte	have[COOP_MAX_FRAGMENTS / 8];
		byte	*buf;
		int		bufSize;
	} assembly[COOP_ASSEMBLY];

	// what it costs (coop_status, coop_netstats)
	int					statStart;
	int					statBytes, statPackets, statFrames, statFulls, statDropped;
	int					statDecodeUsec;
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
	CoopRingFree( &coopGuest.ring );
	for ( int i = 0; i < COOP_ASSEMBLY; i++ )
		free( coopGuest.assembly[i].buf );
	// a replica level can't go on alone: back to the menu, and the next level is this copy's own
	if ( coopGuest.loadLevelId )
	{
		Cvar_Set( "sp_coop", "0" );
		if ( SP_IsActive() || SP_IsLoading() )
			SP_AbortToMenu();
	}
	memset( &coopGuest, 0, sizeof( coopGuest ) );
	coopGuest.state = COOP_GUEST_IDLE;
	coopGuest.sock = -1;
	if ( Cvar_VariableString( "coop_connect" )[0] )
		Cvar_Set( "coop_connect", "" );
}

static void CoopGuestSendConnect( void )
{
	char	info[MAX_INFO_STRING], name[COOP_NAME_MAX + 1];
	const char *sex = CoopSexValue( Cvar_VariableString( "coop_sex" ) );
	char	look[COOP_LOOK_MAX];

	CoopLookValue( Cvar_VariableString( "coop_look" ), look, sizeof( look ) );
	CoopOwnName( name, sizeof( name ) );
	info[0] = 0;
	CoopSetInfo( info, "coopProtocol", va( "%d", VSP_COOP_PROTOCOL ) );
	CoopSetInfo( info, "version", EF_APP_VERSION );
	CoopSetInfo( info, "paks", FS_CoopPaks() );
	CoopSetInfo( info, "name", name[0] ? name : "Player 2" );
	if ( sex[0] )
		CoopSetInfo( info, "sex", sex );
	if ( look[0] )
		CoopSetInfo( info, "look", look );
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
	{
		int size = 8 * 1024 * 1024;		// whole-world frames come in a burst of fragments
		setsockopt( fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof( size ) );
	}
	coopGuest.sock = fd;
	coopGuest.state = COOP_GUEST_CONNECTING;
	Com_sprintf( coopGuest.nonce, sizeof( coopGuest.nonce ), "%08x%08x", arc4random(), arc4random() );
	Com_Printf( "co-op: joining %s...\n", CoopAddressString( &coopGuest.host ) );
	CoopGuestSendConnect();
}

// the host's state (from its accept or status), for coop_status
static void CoopGuestHostState( const char *info )
{
	CoopGetInfo( info, "state", coopGuest.hostState, sizeof( coopGuest.hostState ) );
	CoopGetInfo( info, "map", coopGuest.hostMap, sizeof( coopGuest.hostMap ) );
	{
		char paused[8], frame[16];
		CoopGetInfo( info, "paused", paused, sizeof( paused ) );
		CoopGetInfo( info, "frame", frame, sizeof( frame ) );
		coopGuest.hostPaused = atoi( paused ) ? qtrue : qfalse;
		coopGuest.hostPausedFrame = atoi( frame );
	}
}

/*
=================
CoopGuestFragment

A fragment of a replica frame. When a frame is whole: decompress it, put it
together from its base frame, and acknowledge it.
=================
*/
static void CoopGuestFragment( msg_t *msg )
{
	int		frame = MSG_ReadLong( msg );
	int		index = MSG_ReadShort( msg ) & 0xffff;
	int		count = MSG_ReadShort( msg ) & 0xffff;
	int		zlen = MSG_ReadLong( msg );
	int		rawlen = MSG_ReadLong( msg );
	int		n = msg->cursize - msg->readcount;
	int		a, oldest = 0;

	if ( msg->readcount > msg->cursize || frame <= coopGuest.newestFrame || count < 1 || count > COOP_MAX_FRAGMENTS ||
		index >= count || zlen <= 0 || zlen > count * COOP_FRAGMENT || rawlen <= 0 || rawlen > 64 * 1024 * 1024 || n <= 0 ||
		index * COOP_FRAGMENT + n > zlen )
		return;
	coopGuest.statBytes += msg->cursize + 28;
	coopGuest.statPackets++;

	for ( a = 0; a < COOP_ASSEMBLY; a++ )
	{
		if ( coopGuest.assembly[a].frame == frame )
			break;
		if ( coopGuest.assembly[a].frame < coopGuest.assembly[oldest].frame )
			oldest = a;
	}
	if ( a == COOP_ASSEMBLY )
	{
		a = oldest;
		coopGuest.assembly[a].frame = frame;
		coopGuest.assembly[a].count = count;
		coopGuest.assembly[a].got = 0;
		coopGuest.assembly[a].zlen = zlen;
		coopGuest.assembly[a].rawlen = rawlen;
		memset( coopGuest.assembly[a].have, 0, sizeof( coopGuest.assembly[a].have ) );
		if ( coopGuest.assembly[a].bufSize < zlen )
		{
			coopGuest.assembly[a].bufSize = zlen;
			coopGuest.assembly[a].buf = realloc( coopGuest.assembly[a].buf, zlen );
			if ( !coopGuest.assembly[a].buf )
				Com_Error( ERR_FATAL, "co-op: out of memory for replica frames" );
		}
	}
	if ( coopGuest.assembly[a].count != count || coopGuest.assembly[a].zlen != zlen ||
		( coopGuest.assembly[a].have[index >> 3] & ( 1 << ( index & 7 ) ) ) )
		return;
	coopGuest.assembly[a].have[index >> 3] |= 1 << ( index & 7 );
	MSG_ReadData( msg, coopGuest.assembly[a].buf + index * COOP_FRAGMENT, n );
	if ( ++coopGuest.assembly[a].got < count )
		return;

	// whole: decode it
	{
		static coopBuf_t	raw;
		long long			t0 = CoopUsec();
		coopFrame_t			*f, *base = NULL;
		int					baseNum, got;

		coopGuest.assembly[a].frame = 0;
		raw.len = 0;
		CoopBufNeed( &raw, rawlen );
		got = (int)compression_decode_buffer( raw.data, rawlen, coopGuest.assembly[a].buf, zlen, NULL, COMPRESSION_ZLIB );
		if ( got != rawlen || rawlen < 8 )
		{
			coopGuest.statDropped++;
			return;
		}
		memcpy( &baseNum, raw.data + 4, 4 );
		baseNum = LittleLong( baseNum );
		if ( baseNum )
		{
			base = CoopRingFind( coopGuest.ring, baseNum );
			if ( !base )
			{
				coopGuest.statDropped++;		// its base is gone: the host sends from a newer one soon
				return;
			}
		}
		if ( !coopGuest.ring )
			coopGuest.ring = CoopRingAlloc();
		f = &coopGuest.ring[frame % COOP_FRAME_BACKUP];
		if ( f == base )
		{
			coopGuest.statDropped++;		// would overwrite its own base (more than a ring behind)
			return;
		}
		CoopFrameReset( f, 0 );
		if ( !CoopDecodeFrame( raw.data, rawlen, f, base ) )
		{
			Com_Printf( "co-op: replica frame %d from the host doesn't decode; dropped\n", frame );
			coopGuest.statDropped++;
			return;
		}
		f->num = frame;
		coopGuest.newestFrame = frame;
		coopGuest.newestFrameAt = Sys_Milliseconds();
		coopGuest.statFrames++;
		if ( !baseNum )
			coopGuest.statFulls++;
		coopGuest.statDecodeUsec += (int)( CoopUsec() - t0 );
	}
}

static void CoopGuestReceive( void )
{
	unsigned char	data[COOP_MAX_PACKET];
	char			text[COOP_MAX_PACKET];

	for ( int i = 0; coopGuest.sock >= 0 && i < 4096; i++ )
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
			char nonce[24], sessionText[16], rawName[128], hostName[COOP_NAME_MAX + 1];

			if ( coopGuest.state != COOP_GUEST_CONNECTING )
				continue;
			CoopGetInfo( rest, "nonce", nonce, sizeof( nonce ) );
			CoopGetInfo( rest, "session", sessionText, sizeof( sessionText ) );
			if ( strcmp( nonce, coopGuest.nonce ) || !sessionText[0] )
				continue;
			coopGuest.session = (unsigned)strtoul( sessionText, NULL, 10 );
			coopGuest.state = COOP_GUEST_CONNECTED;
			coopGuest.lastHeard = Sys_Milliseconds();
			coopGuest.statStart = coopGuest.lastHeard;
			CoopGetInfo( rest, "hostName", rawName, sizeof( rawName ) );
			CoopCleanName( rawName, hostName, sizeof( hostName ) );
			Com_Printf( "co-op: joined %s%s%s as player 2\n", hostName[0] ? hostName : "", hostName[0] ? " at " : "",
				CoopAddressString( &coopGuest.host ) );
			{
				char hostPaks[256], note[512];
				CoopGetInfo( rest, "paks", hostPaks, sizeof( hostPaks ) );
				if ( CoopNet_ComparePaks( hostPaks, FS_CoopPaks(), qfalse, note, sizeof( note ) ) != COOP_PAKS_SAME )
					Com_Printf( "co-op: note: %s\n", note );
			}
			CoopGuestHostState( rest );
			continue;
		}
		if ( ( rest = CoopOOBText( data, (int)n, COOP_REJECT, text, sizeof( text ) ) ) != NULL )
		{
			char reason[768];

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
		if ( type == COOP_MSG_FRAME )
		{
			CoopGuestFragment( &msg );
		}
		else if ( type == COOP_MSG_STATUS )
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

static void CoopGuestSend( void );

/*
=================
CoopNet_GuestQueueCmd

The bridge on the guest, once for every host frame its replica level takes in: this
player's command for that frame, numbered by the bridge (the same numbers its
prediction uses). Sent at once, with the few before it in case a packet is lost.
Returns 0 when this player has no input now (a menu, the console, a load).
=================
*/
int CoopNet_GuestQueueCmd( int seq, const int *angles, int buttons, int weapon,
	int forwardmove, int rightmove, int upmove, int viewTime )
{
	coopCmd_t *c;

	if ( coopGuest.state != COOP_GUEST_CONNECTED || !CoopGuestHasInput() )
		return 0;
	if ( coopGuest.numCmds == COOP_INPUT_BACKUP )
	{
		memmove( &coopGuest.cmds[0], &coopGuest.cmds[1], sizeof( coopGuest.cmds[0] ) * ( COOP_INPUT_BACKUP - 1 ) );
		coopGuest.numCmds--;
	}
	c = &coopGuest.cmds[coopGuest.numCmds++];
	c->seq = seq;
	coopGuest.seq = seq;
	for ( int j = 0; j < 3; j++ )
		c->angles[j] = angles[j] & 0xffff;
	c->buttons = buttons;
	c->weapon = weapon;
	c->forwardmove = (signed char)forwardmove;
	c->rightmove = (signed char)rightmove;
	c->upmove = (signed char)upmove;
	c->viewTime = viewTime;
	coopGuest.unsent = qtrue;
	CoopGuestSend();
	return 1;
}

/*
=================
CoopNet_GuestSendCommand

The bridge on the guest: a console command for player 2 on the host (a cheat). Returns 0
when not connected.
=================
*/
int CoopNet_GuestSendCommand( const char *text )
{
	byte	buf[COOP_MAX_PACKET];
	msg_t	msg;
	char	clean[COOP_COMMAND_LEN];

	if ( coopGuest.state != COOP_GUEST_CONNECTED )
		return 0;
	CoopCleanText( text, clean, sizeof( clean ) );
	coopGuest.textSeq++;
	for ( int i = 0; i < COOP_COMMAND_SENDS; i++ )
	{
		CoopBeginMessage( &msg, buf, sizeof( buf ), coopGuest.session, COOP_MSG_COMMAND );
		MSG_WriteLong( &msg, coopGuest.textSeq );
		MSG_WriteString( &msg, clean );
		CoopSendMessage( coopGuest.sock, NULL, &msg );
	}
	return 1;
}

static void CoopGuestSend( void )
{
	byte	buf[COOP_MAX_PACKET];
	msg_t	msg;
	int		now = Sys_Milliseconds();

	if ( coopGuest.unsent && coopGuest.numCmds )
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
			MSG_WriteLong( &msg, c->viewTime );
		}
		MSG_WriteLong( &msg, coopGuest.newestFrame );
		MSG_WriteLong( &msg, coopGuest.readyLevel );
		CoopSendMessage( coopGuest.sock, NULL, &msg );
		coopGuest.unsent = qfalse;
		coopGuest.lastSend = now;
		coopGuest.ackSent = coopGuest.newestFrame;
	}
	else if ( now - coopGuest.lastSend >= COOP_KEEPALIVE_INTERVAL || coopGuest.ackSent != coopGuest.newestFrame )
	{
		// (a new replica frame is acknowledged at once, so the host's next one is a small change)
		CoopBeginMessage( &msg, buf, sizeof( buf ), coopGuest.session, COOP_MSG_KEEPALIVE );
		MSG_WriteLong( &msg, coopGuest.newestFrame );
		MSG_WriteLong( &msg, coopGuest.readyLevel );
		CoopSendMessage( coopGuest.sock, NULL, &msg );
		coopGuest.lastSend = now;
		coopGuest.ackSent = coopGuest.newestFrame;
	}
}

/*
=================
CoopGuestFollowLevel

The host's newest frame is of a level this guest hasn't loaded (it just joined,
or the host changed level): load that map as a replica (sp_coop 2), which then
applies the host's frames instead of running its own game.
=================
*/
static void CoopGuestFollowLevel( void )
{
	coopFrame_t *f = CoopRingFind( coopGuest.ring, coopGuest.newestFrame );

	if ( !f || f->levelId == coopGuest.loadLevelId || SP_IsLoading() || !f->map[0] )
		return;
	for ( const char *p = f->map; *p; p++ )
	{
		if ( !isalnum( (unsigned char)*p ) && *p != '_' && *p != '-' && *p != '/' )
			return;		// not a map name
	}
	coopGuest.loadLevelId = f->levelId;
	Com_Printf( "co-op: the host is on %s; loading it as player 2\n", f->map );
	// a full-screen movie still playing (the logo at startup, when Join starts the game) would
	// end later in the menu, over the host's level: stop it first
	SCR_StopCinematic();
	Cvar_Set( "sp_coop", "2" );
	Cbuf_AddText( va( "map %s\n", f->map ) );
}

int CoopNet_GuestNewestFrame( void )
{
	return coopGuest.state == COOP_GUEST_CONNECTED ? coopGuest.newestFrame : 0;
}

qboolean CoopNet_GuestFrameInfo( int frame, int *levelId, int *levelTime, int *ackCmd )
{
	coopFrame_t *f = CoopRingFind( coopGuest.ring, frame );

	if ( !f )
		return qfalse;
	*levelId = f->levelId;
	*levelTime = f->time;
	*ackCmd = f->ackCmd;
	return qtrue;
}

int CoopNet_GuestFrameSlot( int frame, int slot, const unsigned char **data )
{
	coopFrame_t *f = CoopRingFind( coopGuest.ring, frame );

	*data = NULL;
	if ( !f || slot < 0 || slot >= COOP_NUM_SLOTS || !f->len[slot] )
		return 0;
	*data = f->data + f->ofs[slot];
	return f->len[slot];
}

int CoopNet_GuestLevelId( void )
{
	return coopGuest.loadLevelId;
}

void CoopNet_GuestLevelReady( int levelId )
{
	if ( coopGuest.readyLevel != levelId )
	{
		coopGuest.readyLevel = levelId;
		coopGuest.ackSent = -1;		// tell the host now (a keepalive)
	}
}

int CoopNet_GuestHostPaused( void )
{
	if ( coopGuest.state != COOP_GUEST_CONNECTED || !coopGuest.newestFrame )
		return 0;
	if ( coopGuest.hostPaused && coopGuest.newestFrame <= coopGuest.hostPausedFrame )
		return 1;		// (a newer frame means it went on, even if that status was lost)
	if ( Sys_Milliseconds() - coopGuest.newestFrameAt > COOP_HOST_STALL )
		return 2;
	return 0;
}

static void CoopStatus_f( void );
static void CoopDisconnect_f( void );
static void CoopKick_f( void );

static void CoopAddCommands( void )
{
	if ( coopCommandsAdded )
		return;
	coopCommandsAdded = qtrue;
	Cvar_Get( "coop_netstats", "0", CVAR_TEMP );
	Cvar_Get( "coop_simlatency", "0", CVAR_TEMP );
	Cvar_Get( "coop_simloss", "0", CVAR_TEMP );
	Cmd_AddCommand( "coop_status", CoopStatus_f );
	Cmd_AddCommand( "coop_disconnect", CoopDisconnect_f );
	Cmd_AddCommand( "coop_kick", CoopKick_f );
}

void CoopNet_GuestFrame( void )
{
	const char	*want = Cvar_VariableString( "coop_connect" );
	int			now;

	if ( coopSimCount )
		CoopSimFlush();		// (both sides: this runs every frame, host or guest)

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
	CoopGuestFollowLevel();
	CoopGuestSend();
	CoopNetStats();
}

/*
=================
CoopNetStats

What the replica costs since the counts started: bytes and packets a second, frames
(whole ones), and the time spent on them. coop_status prints it; with coop_netstats 1
it's printed every 5 seconds (and the counts start over).
=================
*/
static void CoopNetStatsPrint( qboolean reset )
{
	int now = Sys_Milliseconds();

	if ( coopHost.active && coopHost.statStart )
	{
		float secs = MAX( 0.001f, ( now - coopHost.statStart ) / 1000.0f );
		int frames = MAX( 1, coopHost.statFrames );
		Com_Printf( "co-op net: host sent %.0f bytes/s, %.1f packets/s, %d frames (%d whole, %d held back), "
			"biggest %d bytes; per frame %.2f ms building, %.2f ms encoding+sending\n",
			coopHost.statBytes / secs, coopHost.statPackets / secs, coopHost.statFrames, coopHost.statFulls,
			coopHost.statSkipped, coopHost.statMaxBytes, coopHost.statBuildUsec / 1000.0f / frames,
			coopHost.statSendUsec / 1000.0f / frames );
		if ( reset && Cvar_VariableIntegerValue( "coop_netstats" ) == 2 )
			CoopChangeStatsPrint();
		if ( reset )
		{
			coopHost.statStart = now;
			coopHost.statBytes = coopHost.statPackets = coopHost.statFrames = coopHost.statFulls = coopHost.statSkipped = 0;
			coopHost.statBuildUsec = coopHost.statSendUsec = coopHost.statMaxBytes = 0;
		}
	}
	if ( coopGuest.state == COOP_GUEST_CONNECTED && coopGuest.statStart )
	{
		float secs = MAX( 0.001f, ( now - coopGuest.statStart ) / 1000.0f );
		Com_Printf( "co-op net: guest received %.0f bytes/s, %.1f packets/s, %d frames (%d whole, %d dropped), "
			"%.2f ms decoding per frame\n", coopGuest.statBytes / secs, coopGuest.statPackets / secs,
			coopGuest.statFrames, coopGuest.statFulls, coopGuest.statDropped,
			coopGuest.statDecodeUsec / 1000.0f / MAX( 1, coopGuest.statFrames ) );
		if ( reset )
		{
			coopGuest.statStart = now;
			coopGuest.statBytes = coopGuest.statPackets = coopGuest.statFrames = coopGuest.statFulls = coopGuest.statDropped = 0;
			coopGuest.statDecodeUsec = 0;
		}
	}
}

static void CoopNetStats( void )
{
	static int last;
	int now = Sys_Milliseconds();

	if ( !Cvar_VariableIntegerValue( "coop_netstats" ) )
		return;
	if ( !last || now - last < 0 )
		last = now;
	if ( now - last < 5000 )
		return;
	last = now;
	CoopNetStatsPrint( qtrue );
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
	CoopNetStatsPrint( qfalse );
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
