/*
===========================================================================
macOS app bundle: the launcher window.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
It is compiled with ARC (see the macOS CMakeLists.txt).

Shown before Com_Init when running from the .app, unless the command line
already names the mode (+set com_holomatch 0|1, as a relaunch from the File
menu does). It offers:
  - the mode: Single Player, Holomatch (multiplayer) or Host a Server,
    remembered (NSUserDefaults key LauncherMode: 0, 1, 2). Hosting shows the
    server's settings in place of the graphics, a Config… sheet, and Start
    Server, which runs the server window of sys_osx_server.m in this
    process (the game itself never starts) and comes back here when the
    server stops;
  - graphics: a quality preset, display (window / full screen), window size
    in points, Retina resolution, anti-aliasing, texture detail, V-Sync;
  - the Holomatch player name;
  - where the game files are (sys_osx_data.m's lookup and chooser).
Play appends +set com_holomatch, the graphics cvars, the name and
fs_basepath (an in-place game folder) to the command line. Command-line
+set values override the config files, and as archived cvars the engine
saves them back into the mode's config (efspconfig.cfg / hmconfig.cfg), so
what the launcher shows next time is what the game last used, including
changes made in the game's own Video menu.

Test hooks (environment variables):
  VOYAGERSP_TEST_LAUNCHER_SNAPSHOT=<file.png>  render the window to a PNG and
                                quit (or go on with ..._PLAY)
  VOYAGERSP_TEST_LAUNCHER_APPEARANCE=dark|light
  VOYAGERSP_TEST_LAUNCHER_MODE=sp|hm|host     preselect the mode
  (Host a Server's hooks are listed in sys_osx_server.m)
  VOYAGERSP_TEST_LAUNCHER_PRESET=performance|balanced|best
  VOYAGERSP_TEST_LAUNCHER_PLAY=1              press Play without showing it
  VOYAGERSP_TEST_LAUNCHER_AUTOPLAY=<s>        show it, press Play after <s> s
  VOYAGERSP_TEST_ADD=<path>                   press Add Game Files… with <path>
===========================================================================
*/

#ifndef __APPLE__
#error This file is for macOS only.
#endif

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

#import <Cocoa/Cocoa.h>

#ifdef USE_LOCAL_HEADERS
#	include "SDL.h"
#else
#	include <SDL.h>
#endif

#include "sys_osx_launcher.h"

#define KEY_LAUNCHERMODE	@"LauncherMode"

extern NSString *Sys_MacAppBundlePath( void );
// sys_osx_data.m
extern qboolean VSP_DataOverridden( int argc, char **argv );
extern BOOL VSP_FindGameData( NSString **outBasepath, NSString **outBase );
extern BOOL VSP_ChooseGameData( NSString **outBasepath );
extern void VSP_RevealFolder( NSString *path );
extern int VSP_AddGameFiles( void );

// the three choices (NSUserDefaults LauncherMode)
typedef enum { MODE_SP, MODE_HOLOMATCH, MODE_HOST, NUM_MODES } launchMode_t;

// runModalForWindow results besides OK (play) and Cancel (quit)
#define RESPONSE_HOST	1000

typedef enum { PRESET_PERFORMANCE, PRESET_BALANCED, PRESET_BEST, PRESET_CUSTOM } preset_t;

// The settings the launcher shows and applies.
typedef struct {
	BOOL	fullscreen;
	int		width, height;		// window size in points
	BOOL	highdpi;
	int		msaa;				// 0, 2, 4, 8
	int		picmip;				// 0 high, 1 medium, 2 low
	BOOL	vsync;
} gfx_t;

static const struct { preset_t preset; BOOL highdpi; int msaa, picmip; BOOL vsync; } presets[] = {
	{ PRESET_PERFORMANCE,	NO,  0, 1, NO  },
	{ PRESET_BALANCED,		NO,  2, 0, YES },
	{ PRESET_BEST,			YES, 4, 0, YES },
};

static const int windowSizes[][2] = {
	{ 1024, 768 }, { 1280, 720 }, { 1280, 800 }, { 1440, 900 }, { 1600, 900 },
	{ 1680, 1050 }, { 1920, 1080 }, { 1920, 1200 }, { 2560, 1440 }, { 2560, 1600 },
};

BOOL VSP_TestEnvIs( const char *name, const char *value )
{
	const char *v = getenv( name );
	return v && !Q_stricmp( v, value );
}

/* ------------------------------------------------------------------------
   Config files
   ------------------------------------------------------------------------ */

static NSString *ConfigPath( BOOL holomatch )
{
	return [VSP_HomeBase() stringByAppendingPathComponent: holomatch ? @"hmconfig.cfg" : @"efspconfig.cfg"];
}

// The "seta name value" lines of a config file, names lowercased.
static NSDictionary<NSString *, NSString *> *ReadConfig( NSString *path )
{
	NSString *text = [NSString stringWithContentsOfFile: path encoding: NSUTF8StringEncoding error: NULL];
	NSMutableDictionary *d = [NSMutableDictionary dictionary];

	if( !text )
		text = [NSString stringWithContentsOfFile: path encoding: NSISOLatin1StringEncoding error: NULL];
	for( NSString *line in [text componentsSeparatedByCharactersInSet: [NSCharacterSet newlineCharacterSet]] )
	{
		NSScanner *sc = [NSScanner scannerWithString: line];
		NSString *cmd, *name, *value = @"";

		if( ![sc scanUpToCharactersFromSet: [NSCharacterSet whitespaceCharacterSet] intoString: &cmd] ||
			( ![cmd isEqualToString: @"seta"] && ![cmd isEqualToString: @"set"] ) ||
			![sc scanUpToCharactersFromSet: [NSCharacterSet whitespaceCharacterSet] intoString: &name] )
			continue;
		if( [sc scanString: @"\"" intoString: NULL] )
			[sc scanUpToString: @"\"" intoString: &value];
		else
			[sc scanUpToCharactersFromSet: [NSCharacterSet whitespaceCharacterSet] intoString: &value];
		d[[name lowercaseString]] = value ? value : @"";
	}
	return d;
}

static int CfgInt( NSDictionary *cfg, NSString *name, int def )
{
	NSString *v = cfg[name];
	return v ? [v intValue] : def;
}

// Graphics from the config last used (or the other mode's, or defaults).
static gfx_t LoadGraphics( BOOL holomatch, BOOL *fromConfig )
{
	NSDictionary *cfg = ReadConfig( ConfigPath( holomatch ) );
	gfx_t g;

	if( ![cfg count] )
		cfg = ReadConfig( ConfigPath( !holomatch ) );
	*fromConfig = [cfg count] > 0;

	g.fullscreen = CfgInt( cfg, @"r_fullscreen", 0 ) != 0;
	g.width = 1920;
	g.height = 1080;
	if( CfgInt( cfg, @"r_mode", -1 ) == -1 && CfgInt( cfg, @"r_customwidth", 0 ) >= 640 && CfgInt( cfg, @"r_customheight", 0 ) >= 480 )
	{
		g.width = CfgInt( cfg, @"r_customwidth", 1920 );
		g.height = CfgInt( cfg, @"r_customheight", 1080 );
	}
	if( *fromConfig )
	{
		g.highdpi = CfgInt( cfg, @"r_highdpi", 0 ) != 0;
		g.msaa = CfgInt( cfg, @"r_fbo", 0 ) ? CfgInt( cfg, @"r_ext_multisample", 0 ) : 0;
		g.picmip = MIN( MAX( CfgInt( cfg, @"r_picmip", 0 ), 0 ), 2 );
		g.vsync = CfgInt( cfg, @"r_swapinterval", 0 ) != 0;
	}
	else
	{
		// first run: the Best preset (Apple Silicon has power to spare for EF)
		g.highdpi = presets[PRESET_BEST].highdpi;
		g.msaa = presets[PRESET_BEST].msaa;
		g.picmip = presets[PRESET_BEST].picmip;
		g.vsync = presets[PRESET_BEST].vsync;
	}
	if( g.msaa != 0 && g.msaa != 2 && g.msaa != 4 && g.msaa != 8 )
		g.msaa = g.msaa > 4 ? 8 : g.msaa > 2 ? 4 : 2;
	return g;
}

