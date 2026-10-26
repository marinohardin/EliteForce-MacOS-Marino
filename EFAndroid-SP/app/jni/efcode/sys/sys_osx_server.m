/*
===========================================================================
macOS app bundle: Host a Server, the launcher's third choice.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
It is compiled with ARC (see the macOS CMakeLists.txt).

  - VSPServerSettings: what the user sets before starting a server,
    remembered in the app's defaults (key ServerSettings).
  - The map catalog: the Holomatch maps in the game files, read from the
    pk3 central directories without the engine (the .bsp files in maps/,
    the arena info in scripts/arenas.txt and the .arena files there, and
    the levelshots/ .jpg and .tga files, stored or deflated, inflated
    with the engine's own zlib).
  - The Config… sheet (Maps, Game, Server tabs).
  - The master servers and the launcher's Servers… sheet: the list in the
    app's defaults (key MasterServers), passed to the game as
    baseEF/vsp_masters.cfg (+exec) and to a server in vsp_server.cfg.
  - The server window: runs the app's own executable as a dedicated server
    child process, with the settings in baseEF/vsp_server.cfg (+exec), talks
    to its console over stdin/stdout, polls "status" for the map and
    players, and offers Play on This Mac (a second instance of the app as
    a Holomatch client connecting to 127.0.0.1).

The child is started as
	<app>/Contents/MacOS/<exe> +set com_holomatch 1 +set dedicated 1|2
		+set net_port <port> [+set fs_basepath <folder>] +exec vsp_server.cfg
and runs headless: console commands are read from stdin, output is plain
text on stdout, and after every map load it prints
	Server ready: map <map> port <bound port> gametype <n> maxclients <n>
		dedicated <1|2> hostname <name>
("quit" or SIGTERM exit 0; no game data exits 1, a bad fs_basepath 3). The
bound port can be up to 9 above net_port when that is taken. A "status"
answer with a map also counts as ready. Holomatch's LAN search only probes
ports 27960-27963: other ports work, but only by address.

Retail rules (from the Holomatch ui.qvm's Create Server menu):
  g_gametype 0 Free For All, 1 Tournament, 3 Team Holomatch, 4 Capture the
  Flag. Assimilation needs Team Holomatch; Elimination is not for CTF;
  Action Hero only for Free For All and Tournament. Assimilation excludes
  Disintegration, Elimination and Action Hero; Specialties exclude
  Disintegration and Action Hero; Disintegration excludes Specialties and
  Action Hero; Elimination excludes Action Hero. Bots: bot_minplayers fills
  the game with bots up to that many players (per team in team games),
  with skill g_spSkill 1-5.

Test hooks (environment variables, see also sys_osx_launcher.m):
  VOYAGERSP_TEST_SERVER_CONFIG_SNAPSHOT=<file.png>  render the Config… sheet
  VOYAGERSP_TEST_SERVER_CONFIG_TAB=maps|game|server  its tab
  VOYAGERSP_TEST_SERVER_START=1           press Start Server (Host mode)
                                         without showing the launcher; the
                                         server window is put off screen
  VOYAGERSP_TEST_SERVER_PORT=<port>       use this port
  VOYAGERSP_TEST_SERVER_SNAPSHOT=<file.png>  render the server window ...
  VOYAGERSP_TEST_SERVER_SNAPSHOT_AFTER=<s>   ... this long after the start
                                         (default 15)
  VOYAGERSP_TEST_SERVER_STOP_AFTER=<s>    press Stop Server
  VOYAGERSP_TEST_SERVER_COMMAND=<cmd>     send it once the server is ready
  VOYAGERSP_TEST_SERVER_PLAY_AFTER=<s>    press Play on This Mac
  VOYAGERSP_TEST_SERVER_INTERNET=1        an internet server opening its port
  VOYAGERSP_TEST_SERVER_NOMASTER=1        ... without listing it on the masters
  VOYAGERSP_TEST_NAT=<ipv4>|fail          fake the router's answer
  VOYAGERSP_TEST_MASTERS=a,b,...         use these master servers (saved, as if set in the sheet)
  VOYAGERSP_TEST_MASTERS_SNAPSHOT=<file.png>  render the Servers… sheet once each master answered
  VOYAGERSP_TEST_MASTERS_ACTIONS=a,b,...  ... after these: edit<row>, type:<text>, drag<row>to<row>, add, end
  VOYAGERSP_TEST_LIST_FAST=1              check the internet list every 2 s, twice
  VOYAGERSP_TEST_SERVER_ACTIONS=a,b,...   press controls 4 s apart once ready: next, restart,
                                         slot<N>, map:<name>, addbot, removebots, kick<row>
With VOYAGERSP_TEST_SERVER_START the server's output is echoed to stdout as
"[server] ..." and each status poll is logged as "[test] status: ...".
===========================================================================
*/

#ifndef __APPLE__
#error This file is for macOS only.
#endif

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "../zlib/zlib.h"

#import <Cocoa/Cocoa.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <dns_sd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include "sys_osx_launcher.h"

#define KEY_SERVERSETTINGS	@"ServerSettings"
#define SERVER_CFG			@"vsp_server.cfg"
#define DEFAULT_PORT		27960
// Holomatch's LAN search (localservers) only probes these
#define LAN_PORT_FIRST		27960
#define LAN_PORT_LAST		27963

static BOOL LANDiscoverable( NSInteger port )	{ return port >= LAN_PORT_FIRST && port <= LAN_PORT_LAST; }

static BOOL TestEnvSet( const char *name )
{
	const char *v = getenv( name );
	return v && *v && strcmp( v, "0" );
}

/* ------------------------------------------------------------------------
   Settings
   ------------------------------------------------------------------------ */

// A value that survives the console and the cfg file's quotes.
static NSString *CleanCvarText( NSString *text, NSUInteger maxLen )
{
	NSMutableString *s = [NSMutableString string];
	NSUInteger i;

	for( i = 0; i < [text length] && [s length] < maxLen; i++ )
	{
		unichar c = [text characterAtIndex: i];
		if( c < 32 || c > 126 || c == '"' || c == ';' || c == '\\' || c == '%' )
			continue;
		[s appendFormat: @"%C", c];
	}
	return [s stringByTrimmingCharactersInSet: [NSCharacterSet whitespaceCharacterSet]];
}

static NSString *DefaultHostname( void )
{
	NSString *first = [[NSFullUserName() componentsSeparatedByString: @" "] firstObject];
	first = CleanCvarText( first, 24 );
	return [first length] ? [NSString stringWithFormat: @"%@'s Server", first] : @"Elite Force Server";
}

static NSArray *Modifiers( void )
{
	// in the order of the retail menu's rules
	return @[ @"assimilation", @"specialties", @"disintegration", @"elimination", @"actionHero" ];
}

@implementation VSPServerSettings

- (instancetype)init
{
	if( !( self = [super init] ) )
		return nil;
	_hostname = DefaultHostname();
	_password = @"";
	_gametype = VSP_GT_FFA;
	_fraglimit = 20;
	_capturelimit = 8;
	_timelimit = 15;
	_maxPlayers = 12;
	_bots = 0;
	_botSkill = 3;
	_allowDownload = YES;
	_pure = YES;
	_openPort = YES;
	_port = DEFAULT_PORT;
	_rotation = @[ @"hm_voy1", @"hm_borg1", @"hm_kln1", @"hm_dn1", @"hm_for1", @"hm_cam" ];
	return self;
}

- (id)copyWithZone: (NSZone *)zone
{
	VSPServerSettings *c = [[VSPServerSettings alloc] init];
	[c setValuesForKeysWithDictionary: [self dictionary]];
	return c;
}

+ (NSArray *)keys
{
	return @[ @"hostname", @"password", @"gametype", @"assimilation", @"disintegration", @"actionHero",
		@"specialties", @"elimination", @"fraglimit", @"capturelimit", @"timelimit", @"maxPlayers", @"bots",
		@"botSkill", @"friendlyFire", @"internet", @"openPort", @"allowDownload", @"pure", @"port", @"rotation" ];
}

- (NSDictionary *)dictionary
{
	return [self dictionaryWithValuesForKeys: [VSPServerSettings keys]];
}

- (void)setValue: (id)value forUndefinedKey: (NSString *)key	{ }

+ (instancetype)load
{
	VSPServerSettings *s = [[VSPServerSettings alloc] init];
	NSDictionary *d = [[NSUserDefaults standardUserDefaults] dictionaryForKey: KEY_SERVERSETTINGS];

	for( NSString *k in [VSPServerSettings keys] )
	{
		id v = d[k], cur = [s valueForKey: k];
		// only values of the right kind
		if( v && [v isKindOfClass: [cur isKindOfClass: [NSString class]] ? [NSString class] :
			[cur isKindOfClass: [NSArray class]] ? [NSArray class] : [NSNumber class]] )
			[s setValue: v forKey: k];
	}
	if( s.gametype != VSP_GT_FFA && s.gametype != VSP_GT_TOURNAMENT && s.gametype != VSP_GT_TEAM && s.gametype != VSP_GT_CTF )
		s.gametype = VSP_GT_FFA;
	s.botSkill = MIN( MAX( s.botSkill, 1 ), 5 );
	s.maxPlayers = MIN( MAX( s.maxPlayers, 2 ), 32 );
	s.bots = MIN( MAX( s.bots, 0 ), 31 );
	if( s.port < 1024 || s.port > 65535 )
		s.port = DEFAULT_PORT;
	if( ![s.hostname length] )
		s.hostname = DefaultHostname();
	[s applyModifierRulesFavoring: nil];
	return s;
}

- (void)save
{
	[[NSUserDefaults standardUserDefaults] setObject: [self dictionary] forKey: KEY_SERVERSETTINGS];
	[[NSUserDefaults standardUserDefaults] synchronize];
}

- (void)setHostname: (NSString *)hostname	{ _hostname = [CleanCvarText( hostname, 48 ) copy]; }
- (void)setPassword: (NSString *)password	{ _password = [CleanCvarText( password, 32 ) copy]; }

- (void)applyModifierRulesFavoring: (NSString *)key
{
	static NSDictionary *excludes;
	NSMutableArray *order = [NSMutableArray arrayWithArray: Modifiers()];

	if( !excludes )
		excludes = @{
			@"assimilation":	@[ @"disintegration", @"elimination", @"actionHero" ],
			@"specialties":		@[ @"disintegration", @"actionHero" ],
			@"disintegration":	@[ @"assimilation", @"actionHero", @"specialties" ],
			@"elimination":		@[ @"assimilation", @"actionHero" ],
			@"actionHero":		@[ @"assimilation", @"specialties", @"elimination", @"disintegration" ],
		};
	if( _gametype != VSP_GT_TEAM )
		_assimilation = NO;
	if( _gametype >= VSP_GT_CTF )
		_elimination = NO;
	if( _gametype >= VSP_GT_TEAM )
		_actionHero = NO;
	if( key && [order containsObject: key] )
	{
		[order removeObject: key];
		[order insertObject: key atIndex: 0];
	}
	for( NSString *m in order )
		if( [[self valueForKey: m] boolValue] )
			for( NSString *x in excludes[m] )
				[self setValue: @NO forKey: x];
}

- (NSArray<NSString *> *)modifierNames
{
	NSMutableArray *a = [NSMutableArray array];
	NSDictionary *names = @{ @"assimilation": @"Assimilation", @"specialties": @"Specialties",
		@"disintegration": @"Disintegration", @"elimination": @"Elimination", @"actionHero": @"Action Hero" };

	for( NSString *m in Modifiers() )
		if( [[self valueForKey: m] boolValue] )
			[a addObject: names[m]];
	return a;
}

+ (NSString *)gametypeName: (NSInteger)gametype
{
	switch( gametype )
	{
	case VSP_GT_TOURNAMENT:	return @"Tournament";
	case VSP_GT_TEAM:		return @"Team Holomatch";
	case VSP_GT_CTF:		return @"Capture the Flag";
	default:				return @"Free For All";
	}
}

+ (NSString *)skillName: (NSInteger)skill
{
	NSArray *n = @[ @"Cadet", @"Ensign", @"Lieutenant", @"Commander", @"Captain" ];
	return n[MIN( MAX( skill, 1 ), 5 ) - 1];
}

@end

/* ------------------------------------------------------------------------
   Reading pk3 files
   ------------------------------------------------------------------------ */

@interface VSPZipEntry : NSObject
{
@public
	unsigned method, csize, usize, offset;
}
@property (nonatomic, copy) NSString *name;
@end
@implementation VSPZipEntry
@end

static unsigned LE16( const unsigned char *p )	{ return p[0] | p[1] << 8; }
static unsigned LE32( const unsigned char *p )	{ return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }

// The entries of the zip at path that want() accepts (it gets the name
// lowercased). Only the central directory is read.
static NSArray<VSPZipEntry *> *ZipList( NSString *path, BOOL ( ^want )( NSString *lower ) )
{
	NSMutableArray *out = [NSMutableArray array];
	NSFileHandle *fh = [NSFileHandle fileHandleForReadingAtPath: path];
	unsigned long long size, tailStart;
	NSData *tail, *cd;
	const unsigned char *t, *p, *end;
	unsigned cdSize, cdOffset;
	long i;

	if( !fh )
		return out;
	size = [fh seekToEndOfFile];
	if( size < 22 )
		return out;
	tailStart = size > 65536 + 22 ? size - ( 65536 + 22 ) : 0;
	[fh seekToFileOffset: tailStart];
	tail = [fh readDataOfLength: (NSUInteger)( size - tailStart )];
	t = [tail bytes];
	for( i = (long)[tail length] - 22; i >= 0; i-- )
		if( t[i] == 'P' && t[i+1] == 'K' && t[i+2] == 5 && t[i+3] == 6 )
			break;
	if( i < 0 )
		return out;
	cdSize = LE32( t + i + 12 );
	cdOffset = LE32( t + i + 16 );
	if( (unsigned long long)cdOffset + cdSize > size )
		return out;
	[fh seekToFileOffset: cdOffset];
	cd = [fh readDataOfLength: cdSize];
	[fh closeFile];
	p = [cd bytes];
	end = p + [cd length];
	while( p + 46 <= end && p[0] == 'P' && p[1] == 'K' && p[2] == 1 && p[3] == 2 )
	{
		unsigned n = LE16( p + 28 ), x = LE16( p + 30 ), c = LE16( p + 32 );
		NSString *name;

		if( p + 46 + n > end )
			break;
		name = [[NSString alloc] initWithBytes: p + 46 length: n encoding: NSISOLatin1StringEncoding];
		if( name && want( [name lowercaseString] ) )
		{
			VSPZipEntry *e = [[VSPZipEntry alloc] init];
			e.name = name;
			e->method = LE16( p + 10 );
			e->csize = LE32( p + 20 );
			e->usize = LE32( p + 24 );
			e->offset = LE32( p + 42 );
			[out addObject: e];
		}
		p += 46 + n + x + c;
	}
	return out;
}

// The contents of one entry: stored, or deflated (inflated with zlib).
static NSData *ZipRead( NSString *path, VSPZipEntry *e )
{
	NSFileHandle *fh;
	NSData *local, *comp;
	NSMutableData *out;
	const unsigned char *h;
	z_stream zs;
	int err;

	if( e->usize > 32 * 1024 * 1024 || ( e->method != 0 && e->method != 8 ) )
		return nil;
	if( !( fh = [NSFileHandle fileHandleForReadingAtPath: path] ) )
		return nil;
	[fh seekToFileOffset: e->offset];
	local = [fh readDataOfLength: 30];
	h = [local bytes];
	if( [local length] < 30 || h[0] != 'P' || h[1] != 'K' || h[2] != 3 || h[3] != 4 )
		return nil;
	[fh seekToFileOffset: (unsigned long long)e->offset + 30 + LE16( h + 26 ) + LE16( h + 28 )];
	comp = [fh readDataOfLength: e->csize];
	[fh closeFile];
	if( [comp length] != e->csize )
		return nil;
	if( e->method == 0 )
		return comp;

	out = [NSMutableData dataWithLength: e->usize];
	memset( &zs, 0, sizeof( zs ) );
	if( inflateInit2( &zs, -MAX_WBITS ) != Z_OK )
		return nil;
	zs.next_in = (Bytef *)[comp bytes];
	zs.avail_in = (uInt)[comp length];
	zs.next_out = [out mutableBytes];
	zs.avail_out = (uInt)[out length];
	err = inflate( &zs, Z_FINISH );
	inflateEnd( &zs );
	if( err != Z_STREAM_END && !( err == Z_BUF_ERROR && zs.avail_out == 0 ) )
		return nil;
	[out setLength: zs.total_out];
	return out;
}

static NSString *TextFromData( NSData *d )
{
	NSString *s;

	if( !d )
		return nil;
	s = [[NSString alloc] initWithData: d encoding: NSUTF8StringEncoding];
	return s ? s : [[NSString alloc] initWithData: d encoding: NSISOLatin1StringEncoding];
}

// Without Quake 3 color codes (^1 etc.).
static NSString *StripColors( NSString *s )
{
	NSMutableString *o;
	NSUInteger i, n = [s length];

	if( [s rangeOfString: @"^"].location == NSNotFound )
		return s;
	o = [NSMutableString stringWithCapacity: n];
	for( i = 0; i < n; i++ )
	{
		unichar c = [s characterAtIndex: i];
		if( c == '^' && i + 1 < n && [s characterAtIndex: i + 1] != '^' && isalnum( [s characterAtIndex: i + 1] ) )
		{
			i++;
			continue;
		}
		[o appendFormat: @"%C", c];
	}
	return o;
}

/* ------------------------------------------------------------------------
   The map catalog
   ------------------------------------------------------------------------ */

@implementation VSPMapInfo
{
@public
	NSString *shotPath;			// the pk3 or the loose file
	VSPZipEntry *shotEntry;		// nil when loose
	NSImage *shot;
	BOOL shotTried;
}

- (NSString *)displayName	{ return [_longName length] ? _longName : _name; }

- (BOOL)supportsGametype: (NSInteger)gametype
{
	NSArray *words;
	NSString *want;

	if( ![_types length] )
		return YES;
	words = [_types componentsSeparatedByCharactersInSet: [NSCharacterSet whitespaceCharacterSet]];
	want = gametype == VSP_GT_TOURNAMENT ? @"tourney" : gametype == VSP_GT_TEAM ? @"team" :
		gametype == VSP_GT_CTF ? @"ctf" : @"ffa";
	return [words containsObject: want];
}

- (NSString *)typesDescription
{
	NSMutableArray *a = [NSMutableArray array];
	NSArray *words = [_types componentsSeparatedByCharactersInSet: [NSCharacterSet whitespaceCharacterSet]];

	if( ![_types length] )
		return nil;
	if( [words containsObject: @"ffa"] )		[a addObject: @"FFA"];
	if( [words containsObject: @"tourney"] )	[a addObject: @"Tourney"];
	if( [words containsObject: @"team"] )		[a addObject: @"Team"];
	if( [words containsObject: @"ctf"] )		[a addObject: @"CTF"];
	return [a count] ? [a componentsJoinedByString: @", "] : nil;
}

- (NSImage *)levelshot
{
	if( !shotTried && shotPath )
	{
		NSData *d = shotEntry ? ZipRead( shotPath, shotEntry ) : [NSData dataWithContentsOfFile: shotPath];
		shotTried = YES;
		if( d )
			shot = [[NSImage alloc] initWithData: d];
	}
	return shot;
}

@end

// Quake 3 style tokens of an arena file: words, quoted strings, braces.
static NSArray<NSString *> *Tokens( NSString *text )
{
	NSMutableArray *out = [NSMutableArray array];
	NSUInteger i = 0, n = [text length];

	while( i < n )
	{
		unichar c = [text characterAtIndex: i];
		NSUInteger start;

		if( c <= ' ' )
			i++;
		else if( c == '/' && i + 1 < n && [text characterAtIndex: i + 1] == '/' )
		{
			while( i < n && [text characterAtIndex: i] != '\n' )
				i++;
		}
		else if( c == '/' && i + 1 < n && [text characterAtIndex: i + 1] == '*' )
		{
			NSRange r = [text rangeOfString: @"*/" options: 0 range: NSMakeRange( i + 2, n - i - 2 )];
			i = r.location == NSNotFound ? n : NSMaxRange( r );
		}
		else if( c == '"' )
		{
			NSRange r = [text rangeOfString: @"\"" options: 0 range: NSMakeRange( i + 1, n - i - 1 )];
			NSUInteger e = r.location == NSNotFound ? n : r.location;
			[out addObject: [text substringWithRange: NSMakeRange( i + 1, e - i - 1 )]];
			i = e + 1;
		}
		else if( c == '{' || c == '}' )
		{
			[out addObject: [NSString stringWithCharacters: &c length: 1]];
			i++;
		}
		else
		{
			start = i;
			while( i < n && [text characterAtIndex: i] > ' ' && [text characterAtIndex: i] != '{' && [text characterAtIndex: i] != '}' )
				i++;
			[out addObject: [text substringWithRange: NSMakeRange( start, i - start )]];
		}
	}
	return out;
}

// { map "x" longname "y" type "z" ... } blocks into arenas[lowercased map].
static void ParseArenas( NSString *text, NSMutableDictionary *arenas )
{
	NSArray *tok = Tokens( text );
	NSUInteger i = 0;

	while( i < [tok count] )
	{
		NSMutableDictionary *info;

		if( ![tok[i] isEqualToString: @"{"] )
		{
			i++;
			continue;
		}
		info = [NSMutableDictionary dictionary];
		for( i++; i + 1 < [tok count] && ![tok[i] isEqualToString: @"}"]; i += 2 )
			info[[tok[i] lowercaseString]] = tok[i + 1];
		i++;
		if( [info[@"map"] length] )
			arenas[[info[@"map"] lowercaseString]] = info;
	}
}

static BOOL IsRetailPak( NSString *file, BOOL *expansion )
{
	NSString *b = [[file stringByDeletingPathExtension] lowercaseString];

	*expansion = [b isEqualToString: @"pak3"];
	return [b length] == 4 && [b hasPrefix: @"pak"] && [b characterAtIndex: 3] >= '0' && [b characterAtIndex: 3] <= '3';
}

static NSArray<NSString *> *DataDirs( NSString *dataFolder )
{
	NSMutableArray *dirs = [NSMutableArray array];

	// the game folder first, the homepath last: it wins, as in the engine
	if( dataFolder )
		[dirs addObject: [dataFolder stringByStandardizingPath]];
	if( ![dirs containsObject: [VSP_HomeBase() stringByStandardizingPath]] )
		[dirs addObject: [VSP_HomeBase() stringByStandardizingPath]];
	return dirs;
}

