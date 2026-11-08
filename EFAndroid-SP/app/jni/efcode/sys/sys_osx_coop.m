/*
===========================================================================
macOS app bundle: single-player co-op in the launcher (experimental).

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
It is compiled with ARC, and only in EF_COOP builds (see the macOS
CMakeLists.txt); sys_osx_coop.h has the hooks the launcher calls.

The Single Player card gets a "Co-op…" button. Its sheet has:
  - Host: plays single player exactly as the card's Play does, plus
    +set sp_coop 1 (the latched cvar that turns co-op on at the next level
    start). The sheet lists this Mac's network addresses for the friend.
  - Join: an address field and Check, which asks that address whether a
    co-op game runs there and shows what the host answers. Connect stays
    disabled: joining over the network comes with co-op M1.

The host's answer (the engine side, below): single player runs with the
engine's networking off (net_enabled is 0 there), so a co-op host opens a
small UDP socket of its own on VSP_COOP_PORT (29760) while sp_coop is 1 and
answers one connectionless query, polled once a frame from Com_Frame:
  -> "\xff\xff\xff\xffgetcoopinfo [challenge]"
  <- "\xff\xff\xff\xffcoopInfoResponse\n" and an infostring with
     coopProtocol, version (the app's), hostname (the Mac's name), state
     (playing / menu), map, mission (the level's title), skill (g_spskill),
     players, maxPlayers, time (seconds of game time on the level) and the
     challenge (up to 32 letters, digits, - or _ of the query; nothing else of
     a query is ever sent back).
The game publishes mission, players and time in the cvars coop_mission,
coop_players and coop_time (efgame/src/coop/coop_info.cpp). Replies are rate
limited as the server's getinfo is: 10 a second per address, 10 per 100 ms
in all. Anything else that arrives on the port is ignored.

Test hooks (environment variables; the launcher's are in sys_osx_launcher.m):
  VOYAGERSP_TEST_COOP_SHEET=<file.png>  once the launcher is up, open the
                                Co-op… sheet, render it to the PNG and quit
  VOYAGERSP_TEST_COOP_QUERY=<address>   ...first type the address, press
                                Check and wait for the answer (logged)
  VOYAGERSP_TEST_COOP_HOLD=<seconds>    ...keep it open that long after the
                                snapshot (the window numbers are logged)
  VOYAGERSP_TEST_COOP_HOST=1    once the launcher is up, open the sheet and
                                press Host
===========================================================================
*/

#ifndef __APPLE__
#error This file is for macOS only.
#endif

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

#import <Cocoa/Cocoa.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include "sys_osx_launcher.h"
#include "sys_osx_coop.h"

#define COOP_QUERY		"getcoopinfo"
#define COOP_RESPONSE	"coopInfoResponse"
#define COOP_OOB		"\xff\xff\xff\xff"

#ifndef EF_APP_VERSION
#define EF_APP_VERSION	"unknown"
#endif

// sp/sp_bridge.cpp
extern int SP_IsActive( void );
extern const char *SP_CurMap( void );

/* ------------------------------------------------------------------------
   The host: answer co-op checks (engine side, game running)
   ------------------------------------------------------------------------ */

typedef struct {
	int		lastTime;
	int		burst;
} coopBucket_t;

typedef struct {
	in_addr_t		addr;
	coopBucket_t	bucket;
} coopAddrBucket_t;

static int				coopSocket = -1;
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

	if( expired > b->burst || interval < 0 )
	{
		b->burst = 0;
		b->lastTime = now;
	}
	else
	{
		b->burst -= expired;
		b->lastTime = now - interval % period;
	}
	if( b->burst < burst )
	{
		b->burst++;
		return qfalse;
	}
	return qtrue;
}

static qboolean CoopAddressLimited( in_addr_t addr )
{
	coopAddrBucket_t *slot = NULL, *oldest = &coopAddrs[0];

	for( int i = 0; i < (int)ARRAY_LEN( coopAddrs ); i++ )
	{
		if( coopAddrs[i].addr == addr )
		{
			slot = &coopAddrs[i];
			break;
		}
		if( coopAddrs[i].bucket.lastTime < oldest->bucket.lastTime )
			oldest = &coopAddrs[i];
	}
	if( !slot )
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

	for( ; *in && n < size - 1; in++ )
	{
		unsigned char c = (unsigned char)*in;
		out[n++] = ( c < ' ' || c == 0x7f || c == '\\' || c == ';' || c == '"' ) ? ' ' : (char)c;
	}
	out[n] = 0;
	if( n && ( (unsigned char)out[n - 1] & 0x80 ) )
	{
		unsigned char lead;
		int need;
		for( i = n - 1; i > 0 && ( (unsigned char)out[i] & 0xc0 ) == 0x80; i-- )
			;
		lead = (unsigned char)out[i];
		need = lead >= 0xf0 ? 4 : lead >= 0xe0 ? 3 : lead >= 0xc0 ? 2 : 1;
		if( n - i < need )
			out[i] = 0;
	}
}