static NSString *DefaultPlayerName( void )
{
	NSString *full = NSFullUserName();
	NSString *first = [[full componentsSeparatedByString: @" "] firstObject];
	return [first length] ? first : @"Crewman";
}

// A name that survives the command line and the userinfo string.
NSString *VSP_CleanName( NSString *name )
{
	NSMutableString *s = [NSMutableString string];
	NSUInteger i;

	for( i = 0; i < [name length] && [s length] < 31; i++ )
	{
		unichar c = [name characterAtIndex: i];
		if( c < 32 || c > 126 || c == '"' || c == '+' || c == ';' || c == '\\' || c == '%' )
			continue;
		[s appendFormat: @"%C", c];
	}
	return [[s stringByTrimmingCharactersInSet: [NSCharacterSet whitespaceCharacterSet]] length] ? s : @"UnnamedPlayer";
}

/* ------------------------------------------------------------------------
   Views
   ------------------------------------------------------------------------ */

NSTextField *VSP_Label( NSString *text, NSFont *font, NSColor *color )
{
	NSTextField *l = [NSTextField labelWithString: text];
	[l setFont: font];
	if( color )
		[l setTextColor: color];
	return l;
}

NSTextField *VSP_WrapLabel( NSString *text, NSFont *font, NSColor *color )
{
	NSTextField *l = [NSTextField wrappingLabelWithString: text];
	[l setFont: font];
	[l setTextColor: color];
	[l setSelectable: NO];
	return l;
}

NSColor *VSP_GroupFill( void )
{
	return [NSColor colorWithName: nil dynamicProvider: ^NSColor *( NSAppearance *a ) {
		BOOL dark = [[a bestMatchFromAppearancesWithNames: @[ NSAppearanceNameAqua, NSAppearanceNameDarkAqua ]]
			isEqualToString: NSAppearanceNameDarkAqua];
		return dark ? [NSColor colorWithWhite: 1 alpha: 0.05] : [NSColor colorWithWhite: 0 alpha: 0.035];
	}];
}

// The rounded background of a group.
@interface VSPGroupBox : NSView
@end

@implementation VSPGroupBox
- (void)drawRect: (NSRect)dirty
{
	NSBezierPath *p = [NSBezierPath bezierPathWithRoundedRect: NSInsetRect( [self bounds], 0.5, 0.5 ) xRadius: 10 yRadius: 10];
	[VSP_GroupFill() setFill];
	[p fill];
	[[NSColor separatorColor] setStroke];
	[p stroke];
}
@end

// A rounded group with a small caption above it, like System Settings.
NSView *VSP_Group( NSString *caption, NSView *content )
{
	VSPGroupBox *box = [[VSPGroupBox alloc] init];
	NSTextField *cap = VSP_Label( [caption uppercaseString], [NSFont systemFontOfSize: 11 weight: NSFontWeightSemibold],
		[NSColor secondaryLabelColor] );
	NSStackView *stack;

	[content setTranslatesAutoresizingMaskIntoConstraints: NO];
	[box addSubview: content];
	[NSLayoutConstraint activateConstraints: @[
		[[content leadingAnchor] constraintEqualToAnchor: [box leadingAnchor] constant: 14],
		[[content trailingAnchor] constraintEqualToAnchor: [box trailingAnchor] constant: -14],
		[[content topAnchor] constraintEqualToAnchor: [box topAnchor] constant: 12],
		[[content bottomAnchor] constraintEqualToAnchor: [box bottomAnchor] constant: -12],
	]];

	stack = [NSStackView stackViewWithViews: @[ cap, box ]];
	[stack setOrientation: NSUserInterfaceLayoutOrientationVertical];
	[stack setAlignment: NSLayoutAttributeLeading];
	[stack setSpacing: 6];
	[[box widthAnchor] constraintEqualToAnchor: [stack widthAnchor]].active = YES;
	return stack;
}

@class VSPLauncher;

// A big selectable card for one of the modes.
@interface VSPModeCard : NSView
@property (nonatomic) BOOL selected;
@property (nonatomic, weak) VSPLauncher *owner;
@property (nonatomic) launchMode_t mode;
@property (nonatomic, strong) NSImageView *icon;
@property (nonatomic, strong) NSTextField *title;
@end

@interface VSPLauncher : NSObject <NSWindowDelegate>
- (void)selectMode: (launchMode_t)mode;
- (void)play: (id)sender;
- (void)addFiles: (id)sender;
@end

@implementation VSPModeCard

// icon and title on a line, the detail under them, and an optional
// accessory (a button) at the bottom
- (instancetype)initWithSymbol: (NSString *)symbol title: (NSString *)title detail: (NSString *)detail
	accessory: (NSView *)accessory
{
	NSImageSymbolConfiguration *cfg = [NSImageSymbolConfiguration configurationWithPointSize: 19 weight: NSFontWeightRegular];
	NSTextField *det;
	NSStackView *head;

	if( !( self = [super initWithFrame: NSMakeRect( 0, 0, 180, 110 )] ) )
		return nil;

	_icon = [NSImageView imageViewWithImage: [[NSImage imageWithSystemSymbolName: symbol accessibilityDescription: nil]
		imageWithSymbolConfiguration: cfg]];
	[_icon setContentHuggingPriority: NSLayoutPriorityRequired forOrientation: NSLayoutConstraintOrientationHorizontal];
	_title = VSP_Label( title, [NSFont systemFontOfSize: 15 weight: NSFontWeightSemibold], nil );
	det = VSP_WrapLabel( detail, [NSFont systemFontOfSize: 12], [NSColor secondaryLabelColor] );
	[det setPreferredMaxLayoutWidth: 150];
	head = [NSStackView stackViewWithViews: @[ _icon, _title ]];
	[head setSpacing: 7];
	[head setAlignment: NSLayoutAttributeCenterY];

	for( NSView *v in accessory ? @[ head, det, accessory ] : @[ head, det ] )
	{
		[v setTranslatesAutoresizingMaskIntoConstraints: NO];
		[self addSubview: v];
	}
	[NSLayoutConstraint activateConstraints: @[
		[[head leadingAnchor] constraintEqualToAnchor: [self leadingAnchor] constant: 14],
		[[head trailingAnchor] constraintLessThanOrEqualToAnchor: [self trailingAnchor] constant: -10],
		[[head topAnchor] constraintEqualToAnchor: [self topAnchor] constant: 13],
		[[_icon widthAnchor] constraintEqualToConstant: 30],
		[[det leadingAnchor] constraintEqualToAnchor: [self leadingAnchor] constant: 14],
		[[det trailingAnchor] constraintLessThanOrEqualToAnchor: [self trailingAnchor] constant: -12],
		[[det topAnchor] constraintEqualToAnchor: [head bottomAnchor] constant: 6],
		[[det bottomAnchor] constraintLessThanOrEqualToAnchor: [self bottomAnchor] constant: -13],
	]];
	if( accessory )
		[NSLayoutConstraint activateConstraints: @[
			[[accessory leadingAnchor] constraintEqualToAnchor: [self leadingAnchor] constant: 14],
			[[accessory topAnchor] constraintGreaterThanOrEqualToAnchor: [det bottomAnchor] constant: 8],
			[[accessory bottomAnchor] constraintEqualToAnchor: [self bottomAnchor] constant: -11],
		]];
	[self setAccessibilityElement: YES];
	[self setAccessibilityRole: NSAccessibilityRadioButtonRole];
	[self setAccessibilityLabel: title];
	[self setAccessibilityHelp: detail];
	return self;
}