// Names, sizes and dates of what the catalog reads, to know when to rescan.
static NSString *CatalogStamp( NSArray *dirs )
{
	NSMutableString *s = [NSMutableString string];
	NSFileManager *fm = [NSFileManager defaultManager];

	for( NSString *dir in dirs )
		for( NSString *sub in @[ @"", @"maps", @"scripts", @"levelshots" ] )
		{
			NSString *d = [dir stringByAppendingPathComponent: sub];
			for( NSString *f in [fm contentsOfDirectoryAtPath: d error: NULL] )
			{
				NSDictionary *a = [fm attributesOfItemAtPath: [d stringByAppendingPathComponent: f] error: NULL];
				[s appendFormat: @"%@/%@:%llu:%f|", d, f, [a fileSize], [[a fileModificationDate] timeIntervalSince1970]];
			}
		}
	return s;
}

NSArray<VSPMapInfo *> *VSP_MapCatalog( NSString *dataFolder )
{
	static NSString *cachedStamp;
	static NSArray *cached;
	NSArray *dirs = DataDirs( dataFolder );
	NSString *stamp = CatalogStamp( dirs );
	NSMutableDictionary<NSString *, VSPMapInfo *> *maps = [NSMutableDictionary dictionary];
	NSMutableDictionary *arenas = [NSMutableDictionary dictionary];
	NSMutableDictionary *shots = [NSMutableDictionary dictionary];	// lowercased map -> @[ path, entry or NSNull ]
	NSFileManager *fm = [NSFileManager defaultManager];
	NSMutableArray *out;
	NSDate *t0 = [NSDate date];

	if( cached && [stamp isEqualToString: cachedStamp] )
		return cached;

	for( NSString *dir in dirs )
	{
		NSArray *files = [[fm contentsOfDirectoryAtPath: dir error: NULL]
			sortedArrayUsingSelector: @selector(caseInsensitiveCompare:)];

		for( NSString *f in files )
		{
			NSString *path = [dir stringByAppendingPathComponent: f];
			BOOL expansion, retail;

			if( [f hasPrefix: @"."] || [[f pathExtension] caseInsensitiveCompare: @"pk3"] != NSOrderedSame )
				continue;
			retail = IsRetailPak( f, &expansion );
			for( VSPZipEntry *e in ZipList( path, ^BOOL( NSString *n ) {
				return ( [n hasPrefix: @"maps/"] && [n hasSuffix: @".bsp"] && [[n pathComponents] count] == 2 ) ||
					( [n hasPrefix: @"scripts/"] && ( [n hasSuffix: @".arena"] || [n isEqualToString: @"scripts/arenas.txt"] ) ) ||
					( [n hasPrefix: @"levelshots/"] && ( [n hasSuffix: @".jpg"] || [n hasSuffix: @".tga"] ) );
			} ) )
			{
				NSString *lower = [e.name lowercaseString];
				NSString *base = [[e.name lastPathComponent] stringByDeletingPathExtension];

				if( [lower hasPrefix: @"maps/"] )
				{
					VSPMapInfo *m = [[VSPMapInfo alloc] init];
					m.name = base;
					m.pk3 = f;
					m.retail = retail;
					m.expansion = expansion;
					maps[[base lowercaseString]] = m;
				}
				else if( [lower hasPrefix: @"scripts/"] )
					ParseArenas( TextFromData( ZipRead( path, e ) ), arenas );
				else if( !shots[[base lowercaseString]] || [lower hasSuffix: @".jpg"] )
					shots[[base lowercaseString]] = @[ path, e ];
			}
		}

		// loose files, as the engine also reads them
		for( NSString *f in [fm contentsOfDirectoryAtPath: [dir stringByAppendingPathComponent: @"maps"] error: NULL] )
			if( [[f pathExtension] caseInsensitiveCompare: @"bsp"] == NSOrderedSame )
			{
				VSPMapInfo *m = [[VSPMapInfo alloc] init];
				m.name = [f stringByDeletingPathExtension];
				maps[[m.name lowercaseString]] = m;
			}
		for( NSString *f in [fm contentsOfDirectoryAtPath: [dir stringByAppendingPathComponent: @"scripts"] error: NULL] )
			if( [[f pathExtension] caseInsensitiveCompare: @"arena"] == NSOrderedSame || [f caseInsensitiveCompare: @"arenas.txt"] == NSOrderedSame )
				ParseArenas( TextFromData( [NSData dataWithContentsOfFile:
					[[dir stringByAppendingPathComponent: @"scripts"] stringByAppendingPathComponent: f]] ), arenas );
		for( NSString *f in [fm contentsOfDirectoryAtPath: [dir stringByAppendingPathComponent: @"levelshots"] error: NULL] )
			if( [[f pathExtension] caseInsensitiveCompare: @"jpg"] == NSOrderedSame || [[f pathExtension] caseInsensitiveCompare: @"tga"] == NSOrderedSame )
				shots[[[f stringByDeletingPathExtension] lowercaseString]] =
					@[ [[dir stringByAppendingPathComponent: @"levelshots"] stringByAppendingPathComponent: f], [NSNull null] ];
	}

	out = [NSMutableArray array];
	for( NSString *key in maps )
	{
		VSPMapInfo *m = maps[key];
		NSDictionary *a = arenas[key];
		NSArray *shotInfo = shots[key];

		// the single player maps share the retail paks, without arena info
		if( !a && ( m.retail || m.expansion ) )
			continue;
		if( a )
		{
			m.longName = [StripColors( a[@"longname"] ) stringByTrimmingCharactersInSet: [NSCharacterSet whitespaceCharacterSet]];
			m.types = [[a[@"type"] lowercaseString] stringByTrimmingCharactersInSet: [NSCharacterSet whitespaceCharacterSet]];
		}
		if( shotInfo )
		{
			m->shotPath = shotInfo[0];
			m->shotEntry = shotInfo[1] == [NSNull null] ? nil : shotInfo[1];
		}
		[out addObject: m];
	}
	[out sortUsingComparator: ^NSComparisonResult( VSPMapInfo *a, VSPMapInfo *b ) {
		NSComparisonResult r = [a.displayName localizedCaseInsensitiveCompare: b.displayName];
		return r != NSOrderedSame ? r : [a.name caseInsensitiveCompare: b.name];
	}];
	LLog( @"Server: %lu Holomatch maps in the game files (%.0f ms)", (unsigned long)[out count],
		-[t0 timeIntervalSinceNow] * 1000 );
	cached = out;
	cachedStamp = stamp;
	return out;
}

VSPMapInfo *VSP_FindMap( NSArray<VSPMapInfo *> *maps, NSString *name )
{
	for( VSPMapInfo *m in maps )
		if( [m.name caseInsensitiveCompare: name] == NSOrderedSame )
			return m;
	return nil;
}

/* ------------------------------------------------------------------------
   Small views
   ------------------------------------------------------------------------ */

static NSFont *Body( void )		{ return [NSFont systemFontOfSize: 13]; }
static NSFont *Small( void )	{ return [NSFont systemFontOfSize: 11]; }

static NSTextField *Hint( NSString *text, CGFloat width )
{
	NSTextField *l = VSP_WrapLabel( text, Small(), [NSColor secondaryLabelColor] );
	[l setPreferredMaxLayoutWidth: width];
	return l;
}

static NSStackView *HStack( NSArray *views, CGFloat spacing )
{
	NSStackView *s = [NSStackView stackViewWithViews: views];
	[s setSpacing: spacing];
	[s setAlignment: NSLayoutAttributeFirstBaseline];
	return s;
}

static NSStackView *VStack( NSArray *views, CGFloat spacing )
{
	NSStackView *s = [NSStackView stackViewWithViews: views];
	[s setOrientation: NSUserInterfaceLayoutOrientationVertical];
	[s setAlignment: NSLayoutAttributeLeading];
	[s setSpacing: spacing];
	return s;
}

// A grid of "Label:" / control rows, like the launcher's.
static NSGridView *FormGrid( NSArray *rows, CGFloat labelWidth )
{
	NSGridView *g = [NSGridView gridViewWithViews: rows];
	[g setRowSpacing: 8];
	[g setColumnSpacing: 10];
	[[g columnAtIndex: 0] setXPlacement: NSGridCellPlacementTrailing];
	[[g columnAtIndex: 0] setWidth: labelWidth];
	[g setRowAlignment: NSGridRowAlignmentFirstBaseline];
	return g;
}

static NSImage *Symbol( NSString *name, CGFloat size )
{
	return [[NSImage imageWithSystemSymbolName: name accessibilityDescription: nil]
		imageWithSymbolConfiguration: [NSImageSymbolConfiguration configurationWithPointSize: size weight: NSFontWeightRegular]];
}

// A number field with a stepper, bound to an integer property of settings.
@interface VSPNumberField : NSObject <NSTextFieldDelegate>
@property (nonatomic, strong) NSTextField *field;
@property (nonatomic, strong) NSStepper *stepper;
@property (nonatomic, strong) NSStackView *view;
@property (nonatomic, copy) NSString *key;
@property (nonatomic, strong) VSPServerSettings *settings;
@property (nonatomic, copy) void ( ^changed )( void );
@end

@implementation VSPNumberField

- (instancetype)initWithKey: (NSString *)key settings: (VSPServerSettings *)settings min: (int)min max: (int)max
	unit: (NSString *)unit width: (CGFloat)width
{
	NSNumberFormatter *f = [[NSNumberFormatter alloc] init];

	if( !( self = [super init] ) )
		return nil;
	_key = key;
	_settings = settings;
	[f setNumberStyle: NSNumberFormatterNoStyle];
	[f setMinimum: @( min )];
	[f setMaximum: @( max )];
	[f setAllowsFloats: NO];
	_field = [NSTextField textFieldWithString: @""];
	[_field setFormatter: f];
	[_field setAlignment: NSTextAlignmentRight];
	[_field setDelegate: self];
	[[_field widthAnchor] constraintEqualToConstant: width].active = YES;
	_stepper = [[NSStepper alloc] init];
	[_stepper setMinValue: min];
	[_stepper setMaxValue: max];
	[_stepper setValueWraps: NO];
	[_stepper setTarget: self];
	[_stepper setAction: @selector(stepped:)];
	_view = HStack( unit ? @[ _field, _stepper, VSP_Label( unit, Body(), [NSColor secondaryLabelColor] ) ] : @[ _field, _stepper ], 4 );
	[_view setAlignment: NSLayoutAttributeCenterY];
	[self refresh];
	return self;
}

- (void)refresh
{
	NSInteger v = [[_settings valueForKey: _key] integerValue];
	[_field setIntegerValue: v];
	[_stepper setIntegerValue: v];
}

- (void)setValue: (NSInteger)v
{
	v = MIN( MAX( v, (NSInteger)[_stepper minValue] ), (NSInteger)[_stepper maxValue] );
	[_settings setValue: @( v ) forKey: _key];
	[_stepper setIntegerValue: v];
	if( _changed )
		_changed();
}

- (void)stepped: (id)sender
{
	[_field setIntegerValue: [_stepper integerValue]];
	[self setValue: [_stepper integerValue]];
}

- (void)controlTextDidChange: (NSNotification *)n
{
	if( [[_field stringValue] length] )
		[self setValue: [_field integerValue]];
}

- (void)controlTextDidEndEditing: (NSNotification *)n
{
	[self refresh];
}

- (void)setEnabled: (BOOL)enabled
{
	[_field setEnabled: enabled];
	[_stepper setEnabled: enabled];
}

@end

// A table row: levelshot, name, details.
@interface VSPMapCell : NSTableCellView
@property (nonatomic, strong) NSTextField *detail;
@end

@implementation VSPMapCell

- (instancetype)initWithIdentifier: (NSString *)ident thumb: (NSSize)thumb
{
	NSImageView *iv = [[NSImageView alloc] init];
	NSTextField *title = VSP_Label( @"", [NSFont systemFontOfSize: 13 weight: NSFontWeightMedium], nil );

	if( !( self = [super initWithFrame: NSMakeRect( 0, 0, 200, thumb.height + 8 )] ) )
		return nil;
	[self setIdentifier: ident];
	_detail = VSP_Label( @"", Small(), [NSColor secondaryLabelColor] );
	[_detail setMaximumNumberOfLines: 1];
	[[_detail cell] setUsesSingleLineMode: YES];
	[[_detail cell] setTruncatesLastVisibleLine: YES];
	[iv setImageScaling: NSImageScaleAxesIndependently];
	[iv setWantsLayer: YES];
	[[iv layer] setCornerRadius: 4];
	[[iv layer] setMasksToBounds: YES];
	for( NSTextField *t in @[ title, _detail ] )
	{
		[t setLineBreakMode: NSLineBreakByTruncatingTail];
		[t setContentCompressionResistancePriority: NSLayoutPriorityDefaultLow forOrientation: NSLayoutConstraintOrientationHorizontal];
	}
	for( NSView *v in @[ iv, title, _detail ] )
	{
		[v setTranslatesAutoresizingMaskIntoConstraints: NO];
		[self addSubview: v];
	}
	[NSLayoutConstraint activateConstraints: @[
		[[iv leadingAnchor] constraintEqualToAnchor: [self leadingAnchor] constant: 4],
		[[iv centerYAnchor] constraintEqualToAnchor: [self centerYAnchor]],
		[[iv widthAnchor] constraintEqualToConstant: thumb.width],
		[[iv heightAnchor] constraintEqualToConstant: thumb.height],
		[[title leadingAnchor] constraintEqualToAnchor: [iv trailingAnchor] constant: 9],
		[[title trailingAnchor] constraintLessThanOrEqualToAnchor: [self trailingAnchor] constant: -4],
		[[title bottomAnchor] constraintEqualToAnchor: [self centerYAnchor] constant: 0],
		[[_detail leadingAnchor] constraintEqualToAnchor: [title leadingAnchor]],
		[[_detail trailingAnchor] constraintLessThanOrEqualToAnchor: [self trailingAnchor] constant: -4],
		[[_detail topAnchor] constraintEqualToAnchor: [self centerYAnchor] constant: 1],
	]];
	[self setImageView: iv];
	[self setTextField: title];
	return self;
}

@end

// A placeholder levelshot.
static NSImage *NoLevelshot( BOOL missing )
{
	return [NSImage imageWithSize: NSMakeSize( 64, 48 ) flipped: NO drawingHandler: ^BOOL( NSRect r ) {
		NSImage *sym = Symbol( missing ? @"questionmark" : @"map", 18 );
		NSSize s = [sym size];
		[VSP_GroupFill() setFill];
		NSRectFill( r );
		[[NSColor quaternaryLabelColor] setFill];
		NSRectFillUsingOperation( r, NSCompositingOperationSourceOver );
		[sym drawInRect: NSMakeRect( ( r.size.width - s.width ) / 2, ( r.size.height - s.height ) / 2, s.width, s.height )
			fromRect: NSZeroRect operation: NSCompositingOperationSourceOver fraction: 0.5];
		return YES;
	}];
}

// A table that reports Delete and double clicks.
@interface VSPTable : NSTableView
@property (nonatomic, copy) void ( ^deleteHandler )( void );
@end

@implementation VSPTable
- (void)keyDown: (NSEvent *)e
{
	unichar k = [[e charactersIgnoringModifiers] length] ? [[e charactersIgnoringModifiers] characterAtIndex: 0] : 0;
	if( _deleteHandler && ( k == NSDeleteCharacter || k == NSBackspaceCharacter || k == NSDeleteFunctionKey ) )
		_deleteHandler();
	else
		[super keyDown: e];
}
@end

static NSScrollView *Scrolled( NSView *doc )
{
	NSScrollView *sv = [[NSScrollView alloc] init];
	[sv setDocumentView: doc];
	[sv setHasVerticalScroller: YES];
	[sv setAutohidesScrollers: YES];
	[sv setBorderType: NSNoBorder];
	[sv setDrawsBackground: NO];
	return sv;
}

// A scroll view in a rounded group box, without a caption.
static NSView *Boxed( NSView *v )
{
	NSView *g = VSP_Group( @"", v );
	// VSP_Group insets its content; tables look better edge to edge
	for( NSLayoutConstraint *c in [[[(NSStackView *)g views][1] constraints] copy] )
		if( [c firstItem] == v || [c secondItem] == v )
			[c setConstant: [c constant] > 0 ? 1 : -1];
	[[(NSStackView *)g views][0] setHidden: YES];
	return g;
}

/* ------------------------------------------------------------------------
   The Config… sheet
   ------------------------------------------------------------------------ */

#define ROW_ROTATION	@"org.voyagersp.rotation-row"
#define ROW_MAP			@"org.voyagersp.map-name"

@interface VSPServerConfigController : NSObject <NSTableViewDataSource, NSTableViewDelegate, NSTextFieldDelegate, NSSearchFieldDelegate>
@property (nonatomic, readonly) NSWindow *panel;
@end

@implementation VSPServerConfigController
{
	VSPServerSettings *orig, *edit;
	NSString *dataFolder;
	NSArray<VSPMapInfo *> *maps, *shown;
	NSMutableArray<NSString *> *rotation;
	void ( ^done )( BOOL saved );

	NSTabView *tabs;
	NSSegmentedControl *tabPicker;
	VSPTable *mapTable, *rotTable;
	NSSearchField *search;
	NSTextField *mapsCaption, *rotCaption;
	NSButton *addButton, *removeButton, *upButton, *downButton;

	NSPopUpButton *gametype, *skill;
	NSTextField *gametypeHint;
	NSMutableDictionary<NSString *, NSButton *> *modButtons;
	VSPNumberField *frag, *capture, *timeLimit, *maxPlayers, *bots, *port;
	NSButton *friendly, *lanButton, *internetButton, *openPort, *downloads, *pure;
	NSTextField *hostname, *password, *networkHint, *botsHint;
}

- (instancetype)initWithSettings: (VSPServerSettings *)settings dataFolder: (NSString *)folder
{
	if( !( self = [super init] ) )
		return nil;
	orig = settings;
	edit = [settings copy];
	dataFolder = folder;
	maps = VSP_MapCatalog( folder );
	rotation = [NSMutableArray arrayWithArray: edit.rotation];
	[self build];
	[self showAll];
	return self;
}

- (void)build
{
	NSView *content;
	NSStackView *root, *footer;
	NSButton *cancel, *ok, *defaults;
	NSTabViewItem *item;
	NSView *box;

	_panel = [[NSWindow alloc] initWithContentRect: NSMakeRect( 0, 0, 700, 560 )
		styleMask: NSWindowStyleMaskTitled backing: NSBackingStoreBuffered defer: NO];
	[_panel setTitle: @"Server Settings"];
	VSP_ApplyTestAppearance( _panel );
	content = [_panel contentView];

	tabs = [[NSTabView alloc] init];
	[tabs setTabViewType: NSNoTabsNoBorder];
	[tabs setDrawsBackground: NO];
	item = [[NSTabViewItem alloc] initWithIdentifier: @"maps"];
	[item setLabel: @"Maps"];
	[item setView: [self mapsTab]];
	[tabs addTabViewItem: item];
	item = [[NSTabViewItem alloc] initWithIdentifier: @"game"];
	[item setLabel: @"Game"];
	[item setView: [self gameTab]];
	[tabs addTabViewItem: item];
	item = [[NSTabViewItem alloc] initWithIdentifier: @"server"];
	[item setLabel: @"Server"];
	[item setView: [self serverTab]];
	[tabs addTabViewItem: item];

	defaults = [NSButton buttonWithTitle: @"Restore Defaults" target: self action: @selector(restoreDefaults:)];
	cancel = [NSButton buttonWithTitle: @"Cancel" target: self action: @selector(cancel:)];
	[cancel setKeyEquivalent: @"\033"];
	ok = [NSButton buttonWithTitle: @"Done" target: self action: @selector(ok:)];
	[ok setKeyEquivalent: @"\r"];
	[[ok widthAnchor] constraintGreaterThanOrEqualToConstant: 80].active = YES;
	[[cancel widthAnchor] constraintGreaterThanOrEqualToConstant: 80].active = YES;
	footer = [NSStackView stackViewWithViews: @[ defaults, [NSView new], cancel, ok ]];
	[[footer views][1] setContentHuggingPriority: 1 forOrientation: NSLayoutConstraintOrientationHorizontal];
	[footer setSpacing: 10];

	// tabs as a segmented control over a group box, like the launcher's look
	tabPicker = [NSSegmentedControl segmentedControlWithLabels: @[ @"Maps", @"Game", @"Server" ]
		trackingMode: NSSegmentSwitchTrackingSelectOne target: self action: @selector(tabPicked:)];
	[tabPicker setSegmentDistribution: NSSegmentDistributionFillEqually];
	[[tabPicker widthAnchor] constraintEqualToConstant: 300].active = YES;
	[tabPicker setSelectedSegment: 0];
	box = Boxed( tabs );

	root = VStack( @[ tabPicker, box, footer ], 14 );
	[root setAlignment: NSLayoutAttributeCenterX];
	[root setEdgeInsets: NSEdgeInsetsMake( 18, 20, 20, 20 )];
	[root setTranslatesAutoresizingMaskIntoConstraints: NO];
	[content addSubview: root];
	[[box widthAnchor] constraintEqualToAnchor: [root widthAnchor] constant: -40].active = YES;
	[[footer widthAnchor] constraintEqualToAnchor: [root widthAnchor] constant: -40].active = YES;
	[NSLayoutConstraint activateConstraints: @[
		[[root leadingAnchor] constraintEqualToAnchor: [content leadingAnchor]],
		[[root trailingAnchor] constraintEqualToAnchor: [content trailingAnchor]],
		[[root topAnchor] constraintEqualToAnchor: [content topAnchor]],
		[[root bottomAnchor] constraintEqualToAnchor: [content bottomAnchor]],
		[[root widthAnchor] constraintEqualToConstant: 700],
		[[tabs heightAnchor] constraintEqualToConstant: 480],
	]];
}

// --- Maps ---

- (VSPTable *)tableWithIdent: (NSString *)ident rowHeight: (CGFloat)h
{
	VSPTable *t = [[VSPTable alloc] init];
	NSTableColumn *c = [[NSTableColumn alloc] initWithIdentifier: ident];

	[c setResizingMask: NSTableColumnAutoresizingMask];
	[t addTableColumn: c];
	[t setHeaderView: nil];
	[t setRowHeight: h];
	[t setStyle: NSTableViewStyleInset];
	[t setBackgroundColor: [NSColor clearColor]];
	[t setAllowsMultipleSelection: YES];
	[t setColumnAutoresizingStyle: NSTableViewUniformColumnAutoresizingStyle];
	[t setDataSource: self];
	[t setDelegate: self];
	[t setTarget: self];
	[t setIdentifier: ident];
	return t;
}

- (NSButton *)iconButton: (NSString *)symbol tip: (NSString *)tip action: (SEL)action
{
	NSButton *b = [NSButton buttonWithImage: Symbol( symbol, 12 ) target: self action: action];
	[b setBezelStyle: NSBezelStyleRounded];
	[b setControlSize: NSControlSizeSmall];
	[b setToolTip: tip];
	[b setAccessibilityLabel: tip];
	return b;
}