static void CoopSetInfo( char *info, const char *key, const char *value )
{
	char clean[128];

	CoopCleanValue( value, clean, sizeof( clean ) );
	Info_SetValueForKey( info, key, clean );
}

static void CoopOpenSocket( void )
{
	struct sockaddr_in sa;
	int fd;

	if( coopRetryTime && Sys_Milliseconds() - coopRetryTime < 0 )
		return;
	fd = socket( AF_INET, SOCK_DGRAM, 0 );
	if( fd < 0 )
	{
		coopRetryTime = Sys_Milliseconds() + 5000;
		return;
	}
	memset( &sa, 0, sizeof( sa ) );
	sa.sin_family = AF_INET;
	sa.sin_port = htons( VSP_COOP_PORT );
	sa.sin_addr.s_addr = htonl( INADDR_ANY );
	if( bind( fd, (struct sockaddr *)&sa, sizeof( sa ) ) < 0 || fcntl( fd, F_SETFL, O_NONBLOCK ) < 0 )
	{
		if( !coopBindFailed )
			Com_Printf( "co-op: can't answer co-op checks, UDP port %d is in use (%s); trying again every 5 s\n",
				VSP_COOP_PORT, strerror( errno ) );
		coopBindFailed = qtrue;
		close( fd );
		coopRetryTime = Sys_Milliseconds() + 5000;
		return;
	}
	coopSocket = fd;
	coopRetryTime = 0;
	coopBindFailed = qfalse;
	@autoreleasepool {
		NSString *name = [[NSHost currentHost] localizedName];
		Q_strncpyz( coopHostName, [name length] ? [name UTF8String] : "Mac", sizeof( coopHostName ) );
	}
	Com_Printf( "co-op: answering co-op checks on UDP port %d\n", VSP_COOP_PORT );
}

static void CoopCloseSocket( void )
{
	if( coopSocket < 0 )
		return;
	close( coopSocket );
	coopSocket = -1;
	Com_Printf( "co-op: no longer answering co-op checks\n" );
}

static void CoopAnswer( const char *packet, int len, const struct sockaddr_in *from )
{
	char cmd[64], challenge[33], info[MAX_INFO_STRING], reply[MAX_INFO_STRING + 32];
	const char *p;
	int n = 0;
	qboolean playing;

	// "\xff\xff\xff\xffgetcoopinfo" and an optional challenge; anything else is dropped
	if( len < 4 + (int)strlen( COOP_QUERY ) || memcmp( packet, COOP_OOB, 4 ) )
		return;
	len = MIN( len - 4, (int)sizeof( cmd ) - 1 );
	memcpy( cmd, packet + 4, len );
	cmd[len] = 0;
	if( strncmp( cmd, COOP_QUERY, strlen( COOP_QUERY ) ) )
		return;
	p = cmd + strlen( COOP_QUERY );
	if( *p && *p != ' ' && *p != '\n' )
		return;
	while( *p == ' ' )
		p++;
	for( ; n < (int)sizeof( challenge ) - 1 && ( isalnum( (unsigned char)*p ) || *p == '-' || *p == '_' ); p++ )
		challenge[n++] = *p;
	challenge[n] = 0;

	if( CoopAddressLimited( from->sin_addr.s_addr ) || CoopRateLimited( &coopOutbound, 10, 100 ) )
		return;

	playing = SP_IsActive() && SP_CurMap()[0];
	info[0] = 0;
	CoopSetInfo( info, "coopProtocol", va( "%d", VSP_COOP_PROTOCOL ) );
	CoopSetInfo( info, "version", EF_APP_VERSION );
	CoopSetInfo( info, "hostname", coopHostName );
	CoopSetInfo( info, "state", playing ? "playing" : "menu" );
	if( playing )
	{
		CoopSetInfo( info, "map", SP_CurMap() );
		CoopSetInfo( info, "mission", Cvar_VariableString( "coop_mission" ) );
		CoopSetInfo( info, "time", va( "%d", Cvar_VariableIntegerValue( "coop_time" ) ) );
	}
	CoopSetInfo( info, "skill", va( "%d", Cvar_VariableIntegerValue( "g_spskill" ) ) );
	CoopSetInfo( info, "players", va( "%d", playing ? MAX( 1, Cvar_VariableIntegerValue( "coop_players" ) ) : 1 ) );
	CoopSetInfo( info, "maxPlayers", "2" );
	if( challenge[0] )
		CoopSetInfo( info, "challenge", challenge );

	n = Com_sprintf( reply, sizeof( reply ), COOP_OOB COOP_RESPONSE "\n%s", info );
	sendto( coopSocket, reply, n, 0, (const struct sockaddr *)from, sizeof( *from ) );
}