- (void)setSelected: (BOOL)selected
{
	_selected = selected;
	[_icon setContentTintColor: selected ? [NSColor controlAccentColor] : [NSColor secondaryLabelColor]];
	[self setAccessibilityValue: @( selected )];
	[self setNeedsDisplay: YES];
}

- (void)drawRect: (NSRect)dirty
{
	NSRect r = NSInsetRect( [self bounds], 1, 1 );
	NSBezierPath *p = [NSBezierPath bezierPathWithRoundedRect: r xRadius: 12 yRadius: 12];

	if( _selected )
	{
		[[[NSColor controlAccentColor] colorWithAlphaComponent: 0.14] setFill];
		[p fill];
		[[NSColor controlAccentColor] setStroke];
		[p setLineWidth: 2];
	}
	else
	{
		[VSP_GroupFill() setFill];
		[p fill];
		[[NSColor separatorColor] setStroke];
		[p setLineWidth: 1];
	}
	[p stroke];
}

- (BOOL)acceptsFirstResponder	{ return YES; }
- (BOOL)acceptsFirstMouse: (NSEvent *)e	{ return YES; }
- (void)drawFocusRingMask
{
	[[NSBezierPath bezierPathWithRoundedRect: NSInsetRect( [self bounds], 1, 1 ) xRadius: 12 yRadius: 12] fill];
}
- (NSRect)focusRingMaskBounds	{ return [self bounds]; }

- (void)mouseDown: (NSEvent *)e
{
	[_owner selectMode: _mode];
	if( [e clickCount] == 2 )
		[_owner play: self];
}

- (void)keyDown: (NSEvent *)e
{
	NSString *c = [e charactersIgnoringModifiers];
	unichar k = [c length] ? [c characterAtIndex: 0] : 0;

	if( k == NSLeftArrowFunctionKey )
		[_owner selectMode: ( _mode + NUM_MODES - 1 ) % NUM_MODES];
	else if( k == NSRightArrowFunctionKey )
		[_owner selectMode: ( _mode + 1 ) % NUM_MODES];
	else if( k == ' ' )
		[_owner selectMode: _mode];
	else
		[super keyDown: e];
}

- (BOOL)accessibilityPerformPress
{
	[_owner selectMode: _mode];
	return YES;
}

@end

/* ------------------------------------------------------------------------
   The launcher
   ------------------------------------------------------------------------ */

@implementation VSPLauncher
{
	launchMode_t mode;
	BOOL dataOverridden, haveData;
	NSString *basepath;				// fs_basepath to pass, or nil
	NSString *dataFolder;			// the BaseEF folder found
	gfx_t gfx;
	VSPServerSettings *server;

	NSWindow *window;
	VSPModeCard *cards[NUM_MODES];
	NSView *graphicsGroup, *serverGroup;
	NSTextField *srvName, *srvGame, *srvMaps, *srvPlayers, *srvNetwork, *srvAddress;
	NSSegmentedControl *preset;
	NSPopUpButton *display, *size, *msaa, *texture;
	NSButton *retina, *vsync;
	NSTextField *nameLabel, *name, *nameHint;
	NSImageView *folderIcon, *statusIcon;
	NSTextField *folderPath, *status;
	NSButton *showButton, *addButton, *changeButton, *playButton;
	NSImageView *expansionIcon;
	NSTextField *expansion;
	BOOL updating;
}

- (instancetype)initWithOverridden: (BOOL)overridden
{
	NSDictionary *hm;
	BOOL fromConfig;

	if( !( self = [super init] ) )
		return nil;
	dataOverridden = overridden;
	mode = (launchMode_t)[[NSUserDefaults standardUserDefaults] integerForKey: KEY_LAUNCHERMODE];
	if( mode < 0 || mode >= NUM_MODES )
		mode = MODE_SP;
	if( getenv( "VOYAGERSP_TEST_LAUNCHER_MODE" ) )
		mode = VSP_TestEnvIs( "VOYAGERSP_TEST_LAUNCHER_MODE", "hm" ) ? MODE_HOLOMATCH :
			VSP_TestEnvIs( "VOYAGERSP_TEST_LAUNCHER_MODE", "host" ) ? MODE_HOST : MODE_SP;
	// a server's Play on This Mac is a Holomatch client
	gfx = LoadGraphics( mode != MODE_SP, &fromConfig );
	server = [VSPServerSettings load];
	if( getenv( "VOYAGERSP_TEST_SERVER_PORT" ) )
		server.port = atoi( getenv( "VOYAGERSP_TEST_SERVER_PORT" ) );
	[self buildWindow];

	hm = ReadConfig( ConfigPath( YES ) );
	[name setStringValue: hm[@"name"] && ![hm[@"name"] isEqualToString: @"UnnamedPlayer"] ? hm[@"name"] : DefaultPlayerName()];

	[self refreshData];
	[self showGraphics];
	[self selectMode: mode];
	if( getenv( "VOYAGERSP_TEST_LAUNCHER_PRESET" ) )
	{
		const char *p = getenv( "VOYAGERSP_TEST_LAUNCHER_PRESET" );
		[preset setSelectedSegment: !Q_stricmp( p, "performance" ) ? PRESET_PERFORMANCE :
			!Q_stricmp( p, "balanced" ) ? PRESET_BALANCED : PRESET_BEST];
		[self presetChanged: preset];
	}
	return self;
}

- (NSPopUpButton *)popup: (NSArray *)titles tags: (NSArray *)tags
{
	NSPopUpButton *b = [[NSPopUpButton alloc] initWithFrame: NSZeroRect pullsDown: NO];
	NSUInteger i;

	for( i = 0; i < [titles count]; i++ )
	{
		[b addItemWithTitle: titles[i]];
		[[b lastItem] setTag: [tags[i] integerValue]];
	}
	[b setTarget: self];
	[b setAction: @selector(graphicsChanged:)];
	return b;
}

- (NSButton *)checkbox: (NSString *)title
{
	NSButton *b = [NSButton checkboxWithTitle: title target: self action: @selector(graphicsChanged:)];
	return b;
}