- (NSView *)mapsTab
{
	NSView *v = [[NSView alloc] init];
	NSStackView *left, *right, *cols, *rotButtons, *all;
	NSView *mapBox, *rotBox;

	search = [[NSSearchField alloc] init];
	[search setPlaceholderString: @"Search maps"];
	[search setDelegate: self];
	[search setTarget: self];
	[search setAction: @selector(searchChanged:)];

	mapTable = [self tableWithIdent: @"maps" rowHeight: 50];
	[mapTable setDoubleAction: @selector(addMaps:)];
	[mapTable setDraggingSourceOperationMask: NSDragOperationCopy forLocal: YES];
	rotTable = [self tableWithIdent: @"rotation" rowHeight: 50];
	[rotTable registerForDraggedTypes: @[ ROW_ROTATION, ROW_MAP ]];
	[rotTable setDraggingDestinationFeedbackStyle: NSTableViewDraggingDestinationFeedbackStyleGap];
	{
		__weak VSPServerConfigController *weakSelf = self;
		rotTable.deleteHandler = ^{ [weakSelf removeMaps: nil]; };
	}

	mapsCaption = VSP_Label( @"", [NSFont systemFontOfSize: 11 weight: NSFontWeightSemibold], [NSColor secondaryLabelColor] );
	rotCaption = VSP_Label( @"", [NSFont systemFontOfSize: 11 weight: NSFontWeightSemibold], [NSColor secondaryLabelColor] );
	addButton = [NSButton buttonWithTitle: @"Add to Rotation" target: self action: @selector(addMaps:)];
	[addButton setImage: Symbol( @"plus", 11 )];
	[addButton setImagePosition: NSImageLeading];
	removeButton = [self iconButton: @"minus" tip: @"Remove from the rotation" action: @selector(removeMaps:)];
	upButton = [self iconButton: @"chevron.up" tip: @"Play earlier" action: @selector(moveUp:)];
	downButton = [self iconButton: @"chevron.down" tip: @"Play later" action: @selector(moveDown:)];
	rotButtons = HStack( @[ removeButton, upButton, downButton, [NSView new],
		VSP_Label( @"Drag to reorder", Small(), [NSColor tertiaryLabelColor] ) ], 6 );
	[rotButtons setAlignment: NSLayoutAttributeCenterY];
	[[rotButtons views][3] setContentHuggingPriority: 1 forOrientation: NSLayoutConstraintOrientationHorizontal];

	mapBox = Boxed( Scrolled( mapTable ) );
	rotBox = Boxed( Scrolled( rotTable ) );
	left = VStack( @[ HStack( @[ mapsCaption ], 0 ), search, mapBox, addButton ], 8 );
	right = VStack( @[ HStack( @[ rotCaption ], 0 ), Hint( @"The server plays these in order, then starts over.", 300 ),
		rotBox, rotButtons ], 8 );
	[right setCustomSpacing: 9 afterView: [right views][1]];
	for( NSStackView *s in @[ left, right ] )
		for( NSView *sub in [s views] )
			if( sub != addButton )
				[[sub widthAnchor] constraintEqualToAnchor: [s widthAnchor]].active = YES;
	[[search heightAnchor] constraintEqualToConstant: 22].active = YES;
	cols = [NSStackView stackViewWithViews: @[ left, right ]];
	[cols setDistribution: NSStackViewDistributionFillEqually];
	[cols setAlignment: NSLayoutAttributeTop];
	[cols setSpacing: 20];
	[[left bottomAnchor] constraintEqualToAnchor: [cols bottomAnchor]].active = YES;
	[[right bottomAnchor] constraintEqualToAnchor: [cols bottomAnchor]].active = YES;
	[[[(NSStackView *)mapBox views][1] bottomAnchor] constraintEqualToAnchor: [[(NSStackView *)rotBox views][1] bottomAnchor]].active = YES;

	all = VStack( @[ cols ], 0 );
	[all setTranslatesAutoresizingMaskIntoConstraints: NO];
	[v addSubview: all];
	[NSLayoutConstraint activateConstraints: @[
		[[all leadingAnchor] constraintEqualToAnchor: [v leadingAnchor] constant: 16],
		[[all trailingAnchor] constraintEqualToAnchor: [v trailingAnchor] constant: -16],
		[[all topAnchor] constraintEqualToAnchor: [v topAnchor] constant: 14],
		[[all bottomAnchor] constraintEqualToAnchor: [v bottomAnchor] constant: -14],
		[[cols widthAnchor] constraintEqualToAnchor: [all widthAnchor]],
		[[cols heightAnchor] constraintEqualToAnchor: [all heightAnchor]],
	]];
	return v;
}

- (void)filterMaps
{
	NSString *q = [[search stringValue] stringByTrimmingCharactersInSet: [NSCharacterSet whitespaceCharacterSet]];

	if( ![q length] )
		shown = maps;
	else
		shown = [maps filteredArrayUsingPredicate: [NSPredicate predicateWithBlock: ^BOOL( VSPMapInfo *m, NSDictionary *b ) {
			return [m.displayName localizedCaseInsensitiveContainsString: q] || [m.name localizedCaseInsensitiveContainsString: q];
		}]];
	[mapsCaption setStringValue: [q length] ? [NSString stringWithFormat: @"MAPS · %lu OF %lu",
		(unsigned long)[shown count], (unsigned long)[maps count]] :
		[NSString stringWithFormat: @"MAPS · %lu", (unsigned long)[maps count]]];
	[mapTable reloadData];
}

- (void)searchChanged: (id)sender	{ [self filterMaps]; }
- (void)controlTextDidChange: (NSNotification *)n
{
	if( [n object] == search )
		[self filterMaps];
	else if( [n object] == hostname )
		edit.hostname = [hostname stringValue];
	else if( [n object] == password )
		edit.password = [password stringValue];
}

- (void)showRotation
{
	edit.rotation = rotation;
	[rotCaption setStringValue: [rotation count] == 1 ? @"ROTATION · 1 MAP" :
		[NSString stringWithFormat: @"ROTATION · %lu MAPS", (unsigned long)[rotation count]]];
	[rotTable reloadData];
	[mapTable reloadData];
	[self updateButtons];
	[self showGametypeHint];
}

- (void)updateButtons
{
	NSIndexSet *sel = [rotTable selectedRowIndexes];
	[addButton setEnabled: [[mapTable selectedRowIndexes] count] > 0];
	[removeButton setEnabled: [sel count] > 0];
	[upButton setEnabled: [sel count] > 0 && [sel firstIndex] > 0];
	[downButton setEnabled: [sel count] > 0 && [sel lastIndex] + 1 < [rotation count]];
}

- (void)tableViewSelectionDidChange: (NSNotification *)n	{ [self updateButtons]; }

- (void)addMaps: (id)sender
{
	NSIndexSet *sel = [mapTable selectedRowIndexes];
	NSInteger clicked = [mapTable clickedRow];

	if( sender == mapTable && clicked >= 0 )
		sel = [NSIndexSet indexSetWithIndex: clicked];
	[sel enumerateIndexesUsingBlock: ^( NSUInteger i, BOOL *stop ) {
		[rotation addObject: shown[i].name];
	}];
	[self showRotation];
	if( [sel count] )
	{
		[rotTable selectRowIndexes: [NSIndexSet indexSetWithIndexesInRange: NSMakeRange( [rotation count] - [sel count], [sel count] )]
			byExtendingSelection: NO];
		[rotTable scrollRowToVisible: [rotation count] - 1];
	}
}

- (void)removeMaps: (id)sender
{
	NSIndexSet *sel = [rotTable selectedRowIndexes];
	NSUInteger first = [sel firstIndex];

	if( ![sel count] )
		return;
	[rotation removeObjectsAtIndexes: sel];
	[self showRotation];
	if( [rotation count] )
		[rotTable selectRowIndexes: [NSIndexSet indexSetWithIndex: MIN( first, [rotation count] - 1 )] byExtendingSelection: NO];
}

- (void)moveBy: (NSInteger)delta
{
	NSIndexSet *sel = [rotTable selectedRowIndexes];
	NSArray *moving = [rotation objectsAtIndexes: sel];
	NSUInteger at = delta < 0 ? [sel firstIndex] - 1 : [sel lastIndex] + 2 - [sel count];

	[rotation removeObjectsAtIndexes: sel];
	[rotation insertObjects: moving atIndexes: [NSIndexSet indexSetWithIndexesInRange: NSMakeRange( at, [moving count] )]];
	[self showRotation];
	[rotTable selectRowIndexes: [NSIndexSet indexSetWithIndexesInRange: NSMakeRange( at, [moving count] )] byExtendingSelection: NO];
}

- (void)moveUp: (id)sender		{ if( [[rotTable selectedRowIndexes] firstIndex] > 0 ) [self moveBy: -1]; }
- (void)moveDown: (id)sender	{ if( [[rotTable selectedRowIndexes] lastIndex] + 1 < [rotation count] ) [self moveBy: 1]; }

- (NSInteger)numberOfRowsInTableView: (NSTableView *)t
{
	return t == mapTable ? (NSInteger)[shown count] : (NSInteger)[rotation count];
}

- (NSAttributedString *)detailFor: (VSPMapInfo *)m name: (NSString *)name
{
	NSMutableAttributedString *s = [[NSMutableAttributedString alloc] init];
	NSMutableParagraphStyle *ps = [[NSMutableParagraphStyle alloc] init];
	NSDictionary *plain;

	[ps setLineBreakMode: NSLineBreakByTruncatingTail];
	plain = @{ NSForegroundColorAttributeName: [NSColor secondaryLabelColor], NSFontAttributeName: Small(), NSParagraphStyleAttributeName: ps };
	void ( ^add )( NSString *, NSColor * ) = ^( NSString *text, NSColor *color ) {
		if( [s length] )
			[s appendAttributedString: [[NSAttributedString alloc] initWithString: @" · " attributes: plain]];
		[s appendAttributedString: [[NSAttributedString alloc] initWithString: text attributes: color ?
			@{ NSForegroundColorAttributeName: color, NSFontAttributeName: [NSFont systemFontOfSize: 11 weight: NSFontWeightMedium],
			NSParagraphStyleAttributeName: ps } : plain]];
	};

	if( !m )
	{
		add( name, nil );
		add( @"Missing from your game files", [NSColor systemRedColor] );
		return s;
	}
	add( m.name, nil );
	if( m.expansion )
		add( @"Expansion Pack", [NSColor systemPurpleColor] );
	else if( !m.retail )
		add( @"Add-on", [NSColor systemBlueColor] );
	if( ![m supportsGametype: edit.gametype] )
		add( [NSString stringWithFormat: @"Not made for %@", [VSPServerSettings gametypeName: edit.gametype]], [NSColor systemOrangeColor] );
	else if( [m typesDescription] )
		add( [m typesDescription], nil );
	return s;
}

- (NSString *)tooltipFor: (VSPMapInfo *)m
{
	if( !m )
		return @"Not in any .pk3 file in your game folders any more. The server skips it.";
	if( m.expansion )
		return [NSString stringWithFormat: @"%@ (in %@). Players need the Expansion Pack: servers never send its files.", m.name, m.pk3];
	if( !m.retail )
		return [NSString stringWithFormat: @"%@ (in %@). Players without it can download it if map downloads are on.",
			m.name, m.pk3 ? m.pk3 : @"a loose .bsp file"];
	return [NSString stringWithFormat: @"%@ (in %@)", m.name, m.pk3];
}

- (NSView *)tableView: (NSTableView *)t viewForTableColumn: (NSTableColumn *)col row: (NSInteger)row
{
	VSPMapCell *cell = [t makeViewWithIdentifier: @"map" owner: self];
	VSPMapInfo *m;
	NSString *name;

	if( !cell )
		cell = [[VSPMapCell alloc] initWithIdentifier: @"map" thumb: NSMakeSize( 56, 42 )];
	if( t == mapTable )
	{
		m = shown[row];
		name = m.name;
		[[cell textField] setStringValue: m.displayName];
	}
	else
	{
		name = rotation[row];
		m = VSP_FindMap( maps, name );
		[[cell textField] setStringValue: [NSString stringWithFormat: @"%ld.  %@", (long)row + 1, m ? m.displayName : name]];
	}
	[[cell imageView] setImage: [m levelshot] ? [m levelshot] : NoLevelshot( !m )];
	[[cell detail] setAttributedStringValue: [self detailFor: m name: name]];
	[cell setToolTip: [self tooltipFor: m]];
	return cell;
}

- (id<NSPasteboardWriting>)tableView: (NSTableView *)t pasteboardWriterForRow: (NSInteger)row
{
	NSPasteboardItem *item = [[NSPasteboardItem alloc] init];
	if( t == rotTable )
		[item setString: [NSString stringWithFormat: @"%ld", (long)row] forType: ROW_ROTATION];
	else
		[item setString: shown[row].name forType: ROW_MAP];
	return item;
}

- (NSDragOperation)tableView: (NSTableView *)t validateDrop: (id<NSDraggingInfo>)info proposedRow: (NSInteger)row
	proposedDropOperation: (NSTableViewDropOperation)op
{
	if( t != rotTable )
		return NSDragOperationNone;
	[t setDropRow: row dropOperation: NSTableViewDropAbove];
	return [[info draggingPasteboard] availableTypeFromArray: @[ ROW_ROTATION ]] ? NSDragOperationMove : NSDragOperationCopy;
}

- (BOOL)tableView: (NSTableView *)t acceptDrop: (id<NSDraggingInfo>)info row: (NSInteger)row
	dropOperation: (NSTableViewDropOperation)op
{
	NSMutableIndexSet *from = [NSMutableIndexSet indexSet];
	NSMutableArray *names = [NSMutableArray array];

	for( NSPasteboardItem *item in [[info draggingPasteboard] pasteboardItems] )
	{
		NSString *r = [item stringForType: ROW_ROTATION], *n = [item stringForType: ROW_MAP];
		if( r )
			[from addIndex: (NSUInteger)[r integerValue]];
		else if( n )
			[names addObject: n];
	}
	if( [from count] )
	{
		NSArray *moving = [rotation objectsAtIndexes: from];
		row -= (NSInteger)[from countOfIndexesInRange: NSMakeRange( 0, (NSUInteger)row )];
		[rotation removeObjectsAtIndexes: from];
		[names setArray: moving];
	}
	row = MIN( MAX( row, 0 ), (NSInteger)[rotation count] );
	[rotation insertObjects: names atIndexes: [NSIndexSet indexSetWithIndexesInRange: NSMakeRange( (NSUInteger)row, [names count] )]];
	[self showRotation];
	[rotTable selectRowIndexes: [NSIndexSet indexSetWithIndexesInRange: NSMakeRange( (NSUInteger)row, [names count] )] byExtendingSelection: NO];
	return YES;
}

// --- Game ---

- (NSView *)gameTab
{
	NSView *v = [[NSView alloc] init];
	NSGridView *grid, *mods;
	NSMutableArray *modRows = [NSMutableArray array];
	NSDictionary *titles = @{ @"assimilation": @"Assimilation", @"specialties": @"Specialties",
		@"disintegration": @"Disintegration", @"elimination": @"Elimination", @"actionHero": @"Action Hero" };
	NSDictionary *details = @{
		@"assimilation": @"Killed players join the enemy team, the Borg. Team Holomatch only.",
		@"specialties": @"Players choose a class, each with its own weapons and abilities.",
		@"disintegration": @"One direct hit causes instant death.",
		@"elimination": @"Last one standing wins. Not with Capture the Flag.",
		@"actionHero": @"One random player gets all the weapons and armor. Free For All and Tournament only.",
	};
	__weak VSPServerConfigController *weakSelf = self;
	void ( ^changed )( void ) = ^{ [weakSelf showLimits]; };

	gametype = [[NSPopUpButton alloc] initWithFrame: NSZeroRect pullsDown: NO];
	for( NSNumber *gt in @[ @(VSP_GT_FFA), @(VSP_GT_TOURNAMENT), @(VSP_GT_TEAM), @(VSP_GT_CTF) ] )
	{
		[gametype addItemWithTitle: [VSPServerSettings gametypeName: [gt integerValue]]];
		[[gametype lastItem] setTag: [gt integerValue]];
	}
	[gametype setTarget: self];
	[gametype setAction: @selector(gametypeChanged:)];
	gametypeHint = Hint( @"", 420 );

	modButtons = [NSMutableDictionary dictionary];
	for( NSString *m in Modifiers() )
	{
		NSButton *b = [NSButton checkboxWithTitle: titles[m] target: self action: @selector(modifierChanged:)];
		[b setIdentifier: m];
		[b setToolTip: details[m]];
		modButtons[m] = b;
		[modRows addObject: @[ b, Hint( details[m], 370 ) ]];
	}
	mods = [NSGridView gridViewWithViews: modRows];
	[mods setRowSpacing: 5];
	[mods setColumnSpacing: 8];
	[[mods columnAtIndex: 0] setWidth: 112];
	[mods setRowAlignment: NSGridRowAlignmentFirstBaseline];

	frag = [[VSPNumberField alloc] initWithKey: @"fraglimit" settings: edit min: 0 max: 999 unit: @"points" width: 50];
	capture = [[VSPNumberField alloc] initWithKey: @"capturelimit" settings: edit min: 0 max: 99 unit: @"flags" width: 50];
	timeLimit = [[VSPNumberField alloc] initWithKey: @"timelimit" settings: edit min: 0 max: 999 unit: @"minutes" width: 50];
	bots = [[VSPNumberField alloc] initWithKey: @"bots" settings: edit min: 0 max: 31 unit: nil width: 50];
	frag.changed = capture.changed = timeLimit.changed = bots.changed = changed;
	friendly = [NSButton checkboxWithTitle: @"Friendly fire: teammates can hurt each other" target: self action: @selector(checkChanged:)];
	skill = [[NSPopUpButton alloc] initWithFrame: NSZeroRect pullsDown: NO];
	for( int i = 1; i <= 5; i++ )
	{
		[skill addItemWithTitle: [NSString stringWithFormat: @"%d · %@", i, [VSPServerSettings skillName: i]]];
		[[skill lastItem] setTag: i];
	}
	[skill setTarget: self];
	[skill setAction: @selector(skillChanged:)];
	botsHint = Hint( @"", 420 );

	grid = FormGrid( @[
		@[ VSP_Label( @"Game type:", Body(), nil ), gametype ],
		@[ [NSGridCell emptyContentView], gametypeHint ],
		@[ VSP_Label( @"Modifiers:", Body(), nil ), mods ],
		@[ [NSGridCell emptyContentView], Hint( @"Some modifiers exclude others, as in the game's own Create Server menu.", 420 ) ],
		@[ VSP_Label( @"Frag limit:", Body(), nil ), frag.view ],
		@[ VSP_Label( @"Capture limit:", Body(), nil ), capture.view ],
		@[ VSP_Label( @"Time limit:", Body(), nil ), timeLimit.view ],
		@[ [NSGridCell emptyContentView], friendly ],
		@[ VSP_Label( @"Bots:", Body(), nil ), HStack( @[ bots.view, skill ], 12 ) ],
		@[ [NSGridCell emptyContentView], botsHint ],
	], 112 );
	[[grid rowAtIndex: 2] setTopPadding: 6];
	[[grid rowAtIndex: 4] setTopPadding: 8];
	[[grid rowAtIndex: 8] setTopPadding: 8];
	[[grid cellForView: mods] setYPlacement: NSGridCellPlacementTop];
	[grid setTranslatesAutoresizingMaskIntoConstraints: NO];
	[v addSubview: grid];
	[NSLayoutConstraint activateConstraints: @[
		[[grid leadingAnchor] constraintEqualToAnchor: [v leadingAnchor] constant: 16],
		[[grid trailingAnchor] constraintLessThanOrEqualToAnchor: [v trailingAnchor] constant: -16],
		[[grid topAnchor] constraintEqualToAnchor: [v topAnchor] constant: 18],
	]];
	return v;
}

- (void)showGametypeHint
{
	NSInteger made = 0, total = 0;

	for( NSString *n in rotation )
	{
		VSPMapInfo *m = VSP_FindMap( maps, n );
		if( !m )
			continue;
		total++;
		made += [m supportsGametype: edit.gametype];
	}
	if( !total )
		[gametypeHint setStringValue: @"Add maps to the rotation on the Maps tab."];
	else if( made == total )
		[gametypeHint setStringValue: total == 1 ? @"The map in the rotation is made for it." :
			[NSString stringWithFormat: @"All %ld maps in the rotation are made for it.", (long)total]];
	else
		[gametypeHint setStringValue: [NSString stringWithFormat: @"%ld of the %ld maps in the rotation are made for it; "
			@"the others may lack spawn points or flags.", (long)made, (long)total]];
	[gametypeHint setTextColor: made == total && total ? [NSColor secondaryLabelColor] : [NSColor systemOrangeColor]];
}

- (void)gametypeChanged: (id)sender
{
	edit.gametype = [[gametype selectedItem] tag];
	[edit applyModifierRulesFavoring: nil];
	[self showGame];
	[mapTable reloadData];
	[rotTable reloadData];
}

- (void)modifierChanged: (NSButton *)b
{
	[edit setValue: @( [b state] == NSControlStateValueOn ) forKey: [b identifier]];
	[edit applyModifierRulesFavoring: [b identifier]];
	[self showGame];
}

- (void)skillChanged: (id)sender	{ edit.botSkill = [[skill selectedItem] tag]; }

- (void)checkChanged: (NSButton *)b
{
	BOOL on = [b state] == NSControlStateValueOn;
	if( b == friendly )			edit.friendlyFire = on;
	else if( b == downloads )	edit.allowDownload = on;
	else if( b == pure )		edit.pure = on;
	else if( b == openPort )	edit.openPort = on;
	else if( b == lanButton || b == internetButton )
	{
		edit.internet = b == internetButton;
		[self showServerTab];
	}
}

- (void)showLimits
{
	BOOL team = edit.gametype >= VSP_GT_TEAM, ctf = edit.gametype == VSP_GT_CTF;

	[frag setEnabled: !ctf];
	[capture setEnabled: ctf];
	[friendly setEnabled: team];
	if( !edit.bots )
		[botsHint setStringValue: @"No bots."];
	else
		[botsHint setStringValue: [NSString stringWithFormat: team ?
			@"Bots keep each team at %ld players; they leave as people join." :
			@"Bots keep the game at %ld players; they leave as people join.", (long)edit.bots]];
	[skill setEnabled: edit.bots > 0];
}

- (void)showGame
{
	[gametype selectItemWithTag: edit.gametype];
	for( NSString *m in Modifiers() )
	{
		NSButton *b = modButtons[m];
		BOOL allowed = !( ( [m isEqualToString: @"assimilation"] && edit.gametype != VSP_GT_TEAM ) ||
			( [m isEqualToString: @"elimination"] && edit.gametype >= VSP_GT_CTF ) ||
			( [m isEqualToString: @"actionHero"] && edit.gametype >= VSP_GT_TEAM ) );
		[b setState: [[edit valueForKey: m] boolValue] ? NSControlStateValueOn : NSControlStateValueOff];
		[b setEnabled: allowed];
	}
	[frag refresh];
	[capture refresh];
	[timeLimit refresh];
	[bots refresh];
	[friendly setState: edit.friendlyFire ? NSControlStateValueOn : NSControlStateValueOff];
	[skill selectItemWithTag: edit.botSkill];
	[self showLimits];
	[self showGametypeHint];
}