/*
=================
Sys_CoopFrame

Once a frame (Com_Frame, EF_COOP builds): while single player runs with
sp_coop 1, answer co-op checks on VSP_COOP_PORT. Does nothing otherwise.
=================
*/
void Sys_CoopFrame( void )
{
	char packet[256];

	if( Com_Holomatch() || Com_DedicatedServer() || Cvar_VariableIntegerValue( "sp_coop" ) != 1 )
	{
		CoopCloseSocket();
		return;
	}
	if( coopSocket < 0 )
		CoopOpenSocket();
	for( int i = 0; coopSocket >= 0 && i < 16; i++ )
	{
		struct sockaddr_in from;
		socklen_t fromLen = sizeof( from );
		ssize_t n = recvfrom( coopSocket, packet, sizeof( packet ), 0, (struct sockaddr *)&from, &fromLen );

		if( n <= 0 )
			break;
		if( fromLen == sizeof( from ) && from.sin_family == AF_INET )
			CoopAnswer( packet, (int)n, &from );
	}
}

/* ------------------------------------------------------------------------
   The guest: is there a co-op game at this address? (launcher)
   ------------------------------------------------------------------------ */

static NSString *CoopString( const char *bytes, size_t len )
{
	NSString *s = [[NSString alloc] initWithBytes: bytes length: len encoding: NSUTF8StringEncoding];
	return s ? s : [[NSString alloc] initWithBytes: bytes length: len encoding: NSISOLatin1StringEncoding];
}

// "\key\value\key\value" -> dictionary (nil when it isn't an infostring)
static NSDictionary *CoopParseInfo( const char *s, size_t len )
{
	NSMutableDictionary *d = [NSMutableDictionary dictionary];
	const char *end = s + len;

	if( !len || *s != '\\' )
		return nil;
	while( s < end && *s == '\\' )
	{
		const char *k = ++s, *v;
		while( s < end && *s != '\\' )
			s++;
		if( s >= end )
			break;
		v = ++s;
		while( s < end && *s != '\\' )
			s++;
		if( s - v >= 0 && v - 1 > k )
			d[CoopString( k, v - 1 - k )] = CoopString( v, s - v );
	}
	return d;
}

static NSDictionary *CoopResult( NSString *status, NSString *address, NSDictionary *info, NSString *error )
{
	NSMutableDictionary *r = [NSMutableDictionary dictionaryWithObject: status forKey: @"status"];
	if( address )
		r[@"address"] = address;
	if( info )
		r[@"info"] = info;
	if( error )
		r[@"error"] = error;
	return r;
}