- (void)buildWindow
{
	NSView *content;
	NSStackView *root, *header, *headerText, *cardRow, *folderRow, *folderText, *footer;
	NSGridView *grid, *nameGrid, *serverGrid;
	NSButton *config;
	NSImageView *appIcon;
	NSString *version = [[NSBundle mainBundle] objectForInfoDictionaryKey: @"CFBundleShortVersionString"];
	NSButton *quit;

	window = [[NSWindow alloc] initWithContentRect: NSMakeRect( 0, 0, 620, 600 )
		styleMask: NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskFullSizeContentView
		backing: NSBackingStoreBuffered defer: NO];
	[window setTitle: @"Elite Force"];
	[window setTitlebarAppearsTransparent: YES];
	[window setTitleVisibility: NSWindowTitleHidden];
	[window setMovableByWindowBackground: YES];
	[window setReleasedWhenClosed: NO];
	[window setDelegate: self];
	VSP_ApplyTestAppearance( window );
	content = [window contentView];

	// header: icon, game name, prompt
	appIcon = [NSImageView imageViewWithImage: [NSApp applicationIconImage]];
	[appIcon setImageScaling: NSImageScaleProportionallyUpOrDown];
	[[appIcon widthAnchor] constraintEqualToConstant: 56].active = YES;
	[[appIcon heightAnchor] constraintEqualToConstant: 56].active = YES;
	headerText = [NSStackView stackViewWithViews: @[
		VSP_Label( @"Elite Force", [NSFont systemFontOfSize: 24 weight: NSFontWeightBold], nil ),
		VSP_Label( @"Choose how you want to play.", [NSFont systemFontOfSize: 13], [NSColor secondaryLabelColor] ) ]];
	[headerText setOrientation: NSUserInterfaceLayoutOrientationVertical];
	[headerText setAlignment: NSLayoutAttributeLeading];
	[headerText setSpacing: 2];
	header = [NSStackView stackViewWithViews: @[ appIcon, headerText ]];
	[header setSpacing: 14];
	[header setAlignment: NSLayoutAttributeCenterY];

	// the three modes; hosting has its Config… right in its card
	config = [NSButton buttonWithTitle: @"Config…" target: self action: @selector(configServer:)];
	[config setControlSize: NSControlSizeSmall];
	[config setFont: [NSFont systemFontOfSize: [NSFont smallSystemFontSize]]];
	[config setToolTip: @"Maps, game type, bots and the server's name, password and network"];
	cards[MODE_SP] = [[VSPModeCard alloc] initWithSymbol: @"person.fill" title: @"Single Player"
		detail: @"The Voyager campaign, your saves and the Virtual Voyager." accessory: nil];
	cards[MODE_HOLOMATCH] = [[VSPModeCard alloc] initWithSymbol: @"person.3.fill" title: @"Holomatch"
		detail: @"Join games on your network or the internet, or play with bots." accessory: nil];
	cards[MODE_HOST] = [[VSPModeCard alloc] initWithSymbol: @"server.rack" title: @"Host a Server"
		detail: @"Run a Holomatch server on this Mac for friends." accessory: config];
	cardRow = [NSStackView stackViewWithViews: @[ cards[MODE_SP], cards[MODE_HOLOMATCH], cards[MODE_HOST] ]];
	[cardRow setDistribution: NSStackViewDistributionFillEqually];
	[cardRow setAlignment: NSLayoutAttributeTop];
	[cardRow setSpacing: 10];
	for( int m = 0; m < NUM_MODES; m++ )
	{
		cards[m].owner = self;
		cards[m].mode = (launchMode_t)m;
		[[cards[m] bottomAnchor] constraintEqualToAnchor: [cardRow bottomAnchor]].active = YES;
	}

	// graphics
	preset = [NSSegmentedControl segmentedControlWithLabels: @[ @"Performance", @"Balanced", @"Best", @"Custom" ]
		trackingMode: NSSegmentSwitchTrackingSelectOne target: self action: @selector(presetChanged:)];
	[preset setEnabled: NO forSegment: PRESET_CUSTOM];
	display = [self popup: @[ @"Window", @"Full Screen" ] tags: @[ @0, @1 ]];
	size = [self popup: @[] tags: @[]];
	retina = [self checkbox: @"Full Retina resolution"];
	msaa = [self popup: @[ @"Off", @"2×", @"4×", @"8×" ] tags: @[ @0, @2, @4, @8 ]];
	texture = [self popup: @[ @"High", @"Medium", @"Low" ] tags: @[ @0, @1, @2 ]];
	vsync = [self checkbox: @"Sync to the display (V-Sync)"];

	grid = [NSGridView gridViewWithViews: @[
		@[ VSP_Label( @"Quality:", [NSFont systemFontOfSize: 13], nil ), preset ],
		@[ VSP_Label( @"Display:", [NSFont systemFontOfSize: 13], nil ), display ],
		@[ VSP_Label( @"Window size:", [NSFont systemFontOfSize: 13], nil ), size ],
		@[ [NSGridCell emptyContentView], retina ],
		@[ VSP_Label( @"Anti-aliasing:", [NSFont systemFontOfSize: 13], nil ), msaa ],
		@[ VSP_Label( @"Texture detail:", [NSFont systemFontOfSize: 13], nil ), texture ],
		@[ [NSGridCell emptyContentView], vsync ],
	]];
	[grid setRowSpacing: 7];
	[grid setColumnSpacing: 10];
	[[grid columnAtIndex: 0] setXPlacement: NSGridCellPlacementTrailing];
	[[grid columnAtIndex: 0] setWidth: 112];
	[grid setRowAlignment: NSGridRowAlignmentFirstBaseline];
	[[grid rowAtIndex: 0] setBottomPadding: 4];

	// in Host mode, the server's settings in place of the graphics
	{
		NSFont *f = [NSFont systemFontOfSize: 13];
		NSArray *values = @[ srvName = VSP_Label( @"", f, nil ), srvGame = VSP_Label( @"", f, nil ),
			srvMaps = VSP_Label( @"", f, nil ), srvPlayers = VSP_Label( @"", f, nil ), srvNetwork = VSP_Label( @"", f, nil ),
			srvAddress = VSP_Label( @"", [NSFont monospacedDigitSystemFontOfSize: 13 weight: NSFontWeightMedium], nil ) ];
		NSArray *names = @[ @"Server name:", @"Game:", @"Maps:", @"Players:", @"Network:", @"Address:" ];
		NSMutableArray *rows = [NSMutableArray array];
		NSTextField *tip = VSP_WrapLabel( @"Friends connect to this address, or find the server in Holomatch's Join menu. "
			@"It runs in its own window, where Play on This Mac joins it with your Holomatch graphics settings.",
			[NSFont systemFontOfSize: 11], [NSColor secondaryLabelColor] );

		for( NSUInteger i = 0; i < [names count]; i++ )
		{
			NSTextField *v = values[i];
			[v setLineBreakMode: NSLineBreakByTruncatingTail];
			[v setContentCompressionResistancePriority: NSLayoutPriorityDefaultLow forOrientation: NSLayoutConstraintOrientationHorizontal];
			[rows addObject: @[ VSP_Label( names[i], f, nil ), v ]];
		}
		[tip setPreferredMaxLayoutWidth: 400];
		[rows addObject: @[ [NSGridCell emptyContentView], tip ]];
		serverGrid = [NSGridView gridViewWithViews: rows];
		[serverGrid setRowSpacing: 7];
		[serverGrid setColumnSpacing: 10];
		[[serverGrid columnAtIndex: 0] setXPlacement: NSGridCellPlacementTrailing];
		[[serverGrid columnAtIndex: 0] setWidth: 112];
		[serverGrid setRowAlignment: NSGridRowAlignmentFirstBaseline];
		[[serverGrid rowAtIndex: [names count]] setTopPadding: -2];
	}

	// Holomatch player name
	nameLabel = VSP_Label( @"Player name:", [NSFont systemFontOfSize: 13], nil );
	name = [NSTextField textFieldWithString: @""];
	[name setPlaceholderString: @"UnnamedPlayer"];
	[[name widthAnchor] constraintEqualToConstant: 220].active = YES;
	nameHint = VSP_Label( @"Shown to other players in Holomatch.", [NSFont systemFontOfSize: 11], [NSColor secondaryLabelColor] );
	nameGrid = [NSGridView gridViewWithViews: @[ @[ nameLabel, name ], @[ [NSGridCell emptyContentView], nameHint ] ]];
	[nameGrid setRowSpacing: 4];
	[nameGrid setColumnSpacing: 10];
	[[nameGrid columnAtIndex: 0] setXPlacement: NSGridCellPlacementTrailing];
	[[nameGrid columnAtIndex: 0] setWidth: 112];
	[nameGrid setRowAlignment: NSGridRowAlignmentFirstBaseline];

	// game files
	folderIcon = [NSImageView imageViewWithImage: [[NSWorkspace sharedWorkspace] iconForFile: @"/System"]];
	[[folderIcon widthAnchor] constraintEqualToConstant: 36].active = YES;
	[[folderIcon heightAnchor] constraintEqualToConstant: 36].active = YES;
	folderPath = VSP_Label( @"", [NSFont systemFontOfSize: 12 weight: NSFontWeightMedium], nil );
	[folderPath setLineBreakMode: NSLineBreakByTruncatingMiddle];
	[folderPath setContentCompressionResistancePriority: NSLayoutPriorityDefaultLow forOrientation: NSLayoutConstraintOrientationHorizontal];
	statusIcon = [[NSImageView alloc] init];
	status = VSP_Label( @"", [NSFont systemFontOfSize: 11], [NSColor secondaryLabelColor] );
	[status setLineBreakMode: NSLineBreakByTruncatingTail];
	[status setContentCompressionResistancePriority: NSLayoutPriorityDefaultLow forOrientation: NSLayoutConstraintOrientationHorizontal];
	{
		NSStackView *statusRow = [NSStackView stackViewWithViews: @[ statusIcon, status ]], *expansionRow;
		[statusRow setSpacing: 4];
		expansionIcon = [[NSImageView alloc] init];
		expansion = VSP_Label( @"", [NSFont systemFontOfSize: 11], [NSColor secondaryLabelColor] );
		[expansion setLineBreakMode: NSLineBreakByTruncatingTail];
		[expansion setContentCompressionResistancePriority: NSLayoutPriorityDefaultLow forOrientation: NSLayoutConstraintOrientationHorizontal];
		expansionRow = [NSStackView stackViewWithViews: @[ expansionIcon, expansion ]];
		[expansionRow setSpacing: 4];
		folderText = [NSStackView stackViewWithViews: @[ folderPath, statusRow, expansionRow ]];
	}
	[folderText setOrientation: NSUserInterfaceLayoutOrientationVertical];
	[folderText setAlignment: NSLayoutAttributeLeading];
	[folderText setSpacing: 3];
	showButton = [NSButton buttonWithTitle: @"Show in Finder" target: self action: @selector(showFolder:)];
	addButton = [NSButton buttonWithTitle: @"Add Game Files…" target: self action: @selector(addFiles:)];
	[addButton setToolTip: @"Copy more .pk3 files into the game, such as the Expansion Pack's or extra maps"];
	changeButton = [NSButton buttonWithTitle: @"Change…" target: self action: @selector(changeFolder:)];
	{
		NSStackView *info = [NSStackView stackViewWithViews: @[ folderIcon, folderText ]];
		NSStackView *buttons = [NSStackView stackViewWithViews: @[ [NSView new], showButton, addButton, changeButton ]];

		[info setSpacing: 10];
		[info setAlignment: NSLayoutAttributeCenterY];
		[folderText setContentHuggingPriority: NSLayoutPriorityDefaultLow forOrientation: NSLayoutConstraintOrientationHorizontal];
		[[buttons views][0] setContentHuggingPriority: 1 forOrientation: NSLayoutConstraintOrientationHorizontal];
		[buttons setSpacing: 8];
		folderRow = [NSStackView stackViewWithViews: @[ info, buttons ]];
		[folderRow setOrientation: NSUserInterfaceLayoutOrientationVertical];
		[folderRow setAlignment: NSLayoutAttributeLeading];
		[folderRow setSpacing: 10];
		[[buttons widthAnchor] constraintEqualToAnchor: [folderRow widthAnchor]].active = YES;
		[[info widthAnchor] constraintEqualToAnchor: [folderRow widthAnchor]].active = YES;
	}

	// footer
	quit = [NSButton buttonWithTitle: @"Quit" target: self action: @selector(quit:)];
	[quit setKeyEquivalent: @"\033"];
	playButton = [NSButton buttonWithTitle: @"Play" target: self action: @selector(play:)];
	[playButton setKeyEquivalent: @"\r"];
	[playButton setControlSize: NSControlSizeLarge];
	[quit setControlSize: NSControlSizeLarge];
	[[playButton widthAnchor] constraintGreaterThanOrEqualToConstant: 110].active = YES;
	footer = [NSStackView stackViewWithViews: @[
		VSP_Label( version ? [NSString stringWithFormat: @"Version %@", version] : @"", [NSFont systemFontOfSize: 11], [NSColor tertiaryLabelColor] ),
		[NSView new], quit, playButton ]];
	[[footer views][1] setContentHuggingPriority: 1 forOrientation: NSLayoutConstraintOrientationHorizontal];
	[footer setSpacing: 10];
	[footer setAlignment: NSLayoutAttributeCenterY];

	graphicsGroup = VSP_Group( @"Graphics", grid );
	{
		// top-aligned in a box as tall as the graphics group
		NSView *box = [[NSView alloc] init];
		[serverGrid setTranslatesAutoresizingMaskIntoConstraints: NO];
		[box addSubview: serverGrid];
		[NSLayoutConstraint activateConstraints: @[
			[[serverGrid leadingAnchor] constraintEqualToAnchor: [box leadingAnchor]],
			[[serverGrid trailingAnchor] constraintLessThanOrEqualToAnchor: [box trailingAnchor]],
			[[serverGrid topAnchor] constraintEqualToAnchor: [box topAnchor]],
			[[serverGrid bottomAnchor] constraintLessThanOrEqualToAnchor: [box bottomAnchor]],
		]];
		serverGroup = VSP_Group( @"Server", box );
	}
	root = [NSStackView stackViewWithViews: @[ header, cardRow, graphicsGroup, serverGroup,
		VSP_Group( @"Holomatch", nameGrid ), VSP_Group( @"Game Files", folderRow ), footer ]];
	[root setOrientation: NSUserInterfaceLayoutOrientationVertical];
	[root setAlignment: NSLayoutAttributeLeading];
	[root setSpacing: 16];
	[root setCustomSpacing: 20 afterView: header];
	[root setCustomSpacing: 20 afterView: [root views][5]];
	[root setEdgeInsets: NSEdgeInsetsMake( 30, 28, 20, 28 )];
	// both take the same room, so the window keeps its size when they swap
	[[[(NSStackView *)serverGroup views][1] heightAnchor] constraintEqualToAnchor:
		[[(NSStackView *)graphicsGroup views][1] heightAnchor]].active = YES;
	[serverGroup setHidden: YES];
	[root setTranslatesAutoresizingMaskIntoConstraints: NO];
	[content addSubview: root];
	for( NSView *v in [root views] )
		if( v != header )
			[[v widthAnchor] constraintEqualToAnchor: [root widthAnchor] constant: -56].active = YES;
	[NSLayoutConstraint activateConstraints: @[
		[[root leadingAnchor] constraintEqualToAnchor: [content leadingAnchor]],
		[[root trailingAnchor] constraintEqualToAnchor: [content trailingAnchor]],
		[[root topAnchor] constraintEqualToAnchor: [content topAnchor]],
		[[root bottomAnchor] constraintEqualToAnchor: [content bottomAnchor]],
		[[root widthAnchor] constraintEqualToConstant: 620],
	]];
	[window setInitialFirstResponder: cards[mode]];
}