// --- Server ---

- (NSView *)serverTab
{
	NSView *v = [[NSView alloc] init];
	NSGridView *grid;

	hostname = [NSTextField textFieldWithString: @""];
	[hostname setDelegate: self];
	[[hostname widthAnchor] constraintEqualToConstant: 300].active = YES;
	password = [NSTextField textFieldWithString: @""];
	[password setPlaceholderString: @"None"];
	[password setDelegate: self];
	[[password widthAnchor] constraintEqualToConstant: 200].active = YES;
	maxPlayers = [[VSPNumberField alloc] initWithKey: @"maxPlayers" settings: edit min: 2 max: 32 unit: @"including bots" width: 50];
	lanButton = [NSButton radioButtonWithTitle: @"Local network only" target: self action: @selector(checkChanged:)];
	internetButton = [NSButton radioButtonWithTitle: @"Internet: also list it on the master server" target: self action: @selector(checkChanged:)];
	openPort = [NSButton checkboxWithTitle: @"Open the port on my router automatically" target: self action: @selector(checkChanged:)];
	[openPort setToolTip: @"Asks your router (NAT-PMP / UPnP) to forward the port while the server runs, so you don't have to set up port forwarding."];
	networkHint = Hint( @"", 400 );
	port = [[VSPNumberField alloc] initWithKey: @"port" settings: edit min: 1024 max: 65535 unit: nil width: 64];
	[port.stepper setHidden: YES];
	{
		__weak VSPServerConfigController *weakSelf = self;
		port.changed = ^{ [weakSelf showServerTab]; };
	}
	downloads = [NSButton checkboxWithTitle: @"Let players download maps they don't have" target: self action: @selector(checkChanged:)];
	pure = [NSButton checkboxWithTitle: @"Pure server: players must use the same game files" target: self action: @selector(checkChanged:)];

	grid = FormGrid( @[
		@[ VSP_Label( @"Server name:", Body(), nil ), hostname ],
		@[ VSP_Label( @"Password:", Body(), nil ), password ],
		@[ [NSGridCell emptyContentView], Hint( @"Players type it in Holomatch's Join menu. Leave it empty for an open server.", 400 ) ],
		@[ VSP_Label( @"Max players:", Body(), nil ), maxPlayers.view ],
		@[ VSP_Label( @"Network:", Body(), nil ), lanButton ],
		@[ [NSGridCell emptyContentView], internetButton ],
		@[ [NSGridCell emptyContentView], openPort ],
		@[ VSP_Label( @"Port:", Body(), nil ), HStack( @[ port.view, VSP_Label( @"UDP · 27960–27963 show up in the Join menu", Small(), [NSColor secondaryLabelColor] ) ], 8 ) ],
		@[ [NSGridCell emptyContentView], networkHint ],
		@[ VSP_Label( @"Downloads:", Body(), nil ), downloads ],
		@[ [NSGridCell emptyContentView], Hint( @"Add-on maps only: the retail and Expansion Pack files are never sent.", 400 ) ],
		@[ VSP_Label( @"Files:", Body(), nil ), pure ],
	], 112 );
	[[grid rowAtIndex: 3] setTopPadding: 6];
	[[grid rowAtIndex: 4] setTopPadding: 6];
	[[grid rowAtIndex: 9] setTopPadding: 6];
	[[grid cellForView: openPort] setXPlacement: NSGridCellPlacementLeading];
	[[[grid cellForView: openPort] row] setTopPadding: 0];
	[openPort setContentHuggingPriority: NSLayoutPriorityDefaultHigh forOrientation: NSLayoutConstraintOrientationHorizontal];
	[grid setTranslatesAutoresizingMaskIntoConstraints: NO];
	[v addSubview: grid];
	[NSLayoutConstraint activateConstraints: @[
		[[grid leadingAnchor] constraintEqualToAnchor: [v leadingAnchor] constant: 16],
		[[grid trailingAnchor] constraintLessThanOrEqualToAnchor: [v trailingAnchor] constant: -16],
		[[grid topAnchor] constraintEqualToAnchor: [v topAnchor] constant: 18],
	]];
	return v;
}

- (void)showServerTab
{
	[lanButton setState: edit.internet ? NSControlStateValueOff : NSControlStateValueOn];
	[internetButton setState: edit.internet ? NSControlStateValueOn : NSControlStateValueOff];
	[openPort setState: edit.openPort ? NSControlStateValueOn : NSControlStateValueOff];
	[openPort setEnabled: edit.internet];
	if( !LANDiscoverable( edit.port ) )
		[networkHint setStringValue: @"Outside 27960–27963, Holomatch's Join menu won't find the server on your network: "
			@"players have to connect by address."];
	else
		[networkHint setStringValue: edit.internet ?
			( edit.openPort ? [NSString stringWithFormat: @"The server window shows your public address once the router has opened UDP port %ld. "
				@"If it can't, forward that port to this Mac by hand.", (long)edit.port] :
			[NSString stringWithFormat: @"Players outside your network need UDP port %ld forwarded to this Mac on your router.", (long)edit.port] ) :
			@"Players on your network find it in Holomatch's Join menu."];
	[networkHint setTextColor: LANDiscoverable( edit.port ) ? [NSColor secondaryLabelColor] : [NSColor systemOrangeColor]];
}

- (void)showAll
{
	[search setStringValue: @""];
	[self filterMaps];
	[self showRotation];
	[self showGame];
	[hostname setStringValue: edit.hostname];
	[password setStringValue: edit.password];
	[maxPlayers refresh];
	[port refresh];
	[downloads setState: edit.allowDownload ? NSControlStateValueOn : NSControlStateValueOff];
	[pure setState: edit.pure ? NSControlStateValueOn : NSControlStateValueOff];
	[self showServerTab];
}

// --- the sheet ---

- (void)restoreDefaults: (id)sender
{
	VSPServerSettings *d = [[VSPServerSettings alloc] init];
	for( NSString *k in [VSPServerSettings keys] )
		[edit setValue: [d valueForKey: k] forKey: k];
	[rotation setArray: edit.rotation];
	[self showAll];
}

- (void)finish: (BOOL)saved
{
	if( saved )
	{
		[_panel makeFirstResponder: nil];		// end editing
		edit.rotation = rotation;
		for( NSString *k in [VSPServerSettings keys] )
			[orig setValue: [edit valueForKey: k] forKey: k];
		[orig save];
		LLog( @"Server settings saved: %@, %@, %lu maps", orig.hostname, [VSPServerSettings gametypeName: orig.gametype],
			(unsigned long)[orig.rotation count] );
	}
	[[_panel sheetParent] endSheet: _panel returnCode: saved ? NSModalResponseOK : NSModalResponseCancel];
}

- (void)ok: (id)sender		{ [self finish: YES]; }
- (void)cancel: (id)sender	{ [self finish: NO]; }

- (void)tabPicked: (id)sender
{
	[tabs selectTabViewItemAtIndex: [tabPicker selectedSegment]];
}

- (void)selectTab: (const char *)tab
{
	NSInteger i;

	if( !tab || !*tab )
		return;
	i = [tabs indexOfTabViewItemWithIdentifier: [[NSString stringWithUTF8String: tab] lowercaseString]];
	if( i == NSNotFound )
		return;
	[tabPicker setSelectedSegment: i];
	[tabs selectTabViewItemAtIndex: i];
}

@end

void VSP_ShowServerConfig( NSWindow *parent, VSPServerSettings *settings, NSString *dataFolder,
	void ( ^done )( BOOL saved ) )
{
	VSPServerConfigController *c = [[VSPServerConfigController alloc] initWithSettings: settings dataFolder: dataFolder];

	// the block keeps the controller alive until the sheet ends
	[parent beginSheet: [c panel] completionHandler: ^( NSModalResponse r ) {
		[[c panel] orderOut: nil];
		done( r == NSModalResponseOK );
	}];
}

BOOL VSP_SnapshotServerConfig( VSPServerSettings *settings, NSString *dataFolder, const char *tab, const char *path )
{
	VSPServerConfigController *c = [[VSPServerConfigController alloc] initWithSettings: settings dataFolder: dataFolder];

	[c selectTab: tab];
	return VSP_SnapshotWindow( [c panel], path );
}

/* ------------------------------------------------------------------------
   The server window
   ------------------------------------------------------------------------ */

static pid_t serverPid;		// for the atexit handler

static void KillServerAtExit( void )
{
	if( serverPid > 0 )
		kill( serverPid, SIGTERM );
}

// The address friends on the network connect to: the first active IPv4
// address of en0, else of any other interface (not loopback/link-local).
NSString *VSP_LANAddress( void )
{
	struct ifaddrs *list, *ifa;
	NSString *best = nil, *other = nil;

	if( getifaddrs( &list ) != 0 )
		return nil;
	for( ifa = list; ifa; ifa = ifa->ifa_next )
	{
		char buf[INET_ADDRSTRLEN];
		const struct sockaddr_in *sin = (const struct sockaddr_in *)ifa->ifa_addr;

		if( !sin || sin->sin_family != AF_INET || !( ifa->ifa_flags & IFF_UP ) || !( ifa->ifa_flags & IFF_RUNNING ) ||
			( ifa->ifa_flags & IFF_LOOPBACK ) )
			continue;
		if( ( ntohl( sin->sin_addr.s_addr ) & 0xffff0000 ) == 0xa9fe0000 )	// 169.254/16
			continue;
		inet_ntop( AF_INET, &sin->sin_addr, buf, sizeof( buf ) );
		if( !strcmp( ifa->ifa_name, "en0" ) && !best )
			best = @( buf );
		else if( !other && !strncmp( ifa->ifa_name, "en", 2 ) )
			other = @( buf );
		else if( !other && strncmp( ifa->ifa_name, "utun", 4 ) && strncmp( ifa->ifa_name, "bridge", 6 ) )
			other = @( buf );
	}
	freeifaddrs( list );
	return best ? best : other;
}

// The child's environment: ours, without the test hooks.
static NSDictionary *ChildEnvironment( void )
{
	NSMutableDictionary *env = [[[NSProcessInfo processInfo] environment] mutableCopy];
	for( NSString *k in [env allKeys] )
		if( [k hasPrefix: @"VOYAGERSP_TEST_"] )
			[env removeObjectForKey: k];
	return env;
}

// baseEF/vsp_server.cfg: every setting, the rotation, and its first map.
static BOOL WriteServerConfig( VSPServerSettings *s, NSArray<VSPMapInfo *> *maps, NSString **error )
{
	NSMutableString *cfg = [NSMutableString string];
	NSMutableArray *rot = [NSMutableArray array];
	NSString *path = [VSP_HomeBase() stringByAppendingPathComponent: SERVER_CFG];
	NSUInteger i;

	for( NSString *n in s.rotation )
	{
		VSPMapInfo *m = VSP_FindMap( maps, n );
		if( m )
			[rot addObject: m.name];
		else
			LLog( @"Server: map %@ is not in the game files, skipped", n );
	}
	if( ![rot count] )
	{
		for( VSPMapInfo *m in maps )
			if( [m supportsGametype: s.gametype] )
			{
				[rot addObject: m.name];
				break;
			}
		if( ![rot count] )
		{
			*error = @"There are no Holomatch maps in your game files.";
			return NO;
		}
		LLog( @"Server: no maps in the rotation, using %@", rot[0] );
	}

	[cfg appendString: @"// Written by the Elite Force launcher (Host a Server) at each start.\n"
		@"// Change these settings in the launcher's Config… sheet; edits here are overwritten.\n\n"];
	[cfg appendFormat: @"set sv_hostname \"%@\"\n", s.hostname];
	[cfg appendFormat: @"set g_password \"%@\"\n", s.password];
	[cfg appendFormat: @"set sv_maxclients %ld\n", (long)s.maxPlayers];
	[cfg appendFormat: @"set sv_allowDownload %d\n", s.allowDownload];
	[cfg appendFormat: @"set sv_pure %d\n\n", s.pure];
	[cfg appendFormat: @"set g_gametype %ld\n", (long)s.gametype];
	[cfg appendFormat: @"set g_pModAssimilation %d\n", s.assimilation];
	[cfg appendFormat: @"set g_pModDisintegration %d\n", s.disintegration];
	[cfg appendFormat: @"set g_pModActionHero %d\n", s.actionHero];
	[cfg appendFormat: @"set g_pModSpecialties %d\n", s.specialties];
	[cfg appendFormat: @"set g_pModElimination %d\n", s.elimination];
	[cfg appendFormat: @"set fraglimit %ld\n", (long)s.fraglimit];
	[cfg appendFormat: @"set capturelimit %ld\n", (long)s.capturelimit];
	[cfg appendFormat: @"set timelimit %ld\n", (long)s.timelimit];
	[cfg appendFormat: @"set g_friendlyFire %d\n\n", s.friendlyFire];
	[cfg appendString: @"// bots fill the game up to this many players (per team in team games)\n"];
	[cfg appendString: @"set bot_enable 1\n"];
	[cfg appendFormat: @"set bot_minplayers %ld\n", (long)s.bots];
	[cfg appendFormat: @"set g_spSkill %ld\n\n", (long)s.botSkill];
	[cfg appendString: @"// the master servers it announces itself to (the launcher's Servers… sheet)\n"];
	[cfg appendString: VSP_MastersCfgText( TestEnvSet( "VOYAGERSP_TEST_SERVER_NOMASTER" ) )];
	[cfg appendString: @"\n// the map rotation\n"];
	for( i = 0; i < [rot count]; i++ )
		[cfg appendFormat: @"set vsp_map%lu \"map %@ ; set nextmap vstr vsp_map%lu\"\n",
			(unsigned long)i + 1, rot[i], (unsigned long)( i + 1 < [rot count] ? i + 2 : 1 )];
	[cfg appendString: @"vstr vsp_map1\n"];

	[[NSFileManager defaultManager] createDirectoryAtPath: VSP_HomeBase() withIntermediateDirectories: YES attributes: nil error: NULL];
	if( ![cfg writeToFile: path atomically: YES encoding: NSUTF8StringEncoding error: NULL] )
	{
		*error = [NSString stringWithFormat: @"Couldn't write %@.", path];
		return NO;
	}
	return YES;
}

typedef enum { SRV_STARTING, SRV_RUNNING, SRV_IDLE, SRV_STOPPING, SRV_STOPPED, SRV_FAILED } srvState_t;

@interface VSPServerWindowController : NSObject <NSWindowDelegate, NSTableViewDataSource, NSTableViewDelegate, NSMenuDelegate>
@property (nonatomic, readonly) NSWindow *window;
- (void)natResult: (DNSServiceErrorType)err address: (uint32_t)externalAddress port: (uint16_t)externalPort;
- (void)foundPublicAddress: (uint32_t)externalAddress;
@end

@implementation VSPServerWindowController
{
	VSPServerSettings *settings;
	NSString *basepath, *dataFolder;
	NSArray *clientArgs;
	NSArray<VSPMapInfo *> *maps;

	NSTask *task;
	NSFileHandle *input;
	NSMutableData *pending;
	srvState_t state;
	NSModalResponse afterStop;
	NSTimer *pollTimer, *killTimer;
	int autoStatus, userStatus;			// status answers still to come
	BOOL inStatus, statusShown;
	NSMutableArray *statusRows;
	NSArray<NSDictionary *> *players;
	NSString *currentMap, *lanAddress;
	NSInteger port;
	// the router port mapping (internet servers with openPort)
	DNSServiceRef natRef;
	NSTimer *natTimer;
	NSString *publicAddress, *natProblem;
	NSInteger publicPort;
	BOOL natOpening;
	// is it on the internet server list (the masters)?
	DNSServiceRef addrRef;				// public address lookup when the port isn't mapped
	NSString *lookupAddress;			// public address from that lookup
	NSTimer *listTimer;
	int listChecks;
	BOOL listChecking, listFound, listMasterDown;
	NSString *listedOn;
	BOOL echo, readyLogged;
	NSDate *lastPoll;

	NSImageView *dot;
	NSTextField *title, *stateText, *address, *addressHint, *mapValue, *gameValue, *playersValue;
	NSTextField *internetValue, *internetHint, *listValue, *listHint;
	NSImageView *internetIcon, *listIcon;
	NSButton *checkAgainButton;
	NSButton *copyButton, *copyInternetButton, *playButton, *stopButton, *sendButton;
	NSButton *nextMapButton, *restartMapButton, *addBotButton, *removeBotsButton, *messageButton, *kickButton;
	NSPopUpButton *changeMapButton;
	NSTableView *playerTable;
	NSTextView *logView;
	NSTextField *command;
}

- (instancetype)initWithSettings: (VSPServerSettings *)s basepath: (NSString *)bp dataFolder: (NSString *)df
	clientArgs: (NSArray *)ca
{
	if( !( self = [super init] ) )
		return nil;
	settings = s;
	basepath = bp;
	dataFolder = df;
	clientArgs = ca;
	maps = VSP_MapCatalog( df );
	if( TestEnvSet( "VOYAGERSP_TEST_SERVER_INTERNET" ) )
	{
		s.internet = YES;
		s.openPort = YES;
	}
	port = s.port;
	lanAddress = VSP_LANAddress();
	pending = [NSMutableData data];
	statusRows = [NSMutableArray array];
	players = @[];
	echo = TestEnvSet( "VOYAGERSP_TEST_SERVER_START" );
	[self build];
	[self showState: SRV_STARTING detail: nil];
	[self showInfo];
	return self;
}

