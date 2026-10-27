/*
===========================================================================
macOS app bundle: what the launcher (sys_osx_launcher.m), its game data
helpers (sys_osx_data.m) and its server host (sys_osx_server.m) share.
Objective-C only; the launcher and server files are compiled with ARC.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#ifndef SYS_OSX_LAUNCHER_H
#define SYS_OSX_LAUNCHER_H

#import <Cocoa/Cocoa.h>

// sys_osx_data.m
NSString *VSP_HomeBase( void );
void VSP_Log( NSString *msg );
BOOL VSP_HasExpansion( NSString *base );
NSArray<NSString *> *VSP_MissingPatchPaks( NSString *base );	// of pak1.pk3, pak2.pk3

#define LLog( ... ) VSP_Log( [NSString stringWithFormat: __VA_ARGS__] )

// sys_osx_launcher.m: the look of the launcher, reused by the server windows
BOOL VSP_TestEnvIs( const char *name, const char *value );
NSString *VSP_CleanName( NSString *name );
NSTextField *VSP_Label( NSString *text, NSFont *font, NSColor *color );
NSTextField *VSP_WrapLabel( NSString *text, NSFont *font, NSColor *color );
NSColor *VSP_GroupFill( void );
NSView *VSP_Group( NSString *caption, NSView *content );
BOOL VSP_SnapshotWindow( NSWindow *w, const char *path );
void VSP_ApplyTestAppearance( NSWindow *w );
// While a modal session runs: the app menu's About, Hide… and Quit work
// (quit is called for Quit). Returns a token for VSP_EndModalMenu.
id VSP_BeginModalMenu( void ( ^quit )( void ) );
void VSP_EndModalMenu( id token );

/* ------------------------------------------------------------------------
   sys_osx_server.m: Host a Server
   ------------------------------------------------------------------------ */

// g_gametype values of the retail Holomatch game (Quake 3's gametype_t)
enum { VSP_GT_FFA = 0, VSP_GT_TOURNAMENT = 1, VSP_GT_TEAM = 3, VSP_GT_CTF = 4 };

// Everything the user sets before starting a server, remembered in the
// app's defaults (key ServerSettings).
@interface VSPServerSettings : NSObject <NSCopying>
@property (nonatomic, copy) NSString *hostname, *password;
@property (nonatomic) NSInteger gametype;
@property (nonatomic) BOOL assimilation, disintegration, actionHero, specialties, elimination;
@property (nonatomic) NSInteger fraglimit, capturelimit, timelimit;	// 0: none; minutes
@property (nonatomic) NSInteger maxPlayers, bots, botSkill;			// bots: bot_minplayers; skill 1-5
@property (nonatomic) BOOL friendlyFire, internet, allowDownload, pure;
@property (nonatomic) BOOL openPort;		// internet: ask the router to forward the port (NAT-PMP / UPnP)
@property (nonatomic) NSInteger port;
@property (nonatomic, copy) NSArray<NSString *> *rotation;			// map names, in order
+ (instancetype)load;
- (void)save;
// the retail Create Server menu's rules: what the game type allows, and the
// modifiers that exclude each other (the one in favor wins)
- (void)applyModifierRulesFavoring: (NSString *)key;
- (NSArray<NSString *> *)modifierNames;			// the ones on, for display
+ (NSString *)gametypeName: (NSInteger)gametype;
+ (NSString *)skillName: (NSInteger)skill;
@end

// A map found in the game files.
@interface VSPMapInfo : NSObject
@property (nonatomic, copy) NSString *name;			// what "map" takes
@property (nonatomic, copy) NSString *longName;		// from the arena info, or nil
@property (nonatomic, copy) NSString *types;			// arena "type", lowercased, or nil
@property (nonatomic, copy) NSString *pk3;			// file name, or nil when loose
@property (nonatomic) BOOL retail, expansion;
@property (nonatomic, readonly) NSString *displayName;
- (BOOL)supportsGametype: (NSInteger)gametype;		// YES when unknown
- (NSString *)typesDescription;
- (NSImage *)levelshot;								// nil when none
@end