// Window sizes (points) that fit the screen, plus the configured one.
- (void)fillSizes
{
	NSRect vis = [[NSScreen mainScreen] visibleFrame];
	int maxW = (int)vis.size.width, maxH = (int)vis.size.height - 28;	// title bar
	BOOL haveCurrent = NO;
	size_t i;

	[size removeAllItems];
	for( i = 0; i < ARRAY_LEN( windowSizes ); i++ )
	{
		int w = windowSizes[i][0], h = windowSizes[i][1];
		if( ( w > maxW || h > maxH ) && !( w == gfx.width && h == gfx.height ) )
			continue;
		[size addItemWithTitle: [NSString stringWithFormat: @"%d × %d", w, h]];
		[[size lastItem] setTag: w * 10000 + h];
		haveCurrent |= ( w == gfx.width && h == gfx.height );
	}
	if( !haveCurrent )
	{
		[size addItemWithTitle: [NSString stringWithFormat: @"%d × %d", gfx.width, gfx.height]];
		[[size lastItem] setTag: gfx.width * 10000 + gfx.height];
	}
	[size selectItemWithTag: gfx.width * 10000 + gfx.height];
}

- (preset_t)matchingPreset
{
	size_t i;
	for( i = 0; i < ARRAY_LEN( presets ); i++ )
		if( presets[i].highdpi == gfx.highdpi && presets[i].msaa == gfx.msaa &&
			presets[i].picmip == gfx.picmip && presets[i].vsync == gfx.vsync )
			return presets[i].preset;
	return PRESET_CUSTOM;
}