- (void)build
{
	NSView *content;
	NSStackView *root, *header, *headerText, *stateRow, *addressRow, *middle, *commandRow, *footer, *controls, *playersHeader;
	NSGridView *info;
	NSImageView *icon;
	NSScrollView *logScroll;
	NSView *playersBox, *logBox, *infoGroup;
	NSFont *f = Body();

	// taller for an internet server: its Internet and Listed rows
	_window = [[NSWindow alloc] initWithContentRect: NSMakeRect( 0, 0, 720, settings.internet ? 840 : 720 )
		styleMask: NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable |
			NSWindowStyleMaskResizable | NSWindowStyleMaskFullSizeContentView
		backing: NSBackingStoreBuffered defer: NO];
	[_window setTitle: [NSString stringWithFormat: @"%@ · Elite Force Server", settings.hostname]];
	[_window setTitlebarAppearsTransparent: YES];
	[_window setTitleVisibility: NSWindowTitleHidden];
	[_window setReleasedWhenClosed: NO];
	[_window setDelegate: self];
	[_window setContentMinSize: NSMakeSize( 640, 600 )];
	VSP_ApplyTestAppearance( _window );
	content = [_window contentView];

	// header: icon, server name, state
	icon = [NSImageView imageViewWithImage: [NSApp applicationIconImage]];
	[icon setImageScaling: NSImageScaleProportionallyUpOrDown];
	[[icon widthAnchor] constraintEqualToConstant: 52].active = YES;
	[[icon heightAnchor] constraintEqualToConstant: 52].active = YES;
	title = VSP_Label( settings.hostname, [NSFont systemFontOfSize: 22 weight: NSFontWeightBold], nil );
	[title setLineBreakMode: NSLineBreakByTruncatingTail];
	[title setContentCompressionResistancePriority: NSLayoutPriorityDefaultLow forOrientation: NSLayoutConstraintOrientationHorizontal];
	dot = [NSImageView imageViewWithImage: Symbol( @"circle.fill", 8 )];
	stateText = VSP_Label( @"", [NSFont systemFontOfSize: 13], [NSColor secondaryLabelColor] );
	stateRow = HStack( @[ dot, stateText ], 6 );
	[stateRow setAlignment: NSLayoutAttributeCenterY];
	headerText = VStack( @[ title, stateRow ], 3 );
	header = HStack( @[ icon, headerText ], 14 );
	[header setAlignment: NSLayoutAttributeCenterY];

	// how to connect, and what runs
	address = VSP_Label( @"", [NSFont monospacedDigitSystemFontOfSize: 15 weight: NSFontWeightSemibold], nil );
	[address setSelectable: YES];
	copyButton = [NSButton buttonWithTitle: @"Copy" target: self action: @selector(copyAddress:)];
	[copyButton setControlSize: NSControlSizeSmall];
	[copyButton setFont: [NSFont systemFontOfSize: [NSFont smallSystemFontSize]]];
	addressRow = HStack( @[ address, copyButton ], 10 );
	addressHint = Hint( @"", 480 );
	internetIcon = [[NSImageView alloc] init];
	internetValue = VSP_Label( @"", [NSFont monospacedDigitSystemFontOfSize: 15 weight: NSFontWeightSemibold], nil );
	[internetValue setSelectable: YES];
	copyInternetButton = [NSButton buttonWithTitle: @"Copy" target: self action: @selector(copyPublicAddress:)];
	[copyInternetButton setControlSize: NSControlSizeSmall];
	[copyInternetButton setFont: [NSFont systemFontOfSize: [NSFont smallSystemFontSize]]];
	internetHint = Hint( @"", 480 );
	[internetHint setLineBreakMode: NSLineBreakByWordWrapping];
	listIcon = [[NSImageView alloc] init];
	listValue = VSP_Label( @"", [NSFont systemFontOfSize: 13], nil );
	checkAgainButton = [NSButton buttonWithTitle: @"Check Again" target: self action: @selector(checkListAgain:)];
	[checkAgainButton setControlSize: NSControlSizeSmall];
	[checkAgainButton setFont: [NSFont systemFontOfSize: [NSFont smallSystemFontSize]]];
	listHint = Hint( @"", 480 );
	[listHint setLineBreakMode: NSLineBreakByWordWrapping];
	mapValue = VSP_Label( @"", f, nil );
	gameValue = VSP_Label( @"", f, nil );
	playersValue = VSP_Label( @"", f, nil );
	for( NSTextField *t in @[ mapValue, gameValue, playersValue, addressHint ] )
	{
		[t setLineBreakMode: NSLineBreakByTruncatingTail];
		[t setContentCompressionResistancePriority: NSLayoutPriorityDefaultLow forOrientation: NSLayoutConstraintOrientationHorizontal];
	}
	info = FormGrid( @[
		@[ VSP_Label( @"Address:", f, nil ), addressRow ],
		@[ [NSGridCell emptyContentView], addressHint ],
		@[ VSP_Label( @"Internet:", f, nil ), HStack( @[ internetIcon, internetValue, copyInternetButton ], 8 ) ],
		@[ [NSGridCell emptyContentView], internetHint ],
		@[ VSP_Label( @"Listed:", f, nil ), HStack( @[ listIcon, listValue, checkAgainButton ], 8 ) ],
		@[ [NSGridCell emptyContentView], listHint ],
		@[ VSP_Label( @"Map:", f, nil ), mapValue ],
		@[ VSP_Label( @"Game:", f, nil ), gameValue ],
		@[ VSP_Label( @"Players:", f, nil ), playersValue ],
	], 80 );
	[info setRowSpacing: 6];
	[[info rowAtIndex: 1] setTopPadding: -2];
	[[info rowAtIndex: 2] setTopPadding: 6];
	[[info rowAtIndex: 3] setTopPadding: -2];
	[[info rowAtIndex: 4] setTopPadding: 6];
	[[info rowAtIndex: 5] setTopPadding: -2];
	[[info rowAtIndex: 6] setTopPadding: 6];
	// only for internet servers
	for( int r = 2; r <= 5; r++ )
		[[info rowAtIndex: r] setHidden: !settings.internet];
	infoGroup = VSP_Group( @"Server", info );

	// server controls, under the Server box
	nextMapButton = [self controlButton: @"Next Map" symbol: @"forward.end.fill" action: @selector(nextMap:)
		tip: @"Go to the next map in the rotation"];
	restartMapButton = [self controlButton: @"Restart Map" symbol: @"arrow.counterclockwise" action: @selector(restartMap:)
		tip: @"Restart the current map; scores start over"];
	changeMapButton = [[NSPopUpButton alloc] initWithFrame: NSZeroRect pullsDown: YES];
	[changeMapButton addItemWithTitle: @"Change Map"];
	[[changeMapButton itemAtIndex: 0] setImage: Symbol( @"map", 13 )];
	[changeMapButton setToolTip: @"Switch to another map now"];
	[[changeMapButton menu] setDelegate: self];
	addBotButton = [self controlButton: @"Add Bot" symbol: @"person.badge.plus" action: @selector(addBot:)
		tip: @"Add one bot at the skill set in Config…"];
	removeBotsButton = [self controlButton: @"Remove Bots" symbol: @"person.2.slash" action: @selector(removeBots:)
		tip: @"Kick all bots (and stop filling the game with bots)"];
	messageButton = [self controlButton: @"Message…" symbol: @"text.bubble" action: @selector(messagePlayers:)
		tip: @"Send a message to everyone on the server"];
	// two rows, so the window keeps its width: the map, then players
	{
		NSStackView *mapRow = HStack( @[ nextMapButton, restartMapButton, changeMapButton ], 8 );
		NSStackView *playerRow = HStack( @[ addBotButton, removeBotsButton, messageButton ], 8 );
		for( NSStackView *r in @[ mapRow, playerRow ] )
		{
			[r setDistribution: NSStackViewDistributionFillEqually];
			for( NSView *v in [r views] )
				[v setContentCompressionResistancePriority: NSLayoutPriorityDefaultLow forOrientation: NSLayoutConstraintOrientationHorizontal];
		}
		controls = VStack( @[ mapRow, playerRow ], 8 );
		[[mapRow widthAnchor] constraintEqualToAnchor: [controls widthAnchor]].active = YES;
		[[playerRow widthAnchor] constraintEqualToAnchor: [controls widthAnchor]].active = YES;
	}

	// players and the console log
	playerTable = [[NSTableView alloc] init];
	for( NSArray *c in @[ @[ @"name", @"Player", @104 ], @[ @"score", @"Score", @42 ], @[ @"ping", @"Ping", @38 ] ] )
	{
		NSTableColumn *col = [[NSTableColumn alloc] initWithIdentifier: c[0]];
		[[col headerCell] setStringValue: c[1]];
		[col setWidth: [c[2] doubleValue]];
		if( ![c[0] isEqualToString: @"name"] )
			[[col headerCell] setAlignment: NSTextAlignmentRight];
		[playerTable addTableColumn: col];
	}
	[playerTable setStyle: NSTableViewStylePlain];
	[playerTable setBackgroundColor: [NSColor clearColor]];
	[playerTable setUsesAlternatingRowBackgroundColors: NO];
	[playerTable setColumnAutoresizingStyle: NSTableViewFirstColumnOnlyAutoresizingStyle];
	[playerTable setDataSource: self];
	[playerTable setDelegate: self];
	[playerTable setRowHeight: 20];
	playersBox = Boxed( Scrolled( playerTable ) );

	logView = [[NSTextView alloc] initWithFrame: NSMakeRect( 0, 0, 400, 200 )];
	[logView setEditable: NO];
	[logView setRichText: NO];
	[logView setDrawsBackground: NO];
	[logView setFont: [NSFont monospacedSystemFontOfSize: 11 weight: NSFontWeightRegular]];
	[logView setTextColor: [NSColor labelColor]];
	[logView setTextContainerInset: NSMakeSize( 6, 6 )];
	[logView setVerticallyResizable: YES];
	[logView setHorizontallyResizable: NO];
	[logView setAutoresizingMask: NSViewWidthSizable];
	[[logView textContainer] setWidthTracksTextView: YES];
	logScroll = Scrolled( logView );
	[logScroll setHasVerticalScroller: YES];
	logBox = Boxed( logScroll );
	kickButton = [NSButton buttonWithTitle: @"Kick" target: self action: @selector(kickPlayer:)];
	[kickButton setControlSize: NSControlSizeSmall];
	[kickButton setFont: [NSFont systemFontOfSize: [NSFont smallSystemFontSize]]];
	[kickButton setToolTip: @"Kick the selected player or bot"];
	playersHeader = HStack( @[ VSP_Label( @"PLAYERS", [NSFont systemFontOfSize: 11 weight: NSFontWeightSemibold],
		[NSColor secondaryLabelColor] ), [NSView new], kickButton ], 6 );
	[[playersHeader views][1] setContentHuggingPriority: 1 forOrientation: NSLayoutConstraintOrientationHorizontal];
	{
		NSMenu *m = [[NSMenu alloc] init];
		[m addItemWithTitle: @"Kick" action: @selector(kickPlayer:) keyEquivalent: @""];
		[[[m itemArray] firstObject] setTarget: self];
		[playerTable setMenu: m];
	}
	middle = [NSStackView stackViewWithViews: @[ VStack( @[ playersHeader, playersBox ], 6 ),
		VStack( @[ VSP_Label( @"CONSOLE", [NSFont systemFontOfSize: 11 weight: NSFontWeightSemibold],
			[NSColor secondaryLabelColor] ), logBox ], 6 ) ]];
	[middle setSpacing: 14];
	[middle setAlignment: NSLayoutAttributeTop];
	for( NSStackView *col in [middle views] )
	{
		[[[col views][1] widthAnchor] constraintEqualToAnchor: [col widthAnchor]].active = YES;
		[[col bottomAnchor] constraintEqualToAnchor: [middle bottomAnchor]].active = YES;
		// the log shrinks (and scrolls) before anything else gives way
		NSLayoutConstraint *minHeight = [[[col views][1] heightAnchor] constraintGreaterThanOrEqualToConstant: 160];
		[minHeight setPriority: NSLayoutPriorityDefaultHigh];
		minHeight.active = YES;
	}
	[[[middle views][0] widthAnchor] constraintEqualToConstant: 230].active = YES;
	[[[middle views][1] widthAnchor] constraintEqualToAnchor: [middle widthAnchor] constant: -( 230 + 14 )].active = YES;

	command = [NSTextField textFieldWithString: @""];
	[command setPlaceholderString: @"Console command, such as say Hello, map hm_borg1 or kick <name>"];
	[command setTarget: self];
	[command setAction: @selector(sendCommand:)];
	sendButton = [NSButton buttonWithTitle: @"Send" target: self action: @selector(sendCommand:)];
	commandRow = HStack( @[ command, sendButton ], 8 );
	[commandRow setAlignment: NSLayoutAttributeCenterY];
	[command setContentHuggingPriority: 1 forOrientation: NSLayoutConstraintOrientationHorizontal];

	playButton = [NSButton buttonWithTitle: @"Play on This Mac" target: self action: @selector(playHere:)];
	[playButton setImage: Symbol( @"play.fill", 11 )];
	[playButton setImagePosition: NSImageLeading];
	[playButton setToolTip: @"Start Elite Force in Holomatch and join this server"];
	stopButton = [NSButton buttonWithTitle: @"Stop Server" target: self action: @selector(stop:)];
	for( NSButton *b in @[ playButton, stopButton ] )
		[b setControlSize: NSControlSizeLarge];
	[[stopButton widthAnchor] constraintGreaterThanOrEqualToConstant: 110].active = YES;
	footer = [NSStackView stackViewWithViews: @[ playButton, [NSView new], stopButton ]];
	[[footer views][1] setContentHuggingPriority: 1 forOrientation: NSLayoutConstraintOrientationHorizontal];
	[footer setSpacing: 10];

	root = VStack( @[ header, infoGroup, controls, middle, commandRow, footer ], 16 );
	[root setCustomSpacing: 12 afterView: infoGroup];
	[[playersHeader widthAnchor] constraintEqualToAnchor: [playersBox widthAnchor]].active = YES;
	[root setCustomSpacing: 20 afterView: header];
	[root setCustomSpacing: 10 afterView: middle];
	[root setCustomSpacing: 20 afterView: commandRow];
	[root setEdgeInsets: NSEdgeInsetsMake( 34, 28, 22, 28 )];
	[root setTranslatesAutoresizingMaskIntoConstraints: NO];
	[content addSubview: root];
	for( NSView *v in [root views] )
		[[v widthAnchor] constraintEqualToAnchor: [root widthAnchor] constant: -56].active = YES;
	[middle setContentHuggingPriority: 1 forOrientation: NSLayoutConstraintOrientationVertical];
	[NSLayoutConstraint activateConstraints: @[
		[[root leadingAnchor] constraintEqualToAnchor: [content leadingAnchor]],
		[[root trailingAnchor] constraintEqualToAnchor: [content trailingAnchor]],
		[[root topAnchor] constraintEqualToAnchor: [content topAnchor]],
		[[root bottomAnchor] constraintEqualToAnchor: [content bottomAnchor]],
	]];
	[_window setInitialFirstResponder: command];
}

// --- what the window shows ---

- (void)showState: (srvState_t)s detail: (NSString *)detail
{
	NSColor *c;
	NSString *t;

	state = s;
	switch( s )
	{
	case SRV_STARTING:	c = [NSColor systemOrangeColor];	t = @"Starting…"; break;
	case SRV_RUNNING:	c = [NSColor systemGreenColor];		t = settings.internet ? @"Running · on your network and the internet" :
															@"Running · on your local network"; break;
	case SRV_IDLE:		c = [NSColor systemOrangeColor];	t = @"Running, but no map is loaded"; break;
	case SRV_STOPPING:	c = [NSColor systemOrangeColor];	t = @"Stopping…"; break;
	case SRV_STOPPED:	c = [NSColor tertiaryLabelColor];	t = @"Stopped"; break;
	default:			c = [NSColor systemRedColor];		t = @"Stopped"; break;
	}
	[dot setContentTintColor: c];
	[stateText setStringValue: detail ? [NSString stringWithFormat: @"%@ · %@", t, detail] : t];
	[stateText setTextColor: s == SRV_FAILED ? [NSColor systemRedColor] : [NSColor secondaryLabelColor]];

	[playButton setEnabled: s == SRV_RUNNING];
	[command setEnabled: s == SRV_STARTING || s == SRV_RUNNING || s == SRV_IDLE];
	[sendButton setEnabled: [command isEnabled]];
	[stopButton setTitle: s == SRV_STOPPED || s == SRV_FAILED ? @"Back to Launcher" : @"Stop Server"];
	[stopButton setEnabled: s != SRV_STOPPING];
	[self updateControls];
}

- (NSString *)addressString
{
	return [NSString stringWithFormat: @"%@:%ld", lanAddress ? lanAddress : @"127.0.0.1", (long)port];
}

- (void)showInfo
{
	VSPMapInfo *m = currentMap ? VSP_FindMap( maps, currentMap ) : nil;
	NSArray *mods = [settings modifierNames];
	NSInteger bots = 0;

	[address setStringValue: [self addressString]];
	if( !lanAddress )
		[addressHint setStringValue: @"This Mac isn't on a network: only Play on This Mac can join."];
	else if( !LANDiscoverable( port ) )
		[addressHint setStringValue: [NSString stringWithFormat: @"Port %ld is outside 27960–27963, so Holomatch's Join menu "
			@"won't list it on your network: players connect to this address.%@", (long)port,
			settings.internet ? @" From the internet: your public address, with the port forwarded." : @""]];
	else if( settings.internet )
		[addressHint setStringValue: @"On your network, players find it in Holomatch's Join menu, or connect to this address."];
	else
		[addressHint setStringValue: @"Players on your network find it in Holomatch's Join menu, or connect to this address."];
	[addressHint setTextColor: LANDiscoverable( port ) || !lanAddress ? [NSColor secondaryLabelColor] : [NSColor systemOrangeColor]];
	[addressHint setToolTip: [addressHint stringValue]];
	[self showInternet];
	[self showList];
	[self updateControls];

	if( m )
		[mapValue setStringValue: [m.displayName isEqualToString: m.name] ? m.name :
			[NSString stringWithFormat: @"%@ (%@)", m.displayName, m.name]];
	else
		[mapValue setStringValue: currentMap ? currentMap : @"—"];
	if( currentMap && [settings.rotation count] > 1 )
		[mapValue setStringValue: [NSString stringWithFormat: @"%@ · %lu maps in rotation", [mapValue stringValue],
			(unsigned long)[settings.rotation count]]];
	[gameValue setStringValue: [mods count] ? [NSString stringWithFormat: @"%@ · %@",
		[VSPServerSettings gametypeName: settings.gametype], [mods componentsJoinedByString: @", "]] :
		[VSPServerSettings gametypeName: settings.gametype]];

	for( NSDictionary *p in players )
		bots += [p[@"bot"] boolValue];
	if( state == SRV_STARTING || !currentMap )
		[playersValue setStringValue: [NSString stringWithFormat: @"Up to %ld", (long)settings.maxPlayers]];
	else
		[playersValue setStringValue: [NSString stringWithFormat: @"%lu of %ld%@", (unsigned long)[players count],
			(long)settings.maxPlayers, bots ? [NSString stringWithFormat: @" · %ld %@", (long)bots, bots == 1 ? @"bot" : @"bots"] : @""]];
}

- (void)appendLog: (NSString *)line color: (NSColor *)color
{
	NSTextStorage *ts = [logView textStorage];
	NSScrollView *sv = [logView enclosingScrollView];
	BOOL atBottom = NSMaxY( [[sv contentView] bounds] ) >= NSHeight( [logView frame] ) - 4;
	NSDictionary *attr = @{ NSFontAttributeName: [logView font] ? [logView font] : [NSFont monospacedSystemFontOfSize: 11 weight: NSFontWeightRegular],
		NSForegroundColorAttributeName: color ? color : [NSColor labelColor] };

	[ts beginEditing];
	[ts appendAttributedString: [[NSAttributedString alloc] initWithString: [line stringByAppendingString: @"\n"] attributes: attr]];
	if( [ts length] > 300000 )
	{
		NSRange cut = [[ts string] lineRangeForRange: NSMakeRange( 100000, 0 )];
		[ts deleteCharactersInRange: NSMakeRange( 0, NSMaxRange( cut ) )];
	}
	[ts endEditing];
	if( atBottom )
		[logView scrollRangeToVisible: NSMakeRange( [ts length], 0 )];
}

// --- the child process ---

- (BOOL)start
{
	NSString *error = nil;
	NSPipe *outPipe = [NSPipe pipe], *inPipe = [NSPipe pipe];
	NSMutableArray *args;
	NSError *err = nil;
	__weak VSPServerWindowController *weakSelf = self;

	if( !WriteServerConfig( settings, maps, &error ) )
	{
		[self appendLog: error color: [NSColor systemRedColor]];
		[self showState: SRV_FAILED detail: @"couldn't start"];
		return NO;
	}
	args = [NSMutableArray arrayWithArray: @[ @"+set", @"com_holomatch", @"1",
		@"+set", @"dedicated", settings.internet ? @"2" : @"1",
		@"+set", @"net_port", [NSString stringWithFormat: @"%ld", (long)settings.port] ]];
	if( basepath )
		[args addObjectsFromArray: @[ @"+set", @"fs_basepath", basepath ]];
	[args addObjectsFromArray: @[ @"+exec", SERVER_CFG ]];

	task = [[NSTask alloc] init];
	[task setExecutableURL: [[NSBundle mainBundle] executableURL]];
	[task setArguments: args];
	[task setEnvironment: ChildEnvironment()];
	[task setStandardOutput: outPipe];
	[task setStandardError: outPipe];
	[task setStandardInput: inPipe];
	[task setTerminationHandler: ^( NSTask *t ) {
		dispatch_async( dispatch_get_main_queue(), ^{ [weakSelf taskEnded]; } );
	}];
	[[outPipe fileHandleForReading] setReadabilityHandler: ^( NSFileHandle *h ) {
		NSData *d = [h availableData];
		if( ![d length] )
			[h setReadabilityHandler: nil];
		else
			dispatch_async( dispatch_get_main_queue(), ^{ [weakSelf received: d]; } );
	}];
	input = [inPipe fileHandleForWriting];
	// a server that quits must not kill us with SIGPIPE
	signal( SIGPIPE, SIG_IGN );

	LLog( @"Server: starting %@ %@", [[task executableURL] path], [args componentsJoinedByString: @" "] );
	[self appendLog: [NSString stringWithFormat: @"Starting the server: %@", [args componentsJoinedByString: @" "]]
		color: [NSColor secondaryLabelColor]];
	if( ![task launchAndReturnError: &err] )
	{
		[self appendLog: [NSString stringWithFormat: @"Couldn't start the server: %@", [err localizedDescription]]
			color: [NSColor systemRedColor]];
		[self showState: SRV_FAILED detail: @"couldn't start"];
		task = nil;
		return NO;
	}
	serverPid = [task processIdentifier];
	atexit( KillServerAtExit );
	pollTimer = [NSTimer timerWithTimeInterval: 2 target: self selector: @selector(poll:) userInfo: nil repeats: YES];
	[[NSRunLoop currentRunLoop] addTimer: pollTimer forMode: NSRunLoopCommonModes];
	return YES;
}

// --- server controls ---

- (NSButton *)controlButton: (NSString *)title symbol: (NSString *)symbol action: (SEL)action tip: (NSString *)tip
{
	NSButton *b = [NSButton buttonWithTitle: title image: Symbol( symbol, 13 ) target: self action: action];
	[b setImagePosition: NSImageLeading];
	[b setToolTip: tip];
	return b;
}

// a console command from a button: shown in the log like a typed one
- (void)run: (NSString *)cmd
{
	[self appendLog: [@"] " stringByAppendingString: cmd] color: [NSColor controlAccentColor]];
	[self send: cmd];
}

- (void)updateControls
{
	BOOL running = task && state == SRV_RUNNING;
	NSInteger row = [playerTable selectedRow];

	for( NSControl *c in @[ nextMapButton, restartMapButton, changeMapButton, addBotButton, removeBotsButton, messageButton ] )
		[c setEnabled: running];
	[kickButton setEnabled: running && row >= 0 && row < (NSInteger)[players count]];
}

- (void)tableViewSelectionDidChange: (NSNotification *)n	{ [self updateControls]; }

- (void)nextMap: (id)sender
{
	[self run: @"vstr nextmap"];
}

- (void)restartMap: (id)sender
{
	[self run: @"map_restart"];
}

// Change Map's menu: the rotation first, then the other maps for this game type.
- (void)menuNeedsUpdate: (NSMenu *)menu
{
	NSArray<NSString *> *rot = settings.rotation;
	NSMutableArray<VSPMapInfo *> *others = [NSMutableArray array];
	NSMenuItem *item;

	while( [menu numberOfItems] > 1 )
		[menu removeItemAtIndex: 1];
	if( [rot count] )
	{
		[[menu addItemWithTitle: @"Rotation" action: nil keyEquivalent: @""] setEnabled: NO];
		for( NSUInteger i = 0; i < [rot count]; i++ )
		{
			VSPMapInfo *m = VSP_FindMap( maps, rot[i] );
			item = [menu addItemWithTitle: m ? [NSString stringWithFormat: @"%@ (%@)", m.displayName, m.name] : rot[i]
				action: @selector(changeMap:) keyEquivalent: @""];
			[item setTarget: self];
			[item setRepresentedObject: @{ @"map": rot[i], @"slot": @( i + 1 ) }];
			[item setState: [rot[i] isEqualToString: currentMap] ? NSControlStateValueOn : NSControlStateValueOff];
			[item setIndentationLevel: 1];
		}
	}
	for( VSPMapInfo *m in maps )
		if( ![rot containsObject: m.name] && [m supportsGametype: settings.gametype] )
			[others addObject: m];
	[others sortUsingComparator: ^NSComparisonResult( VSPMapInfo *a, VSPMapInfo *b ) {
		return [a.displayName localizedCaseInsensitiveCompare: b.displayName];
	}];
	if( [others count] )
	{
		if( [rot count] )
			[menu addItem: [NSMenuItem separatorItem]];
		[[menu addItemWithTitle: @"Other maps" action: nil keyEquivalent: @""] setEnabled: NO];
		for( VSPMapInfo *m in others )
		{
			item = [menu addItemWithTitle: [NSString stringWithFormat: @"%@ (%@)%@", m.displayName, m.name,
				m.expansion ? @" · Expansion Pack" : @""] action: @selector(changeMap:) keyEquivalent: @""];
			[item setTarget: self];
			[item setRepresentedObject: @{ @"map": m.name }];
			[item setState: [m.name isEqualToString: currentMap] ? NSControlStateValueOn : NSControlStateValueOff];
			[item setIndentationLevel: 1];
		}
	}
}

- (void)changeMap: (NSMenuItem *)item
{
	NSDictionary *d = [item representedObject];

	// a rotation map continues the rotation from there; any other map leaves it where it was
	if( d[@"slot"] )
		[self run: [NSString stringWithFormat: @"vstr vsp_map%@", d[@"slot"]]];
	else
		[self run: [NSString stringWithFormat: @"map %@", d[@"map"]]];
}

- (void)addBot: (id)sender
{
	static NSArray *names;
	NSMutableArray *free;
	NSString *bot;

	if( !names )
		names = @[ @"Munro", @"Janeway", @"Chakotay", @"Tuvok", @"Paris", @"Torres", @"Kim", @"Seven", @"Doctor",
			@"Neelix", @"Biessman", @"Chang", @"Chell", @"Foster", @"Jurot", @"Telsia", @"Gowron", @"Kahless", @"Sela",
			@"Alexandria", @"Laird", @"Oviedo", @"Nelson", @"Odell", @"Jaworski", @"Csatlos", @"Vorik", @"BorgQueen" ];
	free = [names mutableCopy];
	for( NSDictionary *p in players )
		for( NSString *n in names )
			if( [p[@"name"] caseInsensitiveCompare: n] == NSOrderedSame )
				[free removeObject: n];
	if( ![free count] )
		free = [names mutableCopy];
	bot = free[arc4random_uniform( (uint32_t)[free count] )];
	[self run: [NSString stringWithFormat: @"addbot %@ %ld", bot, (long)settings.botSkill]];
}

- (void)removeBots: (id)sender
{
	// otherwise bot_minplayers adds them right back
	[self run: @"set bot_minplayers 0"];
	[self run: @"kick allbots"];
	[self appendLog: @"Bots won't refill this game; Add Bot still adds them." color: [NSColor secondaryLabelColor]];
}