NSDictionary *VSP_CoopQuery( NSString *address, double timeout )
{
	NSString *a = [address stringByTrimmingCharactersInSet: [NSCharacterSet whitespaceAndNewlineCharacterSet]];
	NSString *host = a, *shown;
	NSArray *parts = [a componentsSeparatedByString: @":"];
	struct addrinfo hints, *res = NULL;
	struct sockaddr_in to;
	char q[64], challenge[16], buf[1400];
	int port = VSP_COOP_PORT, fd, sends = 0, sendError = 0, ql;
	NSDate *deadline;

	if( ![a length] )
		return CoopResult( @"bad", nil, nil, @"Type the host’s address, such as 192.168.1.20." );
	if( [parts count] > 2 )
		return CoopResult( @"bad", nil, nil, @"IPv6 addresses aren’t supported yet. Use the host’s IPv4 address, such as 192.168.1.20." );
	if( [parts count] == 2 )
	{
		NSString *ps = parts[1];
		host = parts[0];
		if( ![ps length] || [ps length] > 5 ||
			[ps rangeOfCharacterFromSet: [[NSCharacterSet decimalDigitCharacterSet] invertedSet]].location != NSNotFound ||
			[ps intValue] < 1 || [ps intValue] > 65535 )
			return CoopResult( @"bad", nil, nil, [NSString stringWithFormat: @"“%@” isn’t a port. Leave it out for the "
				@"co-op port (%d), or give a number from 1 to 65535.", ps, VSP_COOP_PORT] );
		port = [ps intValue];
	}
	if( ![host length] )
		return CoopResult( @"bad", nil, nil, @"Type the host’s address before the port." );

	memset( &hints, 0, sizeof( hints ) );
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	if( getaddrinfo( [host UTF8String], NULL, &hints, &res ) != 0 || !res )
		return CoopResult( @"bad", nil, nil, [NSString stringWithFormat: @"“%@” couldn’t be found. Check the address.", host] );
	memcpy( &to, res->ai_addr, sizeof( to ) );
	freeaddrinfo( res );
	to.sin_port = htons( port );
	inet_ntop( AF_INET, &to.sin_addr, buf, sizeof( buf ) );
	shown = [NSString stringWithFormat: @"%s:%d", buf, port];

	if( ( fd = socket( AF_INET, SOCK_DGRAM, 0 ) ) < 0 )
		return CoopResult( @"bad", shown, nil, @"A network socket couldn’t be opened." );
	snprintf( challenge, sizeof( challenge ), "%08x", arc4random() );
	ql = snprintf( q, sizeof( q ), COOP_OOB COOP_QUERY " %s", challenge );

	// asked three times over the timeout, in case a packet goes missing
	deadline = [NSDate dateWithTimeIntervalSinceNow: timeout];
	for( ;; )
	{
		double left = [deadline timeIntervalSinceNow];
		struct pollfd pfd = { fd, POLLIN, 0 };
		struct sockaddr_in from;
		socklen_t fromLen = sizeof( from );
		ssize_t n;
		int wait;

		if( left <= 0 )
			break;
		if( sends < 3 && left <= timeout * ( 3 - sends ) / 3 + 0.001 )
		{
			if( sendto( fd, q, ql, 0, (struct sockaddr *)&to, sizeof( to ) ) < 0 )
				sendError = errno;
			sends++;
		}
		wait = (int)( MIN( left, timeout / 3 ) * 1000 ) + 1;
		if( poll( &pfd, 1, wait ) <= 0 )
			continue;
		n = recvfrom( fd, buf, sizeof( buf ) - 1, 0, (struct sockaddr *)&from, &fromLen );
		if( n <= 0 || from.sin_addr.s_addr != to.sin_addr.s_addr || from.sin_port != to.sin_port )
			continue;
		buf[n] = 0;
		{
			static const char prefix[] = COOP_OOB COOP_RESPONSE "\n";
			NSDictionary *info;
			NSString *version, *protocol;

			if( (size_t)n < sizeof( prefix ) - 1 || memcmp( buf, prefix, sizeof( prefix ) - 1 ) )
				continue;
			info = CoopParseInfo( buf + sizeof( prefix ) - 1, n - ( sizeof( prefix ) - 1 ) );
			if( !info || ( info[@"challenge"] && ![info[@"challenge"] isEqualToString: @( challenge )] ) )
				continue;
			close( fd );
			version = info[@"version"] ? info[@"version"] : @"?";
			protocol = info[@"coopProtocol"] ? info[@"coopProtocol"] : @"?";
			if( [protocol intValue] != VSP_COOP_PROTOCOL || ![version isEqualToString: @EF_APP_VERSION] )
				return CoopResult( @"mismatch", shown, info, [NSString stringWithFormat: @"The host runs Elite Force for Mac %@ "
					@"(co-op version %@), and this Mac runs %s (co-op version %d). Both need the same version to play together.",
					version, protocol, EF_APP_VERSION, VSP_COOP_PROTOCOL] );
			return CoopResult( @"found", shown, info, nil );
		}
	}
	close( fd );
	if( sendError )
		return CoopResult( @"none", shown, nil, [NSString stringWithFormat: @"macOS couldn’t send to it (%s). If you didn’t "
			@"allow Elite Force to find devices on your local network, allow it in System Settings > Privacy & Security > "
			@"Local Network.", strerror( sendError )] );
	return CoopResult( @"none", shown, nil, nil );
}

/* ------------------------------------------------------------------------
   This Mac's addresses, for the Host side
   ------------------------------------------------------------------------ */

// Every active IPv4 address that isn't loopback or link-local, en0 first:
// @[ @{ address, interface } ]
static NSArray<NSDictionary *> *CoopLANAddresses( void )
{
	struct ifaddrs *list, *ifa;
	NSMutableArray *a = [NSMutableArray array];

	if( getifaddrs( &list ) != 0 )
		return a;
	for( ifa = list; ifa; ifa = ifa->ifa_next )
	{
		char buf[INET_ADDRSTRLEN];
		const struct sockaddr_in *sin = (const struct sockaddr_in *)ifa->ifa_addr;
		NSDictionary *d;

		if( !sin || sin->sin_family != AF_INET || !( ifa->ifa_flags & IFF_UP ) || !( ifa->ifa_flags & IFF_RUNNING ) ||
			( ifa->ifa_flags & IFF_LOOPBACK ) )
			continue;
		if( ( ntohl( sin->sin_addr.s_addr ) & 0xffff0000 ) == 0xa9fe0000 )	// 169.254/16
			continue;
		inet_ntop( AF_INET, &sin->sin_addr, buf, sizeof( buf ) );
		d = @{ @"address": @( buf ), @"interface": @( ifa->ifa_name ) };
		if( !strcmp( ifa->ifa_name, "en0" ) )
			[a insertObject: d atIndex: 0];
		else
			[a addObject: d];
	}
	freeifaddrs( list );
	return a;
}

/* ------------------------------------------------------------------------
   The Co-op… sheet
   ------------------------------------------------------------------------ */

// what the coop file uses of VSPLauncher (sys_osx_launcher.m)
@protocol VSPCoopLauncher <NSObject>
- (NSWindow *)window;
- (void)selectMode: (int)mode;		// launchMode_t; 0 is Single Player
- (void)play: (id)sender;
- (BOOL)canPlay;
@end