- (void)showGraphics
{
	updating = YES;
	[display selectItemWithTag: gfx.fullscreen];
	[self fillSizes];
	[size setEnabled: !gfx.fullscreen];
	[retina setState: gfx.highdpi ? NSControlStateValueOn : NSControlStateValueOff];
	[msaa selectItemWithTag: gfx.msaa];
	[texture selectItemWithTag: gfx.picmip];
	[vsync setState: gfx.vsync ? NSControlStateValueOn : NSControlStateValueOff];
	[preset setSelectedSegment: [self matchingPreset]];
	updating = NO;
}

- (void)graphicsChanged: (id)sender
{
	if( updating )
		return;
	gfx.fullscreen = [[display selectedItem] tag] != 0;
	gfx.width = (int)( [[size selectedItem] tag] / 10000 );
	gfx.height = (int)( [[size selectedItem] tag] % 10000 );
	gfx.highdpi = [retina state] == NSControlStateValueOn;
	gfx.msaa = (int)[[msaa selectedItem] tag];
	gfx.picmip = (int)[[texture selectedItem] tag];
	gfx.vsync = [vsync state] == NSControlStateValueOn;
	[self showGraphics];
}

- (void)presetChanged: (id)sender
{
	NSInteger p = [preset selectedSegment];

	if( p < 0 || p >= (NSInteger)ARRAY_LEN( presets ) )
		return;
	gfx.highdpi = presets[p].highdpi;
	gfx.msaa = presets[p].msaa;
	gfx.picmip = presets[p].picmip;
	gfx.vsync = presets[p].vsync;
	[self showGraphics];
}

- (void)selectMode: (launchMode_t)m
{
	BOOL client = m == MODE_HOLOMATCH, host = m == MODE_HOST;

	mode = m;
	for( int i = 0; i < NUM_MODES; i++ )
		cards[i].selected = i == m;
	[name setEnabled: client || host];
	[nameLabel setTextColor: client || host ? [NSColor labelColor] : [NSColor tertiaryLabelColor]];
	[nameHint setStringValue: client ? @"Shown to other players in Holomatch." :
		host ? @"Your name when you use Play on This Mac." : @"Used when you play Holomatch."];
	[playButton setTitle: client ? @"Play Holomatch" : host ? @"Start Server" : @"Play"];
	[graphicsGroup setHidden: host];
	[serverGroup setHidden: !host];
	if( host )
		[self showServer];
	for( int i = 0; i < NUM_MODES; i++ )
		if( [window firstResponder] == cards[i] )
			[window makeFirstResponder: cards[m]];
}

// The Server group: a summary of the Config… settings.
- (void)showServer
{
	NSArray *maps = VSP_MapCatalog( dataFolder );
	NSMutableArray *mapNames = [NSMutableArray array];
	NSInteger missing = 0;
	NSArray *mods = [server modifierNames];
	NSMutableString *players;

	[srvName setStringValue: server.hostname];
	[srvGame setStringValue: [mods count] ? [NSString stringWithFormat: @"%@ · %@",
		[VSPServerSettings gametypeName: server.gametype], [mods componentsJoinedByString: @", "]]
		: [VSPServerSettings gametypeName: server.gametype]];

	for( NSString *n in server.rotation )
	{
		VSPMapInfo *mi = VSP_FindMap( maps, n );
		if( mi )
			[mapNames addObject: mi.displayName];
		else
			missing++;
	}
	if( ![server.rotation count] )
		[srvMaps setStringValue: @"None chosen yet"];
	else if( [server.rotation count] == 1 )
		[srvMaps setStringValue: [mapNames count] ? mapNames[0] : @"1 missing map"];
	else
		[srvMaps setStringValue: [NSString stringWithFormat: @"%lu in rotation: %@", (unsigned long)[server.rotation count],
			[mapNames componentsJoinedByString: @", "]]];
	[srvMaps setTextColor: missing || ![server.rotation count] ? [NSColor systemOrangeColor] : [NSColor labelColor]];
	if( missing && [server.rotation count] > 1 )
		[srvMaps setStringValue: [NSString stringWithFormat: @"%@ (%ld missing)", [srvMaps stringValue], (long)missing]];
	[srvMaps setToolTip: [server.rotation componentsJoinedByString: @" → "]];

	players = [NSMutableString stringWithFormat: @"Up to %ld", (long)server.maxPlayers];
	if( server.bots )
		[players appendFormat: @" · bots fill to %ld%@ (%@)", (long)server.bots,
			server.gametype >= VSP_GT_TEAM ? @" per team" : @"", [VSPServerSettings skillName: server.botSkill]];
	else
		[players appendString: @" · no bots"];
	[srvPlayers setStringValue: players];

	[srvNetwork setStringValue: [NSString stringWithFormat: @"%@ · port %ld%@",
		server.internet ? @"Internet" : @"Local network only", (long)server.port,
		[server.password length] ? @" · password" : @""]];
	[srvAddress setStringValue: [NSString stringWithFormat: @"%@:%ld", VSP_LANAddress() ? VSP_LANAddress() : @"127.0.0.1",
		(long)server.port]];
}