- (void)kickPlayer: (id)sender
{
	NSInteger row = [playerTable clickedRow] >= 0 ? [playerTable clickedRow] : [playerTable selectedRow];
	NSDictionary *p;

	if( row < 0 || row >= (NSInteger)[players count] || !task )
		return;
	p = players[row];
	if( ![p[@"bot"] boolValue] )
	{
		NSAlert *a = [[NSAlert alloc] init];
		[a setMessageText: [NSString stringWithFormat: @"Kick %@?", p[@"name"]]];
		[a setInformativeText: @"They're disconnected from the server, but can join again."];
		[a addButtonWithTitle: @"Kick"];
		[a addButtonWithTitle: @"Cancel"];
		if( [[a buttons][0] respondsToSelector: @selector(setHasDestructiveAction:)] )
			[[a buttons][0] setHasDestructiveAction: YES];
		if( [a runModal] != NSAlertFirstButtonReturn )
			return;
	}
	[self run: [NSString stringWithFormat: @"clientkick %@", p[@"num"]]];
}

- (void)messagePlayers: (id)sender
{
	NSAlert *a = [[NSAlert alloc] init];
	NSTextField *f = [NSTextField textFieldWithString: @""];
	NSString *text;

	[f setFrame: NSMakeRect( 0, 0, 300, 24 )];
	[f setPlaceholderString: @"Message"];
	[a setMessageText: @"Message everyone on the server"];
	[a setInformativeText: @"Shown in the game's chat area as coming from the server."];
	[a setAccessoryView: f];
	[a addButtonWithTitle: @"Send"];
	[a addButtonWithTitle: @"Cancel"];
	[[a window] setInitialFirstResponder: f];
	if( [a runModal] != NSAlertFirstButtonReturn )
		return;
	// no quotes or semicolons: they would end the command
	text = [[[[f stringValue] stringByReplacingOccurrencesOfString: @"\"" withString: @"'"]
		stringByReplacingOccurrencesOfString: @";" withString: @","]
		stringByTrimmingCharactersInSet: [NSCharacterSet whitespaceAndNewlineCharacterSet]];
	if( [text length] > 150 )
		text = [text substringToIndex: 150];
	if( [text length] )
		[self run: [NSString stringWithFormat: @"say \"%@\"", text]];
}

- (void)send: (NSString *)cmd
{
	if( !task || ![task isRunning] )
		return;
	@try {
		[input writeData: [[cmd stringByAppendingString: @"\n"] dataUsingEncoding: NSUTF8StringEncoding]];
	} @catch( NSException *e ) {
		LLog( @"Server: couldn't send \"%@\": %@", cmd, [e reason] );
	}
}

- (void)poll: (NSTimer *)t
{
	// one at a time, unless an answer got lost
	if( autoStatus && lastPoll && -[lastPoll timeIntervalSinceNow] < 10 )
		return;
	autoStatus = 1;
	lastPoll = [NSDate date];
	[self send: @"status"];
}

- (void)received: (NSData *)d
{
	const char *b;
	NSUInteger start = 0, i, n;

	[pending appendData: d];
	b = [pending bytes];
	n = [pending length];
	for( i = 0; i < n; i++ )
		if( b[i] == '\n' )
		{
			NSData *lineData = [pending subdataWithRange: NSMakeRange( start, i - start )];
			NSString *line = TextFromData( lineData );
			[self handleLine: [line stringByTrimmingCharactersInSet: [NSCharacterSet characterSetWithCharactersInString: @"\r"]]];
			start = i + 1;
		}
	[pending replaceBytesInRange: NSMakeRange( 0, start ) withBytes: NULL length: 0];
}

// --- the router port mapping (NAT-PMP / PCP / UPnP through mDNSResponder) ---

static void NATCallback( DNSServiceRef ref, DNSServiceFlags flags, uint32_t ifIndex, DNSServiceErrorType err,
	uint32_t externalAddress, DNSServiceProtocol protocol, uint16_t internalPort, uint16_t externalPort,
	uint32_t ttl, void *context )
{
	[(__bridge VSPServerWindowController *)context natResult: err address: externalAddress port: ntohs( externalPort )];
}

// 10/8, 172.16/12, 192.168/16, 100.64/10 (carrier-grade NAT): not reachable from the internet
static BOOL PrivateIPv4( uint32_t a )
{
	return ( a >> 24 ) == 10 || ( a >> 20 ) == ( ( 172 << 4 ) | 1 ) || ( a >> 16 ) == ( ( 192 << 8 ) | 168 ) ||
		( a >> 22 ) == ( ( 100 << 2 ) | 1 );
}

- (void)openRouterPort
{
	DNSServiceErrorType err;

	if( natRef || natOpening )
		return;
	natOpening = YES;
	natProblem = nil;
	publicAddress = nil;
	LLog( @"Server: asking the router to forward UDP %ld", (long)port );
	if( getenv( "VOYAGERSP_TEST_NAT" ) )
	{
		NSString *fake = @( getenv( "VOYAGERSP_TEST_NAT" ) );
		dispatch_after( dispatch_time( DISPATCH_TIME_NOW, NSEC_PER_SEC ), dispatch_get_main_queue(), ^{
			struct in_addr in;
			if( [fake isEqualToString: @"fail"] )
				[self natResult: kDNSServiceErr_NATPortMappingUnsupported address: 0 port: 0];
			else if( inet_pton( AF_INET, [fake UTF8String], &in ) == 1 )
				[self natResult: kDNSServiceErr_NoError address: in.s_addr port: (uint16_t)self->port];
		} );
		[self showInternet];
		return;
	}
	// ttl 0: the default lease, renewed by mDNSResponder until deallocated
	err = DNSServiceNATPortMappingCreate( &natRef, 0, 0, kDNSServiceProtocol_UDP, htons( (uint16_t)port ),
		htons( (uint16_t)port ), 0, NATCallback, (__bridge void *)self );
	if( err == kDNSServiceErr_NoError )
		err = DNSServiceSetDispatchQueue( natRef, dispatch_get_main_queue() );
	if( err != kDNSServiceErr_NoError )
	{
		[self natResult: err address: 0 port: 0];
		return;
	}
	natTimer = [NSTimer timerWithTimeInterval: 15 repeats: NO block: ^( NSTimer *t ) {
		if( self->natOpening )
			[self natFailed: @"Your router didn't answer the request to open the port: it may not support automatic "
				@"port opening (NAT-PMP / UPnP), or has it turned off."];
	}];
	[[NSRunLoop currentRunLoop] addTimer: natTimer forMode: NSRunLoopCommonModes];
	[self showInternet];
}

- (void)closeRouterPort
{
	[natTimer invalidate];
	natTimer = nil;
	natOpening = NO;
	if( natRef )
	{
		DNSServiceRefDeallocate( natRef );		// removes the mapping
		natRef = NULL;
		LLog( @"Server: router port mapping removed" );
	}
	publicAddress = nil;
	[self showInternet];
}

- (void)natFailed: (NSString *)why
{
	natOpening = NO;
	[natTimer invalidate];
	natTimer = nil;
	natProblem = why;
	publicAddress = nil;
	LLog( @"Server: port not opened: %@", why );
	[self appendLog: [@"Router: " stringByAppendingString: why] color: [NSColor systemOrangeColor]];
	[self showInternet];
}

- (void)natResult: (DNSServiceErrorType)err address: (uint32_t)externalAddress port: (uint16_t)externalPort
{
	uint32_t a = ntohl( externalAddress );
	char buf[INET_ADDRSTRLEN];

	if( err == kDNSServiceErr_NATPortMappingUnsupported )
		[self natFailed: @"Your router doesn't support automatic port opening (NAT-PMP / UPnP), or has it turned off."];
	else if( err == kDNSServiceErr_NATPortMappingDisabled )
		[self natFailed: @"Automatic port opening (NAT-PMP / UPnP) is turned off in your router's settings."];
	else if( err == kDNSServiceErr_DoubleNAT )
		[self natFailed: @"Your network has two routers in a row (double NAT), so the outer one still blocks the port."];
	else if( err != kDNSServiceErr_NoError )
		[self natFailed: [NSString stringWithFormat: @"The router didn't open the port (error %d).", (int)err]];
	else if( !a )
		return;		// no answer yet (mDNSResponder reports again); the timer gives up
	else if( PrivateIPv4( a ) )
		[self natFailed: [NSString stringWithFormat: @"Your router opened the port, but its own internet address (%s) is private: "
			@"your provider or another router puts it behind a second NAT, which nobody outside can get through.",
			inet_ntop( AF_INET, &externalAddress, buf, sizeof( buf ) )]];
	else
	{
		natOpening = NO;
		[natTimer invalidate];
		natTimer = nil;
		natProblem = nil;
		publicAddress = @( inet_ntop( AF_INET, &externalAddress, buf, sizeof( buf ) ) );
		publicPort = externalPort ? externalPort : port;
		LLog( @"Server: the router forwards %@:%ld to this Mac", publicAddress, (long)publicPort );
		[self appendLog: [NSString stringWithFormat: @"Router: port opened, players on the internet connect to %@:%ld",
			publicAddress, (long)publicPort] color: [NSColor systemGreenColor]];
		[self showInternet];
	}
}

- (void)showInternet
{
	NSString *manual = [NSString stringWithFormat: @"Forward UDP port %ld to this Mac (%@) in your router's settings, "
		@"or use Tailscale (see the README).", (long)port, lanAddress ? lanAddress : @"this Mac's address"];
	NSFont *addressFont = [NSFont monospacedDigitSystemFontOfSize: 15 weight: NSFontWeightSemibold];
	NSString *symbol = nil, *value, *hint;
	NSColor *tint = [NSColor secondaryLabelColor];

	if( !internetValue || !settings.internet )
		return;
	if( publicAddress )
	{
		value = [NSString stringWithFormat: @"%@:%ld", publicAddress, (long)publicPort];
		hint = @"Your router forwards this address to the server while it runs. Friends on the internet connect to it, "
			@"or find the server in Holomatch's internet list.";
		symbol = @"checkmark.circle.fill";
		tint = [NSColor systemGreenColor];
	}
	else if( natOpening )
	{
		value = @"Opening the port on your router…";
		hint = @"";
		symbol = @"arrow.triangle.2.circlepath";
	}
	else if( natProblem )
	{
		value = @"Port not opened";
		hint = [NSString stringWithFormat: @"%@ %@", natProblem, manual];
		symbol = @"exclamationmark.triangle.fill";
		tint = [NSColor systemOrangeColor];
	}
	else if( !settings.openPort )
	{
		value = @"Your public address";
		hint = [NSString stringWithFormat: @"Automatic port opening is off. %@", manual];
	}
	else
	{
		value = task ? @"Waiting for the server…" : @"—";
		hint = @"";
	}
	[internetValue setFont: publicAddress ? addressFont : [NSFont systemFontOfSize: 13]];
	[internetValue setStringValue: value];
	[internetIcon setImage: symbol ? [NSImage imageWithSystemSymbolName: symbol accessibilityDescription: nil] : nil];
	[internetIcon setHidden: !symbol];
	[internetIcon setContentTintColor: tint];
	[copyInternetButton setHidden: !publicAddress];
	[internetHint setStringValue: hint];
	[internetHint setHidden: ![hint length]];
	[internetHint setTextColor: natProblem ? [NSColor systemOrangeColor] : [NSColor secondaryLabelColor]];
}

// --- is the server on the internet server list? ---
// A master only lists a server after it reached it from the internet (it answers the
// heartbeat with a getinfo to the public address), so being listed proves the port is
// open. The check asks the masters for their list, like Holomatch's browser does.

#define LIST_FIRST_CHECK	( TestEnvSet( "VOYAGERSP_TEST_LIST_FAST" ) ? 2 : 8 )		// s after the server is ready
#define LIST_INTERVAL		( TestEnvSet( "VOYAGERSP_TEST_LIST_FAST" ) ? 2 : 20 )	// s between checks
#define LIST_MAX_CHECKS		( TestEnvSet( "VOYAGERSP_TEST_LIST_FAST" ) ? 2 : 9 )		// then give up (about 3 minutes)