static BOOL coopHostChosen;

@interface VSPCoopController : NSObject
@property (nonatomic, strong) NSWindow *panel;
@property (nonatomic, weak) id<VSPCoopLauncher> launcher;
- (void)check: (id)sender;
- (void)host: (id)sender;
- (void)cancel: (id)sender;
@property (nonatomic, readonly) NSTextField *address;
@property (nonatomic, copy) void ( ^checked )( NSDictionary *result );	// tests
@end

static NSStackView *CoopStack( NSArray<NSView *> *views, NSUserInterfaceLayoutOrientation o, CGFloat spacing )
{
	NSStackView *s = [NSStackView stackViewWithViews: views];
	[s setOrientation: o];
	[s setSpacing: spacing];
	[s setAlignment: o == NSUserInterfaceLayoutOrientationVertical ? NSLayoutAttributeLeading : NSLayoutAttributeCenterY];
	return s;
}

static NSString *CoopSkillName( NSString *skill )
{
	switch( [skill intValue] )
	{
	case 0:		return @"Easy";
	case 1:		return @"Medium";
	case 2:		return @"Hard";
	default:	return [NSString stringWithFormat: @"Skill %@", skill];
	}
}

@implementation VSPCoopController
{
	NSTextField *resultTitle, *resultText;
	NSImageView *resultIcon;
	NSProgressIndicator *spinner;
	NSButton *checkButton, *hostButton;
	NSView *resultRow;
	BOOL checking;
}

- (instancetype)initWithLauncher: (id)launcher
{
	if( !( self = [super init] ) )
		return nil;
	_launcher = launcher;
	[self build];
	return self;
}

- (NSView *)hostGroup
{
	NSArray<NSDictionary *> *addrs = CoopLANAddresses();
	NSMutableArray<NSString *> *lines = [NSMutableArray array];
	NSTextField *list, *intro, *note;
	NSStackView *row, *col;
	BOOL canPlay = [_launcher respondsToSelector: @selector(canPlay)] && [_launcher canPlay];

	for( NSDictionary *d in addrs )
		[lines addObject: [NSString stringWithFormat: @"%@    (%@)", d[@"address"], d[@"interface"]]];
	intro = VSP_WrapLabel( @"Starts single player with co-op on: choose New Game or Load as usual. Your friend "
		@"then joins as player 2 with this Mac’s address:", [NSFont systemFontOfSize: 12], nil );
	if( [lines count] )
	{
		list = VSP_Label( [lines componentsJoinedByString: @"\n"],
			[NSFont monospacedSystemFontOfSize: 13 weight: NSFontWeightMedium], nil );
		[list setSelectable: YES];
	}
	else
		list = VSP_Label( @"No network found. Connect this Mac to Wi-Fi or Ethernet first.",
			[NSFont systemFontOfSize: 12], [NSColor systemOrangeColor] );
	note = VSP_WrapLabel( [NSString stringWithFormat: @"Co-op uses UDP port %d. macOS may ask whether Elite Force can "
		@"find devices on your local network: allow it. Until the next co-op update, player 2 is a stand-in on this Mac "
		@"that follows you.", VSP_COOP_PORT], [NSFont systemFontOfSize: 11], [NSColor secondaryLabelColor] );
	for( NSTextField *t in @[ intro, note ] )
		[t setPreferredMaxLayoutWidth: 400];
	hostButton = [NSButton buttonWithTitle: @"Host" target: self action: @selector(host:)];
	[hostButton setEnabled: canPlay];
	[hostButton setToolTip: canPlay ? @"Play single player with co-op on" : @"Add the game files first"];
	[[hostButton widthAnchor] constraintGreaterThanOrEqualToConstant: 80].active = YES;
	col = CoopStack( @[ intro, list, note ], NSUserInterfaceLayoutOrientationVertical, 8 );
	row = CoopStack( @[ col, hostButton ], NSUserInterfaceLayoutOrientationHorizontal, 16 );
	[row setAlignment: NSLayoutAttributeTop];
	[hostButton setContentHuggingPriority: NSLayoutPriorityRequired forOrientation: NSLayoutConstraintOrientationHorizontal];
	return VSP_Group( @"HOST", row );
}