- (void)configServer: (id)sender
{
	[self selectMode: MODE_HOST];
	VSP_ShowServerConfig( window, server, dataFolder, ^( BOOL saved ) {
		[self showServer];
	} );
}

- (void)refreshData
{
	NSString *shown;
	NSColor *color;
	NSString *symbol;

	if( dataOverridden )
	{
		haveData = YES;
		basepath = nil;
		dataFolder = nil;
		shown = @"Set on the command line";
		[status setStringValue: @"fs_basepath / fs_homepath given on the command line"];
		symbol = @"terminal";
		color = [NSColor secondaryLabelColor];
	}
	else
	{
		NSString *bp, *base;
		haveData = VSP_FindGameData( &bp, &base );
		basepath = bp;
		dataFolder = base;
		if( haveData )
		{
			shown = [dataFolder stringByAbbreviatingWithTildeInPath];
			[status setStringValue: bp ? @"Ready · using the files in this folder"
				: @"Ready · copied into the app's own folder"];
			symbol = @"checkmark.circle.fill";
			color = [NSColor systemGreenColor];
		}
		else
		{
			shown = @"No game files yet";
			[status setStringValue: @"Choose the Elite Force folder (the one with BaseEF)."];
			symbol = @"exclamationmark.triangle.fill";
			color = [NSColor systemOrangeColor];
		}
	}
	[folderPath setStringValue: shown];
	[folderPath setToolTip: dataFolder];
	[folderIcon setImage: dataFolder ? [[NSWorkspace sharedWorkspace] iconForFile: dataFolder]
		: [NSImage imageNamed: NSImageNameFolder]];
	[statusIcon setImage: [NSImage imageWithSystemSymbolName: symbol accessibilityDescription: nil]];
	[statusIcon setContentTintColor: color];
	[showButton setHidden: dataFolder == nil];
	[addButton setHidden: !haveData || dataOverridden];
	[expansionIcon setHidden: !haveData || dataOverridden];
	[expansion setHidden: !haveData || dataOverridden];
	if( haveData && !dataOverridden )
	{
		BOOL have = VSP_HasExpansion( dataFolder );
		[expansion setStringValue: have ? @"Expansion Pack found · Virtual Voyager and its Holomatch maps"
			: @"No Expansion Pack · add its files to play Virtual Voyager and its maps"];
		[expansionIcon setImage: [NSImage imageWithSystemSymbolName: have ? @"puzzlepiece.extension.fill" : @"puzzlepiece.extension"
			accessibilityDescription: nil]];
		[expansionIcon setContentTintColor: have ? [NSColor systemGreenColor] : [NSColor tertiaryLabelColor]];
	}
	[changeButton setEnabled: !dataOverridden];
	[changeButton setTitle: haveData ? @"Change…" : @"Choose…"];
	[playButton setEnabled: haveData];
}

- (void)showFolder: (id)sender
{
	if( dataFolder )
		VSP_RevealFolder( dataFolder );
}

- (void)addFiles: (id)sender
{
	if( VSP_AddGameFiles() > 0 )
		LLog( @"Launcher: game files added" );
	[self refreshData];
	[window makeKeyAndOrderFront: nil];
}

- (void)changeFolder: (id)sender
{
	NSString *chosen = nil;

	if( VSP_ChooseGameData( &chosen ) )
		LLog( @"Launcher: game folder changed" );
	[self refreshData];
	[window makeKeyAndOrderFront: nil];
}

- (void)quit: (id)sender
{
	[NSApp stopModalWithCode: NSModalResponseCancel];
}

- (BOOL)windowShouldClose: (NSWindow *)sender
{
	[self quit: sender];
	return NO;
}

- (void)play: (id)sender
{
	if( !haveData )
		return;
	[NSApp stopModalWithCode: mode == MODE_HOST ? RESPONSE_HOST : NSModalResponseOK];
}

- (launchMode_t)mode	{ return mode; }
- (VSPServerSettings *)server	{ return server; }
- (NSString *)basepath	{ return basepath; }
- (NSString *)dataFolder	{ return dataFolder; }

// What a Holomatch client started from here gets: the graphics and the name.
- (NSArray<NSString *> *)clientArgs
{
	NSMutableArray *a = [NSMutableArray arrayWithArray: @[ @"+set", @"com_holomatch", @"1",
		@"+set", @"r_fullscreen", @( gfx.fullscreen ).stringValue, @"+set", @"r_mode", @"-1",
		@"+set", @"r_customwidth", @( gfx.width ).stringValue, @"+set", @"r_customheight", @( gfx.height ).stringValue,
		@"+set", @"r_highdpi", @( gfx.highdpi ).stringValue, @"+set", @"r_picmip", @( gfx.picmip ).stringValue,
		@"+set", @"r_swapInterval", @( gfx.vsync ).stringValue,
		@"+set", @"r_fbo", @"1", @"+set", @"r_ext_multisample", @( gfx.msaa ).stringValue,
		@"+set", @"name", VSP_CleanName( [name stringValue] ) ]];
	if( basepath )
		[a addObjectsFromArray: @[ @"+set", @"fs_basepath", basepath ]];
	return a;
}

// Append the chosen mode and settings to the engine's command line.
- (void)appendTo: (char *)commandLine size: (int)size
{
	NSMutableString *args = [NSMutableString string];

	BOOL holomatch = mode != MODE_SP;

	[args appendFormat: @"+set com_holomatch %d ", holomatch];
	[args appendFormat: @"+set r_fullscreen %d +set r_mode -1 +set r_customwidth %d +set r_customheight %d ",
		gfx.fullscreen, gfx.width, gfx.height];
	[args appendFormat: @"+set r_highdpi %d +set r_picmip %d +set r_swapInterval %d ",
		gfx.highdpi, gfx.picmip, gfx.vsync];
	// framebuffer objects always: multisampling needs them, and their gamma
	// post-pass is what makes the Brightness sliders apply live
	[args appendFormat: @"+set r_fbo 1 +set r_ext_multisample %d ", gfx.msaa];
	if( holomatch )
		[args appendFormat: @"+set name \"%@\" ", VSP_CleanName( [name stringValue] )];
	if( basepath )
		[args appendFormat: @"+set fs_basepath \"%@\" ", basepath];
	LLog( @"Launcher: %@", args );
	Q_strcat( commandLine, size, [args UTF8String] );
}