static int HexNibble( unsigned char c )
{
	return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

// Entries of a getserversResponse, as "a.b.c.d:port". Two encodings exist: Quake 3's
// "\\" + 4 address bytes + 2 port bytes, and the Elite Force master's older text form,
// "\\" + 12 hex digits (efmaster.tjps.eu uses this). Both end with "\\EOT".
static void ParseServerList( const unsigned char *buf, ssize_t n, NSMutableSet *list )
{
	const unsigned char *p = memchr( buf, '\\', n ), *end = buf + n;
	BOOL hex;
	int i;

	if( !p )
		return;
	// text form: 12 hex digits, then the next separator
	hex = p + 14 <= end && p[13] == '\\';
	for( i = 1; hex && i <= 12; i++ )
		hex = HexNibble( p[i] ) >= 0;
	while( p && p + 7 <= end && memcmp( p + 1, "EOT", 3 ) )
	{
		unsigned v[6];
		if( hex )
		{
			if( p + 13 > end )
				break;
			for( i = 0; i < 6; i++ )
				v[i] = HexNibble( p[1 + 2*i] ) << 4 | HexNibble( p[2 + 2*i] );
			p += 13;
		}
		else
		{
			for( i = 0; i < 6; i++ )
				v[i] = p[1 + i];
			p += 7;
		}
		[list addObject: [NSString stringWithFormat: @"%u.%u.%u.%u:%u", v[0], v[1], v[2], v[3], v[4] << 8 | v[5]]];
		if( p < end && *p != '\\' )
			p = memchr( p, '\\', end - p );	// tolerate stray bytes
	}
}

// Ask one master ("host", "host:port" or "[v6]:port") for its protocol-24 list
// (blocking, a few seconds); nil if it didn't answer.
static NSSet<NSString *> *QueryMaster( NSString *master )
{
	struct addrinfo hints = { 0 }, *res = NULL;
	struct timeval tv = { 2, 0 };
	NSMutableSet *list = nil;
	unsigned char buf[16384];
	static const char q[] = "\xff\xff\xff\xffgetservers 24 full empty";
	NSString *host = master, *port = @"27953";
	NSRange colon = [master rangeOfString: @":" options: NSBackwardsSearch];
	int fd;

	if( [master hasPrefix: @"["] )
	{
		NSRange close = [master rangeOfString: @"]"];
		if( close.location == NSNotFound )
			return nil;
		host = [master substringWithRange: NSMakeRange( 1, close.location - 1 )];
		if( [master length] > NSMaxRange( close ) + 1 && [master characterAtIndex: NSMaxRange( close )] == ':' )
			port = [master substringFromIndex: NSMaxRange( close ) + 1];
	}
	else if( colon.location != NSNotFound && [master rangeOfString: @":"].location == colon.location )
	{
		host = [master substringToIndex: colon.location];
		port = [master substringFromIndex: colon.location + 1];
	}
	hints.ai_family = AF_UNSPEC;	// ipv6.tjps.eu has only an IPv6 address
	hints.ai_socktype = SOCK_DGRAM;
	if( getaddrinfo( [host UTF8String], [port UTF8String], &hints, &res ) || !res )
		return nil;
	if( ( fd = socket( res->ai_family, SOCK_DGRAM, 0 ) ) < 0 )
	{
		freeaddrinfo( res );
		return nil;
	}
	setsockopt( fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof( tv ) );
	sendto( fd, q, sizeof( q ) - 1, 0, res->ai_addr, res->ai_addrlen );
	freeaddrinfo( res );
	for( ;; )
	{
		ssize_t n = recv( fd, buf, sizeof( buf ), 0 ), i;
		if( n <= 0 )
			break;		// timeout: the list is complete
		if( !list )
			list = [NSMutableSet set];
		ParseServerList( buf, n, list );
	}
	close( fd );
	return list;
}

static void AddrCallback( DNSServiceRef ref, DNSServiceFlags flags, uint32_t ifIndex, DNSServiceErrorType err,
	uint32_t externalAddress, DNSServiceProtocol protocol, uint16_t internalPort, uint16_t externalPort,
	uint32_t ttl, void *context )
{
	if( err == kDNSServiceErr_NoError && externalAddress )
		[(__bridge VSPServerWindowController *)context foundPublicAddress: externalAddress];
}

- (void)foundPublicAddress: (uint32_t)externalAddress
{
	char buf[INET_ADDRSTRLEN];
	lookupAddress = @( inet_ntop( AF_INET, &externalAddress, buf, sizeof( buf ) ) );
}

- (NSString *)publicIP	{ return publicAddress ? publicAddress : lookupAddress; }

- (void)startListCheck
{
	[self stopListCheck];
	listChecks = 0;
	listFound = listMasterDown = NO;
	listedOn = nil;
	listChecking = YES;
	// the public address, to find in the lists (the port mapping reports it too)
	if( !addrRef && DNSServiceNATPortMappingCreate( &addrRef, 0, 0, 0, 0, 0, 0, AddrCallback, (__bridge void *)self ) == kDNSServiceErr_NoError )
		DNSServiceSetDispatchQueue( addrRef, dispatch_get_main_queue() );
	listTimer = [NSTimer timerWithTimeInterval: LIST_FIRST_CHECK repeats: NO block: ^( NSTimer *t ) { [self checkList]; }];
	[[NSRunLoop currentRunLoop] addTimer: listTimer forMode: NSRunLoopCommonModes];
	[self showList];
}

- (void)stopListCheck
{
	[listTimer invalidate];
	listTimer = nil;
	listChecking = NO;
	if( addrRef )
	{
		DNSServiceRefDeallocate( addrRef );
		addrRef = NULL;
	}
	[self showList];
}

- (void)checkList
{
	NSString *ip = [self publicIP];
	NSString *want = ip ? [NSString stringWithFormat: @"%@:%ld", ip, (long)( publicAddress ? publicPort : port )] : nil;
	NSString *wantIP = ip ? [ip stringByAppendingString: @":"] : nil;

	listTimer = nil;
	if( !task || !listChecking )
		return;
	listChecks++;
	dispatch_async( dispatch_get_global_queue( QOS_CLASS_UTILITY, 0 ), ^{
		NSString *found = nil;
		BOOL anyAnswer = NO;
		for( NSString *m in VSP_MasterServers() )
		{
			NSSet *list = QueryMaster( m );
			if( !list )
				continue;
			anyAnswer = YES;
			for( NSString *e in list )
				if( want && ( [e isEqualToString: want] || [e hasPrefix: wantIP] ) )
					found = m;
			if( found )
				break;
		}
		dispatch_async( dispatch_get_main_queue(), ^{ [self listResult: found anyAnswer: anyAnswer]; } );
	} );
}

- (void)listResult: (NSString *)master anyAnswer: (BOOL)anyAnswer
{
	if( !task || !listChecking )
		return;
	LLog( @"Server: internet list check %d: %@", listChecks, master ? [@"listed on " stringByAppendingString: master] :
		anyAnswer ? @"not listed" : @"no master answered" );
	if( master )
	{
		listFound = YES;
		listedOn = master;
		listChecking = NO;
		[self appendLog: [NSString stringWithFormat: @"Internet: listed on %@, so players anywhere can reach it.", master]
			color: [NSColor systemGreenColor]];
		[self showList];
		return;
	}
	listMasterDown = !anyAnswer;
	if( listChecks >= LIST_MAX_CHECKS )
	{
		listChecking = NO;
		[self appendLog: listMasterDown ? @"Internet: no master server answered, so the listing couldn't be checked." :
			@"Internet: not on the server list. The master server couldn't reach this server from the internet."
			color: [NSColor systemOrangeColor]];
		[self showList];
		return;
	}
	listTimer = [NSTimer timerWithTimeInterval: LIST_INTERVAL repeats: NO block: ^( NSTimer *t ) { [self checkList]; }];
	[[NSRunLoop currentRunLoop] addTimer: listTimer forMode: NSRunLoopCommonModes];
	[self showList];
}

- (void)checkListAgain: (id)sender
{
	if( !task )
		return;
	[self send: @"heartbeat"];		// announce again, then look
	[self startListCheck];
}

- (void)showList
{
	NSString *symbol = nil, *value, *hint = @"";
	NSColor *tint = [NSColor secondaryLabelColor];

	if( !listValue || !settings.internet )
		return;
	if( listFound )
	{
		value = [NSString stringWithFormat: @"Yes, on %@", listedOn];
		NSUInteger slot = [VSP_MasterServers() indexOfObject: listedOn];
		hint = [NSString stringWithFormat: @"The master server reached your server from the internet: players anywhere "
			@"find it in Holomatch's internet list (%@). This Mac itself won't see it there when "
			@"your router doesn't loop its public address back: use Play on This Mac.",
			slot < 5 ? [NSString stringWithFormat: @"Internet %lu for this master", (unsigned long)slot + 1] :
			@"Internet 1 to 5 for the masters that list it"];
		symbol = @"checkmark.circle.fill";
		tint = [NSColor systemGreenColor];
	}
	else if( listChecking )
	{
		value = listChecks ? [NSString stringWithFormat: @"Not yet, checking again… (%d of %d)", listChecks, LIST_MAX_CHECKS] :
			@"Checking the internet server list…";
		hint = @"Your Mac can't test its own public address (most routers don't loop it back), so this asks the "
			@"master server whether it reached your server.";
		symbol = @"arrow.triangle.2.circlepath";
	}
	else if( listChecks && listMasterDown )
	{
		value = @"Couldn't check";
		hint = @"No master server answered. Players can still connect by your public address if the port is open.";
		symbol = @"questionmark.circle";
	}
	else if( listChecks )
	{
		value = @"Not listed";
		hint = [NSString stringWithFormat: @"The master server couldn't reach your server, so players outside your network "
			@"can't either%@. Forward UDP %ld to %@ in your router's settings, or use Tailscale (see the README).",
			publicAddress ? @" (your router accepted the port request but doesn't forward it)" : @"",
			(long)port, lanAddress ? lanAddress : @"this Mac"];
		symbol = @"exclamationmark.triangle.fill";
		tint = [NSColor systemOrangeColor];
	}
	else
		value = task ? @"Waiting for the server…" : @"—";
	[listValue setStringValue: value];
	[listIcon setImage: symbol ? [NSImage imageWithSystemSymbolName: symbol accessibilityDescription: nil] : nil];
	[listIcon setHidden: !symbol];
	[listIcon setContentTintColor: tint];
	[checkAgainButton setHidden: !task || listChecking || listFound];
	[listHint setStringValue: hint];
	[listHint setHidden: ![hint length]];
	[listHint setTextColor: ( listChecks && !listFound && !listChecking && !listMasterDown ) ?
		[NSColor systemOrangeColor] : [NSColor secondaryLabelColor]];
}

- (void)dealloc
{
	if( natRef )
		DNSServiceRefDeallocate( natRef );
	if( addrRef )
		DNSServiceRefDeallocate( addrRef );
}

- (void)becameReady
{
	if( readyLogged )
		return;
	readyLogged = YES;
	LLog( @"Server: ready on %@ (map %@)", [self addressString], currentMap );
	if( settings.internet && settings.openPort )
		[self openRouterPort];
	if( settings.internet )
		[self startListCheck];
	if( getenv( "VOYAGERSP_TEST_SERVER_ACTIONS" ) )
	{
		NSArray *actions = [@( getenv( "VOYAGERSP_TEST_SERVER_ACTIONS" ) ) componentsSeparatedByString: @","];
		[actions enumerateObjectsUsingBlock: ^( NSString *act, NSUInteger i, BOOL *stop ) {
			dispatch_after( dispatch_time( DISPATCH_TIME_NOW, (int64_t)( 4 * ( i + 1 ) ) * NSEC_PER_SEC ), dispatch_get_main_queue(), ^{
				NSMenuItem *item = [[NSMenuItem alloc] init];
				LLog( @"[test] control: %@", act );
				if( [act isEqualToString: @"next"] )				[self nextMap: nil];
				else if( [act isEqualToString: @"restart"] )		[self restartMap: nil];
				else if( [act isEqualToString: @"addbot"] )			[self addBot: nil];
				else if( [act isEqualToString: @"removebots"] )		[self removeBots: nil];
				else if( [act hasPrefix: @"slot"] )
				{
					NSInteger n = [[act substringFromIndex: 4] integerValue];
					[item setRepresentedObject: @{ @"map": self->settings.rotation[n - 1], @"slot": @( n ) }];
					[self changeMap: item];
				}
				else if( [act hasPrefix: @"map:"] )
				{
					[item setRepresentedObject: @{ @"map": [act substringFromIndex: 4] }];
					[self changeMap: item];
				}
				else if( [act hasPrefix: @"kick"] )
				{
					[self->playerTable selectRowIndexes: [NSIndexSet indexSetWithIndex: [[act substringFromIndex: 4] integerValue]]
						byExtendingSelection: NO];
					[self kickPlayer: nil];
				}
			} );
		}];
	}
	if( getenv( "VOYAGERSP_TEST_SERVER_COMMAND" ) )
	{
		NSString *c = @( getenv( "VOYAGERSP_TEST_SERVER_COMMAND" ) );
		[self appendLog: [@"] " stringByAppendingString: c] color: [NSColor controlAccentColor]];
		if( [[c lowercaseString] isEqualToString: @"status"] )
			userStatus++;
		[self send: c];
	}
}

- (void)endStatus
{
	inStatus = NO;
	players = [statusRows copy];
	[statusRows removeAllObjects];
	if( state == SRV_STARTING || state == SRV_IDLE )
		[self showState: SRV_RUNNING detail: nil];
	[self becameReady];
	[playerTable reloadData];
	[self showInfo];
	if( echo )
		LLog( @"[test] status: map %@, %lu players: %@", currentMap, (unsigned long)[players count],
			[[players valueForKey: @"name"] componentsJoinedByString: @", "] );
}

- (void)handleLine: (NSString *)raw
{
	NSString *line = StripColors( raw );
	BOOL show = YES;
	NSTextCheckingResult *m;
	static NSRegularExpression *rowRE, *readyRE, *portRE, *tailRE;

	if( !rowRE )
	{
		rowRE = [NSRegularExpression regularExpressionWithPattern: @"^\\s*(\\d+)\\s+(-?\\d+)\\s+(\\d+|CON|ZMB)\\s(.*)$" options: 0 error: NULL];
		readyRE = [NSRegularExpression regularExpressionWithPattern: @"^Server ready: map (\\S+) port (\\d+)" options: 0 error: NULL];
		portRE = [NSRegularExpression regularExpressionWithPattern: @"^Opening IP socket: [^ ]*:(\\d+)" options: 0 error: NULL];
		// what follows the cl/score/ping columns: name (may contain spaces), address ("bot"), rate
		tailRE = [NSRegularExpression regularExpressionWithPattern: @"^(.*?)\\s+(\\S+)\\s+(-?\\d+)\\s*$" options: 0 error: NULL];
	}
	if( echo )
	{
		printf( "[server] %s\n", [line UTF8String] );
		fflush( stdout );
	}

	if( inStatus )
	{
		show = statusShown;
		if( ![[line stringByTrimmingCharactersInSet: [NSCharacterSet whitespaceCharacterSet]] length] )
			[self endStatus];
		else if( ( m = [rowRE firstMatchInString: raw options: 0 range: NSMakeRange( 0, [raw length] )] ) )
		{
			// the headless server strips colour codes, so the "^7" ending the name may be gone
			NSString *rest = [[raw substringWithRange: [m rangeAtIndex: 4]] stringByReplacingOccurrencesOfString: @"^7" withString: @" "];
			NSTextCheckingResult *t = [tailRE firstMatchInString: rest options: 0 range: NSMakeRange( 0, [rest length] )];
			NSString *name = t ? [rest substringWithRange: [t rangeAtIndex: 1]] : rest;
			NSString *addr = t ? [rest substringWithRange: [t rangeAtIndex: 2]] : @"";
			BOOL bot = [addr isEqualToString: @"bot"];
			NSString *ping = [raw substringWithRange: [m rangeAtIndex: 3]];

			[statusRows addObject: @{ @"name": [StripColors( name ) stringByTrimmingCharactersInSet: [NSCharacterSet whitespaceCharacterSet]],
				@"score": [raw substringWithRange: [m rangeAtIndex: 2]],
				@"ping": bot ? @"bot" : [ping isEqualToString: @"CON"] ? @"…" : ping,
				@"bot": @( bot ), @"address": addr ? addr : @"",
				@"num": [raw substringWithRange: [m rangeAtIndex: 1]] }];
		}
	}
	else if( [line hasPrefix: @"map: "] )
	{
		inStatus = YES;
		statusShown = userStatus > 0 || !autoStatus;
		if( userStatus > 0 )
			userStatus--;
		else if( autoStatus > 0 )
			autoStatus--;
		show = statusShown;
		currentMap = [[line substringFromIndex: 5] stringByTrimmingCharactersInSet: [NSCharacterSet whitespaceCharacterSet]];
		[statusRows removeAllObjects];
	}
	else if( [line isEqualToString: @"Server is not running."] && autoStatus > 0 && !userStatus )
	{
		autoStatus--;
		show = NO;
		if( state == SRV_RUNNING )
		{
			currentMap = nil;
			players = @[];
			[playerTable reloadData];
			[self showState: SRV_IDLE detail: nil];
			[self showInfo];
		}
	}
	else if( ( m = [readyRE firstMatchInString: line options: 0 range: NSMakeRange( 0, [line length] )] ) )
	{
		currentMap = [line substringWithRange: [m rangeAtIndex: 1]];
		port = [[line substringWithRange: [m rangeAtIndex: 2]] integerValue];
		if( port != settings.port )
			[self appendLog: [NSString stringWithFormat: @"Port %ld is in use; the server took %ld.", (long)settings.port, (long)port]
				color: [NSColor systemOrangeColor]];
		[self showState: SRV_RUNNING detail: nil];
		[self becameReady];
		[self showInfo];
	}
	else if( ( m = [portRE firstMatchInString: line options: 0 range: NSMakeRange( 0, [line length] )] ) )
	{
		port = [[line substringWithRange: [m rangeAtIndex: 1]] integerValue];
		[self showInfo];
	}
	// the dedicated console's prompt
	if( show && ( [line isEqualToString: @"]"] || [line isEqualToString: @"] "] ) )
		show = NO;
	if( show )
		[self appendLog: line color: [line hasPrefix: @"WARNING"] ? [NSColor systemOrangeColor] :
			( [line hasPrefix: @"ERROR"] || [line rangeOfString: @"Error" ].location == 0 ) ? [NSColor systemRedColor] : nil];
}

- (void)taskEnded
{
	int code = [task terminationStatus];
	BOOL signaled = [task terminationReason] == NSTaskTerminationReasonUncaughtSignal;

	if( !task )
		return;
	[pending appendBytes: "\n" length: 1];
	[self received: [NSData data]];
	LLog( @"Server: exited (%s %d)", signaled ? "signal" : "code", code );
	serverPid = 0;
	task = nil;
	[self closeRouterPort];
	[self stopListCheck];
	[pollTimer invalidate];
	[killTimer invalidate];
	pollTimer = killTimer = nil;
	currentMap = nil;
	players = @[];
	[playerTable reloadData];
	if( state == SRV_STOPPING )
	{
		[self showState: SRV_STOPPED detail: nil];
		[self appendLog: @"The server stopped." color: [NSColor secondaryLabelColor]];
		[self endModal: afterStop];
		return;
	}
	[self appendLog: signaled ? [NSString stringWithFormat: @"The server crashed (signal %d).", code] :
		code == 1 && !readyLogged ? @"The server found no Elite Force game files (exit code 1)." :
		code == 3 && !readyLogged ? @"The server couldn't use the game folder (exit code 3)." :
		[NSString stringWithFormat: @"The server quit (exit code %d).", code] color: [NSColor systemRedColor]];
	[self showState: SRV_FAILED detail: readyLogged ? @"the server quit unexpectedly" :
		code == 1 ? @"no game files" : code == 3 ? @"the game folder can't be used" : @"the server didn't start, see the console below"];
	[self showInfo];
}

- (void)endModal: (NSModalResponse)r
{
	[NSApp stopModalWithCode: r];
	// stopModal from outside an event: wake the modal loop up
	[NSApp postEvent: [NSEvent otherEventWithType: NSEventTypeApplicationDefined location: NSZeroPoint modifierFlags: 0
		timestamp: 0 windowNumber: 0 context: nil subtype: 0 data1: 0 data2: 0] atStart: YES];
}

// Stop (quit, then SIGTERM, then SIGKILL), then end with r.
- (void)stopThen: (NSModalResponse)r
{
	pid_t pid;

	afterStop = r;
	if( !task || ![task isRunning] )
	{
		[self endModal: r];
		return;
	}
	if( state == SRV_STOPPING )
		return;
	[self showState: SRV_STOPPING detail: nil];
	[self appendLog: @"] quit" color: [NSColor controlAccentColor]];
	[self send: @"quit"];
	pid = [task processIdentifier];
	killTimer = [NSTimer timerWithTimeInterval: 5 repeats: NO block: ^( NSTimer *t ) {
		if( !self->task || ![self->task isRunning] )
			return;
		LLog( @"Server: no exit after quit, sending SIGTERM" );
		[self->task terminate];
		self->killTimer = [NSTimer timerWithTimeInterval: 3 repeats: NO block: ^( NSTimer *t2 ) {
			if( self->task && [self->task isRunning] && [self->task processIdentifier] == pid )
			{
				LLog( @"Server: still running, sending SIGKILL" );
				kill( pid, SIGKILL );
			}
		}];
		[[NSRunLoop currentRunLoop] addTimer: self->killTimer forMode: NSRunLoopCommonModes];
	}];
	[[NSRunLoop currentRunLoop] addTimer: killTimer forMode: NSRunLoopCommonModes];
}

// --- actions ---

- (void)stop: (id)sender
{
	[self stopThen: NSModalResponseStop];
}

- (BOOL)windowShouldClose: (NSWindow *)sender
{
	[self stopThen: NSModalResponseStop];
	return NO;
}

- (void)copyPublicAddress: (id)sender
{
	if( !publicAddress )
		return;
	[[NSPasteboard generalPasteboard] clearContents];
	[[NSPasteboard generalPasteboard] setString: [NSString stringWithFormat: @"%@:%ld", publicAddress, (long)publicPort]
		forType: NSPasteboardTypeString];
}

- (void)copyAddress: (id)sender
{
	[[NSPasteboard generalPasteboard] clearContents];
	[[NSPasteboard generalPasteboard] setString: [self addressString] forType: NSPasteboardTypeString];
}

- (void)sendCommand: (id)sender
{
	NSString *c = [[command stringValue] stringByTrimmingCharactersInSet: [NSCharacterSet whitespaceCharacterSet]];

	if( ![c length] || !task )
		return;
	if( [[c lowercaseString] isEqualToString: @"quit"] )
	{
		[command setStringValue: @""];
		[self stopThen: NSModalResponseStop];
		return;
	}
	[self appendLog: [@"] " stringByAppendingString: c] color: [NSColor controlAccentColor]];
	if( [[c lowercaseString] isEqualToString: @"status"] )
		userStatus++;
	[self send: c];
	[command setStringValue: @""];
}

- (void)playHere: (id)sender
{
	NSWorkspaceOpenConfiguration *cfg = [NSWorkspaceOpenConfiguration configuration];
	NSMutableArray *args = [NSMutableArray arrayWithArray: clientArgs];

	if( [settings.password length] )
		[args addObjectsFromArray: @[ @"+set", @"password", settings.password ]];
	[args addObjectsFromArray: @[ @"+connect", [NSString stringWithFormat: @"127.0.0.1:%ld", (long)port] ]];
	[cfg setCreatesNewApplicationInstance: YES];
	[cfg setArguments: args];
	[cfg setEnvironment: ChildEnvironment()];
	[cfg setActivates: YES];
	LLog( @"Server: Play on This Mac: %@", [args componentsJoinedByString: @" "] );
	[self appendLog: @"Starting Elite Force to join this server…" color: [NSColor secondaryLabelColor]];
	[[NSWorkspace sharedWorkspace] openApplicationAtURL: [[NSBundle mainBundle] bundleURL] configuration: cfg
		completionHandler: ^( NSRunningApplication *app, NSError *error ) {
		dispatch_async( dispatch_get_main_queue(), ^{
			if( error )
				[self appendLog: [NSString stringWithFormat: @"Couldn't start the game: %@", [error localizedDescription]]
					color: [NSColor systemRedColor]];
			else
				LLog( @"Server: Play on This Mac started pid %d", [app processIdentifier] );
		} );
	}];
}

// --- the players table ---

- (NSInteger)numberOfRowsInTableView: (NSTableView *)t	{ return (NSInteger)[players count]; }

- (NSView *)tableView: (NSTableView *)t viewForTableColumn: (NSTableColumn *)col row: (NSInteger)row
{
	NSString *ident = [col identifier];
	NSTextField *l = [t makeViewWithIdentifier: ident owner: self];
	NSDictionary *p = players[row];

	if( !l )
	{
		l = VSP_Label( @"", [NSFont monospacedDigitSystemFontOfSize: 12 weight: NSFontWeightRegular], nil );
		[l setIdentifier: ident];
		[l setLineBreakMode: NSLineBreakByTruncatingTail];
		[l setAlignment: [ident isEqualToString: @"name"] ? NSTextAlignmentLeft : NSTextAlignmentRight];
	}
	[l setStringValue: p[ident]];
	[l setTextColor: [ident isEqualToString: @"ping"] && [p[@"bot"] boolValue] ? [NSColor tertiaryLabelColor] : [NSColor labelColor]];
	if( [ident isEqualToString: @"name"] )
		[l setToolTip: [p[@"bot"] boolValue] ? @"Bot" : p[@"address"]];
	return l;
}

// --- running it ---

- (NSModalResponse)run
{
	BOOL test = TestEnvSet( "VOYAGERSP_TEST_SERVER_START" );
	NSModalResponse r;
	id monitor, menu;
	__weak VSPServerWindowController *weakSelf = self;

	[_window center];
	if( test )
		[_window setFrameOrigin: NSMakePoint( -20000, -20000 )];		// off screen
	else
		[NSApp activateIgnoringOtherApps: YES];
	[_window makeKeyAndOrderFront: nil];

	monitor = [NSEvent addLocalMonitorForEventsMatchingMask: NSEventMaskKeyDown handler: ^NSEvent *( NSEvent *e ) {
		if( ( [e modifierFlags] & NSEventModifierFlagCommand ) && [[e charactersIgnoringModifiers] isEqualToString: @"q"] )
		{
			[weakSelf stopThen: NSModalResponseCancel];
			return nil;
		}
		return e;
	}];
	if( getenv( "VOYAGERSP_TEST_SERVER_SNAPSHOT" ) )
	{
		NSString *path = @( getenv( "VOYAGERSP_TEST_SERVER_SNAPSHOT" ) );
		double after = getenv( "VOYAGERSP_TEST_SERVER_SNAPSHOT_AFTER" ) ? atof( getenv( "VOYAGERSP_TEST_SERVER_SNAPSHOT_AFTER" ) ) : 15;
		NSTimer *t = [NSTimer timerWithTimeInterval: after repeats: NO block: ^( NSTimer *timer ) {
			VSP_SnapshotWindow( [weakSelf window], [path fileSystemRepresentation] );
		}];
		[[NSRunLoop currentRunLoop] addTimer: t forMode: NSRunLoopCommonModes];
	}
	if( getenv( "VOYAGERSP_TEST_SERVER_PLAY_AFTER" ) )
	{
		NSTimer *t = [NSTimer timerWithTimeInterval: atof( getenv( "VOYAGERSP_TEST_SERVER_PLAY_AFTER" ) ) repeats: NO block: ^( NSTimer *timer ) {
			LLog( @"[test] pressing Play on This Mac" );
			[weakSelf playHere: nil];
		}];
		[[NSRunLoop currentRunLoop] addTimer: t forMode: NSRunLoopCommonModes];
	}
	if( getenv( "VOYAGERSP_TEST_SERVER_STOP_AFTER" ) )
	{
		NSTimer *t = [NSTimer timerWithTimeInterval: atof( getenv( "VOYAGERSP_TEST_SERVER_STOP_AFTER" ) ) repeats: NO block: ^( NSTimer *timer ) {
			LLog( @"[test] pressing Stop Server" );
			[weakSelf stop: nil];
		}];
		[[NSRunLoop currentRunLoop] addTimer: t forMode: NSRunLoopCommonModes];
	}

	menu = VSP_BeginModalMenu( ^{ [weakSelf stopThen: NSModalResponseCancel]; } );
	[self start];
	r = [NSApp runModalForWindow: _window];
	VSP_EndModalMenu( menu );
	[NSEvent removeMonitor: monitor];
	[_window orderOut: nil];
	return r;
}

@end

NSModalResponse VSP_RunServerWindow( VSPServerSettings *settings, NSString *basepath, NSString *dataFolder,
	NSArray<NSString *> *clientArgs )
{
	@autoreleasepool {
		VSPServerWindowController *c = [[VSPServerWindowController alloc] initWithSettings: settings basepath: basepath
			dataFolder: dataFolder clientArgs: clientArgs];
		NSModalResponse r = [c run];
		LLog( @"Server: window closed, %@", r == NSModalResponseStop ? @"back to the launcher" : @"quitting" );
		return r;
	}
}

/* ------------------------------------------------------------------------
   Master servers and the Servers… sheet
   ------------------------------------------------------------------------ */

#define KEY_MASTERS		@"MasterServers"
#define ROW_MASTER		@"org.voyagersp.master-row"
#define MASTER_SLOTS	5		// Holomatch's Join menu: Internet 1-5

NSArray<NSString *> *VSP_DefaultMasters( void )
{
	// as EF_DEFAULT_MASTERS (qcommon.h): the live community masters first
	return @[ @"master.stvef.org", @"efmaster.tjps.eu", @"master.stef1.daggolin.de",
		@"master.stef1.ravensoft.com", @"ipv6.tjps.eu" ];
}

NSArray<NSString *> *VSP_OriginalMasters( void )
{
	// what this port shipped before: Internet 1 Raven's (gone), Internet 2 tjps, 3-5 blank
	return @[ @"master.stef1.ravensoft.com", @"efmaster.tjps.eu" ];
}

// A master's address as typed: host, host:port or [IPv6]:port, lowercased;
// nothing that could break out of the cfg file's quotes.
static NSString *CleanMaster( NSString *text )
{
	NSMutableString *s = [NSMutableString string];
	NSCharacterSet *ok = [NSCharacterSet characterSetWithCharactersInString:
		@"abcdefghijklmnopqrstuvwxyz0123456789.-:[]_"];

	text = [[text stringByTrimmingCharactersInSet: [NSCharacterSet whitespaceCharacterSet]] lowercaseString];
	for( NSUInteger i = 0; i < [text length] && [s length] < 64; i++ )
		if( [ok characterIsMember: [text characterAtIndex: i]] )
			[s appendFormat: @"%C", [text characterAtIndex: i]];
	return s;
}

// Cleaned, without blanks or repeats, at most VSP_MAX_MASTERS.
static NSArray<NSString *> *CleanMasters( NSArray *masters )
{
	NSMutableArray *list = [NSMutableArray array];
	for( id m in masters )
	{
		NSString *c = [m isKindOfClass: [NSString class]] ? CleanMaster( m ) : @"";
		if( [c length] && ![list containsObject: c] && [list count] < VSP_MAX_MASTERS )
			[list addObject: c];
	}
	return list;
}

NSArray<NSString *> *VSP_MasterServers( void )
{
	id saved = [[NSUserDefaults standardUserDefaults] objectForKey: KEY_MASTERS];
	return [saved isKindOfClass: [NSArray class]] ? CleanMasters( saved ) : VSP_DefaultMasters();
}

void VSP_SetMasterServers( NSArray<NSString *> *masters )
{
	if( !masters || [CleanMasters( masters ) isEqualToArray: VSP_DefaultMasters()] )
		[[NSUserDefaults standardUserDefaults] removeObjectForKey: KEY_MASTERS];	// follow future defaults
	else
		[[NSUserDefaults standardUserDefaults] setObject: CleanMasters( masters ) forKey: KEY_MASTERS];
	LLog( @"Master servers: %@", [VSP_MasterServers() componentsJoinedByString: @", "] );
}

NSString *VSP_MastersCfgText( BOOL blank )
{
	NSArray *list = blank ? @[] : VSP_MasterServers();
	NSMutableString *cfg = [NSMutableString string];
	for( NSUInteger i = 0; i < VSP_MAX_MASTERS; i++ )
		[cfg appendFormat: @"set sv_master%lu \"%@\"\n", (unsigned long)i + 1, i < [list count] ? list[i] : @""];
	return cfg;
}

BOOL VSP_WriteMastersConfig( void )
{
	NSString *path = [VSP_HomeBase() stringByAppendingPathComponent: VSP_MASTERS_CFG];
	NSString *cfg = [NSString stringWithFormat: @"// Written by the Elite Force launcher at each start: its Servers… list.\n"
		@"// sv_master1-5 are Holomatch's Internet 1-5. Edits here are overwritten.\n\n%@", VSP_MastersCfgText( NO )];

	[[NSFileManager defaultManager] createDirectoryAtPath: VSP_HomeBase() withIntermediateDirectories: YES attributes: nil error: NULL];
	if( [cfg writeToFile: path atomically: YES encoding: NSUTF8StringEncoding error: NULL] )
		return YES;
	LLog( @"Couldn't write %@", path );
	return NO;
}

NSInteger VSP_QueryMasterCount( NSString *master )
{
	NSSet *list = QueryMaster( master );
	return list ? (NSInteger)[list count] : -1;
}

// One row: the browser source, the address (editable) and whether it answers.
@interface VSPMasterCell : NSTableCellView
@property (nonatomic, readonly) NSTextField *slot, *status;
@property (nonatomic, readonly) NSImageView *statusIcon;
@end

@implementation VSPMasterCell
- (instancetype)initWithOwner: (id<NSTextFieldDelegate>)owner
{
	NSTextField *host;
	NSStackView *row;

	if( !( self = [super initWithFrame: NSZeroRect] ) )
		return nil;
	[self setIdentifier: @"master"];
	_slot = VSP_Label( @"", [NSFont systemFontOfSize: 12], [NSColor secondaryLabelColor] );
	[[_slot widthAnchor] constraintEqualToConstant: 92].active = YES;
	host = [NSTextField textFieldWithString: @""];
	[host setPlaceholderString: @"host or host:port"];
	[host setFont: [NSFont systemFontOfSize: 13 weight: NSFontWeightMedium]];
	[host setBordered: NO];
	[host setDrawsBackground: NO];
	[host setFocusRingType: NSFocusRingTypeNone];
	[host setLineBreakMode: NSLineBreakByTruncatingTail];
	[host setDelegate: owner];
	[host setEditable: NO];		// a click selects or drags the row; double-click edits
	[host setSelectable: NO];
	[host setContentHuggingPriority: 1 forOrientation: NSLayoutConstraintOrientationHorizontal];
	[host setContentCompressionResistancePriority: NSLayoutPriorityDefaultLow forOrientation: NSLayoutConstraintOrientationHorizontal];
	[self setTextField: host];
	_statusIcon = [[NSImageView alloc] init];
	_status = VSP_Label( @"", Small(), [NSColor secondaryLabelColor] );
	[_status setAlignment: NSTextAlignmentRight];
	[[_status widthAnchor] constraintEqualToConstant: 128].active = YES;
	row = HStack( @[ _slot, host, _statusIcon, _status ], 8 );
	[row setAlignment: NSLayoutAttributeCenterY];
	[row setTranslatesAutoresizingMaskIntoConstraints: NO];
	[self addSubview: row];
	[NSLayoutConstraint activateConstraints: @[
		[[row leadingAnchor] constraintEqualToAnchor: [self leadingAnchor] constant: 6],
		[[row trailingAnchor] constraintEqualToAnchor: [self trailingAnchor] constant: -6],
		[[row centerYAnchor] constraintEqualToAnchor: [self centerYAnchor]],
	]];
	return self;
}
@end

@interface VSPMastersController : NSObject <NSTableViewDataSource, NSTableViewDelegate, NSTextFieldDelegate>
@property (nonatomic, readonly) NSWindow *panel;
- (void)checkAllWaiting: (BOOL)wait;
- (void)testActions: (const char *)actions;
@end

@implementation VSPMastersController
{
	NSMutableArray<NSString *> *masters;
	NSMutableDictionary<NSString *, NSNumber *> *answers;	// server count, or -1: no answer
	NSMutableSet<NSString *> *checking;
	NSTextField *editing;		// the address being edited, and what it was
	NSString *editingWas;
	VSPTable *table;
	NSTextField *caption;
	NSButton *addButton, *removeButton, *upButton, *downButton;
}

- (instancetype)init
{
	if( !( self = [super init] ) )
		return nil;
	masters = [VSP_MasterServers() mutableCopy];
	answers = [NSMutableDictionary dictionary];
	checking = [NSMutableSet set];
	[self build];
	[self show];
	return self;
}

- (NSButton *)iconButton: (NSString *)symbol tip: (NSString *)tip action: (SEL)action
{
	NSButton *b = [NSButton buttonWithImage: Symbol( symbol, 12 ) target: self action: action];
	[b setBezelStyle: NSBezelStyleRounded];
	[b setControlSize: NSControlSizeSmall];
	[b setToolTip: tip];
	[b setAccessibilityLabel: tip];
	return b;
}

- (void)build
{
	NSView *content, *box;
	NSStackView *root, *buttons, *footer;
	NSButton *cancel, *ok, *defaults, *original, *check;
	NSTableColumn *c = [[NSTableColumn alloc] initWithIdentifier: @"master"];

	_panel = [[NSWindow alloc] initWithContentRect: NSMakeRect( 0, 0, 660, 470 )
		styleMask: NSWindowStyleMaskTitled backing: NSBackingStoreBuffered defer: NO];
	[_panel setTitle: @"Master Servers"];
	VSP_ApplyTestAppearance( _panel );
	content = [_panel contentView];

	table = [[VSPTable alloc] init];
	[c setResizingMask: NSTableColumnAutoresizingMask];
	[table addTableColumn: c];
	[table setHeaderView: nil];
	[table setRowHeight: 30];
	[table setStyle: NSTableViewStyleInset];
	[table setBackgroundColor: [NSColor clearColor]];
	[table setAllowsMultipleSelection: YES];
	[table setColumnAutoresizingStyle: NSTableViewUniformColumnAutoresizingStyle];
	[table setDataSource: self];
	[table setDelegate: self];
	[table registerForDraggedTypes: @[ ROW_MASTER ]];
	[table setTarget: self];
	[table setDoubleAction: @selector(editClicked:)];
	[table setDraggingDestinationFeedbackStyle: NSTableViewDraggingDestinationFeedbackStyleGap];
	{
		__weak VSPMastersController *weakSelf = self;
		table.deleteHandler = ^{ [weakSelf removeMasters: nil]; };
	}
	box = Boxed( Scrolled( table ) );

	caption = VSP_Label( @"", [NSFont systemFontOfSize: 11 weight: NSFontWeightSemibold], [NSColor secondaryLabelColor] );
	addButton = [self iconButton: @"plus" tip: @"Add a master server" action: @selector(addMaster:)];
	removeButton = [self iconButton: @"minus" tip: @"Remove" action: @selector(removeMasters:)];
	upButton = [self iconButton: @"chevron.up" tip: @"Move up" action: @selector(moveUp:)];
	downButton = [self iconButton: @"chevron.down" tip: @"Move down" action: @selector(moveDown:)];
	defaults = [NSButton buttonWithTitle: @"Restore Recommended Defaults" target: self action: @selector(restoreDefaults:)];
	[defaults setToolTip: [@"The live community masters first: " stringByAppendingString:
		[VSP_DefaultMasters() componentsJoinedByString: @", "]]];
	original = [NSButton buttonWithTitle: @"Restore Original Defaults" target: self action: @selector(restoreOriginal:)];
	[original setToolTip: @"As the game came: Internet 1 Raven's master (no longer running), Internet 2 efmaster.tjps.eu, 3 to 5 blank"];
	check = [NSButton buttonWithTitle: @"Check Now" target: self action: @selector(checkNow:)];
	[check setControlSize: NSControlSizeSmall];
	[check setToolTip: @"Ask each master server for its list of games"];
	cancel = [NSButton buttonWithTitle: @"Cancel" target: self action: @selector(cancel:)];
	[cancel setKeyEquivalent: @"\033"];
	ok = [NSButton buttonWithTitle: @"Done" target: self action: @selector(ok:)];
	[ok setKeyEquivalent: @"\r"];
	[[ok widthAnchor] constraintGreaterThanOrEqualToConstant: 80].active = YES;
	[[cancel widthAnchor] constraintGreaterThanOrEqualToConstant: 80].active = YES;
	buttons = HStack( @[ addButton, removeButton, upButton, downButton,
		VSP_Label( @"Drag to reorder, double-click to edit", Small(), [NSColor tertiaryLabelColor] ), [NSView new], check ], 6 );
	[buttons setCustomSpacing: 12 afterView: downButton];
	[buttons setAlignment: NSLayoutAttributeCenterY];
	[[buttons views][5] setContentHuggingPriority: 1 forOrientation: NSLayoutConstraintOrientationHorizontal];
	footer = HStack( @[ defaults, original, [NSView new], cancel, ok ], 10 );
	[[footer views][2] setContentHuggingPriority: 1 forOrientation: NSLayoutConstraintOrientationHorizontal];

	root = VStack( @[ caption,
		Hint( @"Master servers keep the list of Holomatch games on the internet. The first five are the "
			@"Join menu's Internet 1 to 5 sources, in this order. When you host an internet server, it "
			@"announces itself to every one, so add new masters here as the community starts them.", 610 ),
		box, buttons, footer ], 8 );
	[root setCustomSpacing: 10 afterView: [root views][1]];
	[root setCustomSpacing: 18 afterView: buttons];
	[root setEdgeInsets: NSEdgeInsetsMake( 18, 20, 20, 20 )];
	[root setTranslatesAutoresizingMaskIntoConstraints: NO];
	[content addSubview: root];
	for( NSView *v in @[ box, buttons, footer ] )
		[[v widthAnchor] constraintEqualToAnchor: [root widthAnchor] constant: -40].active = YES;
	[NSLayoutConstraint activateConstraints: @[
		[[root leadingAnchor] constraintEqualToAnchor: [content leadingAnchor]],
		[[root trailingAnchor] constraintEqualToAnchor: [content trailingAnchor]],
		[[root topAnchor] constraintEqualToAnchor: [content topAnchor]],
		[[root bottomAnchor] constraintEqualToAnchor: [content bottomAnchor]],
		[[root widthAnchor] constraintEqualToConstant: 660],
		[[[(NSStackView *)box views][1] heightAnchor] constraintEqualToConstant: 280],
	]];
}

- (void)show
{
	[caption setStringValue: [masters count] == 1 ? @"MASTER SERVERS · 1" :
		[NSString stringWithFormat: @"MASTER SERVERS · %lu", (unsigned long)[masters count]]];
	[table reloadData];
	[self updateButtons];
}

- (void)updateButtons
{
	NSIndexSet *sel = [table selectedRowIndexes];
	[addButton setEnabled: [masters count] < VSP_MAX_MASTERS];
	[removeButton setEnabled: [sel count] > 0];
	[upButton setEnabled: [sel count] > 0 && [sel firstIndex] > 0];
	[downButton setEnabled: [sel count] > 0 && [sel lastIndex] + 1 < [masters count]];
}

- (void)tableViewSelectionDidChange: (NSNotification *)n	{ [self updateButtons]; }

// --- checking ---

- (void)check: (NSString *)m done: (dispatch_group_t)group
{
	if( ![m length] || [checking containsObject: m] )
		return;
	[checking addObject: m];
	[answers removeObjectForKey: m];
	if( group )
		dispatch_group_enter( group );
	dispatch_async( dispatch_get_global_queue( QOS_CLASS_UTILITY, 0 ), ^{
		NSInteger n = VSP_QueryMasterCount( m );
		dispatch_async( dispatch_get_main_queue(), ^{
			[self->checking removeObject: m];
			self->answers[m] = @( n );
			LLog( @"Master %@: %@", m, n < 0 ? @"no answer" : [NSString stringWithFormat: @"%ld servers", (long)n] );
			[self reloadMaster: m];
			if( group )
				dispatch_group_leave( group );
		} );
	} );
	[self reloadMaster: m];
}

- (void)reloadMaster: (NSString *)m
{
	NSUInteger i = [masters indexOfObject: m];
	if( i != NSNotFound )
		[table reloadDataForRowIndexes: [NSIndexSet indexSetWithIndex: i] columnIndexes: [NSIndexSet indexSetWithIndex: 0]];
}

- (void)checkAllWaiting: (BOOL)wait
{
	dispatch_group_t group = wait ? dispatch_group_create() : nil;

	for( NSString *m in masters )
		[self check: m done: group];
	// for a snapshot: run the main queue until every answer is in
	while( group && dispatch_group_wait( group, DISPATCH_TIME_NOW ) )
		[[NSRunLoop currentRunLoop] runMode: NSDefaultRunLoopMode beforeDate: [NSDate dateWithTimeIntervalSinceNow: 0.05]];
}

- (void)checkNow: (id)sender
{
	[_panel makeFirstResponder: nil];
	[self checkAllWaiting: NO];
}

// --- the table ---

- (NSInteger)numberOfRowsInTableView: (NSTableView *)t	{ return (NSInteger)[masters count]; }

- (NSView *)tableView: (NSTableView *)t viewForTableColumn: (NSTableColumn *)col row: (NSInteger)row
{
	VSPMasterCell *cell = [t makeViewWithIdentifier: @"master" owner: self];
	NSString *m = masters[row];
	NSNumber *a = answers[m];
	NSString *symbol = nil, *text = @"";
	NSColor *tint = [NSColor secondaryLabelColor];

	if( !cell )
		cell = [[VSPMasterCell alloc] initWithOwner: self];
	[[cell slot] setStringValue: row < MASTER_SLOTS ? [NSString stringWithFormat: @"Internet %ld", (long)row + 1] : @"Announce only"];
	[[cell slot] setToolTip: row < MASTER_SLOTS ?
		[NSString stringWithFormat: @"Holomatch's Join menu lists this master's games under Internet %ld", (long)row + 1] :
		@"Not in the Join menu (it has Internet 1 to 5), but a server you host announces itself here too"];
	if( [cell textField] != editing )		// a status update mustn't reset what's being typed
		[[cell textField] setStringValue: m];
	if( [checking containsObject: m] )
	{
		symbol = @"arrow.triangle.2.circlepath";
		text = @"Checking…";
	}
	else if( a && [a integerValue] >= 0 )
	{
		symbol = @"checkmark.circle.fill";
		tint = [NSColor systemGreenColor];
		text = [a integerValue] == 1 ? @"Answers · 1 game" : [NSString stringWithFormat: @"Answers · %ld games", (long)[a integerValue]];
	}
	else if( a )
	{
		symbol = @"xmark.circle.fill";
		tint = [NSColor systemOrangeColor];
		text = @"No answer";
	}
	[[cell statusIcon] setImage: symbol ? Symbol( symbol, 12 ) : nil];
	[[cell statusIcon] setContentTintColor: tint];
	[[cell status] setStringValue: text];
	[[cell status] setTextColor: a && [a integerValue] < 0 ? [NSColor systemOrangeColor] : [NSColor secondaryLabelColor]];
	[[cell status] setToolTip: a && [a integerValue] < 0 ? @"It didn't answer from this network. It may be down, or "
		@"reachable only over IPv6. Games it lists still show up for players who can reach it." : nil];
	return cell;
}

- (void)beginEditingRow: (NSInteger)row
{
	VSPMasterCell *cell;
	NSString *m;

	if( row < 0 || row >= (NSInteger)[masters count] )
		return;
	m = masters[row];
	[_panel makeFirstResponder: nil];		// end any other edit first, which may move rows
	if( ( row = (NSInteger)[masters indexOfObject: m] ) == NSNotFound )
		return;
	[table selectRowIndexes: [NSIndexSet indexSetWithIndex: row] byExtendingSelection: NO];
	[table scrollRowToVisible: row];
	cell = [table viewAtColumn: 0 row: row makeIfNecessary: YES];
	if( !cell )
		return;
	editing = [cell textField];
	editingWas = masters[row];
	[editing setEditable: YES];
	[editing setSelectable: YES];
	[_panel makeFirstResponder: editing];	// selects the text
}

- (void)editClicked: (id)sender
{
	[self beginEditingRow: [table clickedRow]];
}

// The edit is found by the address it started from, not by its row: rows may
// have moved since.
- (void)controlTextDidEndEditing: (NSNotification *)n
{
	NSTextField *f = [n object];
	NSString *was = editingWas, *m = CleanMaster( [f stringValue] );
	NSUInteger row = was ? [masters indexOfObject: was] : NSNotFound, other;

	if( f != editing )
		return;
	editing = nil;
	editingWas = nil;
	[f setEditable: NO];
	[f setSelectable: NO];
	if( row == NSNotFound )
	{
		[self show];
		return;
	}
	other = [m length] ? [masters indexOfObject: m] : NSNotFound;
	if( ![m length] )
		[masters removeObjectAtIndex: row];		// cleared: remove it
	else if( other != NSNotFound && other != row )
	{
		NSBeep();								// already in the list: keep what it was
		if( ![was length] )
			[masters removeObjectAtIndex: row];
	}
	else
		masters[row] = m;
	[self show];
	if( [m length] && [masters containsObject: m] && !answers[m] )
		[self check: m done: nil];
}

- (void)addMaster: (id)sender
{
	if( [masters count] >= VSP_MAX_MASTERS )
		return;
	[_panel makeFirstResponder: nil];
	[masters addObject: @""];
	[self show];
	[self beginEditingRow: [masters count] - 1];
}

- (void)removeMasters: (id)sender
{
	NSIndexSet *sel = [table selectedRowIndexes];
	NSUInteger first = [sel firstIndex];

	if( ![sel count] )
		return;
	[_panel makeFirstResponder: nil];
	sel = [table selectedRowIndexes];	// ending an edit may have changed the rows
	if( ![sel count] )
		return;
	[masters removeObjectsAtIndexes: sel];
	[self show];
	if( [masters count] )
		[table selectRowIndexes: [NSIndexSet indexSetWithIndex: MIN( first, [masters count] - 1 )] byExtendingSelection: NO];
}

- (void)moveBy: (NSInteger)delta
{
	NSIndexSet *sel = [table selectedRowIndexes];
	NSArray *moving = [masters objectsAtIndexes: sel];
	NSUInteger at = delta < 0 ? [sel firstIndex] - 1 : [sel lastIndex] + 2 - [sel count];

	[masters removeObjectsAtIndexes: sel];
	[masters insertObjects: moving atIndexes: [NSIndexSet indexSetWithIndexesInRange: NSMakeRange( at, [moving count] )]];
	[self show];
	[table selectRowIndexes: [NSIndexSet indexSetWithIndexesInRange: NSMakeRange( at, [moving count] )] byExtendingSelection: NO];
}

- (void)moveUp: (id)sender
{
	[_panel makeFirstResponder: nil];
	if( [[table selectedRowIndexes] count] && [[table selectedRowIndexes] firstIndex] > 0 )
		[self moveBy: -1];
}

- (void)moveDown: (id)sender
{
	[_panel makeFirstResponder: nil];
	if( [[table selectedRowIndexes] count] && [[table selectedRowIndexes] lastIndex] + 1 < [masters count] )
		[self moveBy: 1];
}

- (id<NSPasteboardWriting>)tableView: (NSTableView *)t pasteboardWriterForRow: (NSInteger)row
{
	NSPasteboardItem *item;

	if( editing || row < 0 || row >= (NSInteger)[masters count] || ![masters[row] length] )
		return nil;		// not while an address is being typed
	item = [[NSPasteboardItem alloc] init];
	[item setString: [NSString stringWithFormat: @"%ld", (long)row] forType: ROW_MASTER];
	return item;
}

- (NSDragOperation)tableView: (NSTableView *)t validateDrop: (id<NSDraggingInfo>)info proposedRow: (NSInteger)row
	proposedDropOperation: (NSTableViewDropOperation)op
{
	if( [info draggingSource] != table )
		return NSDragOperationNone;
	[t setDropRow: row dropOperation: NSTableViewDropAbove];
	return NSDragOperationMove;
}

- (BOOL)tableView: (NSTableView *)t acceptDrop: (id<NSDraggingInfo>)info row: (NSInteger)row
	dropOperation: (NSTableViewDropOperation)op
{
	NSMutableIndexSet *from = [NSMutableIndexSet indexSet];

	for( NSPasteboardItem *item in [[info draggingPasteboard] pasteboardItems] )
	{
		NSString *r = [item stringForType: ROW_MASTER];
		if( r && [r integerValue] >= 0 && [r integerValue] < (NSInteger)[masters count] )
			[from addIndex: (NSUInteger)[r integerValue]];
	}
	if( ![from count] )
		return NO;
	[self moveRows: from to: row];
	return YES;
}

- (void)moveRows: (NSIndexSet *)from to: (NSInteger)row
{
	NSArray *moving = [masters objectsAtIndexes: from];

	row -= (NSInteger)[from countOfIndexesInRange: NSMakeRange( 0, (NSUInteger)row )];
	[masters removeObjectsAtIndexes: from];
	row = MIN( MAX( row, 0 ), (NSInteger)[masters count] );
	[masters insertObjects: moving atIndexes: [NSIndexSet indexSetWithIndexesInRange: NSMakeRange( (NSUInteger)row, [moving count] )]];
	[self show];
	[table selectRowIndexes: [NSIndexSet indexSetWithIndexesInRange: NSMakeRange( (NSUInteger)row, [moving count] )] byExtendingSelection: NO];
}

// VOYAGERSP_TEST_MASTERS_ACTIONS: edit<row>, type:<text>, drag<row>to<row> (as a drop
// above that row; refused while editing, as the table does), add, end (focus elsewhere)
- (void)testActions: (const char *)actions
{
	[_panel orderBack: nil];
	for( NSString *a in [@( actions ) componentsSeparatedByString: @","] )
	{
		NSInteger x = 0, y = 0;
		if( [a hasPrefix: @"edit"] )
			[self beginEditingRow: [[a substringFromIndex: 4] integerValue]];
		else if( [a hasPrefix: @"type:"] )
			[[editing currentEditor] setString: [a substringFromIndex: 5]];
		else if( sscanf( [a UTF8String], "drag%ldto%ld", &x, &y ) == 2 )
		{
			if( [self tableView: table pasteboardWriterForRow: x] )
				[self moveRows: [NSIndexSet indexSetWithIndex: x] to: y];
			else
				LLog( @"[test] drag %ld refused", (long)x );
		}
		else if( [a isEqualToString: @"add"] )
			[self addMaster: nil];
		else if( [a isEqualToString: @"end"] )
			[_panel makeFirstResponder: nil];
		LLog( @"[test] %@ -> %@%@", a, [masters componentsJoinedByString: @" | "], editing ? @" (editing)" : @"" );
	}
}

// --- the sheet ---

- (void)restore: (NSArray *)list
{
	[_panel makeFirstResponder: nil];
	[masters setArray: list];
	[self show];
	for( NSString *m in masters )
		if( !answers[m] )
			[self check: m done: nil];
}

- (void)restoreDefaults: (id)sender	{ [self restore: VSP_DefaultMasters()]; }
- (void)restoreOriginal: (id)sender	{ [self restore: VSP_OriginalMasters()]; }

- (void)finish: (BOOL)saved
{
	if( saved )
	{
		[_panel makeFirstResponder: nil];		// end editing
		VSP_SetMasterServers( masters );
	}
	[[_panel sheetParent] endSheet: _panel returnCode: saved ? NSModalResponseOK : NSModalResponseCancel];
}

- (void)ok: (id)sender		{ [self finish: YES]; }
- (void)cancel: (id)sender	{ [self finish: NO]; }

@end

void VSP_ShowMasterServers( NSWindow *parent, void ( ^done )( BOOL saved ) )
{
	VSPMastersController *c = [[VSPMastersController alloc] init];

	[c checkAllWaiting: NO];
	// the block keeps the controller alive until the sheet ends
	[parent beginSheet: [c panel] completionHandler: ^( NSModalResponse r ) {
		[[c panel] orderOut: nil];
		done( r == NSModalResponseOK );
	}];
}

BOOL VSP_SnapshotMasterServers( const char *path )
{
	VSPMastersController *c = [[VSPMastersController alloc] init];

	if( getenv( "VOYAGERSP_TEST_MASTERS_ACTIONS" ) )
		[c testActions: getenv( "VOYAGERSP_TEST_MASTERS_ACTIONS" )];
	[c checkAllWaiting: YES];
	return VSP_SnapshotWindow( [c panel], path );
}