- (NSView *)joinGroup
{
	NSTextField *label = VSP_Label( @"Host’s address", [NSFont systemFontOfSize: 12], nil );
	NSButton *connect = [NSButton buttonWithTitle: @"Connect" target: nil action: nil];
	NSTextField *later = VSP_WrapLabel( @"Joining a co-op game comes with the next co-op update (M1)",
		[NSFont systemFontOfSize: 11], [NSColor secondaryLabelColor] );
	NSStackView *line, *result, *texts, *bottom, *col;

	_address = [NSTextField textFieldWithString: @""];
	[_address setPlaceholderString: [NSString stringWithFormat: @"192.168.1.20, or address:port (default %d)", VSP_COOP_PORT]];
	[_address setTarget: self];
	[_address setAction: @selector(check:)];
	[[_address widthAnchor] constraintGreaterThanOrEqualToConstant: 260].active = YES;
	checkButton = [NSButton buttonWithTitle: @"Check" target: self action: @selector(check:)];
	[checkButton setToolTip: @"Ask that address whether a co-op game runs there"];
	[[checkButton widthAnchor] constraintGreaterThanOrEqualToConstant: 80].active = YES;
	line = CoopStack( @[ label, _address, checkButton ], NSUserInterfaceLayoutOrientationHorizontal, 8 );

	resultIcon = [NSImageView imageViewWithImage: [NSImage imageWithSystemSymbolName: @"questionmark.circle"
		accessibilityDescription: nil]];
	[resultIcon setSymbolConfiguration: [NSImageSymbolConfiguration configurationWithPointSize: 20 weight: NSFontWeightRegular]];
	[[resultIcon widthAnchor] constraintEqualToConstant: 26].active = YES;
	spinner = [[NSProgressIndicator alloc] init];
	[spinner setStyle: NSProgressIndicatorStyleSpinning];
	[spinner setControlSize: NSControlSizeSmall];
	[spinner setHidden: YES];
	resultTitle = VSP_WrapLabel( @"", [NSFont systemFontOfSize: 13 weight: NSFontWeightSemibold], nil );
	[resultTitle setPreferredMaxLayoutWidth: 440];
	resultText = VSP_WrapLabel( @"", [NSFont systemFontOfSize: 12], [NSColor secondaryLabelColor] );
	[resultText setPreferredMaxLayoutWidth: 440];
	[resultText setSelectable: YES];
	texts = CoopStack( @[ resultTitle, resultText ], NSUserInterfaceLayoutOrientationVertical, 3 );
	result = CoopStack( @[ resultIcon, spinner, texts ], NSUserInterfaceLayoutOrientationHorizontal, 8 );
	[result setAlignment: NSLayoutAttributeTop];
	resultRow = result;
	[resultRow setHidden: YES];

	[connect setEnabled: NO];
	[connect setToolTip: @"Joining a co-op game comes with the next co-op update (M1)"];
	bottom = CoopStack( @[ connect, later ], NSUserInterfaceLayoutOrientationHorizontal, 10 );
	col = CoopStack( @[ line, result, bottom ], NSUserInterfaceLayoutOrientationVertical, 12 );
	return VSP_Group( @"JOIN", col );
}

- (void)build
{
	NSView *content;
	NSStackView *root, *footer, *head;
	NSButton *cancel;
	NSTextField *badge;

	_panel = [[NSWindow alloc] initWithContentRect: NSMakeRect( 0, 0, 560, 420 )
		styleMask: NSWindowStyleMaskTitled backing: NSBackingStoreBuffered defer: NO];
	[_panel setTitle: @"Co-op"];
	VSP_ApplyTestAppearance( _panel );
	content = [_panel contentView];

	badge = VSP_Label( @"EXPERIMENTAL", [NSFont systemFontOfSize: 10 weight: NSFontWeightSemibold], [NSColor systemOrangeColor] );
	head = CoopStack( @[ VSP_Label( @"Co-op", [NSFont systemFontOfSize: 17 weight: NSFontWeightBold], nil ), badge ],
		NSUserInterfaceLayoutOrientationHorizontal, 8 );
	[head setAlignment: NSLayoutAttributeFirstBaseline];
	cancel = [NSButton buttonWithTitle: @"Cancel" target: self action: @selector(cancel:)];
	[cancel setKeyEquivalent: @"\033"];
	[[cancel widthAnchor] constraintGreaterThanOrEqualToConstant: 80].active = YES;
	footer = CoopStack( @[ [NSView new], cancel ], NSUserInterfaceLayoutOrientationHorizontal, 10 );
	[[footer views][0] setContentHuggingPriority: 1 forOrientation: NSLayoutConstraintOrientationHorizontal];

	root = CoopStack( @[ head,
		VSP_WrapLabel( @"Play the campaign with a friend. The host is player 1 and plays the story; the friend joins as player 2.",
			[NSFont systemFontOfSize: 12], [NSColor secondaryLabelColor] ),
		[self hostGroup], [self joinGroup], footer ], NSUserInterfaceLayoutOrientationVertical, 12 );
	[(NSTextField *)[root views][1] setPreferredMaxLayoutWidth: 520];
	[root setCustomSpacing: 4 afterView: head];
	[root setCustomSpacing: 18 afterView: [root views][3]];
	[root setEdgeInsets: NSEdgeInsetsMake( 18, 20, 20, 20 )];
	[root setTranslatesAutoresizingMaskIntoConstraints: NO];
	[content addSubview: root];
	for( NSView *v in @[ [root views][2], [root views][3], footer ] )
		[[v widthAnchor] constraintEqualToAnchor: [root widthAnchor] constant: -40].active = YES;
	[NSLayoutConstraint activateConstraints: @[
		[[root leadingAnchor] constraintEqualToAnchor: [content leadingAnchor]],
		[[root trailingAnchor] constraintEqualToAnchor: [content trailingAnchor]],
		[[root topAnchor] constraintEqualToAnchor: [content topAnchor]],
		[[root bottomAnchor] constraintEqualToAnchor: [content bottomAnchor]],
		[[root widthAnchor] constraintEqualToConstant: 560],
	]];
	[_panel setInitialFirstResponder: _address];
}