- (NSWindow *)window	{ return window; }

@end

/* ------------------------------------------------------------------------
   Entry point
   ------------------------------------------------------------------------ */

// VOYAGERSP_TEST_LAUNCHER_APPEARANCE=dark|light, for the launcher's windows
void VSP_ApplyTestAppearance( NSWindow *w )
{
	NSAppearance *a;

	if( !getenv( "VOYAGERSP_TEST_LAUNCHER_APPEARANCE" ) )
		return;
	a = [NSAppearance appearanceNamed: VSP_TestEnvIs( "VOYAGERSP_TEST_LAUNCHER_APPEARANCE", "dark" ) ?
		NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
	[NSApp setAppearance: a];
	[w setAppearance: a];
}

BOOL VSP_SnapshotWindow( NSWindow *w, const char *path )
{
	NSView *v = [w contentView];
	__block NSData *png = nil;

	[w layoutIfNeeded];
	[v layoutSubtreeIfNeeded];
	[v displayIfNeeded];
	[[v effectiveAppearance] performAsCurrentDrawingAppearance: ^{
		NSBitmapImageRep *r = [v bitmapImageRepForCachingDisplayInRect: [v bounds]];
		NSBitmapImageRep *out = [r copy];
		NSGraphicsContext *ctx;

		[v cacheDisplayInRect: [v bounds] toBitmapImageRep: r];
		// the window background is not part of the view: put it under
		ctx = [NSGraphicsContext graphicsContextWithBitmapImageRep: out];
		[NSGraphicsContext saveGraphicsState];
		[NSGraphicsContext setCurrentContext: ctx];
		[[NSColor windowBackgroundColor] setFill];
		NSRectFill( NSMakeRect( 0, 0, [out size].width, [out size].height ) );
		[r drawInRect: NSMakeRect( 0, 0, [out size].width, [out size].height ) fromRect: NSZeroRect
			operation: NSCompositingOperationSourceOver fraction: 1 respectFlipped: YES hints: nil];
		[NSGraphicsContext restoreGraphicsState];
		png = [out representationUsingType: NSBitmapImageFileTypePNG properties: @{}];
	}];
	LLog( @"[test] window %.0fx%.0f pt -> %s", [v bounds].size.width, [v bounds].size.height, path );
	return [png writeToFile: [NSString stringWithUTF8String: path] atomically: YES];
}

/*
=================
Sys_MacLauncher

Called from main() before Com_Init. Returns qfalse when there is no
launcher (not the .app, or the command line already names the mode);
otherwise shows it, appends the choices to commandLine and returns qtrue,
or quits when the user does.
=================
*/
qboolean Sys_MacLauncher( int argc, char **argv, char *commandLine, int size )
{
	int i;

	for( i = 1; i < argc; i++ )
		if( !Q_stricmp( argv[i], "com_holomatch" ) )
			return qfalse;

	@autoreleasepool {
		VSPLauncher *l;
		NSModalResponse r;
		id monitor;
		BOOL testHost = getenv( "VOYAGERSP_TEST_SERVER_START" ) != NULL;

		if( !Sys_MacAppBundlePath() )
			return qfalse;

		// SDL's video init sets up NSApp (SDL's NSApplication subclass), as
		// for the game folder chooser
		SDL_InitSubSystem( SDL_INIT_VIDEO );
		l = [[VSPLauncher alloc] initWithOverridden: VSP_DataOverridden( argc, argv )];

		if( getenv( "VOYAGERSP_TEST_ADD" ) )
			[l addFiles: nil];		// with sys_osx_data.m's hook instead of the open panel
		if( getenv( "VOYAGERSP_TEST_SERVER_CONFIG_SNAPSHOT" ) )
		{
			VSP_SnapshotServerConfig( [l server], [l dataFolder], getenv( "VOYAGERSP_TEST_SERVER_CONFIG_TAB" ),
				getenv( "VOYAGERSP_TEST_SERVER_CONFIG_SNAPSHOT" ) );
			if( !getenv( "VOYAGERSP_TEST_LAUNCHER_SNAPSHOT" ) )
			{
				SDL_Quit( );
				exit( 0 );
			}
		}
		if( getenv( "VOYAGERSP_TEST_LAUNCHER_SNAPSHOT" ) )
		{
			VSP_SnapshotWindow( [l window], getenv( "VOYAGERSP_TEST_LAUNCHER_SNAPSHOT" ) );
			if( !getenv( "VOYAGERSP_TEST_LAUNCHER_PLAY" ) && !testHost )
			{
				SDL_Quit( );
				exit( 0 );
			}
		}

		// Cmd+Q: SDL's Quit menu item only posts an SDL event, which nothing
		// reads yet
		monitor = [NSEvent addLocalMonitorForEventsMatchingMask: NSEventMaskKeyDown handler: ^NSEvent *( NSEvent *e ) {
			if( ( [e modifierFlags] & NSEventModifierFlagCommand ) && [[e charactersIgnoringModifiers] isEqualToString: @"q"] )
			{
				[NSApp stopModalWithCode: NSModalResponseCancel];
				return nil;
			}
			return e;
		}];
		for( ;; )
		{
			if( getenv( "VOYAGERSP_TEST_LAUNCHER_PLAY" ) )
				r = NSModalResponseOK;
			else if( testHost )
			{
				[l selectMode: MODE_HOST];
				r = RESPONSE_HOST;
			}
			else
			{
				if( getenv( "VOYAGERSP_TEST_LAUNCHER_AUTOPLAY" ) )
				{
					NSTimer *t = [NSTimer timerWithTimeInterval: atof( getenv( "VOYAGERSP_TEST_LAUNCHER_AUTOPLAY" ) ) repeats: NO block: ^( NSTimer *timer ) {
						LLog( @"[test] launcher %s, pressing Play", [[l window] isVisible] ? "visible" : "not visible" );
						[l play: nil];
					}];
					[[NSRunLoop currentRunLoop] addTimer: t forMode: NSModalPanelRunLoopMode];
				}
				[[l window] center];
				[NSApp activateIgnoringOtherApps: YES];
				[[l window] makeKeyAndOrderFront: nil];
				r = [NSApp runModalForWindow: [l window]];
				[[l window] orderOut: nil];
			}
			if( r != RESPONSE_HOST )
				break;

			// Host a Server: this process shows the server, never the game
			[[NSUserDefaults standardUserDefaults] setInteger: MODE_HOST forKey: KEY_LAUNCHERMODE];
			[[l server] save];
			r = VSP_RunServerWindow( [l server], [l basepath], [l dataFolder], [l clientArgs] );
			if( r != NSModalResponseStop || testHost )
			{
				r = NSModalResponseCancel;
				break;
			}
			// back to the launcher
		}
		[NSEvent removeMonitor: monitor];

		if( r != NSModalResponseOK )
		{
			LLog( @"Launcher: quit" );
			SDL_Quit( );
			exit( 0 );
		}
		[[NSUserDefaults standardUserDefaults] setInteger: [l mode] forKey: KEY_LAUNCHERMODE];
		[[NSUserDefaults standardUserDefaults] synchronize];
		[l appendTo: commandLine size: size];
		SDL_QuitSubSystem( SDL_INIT_VIDEO );
	}
	return qtrue;
}