// The Holomatch maps in the homepath's baseEF and dataFolder (may be nil),
// sorted by display name. Cached until the folders' contents change.
NSArray<VSPMapInfo *> *VSP_MapCatalog( NSString *dataFolder );
// This Mac's address on the local network (IPv4), or nil.
NSString *VSP_LANAddress( void );

VSPMapInfo *VSP_FindMap( NSArray<VSPMapInfo *> *maps, NSString *name );

// The Config… sheet: edits settings in place and calls done with YES when
// the user pressed Done. Test hooks may snapshot it instead (returns YES).
void VSP_ShowServerConfig( NSWindow *parent, VSPServerSettings *settings, NSString *dataFolder,
	void ( ^done )( BOOL saved ) );
BOOL VSP_SnapshotServerConfig( VSPServerSettings *settings, NSString *dataFolder, const char *tab, const char *path );

// Master servers (the launcher's Servers… sheet). The list is sv_master1, 2, …:
// the first five are Holomatch's Internet 1-5 in the Join menu, and a hosted
// internet server announces itself to all of them. Saved in the app's
// defaults (key MasterServers); the engine's defaults (EF_DEFAULT_MASTERS) match.
#define VSP_MAX_MASTERS	16		// MAX_MASTER_SERVERS
#define VSP_MASTERS_CFG	@"vsp_masters.cfg"
NSArray<NSString *> *VSP_DefaultMasters( void );			// the recommended list
NSArray<NSString *> *VSP_OriginalMasters( void );			// the game's own: Raven's, then tjps
NSArray<NSString *> *VSP_MasterServers( void );				// the saved list, or the defaults
void VSP_SetMasterServers( NSArray<NSString *> *masters );	// cleaned; nil: the defaults
// "set sv_masterN …" for every slot (empty past the list, or all empty when blank)
NSString *VSP_MastersCfgText( BOOL blank );
// baseEF/vsp_masters.cfg, for "+exec" (a command line can't hold 16 addresses)
BOOL VSP_WriteMastersConfig( void );
// Ask a master ("host" or "host:port") for its server list; blocks a few
// seconds. The number of servers listed, or -1 when it didn't answer.
NSInteger VSP_QueryMasterCount( NSString *master );
// The Servers… sheet: done is called with YES when the user pressed Done.
void VSP_ShowMasterServers( NSWindow *parent, void ( ^done )( BOOL saved ) );
BOOL VSP_SnapshotMasterServers( const char *path );		// after checking each master

// Loopback Host Fix (the launcher's Holomatch group, on by default): the game
// swaps this network's public address in the masters' lists for 127.0.0.1, so
// a server hosted here shows up in the host's own internet lists. The public
// address comes from the router (NAT-PMP), looked up in the background.
BOOL VSP_LoopbackFix( void );
void VSP_SetLoopbackFix( BOOL on );
void VSP_StartPublicAddressLookup( void );
NSString *VSP_PublicAddress( void );			// nil until the router answered
NSArray<NSString *> *VSP_LoopbackArgs( void );	// +set cl_loopbackHostIP <ip>, or none

// The Browse… sheet: Holomatch servers on this network (Local) and on each
// master (Internet 1, 2…), Internet 1 first. Maps missing from the game files
// in dataFolder (may be nil) and the homepath are marked. done gets the address
// to join and the password the user typed (or nil), only when the user picked one.
void VSP_ShowServerBrowser( NSWindow *parent, NSString *dataFolder,
	void ( ^done )( NSString *address, NSString *password ) );
BOOL VSP_SnapshotServerBrowser( const char *path, const char *tab, int join, NSString *dataFolder,
	void ( ^done )( NSString *address, NSString *password ) );

// The server window: starts the server as a child process (the app's own
// executable, dedicated) and shows it until it stops or the window closes.
// It runs in an app instance of its own (VSP_HOST_SERVER_ARG), so the
// launcher stays free to play. clientArgs are the graphics/name arguments for
// Play on This Mac.
#define VSP_HOST_SERVER_ARG	"--host-server"
void VSP_RunServerWindow( VSPServerSettings *settings, NSString *basepath, NSString *dataFolder,
	NSArray<NSString *> *clientArgs );

// The servers that app instances on this Mac are running: pid, name, port
// (NSNumber, NSString, NSNumber), oldest first.
NSArray<NSDictionary *> *VSP_RunningServers( void );

#endif