- (void)showIcon: (NSString *)symbol color: (NSColor *)color title: (NSString *)title text: (NSString *)text
{
	[resultRow setHidden: NO];
	[spinner stopAnimation: nil];
	[spinner setHidden: symbol != nil];		// no icon: checking
	[resultIcon setHidden: symbol == nil];
	if( symbol )
	{
		[resultIcon setImage: [NSImage imageWithSystemSymbolName: symbol accessibilityDescription: nil]];
		[resultIcon setContentTintColor: color];
	}
	[resultTitle setStringValue: title];
	[resultText setStringValue: text ? text : @""];
	[resultText setHidden: ![text length]];
}

// what the host told us, as lines for the result
static NSString *CoopDescribe( NSDictionary *info )
{
	NSMutableArray *lines = [NSMutableArray array];
	NSString *mission = info[@"mission"], *map = info[@"map"];
	int players = [info[@"players"] intValue], maxPlayers = [info[@"maxPlayers"] intValue];

	if( ![info[@"state"] isEqualToString: @"playing"] || ![map length] )
		[lines addObject: @"The host is in the menus: no level is loaded yet."];
	else if( [mission length] )
		[lines addObject: [NSString stringWithFormat: @"Mission: “%@” (map %@)", mission, map]];
	else
		[lines addObject: [NSString stringWithFormat: @"Map: %@", map]];
	{
		NSMutableString *s = [NSMutableString stringWithFormat: @"Difficulty: %@", CoopSkillName( info[@"skill"] )];
		if( info[@"time"] )
		{
			int t = [info[@"time"] intValue];
			[s appendString: t < 60 ? @" · on this level for under a minute" :
				[NSString stringWithFormat: @" · on this level for %d min", t / 60]];
		}
		[lines addObject: s];
	}
	[lines addObject: [NSString stringWithFormat: @"Players: %d of %d%@", MAX( players, 1 ), maxPlayers > 0 ? maxPlayers : 2,
		players >= 2 ? @" (the host, and player 2 as a stand-in on the host’s Mac)" : @" (the host)"]];
	[lines addObject: [NSString stringWithFormat: @"Host’s app: Elite Force for Mac %@ · co-op version %@",
		info[@"version"] ? info[@"version"] : @"?", info[@"coopProtocol"] ? info[@"coopProtocol"] : @"?"]];
	return [lines componentsJoinedByString: @"\n"];
}

- (void)showResult: (NSDictionary *)r
{
	NSString *status = r[@"status"], *where = r[@"address"] ? r[@"address"] : @"";
	NSDictionary *info = r[@"info"];
	NSString *host = [info[@"hostname"] length] ? [NSString stringWithFormat: @" on “%@”", info[@"hostname"]] : @"";

	if( [status isEqualToString: @"found"] )
		[self showIcon: @"checkmark.circle.fill" color: [NSColor systemGreenColor]
			title: [NSString stringWithFormat: @"Co-op game found%@ (%@)", host, where] text: CoopDescribe( info )];
	else if( [status isEqualToString: @"mismatch"] )
		[self showIcon: @"exclamationmark.triangle.fill" color: [NSColor systemOrangeColor]
			title: [NSString stringWithFormat: @"Version mismatch: the co-op game%@ (%@) can’t be joined from this Mac", host, where]
			text: [NSString stringWithFormat: @"%@\n\n%@", r[@"error"], CoopDescribe( info )]];
	else if( [status isEqualToString: @"none"] )
		[self showIcon: @"xmark.circle.fill" color: [NSColor secondaryLabelColor]
			title: [NSString stringWithFormat: @"No co-op game found at %@", where]
			text: r[@"error"] ? r[@"error"] : @"Nothing answered within 2 seconds. Check the address, that the host pressed "
				@"Host and is running co-op, and that both Macs are on the same network."];
	else
		[self showIcon: @"exclamationmark.circle.fill" color: [NSColor systemRedColor]
			title: @"This address can’t be used" text: r[@"error"]];
}

- (void)check: (id)sender
{
	NSString *a = [_address stringValue];

	if( checking )
		return;
	checking = YES;
	[checkButton setEnabled: NO];
	[self showIcon: nil color: nil title: [NSString stringWithFormat: @"Checking %@…",
		[a length] ? a : @"the address"] text: nil];
	[spinner startAnimation: nil];
	dispatch_async( dispatch_get_global_queue( QOS_CLASS_USER_INITIATED, 0 ), ^{
		NSDictionary *r = VSP_CoopQuery( a, 2.0 );
		dispatch_async( dispatch_get_main_queue(), ^{
			self->checking = NO;
			[self->checkButton setEnabled: YES];
			LLog( @"Co-op: check %@ -> %@", a, r );
			[self showResult: r];
			if( self.checked )
				self.checked( r );
		} );
	} );
}

- (void)host: (id)sender
{
	id<VSPCoopLauncher> l = _launcher;

	if( ![l canPlay] )
		return;
	[[_panel sheetParent] endSheet: _panel returnCode: NSModalResponseOK];
	coopHostChosen = YES;
	LLog( @"Co-op: hosting (single player with sp_coop 1)" );
	[l selectMode: 0];
	[l play: nil];
}

- (void)cancel: (id)sender
{
	[[_panel sheetParent] endSheet: _panel returnCode: NSModalResponseCancel];
}

@end

/* ------------------------------------------------------------------------
   The launcher's hooks
   ------------------------------------------------------------------------ */

@interface VSPCoopButtonTarget : NSObject
@property (nonatomic, weak) id<VSPCoopLauncher> launcher;
- (VSPCoopController *)open;
@end

@implementation VSPCoopButtonTarget

- (VSPCoopController *)open
{
	NSWindow *parent = [_launcher window];
	VSPCoopController *c;

	if( [parent attachedSheet] )
		return nil;
	c = [[VSPCoopController alloc] initWithLauncher: _launcher];
	// the block keeps the controller alive until the sheet ends
	[parent beginSheet: [c panel] completionHandler: ^( NSModalResponse r ) {
		[[c panel] orderOut: nil];
	}];
	return c;
}

- (void)openCoop: (id)sender	{ [self open]; }

// VOYAGERSP_TEST_COOP_*: see the top of this file
- (void)runTests
{
	const char *sheet = getenv( "VOYAGERSP_TEST_COOP_SHEET" ), *query = getenv( "VOYAGERSP_TEST_COOP_QUERY" );
	VSPCoopController *c = [self open];
	void ( ^finish )( void );

	if( !c )
		return;
	if( getenv( "VOYAGERSP_TEST_COOP_HOST" ) )
	{
		LLog( @"[test] co-op: pressing Host" );
		[c host: nil];
		return;
	}
	finish = ^{
		NSWindow *p = [c panel];
		double hold = getenv( "VOYAGERSP_TEST_COOP_HOLD" ) ? atof( getenv( "VOYAGERSP_TEST_COOP_HOLD" ) ) : 0;
		if( sheet )
			VSP_SnapshotWindow( p, sheet );
		LLog( @"[test] co-op sheet: window %ld, launcher window %ld, holding %.0f s", (long)[p windowNumber],
			(long)[[self->_launcher window] windowNumber], hold );
		NSTimer *t = [NSTimer timerWithTimeInterval: hold repeats: NO block: ^( NSTimer *timer ) {
			[c cancel: nil];
			[NSApp stopModalWithCode: NSModalResponseCancel];
		}];
		[[NSRunLoop currentRunLoop] addTimer: t forMode: NSModalPanelRunLoopMode];
	};
	if( query )
	{
		c.checked = ^( NSDictionary *r ) {
			LLog( @"[test] co-op check %s: %@", query, r[@"status"] );
			finish();
		};
		[[c address] setStringValue: @( query )];
		[c check: nil];
	}
	else
		finish();
}

@end

NSButton *VSP_CoopButton( id launcher )
{
	static VSPCoopButtonTarget *target;		// lives as long as the launcher's button
	NSButton *b;

	target = [[VSPCoopButtonTarget alloc] init];
	target.launcher = launcher;
	b = [NSButton buttonWithTitle: @"Co-op…" target: target action: @selector(openCoop:)];
	[b setControlSize: NSControlSizeSmall];
	[b setFont: [NSFont systemFontOfSize: [NSFont smallSystemFontSize]]];
	[b setToolTip: @"Play the campaign with a friend: host a co-op game, or check for one to join (experimental)"];

	if( getenv( "VOYAGERSP_TEST_COOP_SHEET" ) || getenv( "VOYAGERSP_TEST_COOP_HOST" ) ||
		getenv( "VOYAGERSP_TEST_COOP_QUERY" ) )
	{
		VSPCoopButtonTarget *t = target;
		NSTimer *timer = [NSTimer timerWithTimeInterval: 1 repeats: NO block: ^( NSTimer *tm ) { [t runTests]; }];
		[[NSRunLoop currentRunLoop] addTimer: timer forMode: NSModalPanelRunLoopMode];
	}
	return b;
}

void VSP_CoopAppendArgs( NSMutableString *args, BOOL holomatch )
{
	if( coopHostChosen && !holomatch )
		[args appendString: @"+set sp_coop 1 "];
}
