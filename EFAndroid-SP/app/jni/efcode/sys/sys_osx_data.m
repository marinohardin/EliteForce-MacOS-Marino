/*
===========================================================================
macOS app bundle: game data location, first-run chooser and app menu items.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).

The .app ships no game data. At startup (bundle only, and only when the
command line does not override the data paths) Sys_MacResolveGameData looks,
without any UI, for BaseEF/pak0.pk3 in:
  a) the game folder remembered from an earlier "use in place" choice
     (NSUserDefaults key GameFolder, in the app's bundle-id domain);
  b) ~/Library/Application Support/<MACOS_HOMEPATH_NAME>/baseEF  (files the
     app copied there; this is also fs_homepath, so it is searched anyway);
  c) a baseEF folder next to the .app (ignored when App Translocation runs the
     app from a randomised read-only path).
If none has it, an NSOpenPanel asks for the Elite Force install (or its
BaseEF folder). By default the .pk3 files (and old saves) are copied into (b);
the "Use BaseEF files in place rather than copy" checkbox instead remembers
the chosen folder and uses it as fs_basepath.

Normally the launcher (sys_osx_launcher.m) runs first: it shows where the
game data was found (VSP_FindGameData, the same lookup) and offers the
chooser (VSP_ChooseGameData). Sys_MacResolveGameData is the path without it,
when the mode is on the command line (a relaunch from the File menu, tests).

Sys_MacFrame adds a File menu (Return to Launcher…, Switch to Holomatch /
Single Player, Show Saves Folder, Show Game Folder, Change Game Folder…,
Delete Saves & Settings…) after the application menu once SDL has created
the menu bar. Those that change the mode or folder relaunch the app
(open -n, after this process has exited).
The same actions are console commands (macos_launcher, macos_switchmode,
macos_showsaves, macos_showgamefolder, macos_changegamefolder,
macos_deletesettings).

Delete Saves & Settings only ever moves ~/Library/Application Support/
<MACOS_HOMEPATH_NAME> to the Trash, after the engine has shut down. It never
touches a user-chosen game folder or anything outside that directory.

Test hooks (environment variables, for automated tests without clicking):
  VOYAGERSP_TEST_CHOOSE=<dir>  use <dir> instead of showing the open panel
  VOYAGERSP_TEST_INPLACE=1     ...with the "in place" checkbox ticked
  VOYAGERSP_TEST_CONFIRM=1     answer confirmation alerts with the first button
  VOYAGERSP_TEST_NOTRASH=1     log instead of moving to the Trash
  VOYAGERSP_TEST_NORELAUNCH=1  log instead of relaunching the app
  VOYAGERSP_TEST_PANEL_TIMEOUT=<s>  log the open panel's size and cancel it after <s> s
===========================================================================
*/

#ifndef __APPLE__
#error This file is for macOS only.
#endif

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "sys_local.h"

#import <Cocoa/Cocoa.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <dlfcn.h>
#include <spawn.h>
#include <sys/wait.h>

extern char **environ;

void SNDDMA_PauseOutput( qboolean pause );	// sdl_snd.c

#ifdef USE_LOCAL_HEADERS
#	include "SDL.h"
#else
#	include <SDL.h>
#endif

#ifndef MACOS_HOMEPATH_NAME
#define MACOS_HOMEPATH_NAME "EliteForce-MacOS"
#endif

#define KEY_GAMEFOLDER	@"GameFolder"
#define KEY_PANELSIZERESET	@"ChooserPanelSizeReset"
#define MSG_PAK0_ON_CD	@"pak0.pk3 is missing — minimum CD installs leave it on the CD; copy it from BaseEF on the CD into this BaseEF folder, or choose a full installation."

extern NSString *Sys_MacAppBundlePath( void );

static NSMutableString *earlyLog;	// messages from before Com_Init, replayed into the console
static qboolean pendingDelete;
static int pendingRelaunch = -2;	// mode to relaunch in at exit: 0 SP, 1 Holomatch, -1 launcher

/* ------------------------------------------------------------------------ */

static void MacLog( NSString *fmt, ... ) NS_FORMAT_FUNCTION(1,2);
static void MacLog( NSString *fmt, ... )
{
	va_list ap;
	NSString *s;

	va_start( ap, fmt );
	s = [[[NSString alloc] initWithFormat: fmt arguments: ap] autorelease];
	va_end( ap );

	if( com_fullyInitialized )
	{
		Com_Printf( "%s\n", [s UTF8String] );
		return;
	}
	fprintf( stderr, "%s\n", [s UTF8String] );
	if( !earlyLog )
		earlyLog = [[NSMutableString alloc] init];
	[earlyLog appendFormat: @"%@\n", s];
}

static qboolean TestEnv( const char *name )
{
	const char *v = getenv( name );
	return v && *v && strcmp( v, "0" );
}

// $HOME, like Sys_DefaultHomePath(), so both agree on where the homepath is
static NSString *HomeRoot( void )
{
	const char *home = getenv( "HOME" );
	NSString *h = ( home && *home ) ? [NSString stringWithUTF8String: home] : NSHomeDirectory();
	return [[h stringByAppendingPathComponent: @"Library/Application Support"]
		stringByAppendingPathComponent: @MACOS_HOMEPATH_NAME];
}

static NSString *HomeBase( void )
{
	return [HomeRoot() stringByAppendingPathComponent: @BASEGAME];
}

// Existing entry of dir whose name matches name case-insensitively, or nil.
static NSString *ChildCI( NSString *dir, NSString *name )
{
	NSString *exact = [dir stringByAppendingPathComponent: name];

	if( !dir )
		return nil;
	if( [[NSFileManager defaultManager] fileExistsAtPath: exact] )
		return exact;		// default APFS is case-insensitive anyway
	for( NSString *e in [[NSFileManager defaultManager] contentsOfDirectoryAtPath: dir error: NULL] )
		if( [e caseInsensitiveCompare: name] == NSOrderedSame )
			return [dir stringByAppendingPathComponent: e];
	return nil;
}

static NSString *BaseIn( NSString *folder )	{ return ChildCI( folder, @BASEGAME ); }
static NSString *Pak0In( NSString *base )	{ return ChildCI( base, @"pak0.pk3" ); }

static BOOL IsDir( NSString *path )
{
	BOOL d = NO;
	return path && [[NSFileManager defaultManager] fileExistsAtPath: path isDirectory: &d] && d;
}

static BOOL IsTranslocated( NSString *bundle )
{
	typedef Boolean (*fn_t)( CFURLRef, bool *, CFErrorRef * );
	static fn_t fn;
	static qboolean looked;
	bool translocated = false;

	if( [bundle rangeOfString: @"/AppTranslocation/"].location != NSNotFound )
		return YES;
	if( !looked )
	{
		void *h = dlopen( "/System/Library/Frameworks/Security.framework/Security", RTLD_LAZY );
		fn = h ? (fn_t)dlsym( h, "SecTranslocateIsTranslocatedURL" ) : NULL;
		looked = qtrue;
	}
	if( fn && fn( (CFURLRef)[NSURL fileURLWithPath: bundle], &translocated, NULL ) && translocated )
		return YES;
	return NO;
}

static NSString *FormatBytes( unsigned long long n )
{
	return [NSByteCountFormatter stringFromByteCount: (long long)n countStyle: NSByteCountFormatterCountStyleFile];
}

// Modal alert; returns the index of the button pressed (0 = first).
static NSInteger Alert( NSAlertStyle style, NSString *title, NSString *text, NSArray *buttons, BOOL destructiveFirst )
{
	NSAlert *a;
	NSInteger r;

	MacLog( @"[dialog] %@ — %@", title, [text stringByReplacingOccurrencesOfString: @"\n" withString: @" "] );
	if( TestEnv( "VOYAGERSP_TEST_CONFIRM" ) )
		return 0;

	a = [[[NSAlert alloc] init] autorelease];
	[a setAlertStyle: style];
	[a setMessageText: title];
	[a setInformativeText: text];
	for( NSString *b in buttons )
		[a addButtonWithTitle: b];
	if( destructiveFirst && [[[a buttons] objectAtIndex: 0] respondsToSelector: @selector(setHasDestructiveAction:)] )
		[[[a buttons] objectAtIndex: 0] setHasDestructiveAction: YES];
	[NSApp activateIgnoringOtherApps: YES];
	r = [a runModal] - NSAlertFirstButtonReturn;
	return r;
}

static void ErrorAlert( NSString *title, NSString *text )
{
	Alert( NSAlertStyleWarning, title, text, @[ @"OK" ], NO );
}

/* ------------------------------------------------------------------------
   Copying game files into the homepath
   ------------------------------------------------------------------------ */

@interface VSPCopyJob : NSObject
{
@public
	NSArray *src, *dst;		// parallel arrays of paths
	unsigned long long total;
	volatile unsigned long long copied;
	volatile BOOL cancel, done, failed;
	NSString *error;		// retained
	NSMutableArray *created;	// destination files that did not exist before
}
- (void)cancelPressed: (id)sender;
@end

@implementation VSPCopyJob
- (void)cancelPressed: (id)sender { cancel = YES; }
- (void)dealloc { [src release]; [dst release]; [error release]; [created release]; [super dealloc]; }
@end

static BOOL CopyOneFile( VSPCopyJob *job, const char *from, const char *to, char *err, size_t errSize )
{
	char part[PATH_MAX];
	static char buf[1 << 20];
	struct stat st;
	struct timeval tv[2];
	int in, out;
	ssize_t n;

	Com_sprintf( part, sizeof( part ), "%s.part", to );
	if( ( in = open( from, O_RDONLY ) ) < 0 || fstat( in, &st ) )
	{
		Com_sprintf( err, errSize, "Cannot read %s: %s", from, strerror( errno ) );
		if( in >= 0 ) close( in );
		return NO;
	}
	if( ( out = open( part, O_WRONLY | O_CREAT | O_TRUNC, 0644 ) ) < 0 )
	{
		Com_sprintf( err, errSize, "Cannot write %s: %s", part, strerror( errno ) );
		close( in );
		return NO;
	}
	while( ( n = read( in, buf, sizeof( buf ) ) ) > 0 )
	{
		if( job->cancel )
			break;
		if( write( out, buf, n ) != n )
		{
			Com_sprintf( err, errSize, "Cannot write %s: %s", part, strerror( errno ) );
			n = -2;
			break;
		}
		job->copied += n;
	}
	if( n == -1 )
		Com_sprintf( err, errSize, "Cannot read %s: %s", from, strerror( errno ) );
	close( in );
	if( close( out ) && n == 0 )
	{
		Com_sprintf( err, errSize, "Cannot write %s: %s", part, strerror( errno ) );
		n = -2;
	}
	if( n != 0 || job->cancel )
	{
		unlink( part );
		if( job->cancel )
			Q_strncpyz( err, "Cancelled.", errSize );
		return NO;
	}
	// keep the original dates (the save menu shows them)
	TIMESPEC_TO_TIMEVAL( &tv[0], &st.st_atimespec );
	TIMESPEC_TO_TIMEVAL( &tv[1], &st.st_mtimespec );
	utimes( part, tv );
	// rename into place: a running engine keeps reading the old inode
	if( rename( part, to ) )
	{
		Com_sprintf( err, errSize, "Cannot rename %s: %s", part, strerror( errno ) );
		unlink( part );
		return NO;
	}
	return YES;
}

static void CopyWorker( VSPCopyJob *job )
{
	@autoreleasepool {
		char err[MAX_OSPATH * 2] = "";
		NSFileManager *fm = [NSFileManager defaultManager];
		NSUInteger i;

		for( i = 0; i < [job->src count]; i++ )
		{
			NSString *to = [job->dst objectAtIndex: i];
			BOOL existed = [fm fileExistsAtPath: to];

			[fm createDirectoryAtPath: [to stringByDeletingLastPathComponent]
				withIntermediateDirectories: YES attributes: nil error: NULL];
			if( !CopyOneFile( job, [[job->src objectAtIndex: i] fileSystemRepresentation],
					[to fileSystemRepresentation], err, sizeof( err ) ) )
			{
				job->failed = YES;
				break;
			}
			if( !existed )
				@synchronized( job ) { [job->created addObject: to]; }
		}
		if( job->failed )
		{
			// remove what this copy created; files that existed before stay
			for( NSString *p in job->created )
				[fm removeItemAtPath: p error: NULL];
			job->error = [[NSString alloc] initWithUTF8String: err];
		}
		job->done = YES;
	}
}

// Copy *.pk3 and saves/ from srcBase into HomeBase(), with a progress panel.
static BOOL CopyGameFiles( NSString *srcBase )
{
	NSFileManager *fm = [NSFileManager defaultManager];
	NSString *dstBase = HomeBase();
	NSMutableArray *src = [NSMutableArray array], *dst = [NSMutableArray array];
	unsigned long long total = 0, freeBytes = 0;
	NSString *saves;
	NSNumber *avail = nil;
	NSError *err = nil;
	VSPCopyJob *job;
	NSPanel *panel;
	NSProgressIndicator *bar;
	NSTextField *label;
	NSButton *cancel;
	NSModalSession session;
	BOOL ok;
	int pk3s = 0, savesCopied = 0, savesSkipped = 0;

	// top-level *.pk3 only (no .tmp/.bsp/.map/.cfg/.log)
	for( NSString *e in [fm contentsOfDirectoryAtPath: srcBase error: NULL] )
	{
		NSString *p = [srcBase stringByAppendingPathComponent: e];
		NSDictionary *a = [fm attributesOfItemAtPath: [p stringByResolvingSymlinksInPath] error: NULL];	// follow symlinks

		if( [e hasPrefix: @"."] || [[e pathExtension] caseInsensitiveCompare: @"pk3"] != NSOrderedSame )
			continue;
		if( ![[a fileType] isEqualToString: NSFileTypeRegular] )
			continue;
		[src addObject: p];
		[dst addObject: [dstBase stringByAppendingPathComponent: e]];
		total += [a fileSize];
		pk3s++;
	}
	// saves/: never overwrite saves already in the destination
	if( ( saves = ChildCI( srcBase, @"saves" ) ) && IsDir( saves ) )
	{
		NSDirectoryEnumerator *en = [fm enumeratorAtPath: saves];
		NSString *rel;

		while( ( rel = [en nextObject] ) )
		{
			NSString *p = [saves stringByAppendingPathComponent: rel];
			NSString *to = [[dstBase stringByAppendingPathComponent: @"saves"] stringByAppendingPathComponent: rel];
			NSDictionary *a = [en fileAttributes];

			if( ![[a fileType] isEqualToString: NSFileTypeRegular] || [[rel lastPathComponent] hasPrefix: @"."] )
				continue;
			if( [fm fileExistsAtPath: to] )
			{
				savesSkipped++;
				continue;
			}
			[src addObject: p];
			[dst addObject: to];
			total += [a fileSize];
			savesCopied++;
		}
	}
	MacLog( @"Copying %d .pk3 files and %d saves (%d existing saves kept) from \"%@\" to \"%@\": %@",
		pk3s, savesCopied, savesSkipped, srcBase, dstBase, FormatBytes( total ) );

	// free space
	[fm createDirectoryAtPath: dstBase withIntermediateDirectories: YES attributes: nil error: NULL];
	if( [[NSURL fileURLWithPath: dstBase] getResourceValue: &avail
			forKey: NSURLVolumeAvailableCapacityForImportantUsageKey error: &err] && avail )
		freeBytes = [avail unsignedLongLongValue];
	else if( ( avail = [[fm attributesOfFileSystemForPath: dstBase error: NULL] objectForKey: NSFileSystemFreeSize] ) )
		freeBytes = [avail unsignedLongLongValue];
	if( freeBytes && freeBytes < total + 64ull * 1024 * 1024 )
	{
		ErrorAlert( @"Not enough disk space",
			[NSString stringWithFormat: @"Copying the game files needs %@, but only %@ is free on the disk containing\n%@.\n\n"
				"Free up some space, or tick \"Use BaseEF files in place rather than copy\" to use the game folder directly.",
				FormatBytes( total + 64ull * 1024 * 1024 ), FormatBytes( freeBytes ), dstBase] );
		return NO;
	}

	job = [[[VSPCopyJob alloc] init] autorelease];
	job->src = [src copy];
	job->dst = [dst copy];
	job->total = total ? total : 1;
	job->created = [[NSMutableArray alloc] init];

	panel = [[[NSPanel alloc] initWithContentRect: NSMakeRect( 0, 0, 440, 120 )
		styleMask: NSWindowStyleMaskTitled backing: NSBackingStoreBuffered defer: NO] autorelease];
	[panel setTitle: @"Copying game files"];
	label = [NSTextField labelWithString: [NSString stringWithFormat: @"Copying game files to %@ …", dstBase]];
	[label setFrame: NSMakeRect( 20, 80, 400, 20 )];
	[label setLineBreakMode: NSLineBreakByTruncatingMiddle];
	bar = [[[NSProgressIndicator alloc] initWithFrame: NSMakeRect( 20, 52, 400, 20 )] autorelease];
	[bar setIndeterminate: NO];
	[bar setMinValue: 0];
	[bar setMaxValue: (double)job->total];
	cancel = [NSButton buttonWithTitle: @"Cancel" target: job action: @selector(cancelPressed:)];
	[cancel setFrame: NSMakeRect( 330, 12, 90, 30 )];
	[[panel contentView] addSubview: label];
	[[panel contentView] addSubview: bar];
	[[panel contentView] addSubview: cancel];
	[panel center];

	[job retain];
	dispatch_async( dispatch_get_global_queue( QOS_CLASS_USER_INITIATED, 0 ), ^{
		CopyWorker( job );
	} );
	[NSApp activateIgnoringOtherApps: YES];
	session = [NSApp beginModalSessionForWindow: panel];
	while( !job->done )
	{
		[NSApp runModalSession: session];
		[bar setDoubleValue: (double)job->copied];
		usleep( 20000 );
	}
	[NSApp endModalSession: session];
	[panel orderOut: nil];

	ok = !job->failed;
	if( ok && !Pak0In( dstBase ) )
	{
		ok = NO;
		ErrorAlert( @"Copying the game files failed", @"pak0.pk3 did not end up in the destination folder." );
	}
	else if( ok )
		MacLog( @"Copied %@ of game files to \"%@\"", FormatBytes( job->copied ), dstBase );
	else
	{
		MacLog( @"Copy failed: %@", job->error );
		if( !job->cancel )
			ErrorAlert( @"Copying the game files failed",
				[NSString stringWithFormat: @"%@\n\nThe partial copy was removed.", job->error] );
	}
	[job release];
	return ok;
}

/* ------------------------------------------------------------------------
   Chooser
   ------------------------------------------------------------------------ */

#define CHOOSER_TEXT_WIDTH	480

// Accessory view of the game folder open panel: an explanation, the
// "in place" checkbox and a line saying what the current choice will do.
@interface VSPChooserOptions : NSObject
{
	NSView *view;
	NSButton *check;
	NSTextField *status;
}
- (NSView *)view;
- (BOOL)inPlace;
- (void)toggled: (id)sender;
@end

@implementation VSPChooserOptions
static NSTextField *WrappingLabel( NSString *text, NSFont *font )
{
	NSTextField *l = [NSTextField wrappingLabelWithString: text];

	[l setFont: font];
	[l setSelectable: NO];
	[l setPreferredMaxLayoutWidth: CHOOSER_TEXT_WIDTH];
	[l setTranslatesAutoresizingMaskIntoConstraints: NO];
	[[[l widthAnchor] constraintEqualToConstant: CHOOSER_TEXT_WIDTH] setActive: YES];
	return l;
}

- (id)init
{
	NSStackView *stack;
	NSTextField *info;	// all autoreleased; view retains them
	NSSize size;

	if( !( self = [super init] ) )
		return nil;
	info = WrappingLabel( @"Select the folder where Star Trek: Voyager – Elite Force is installed "
		"(the one containing BaseEF with pak0.pk3), or the BaseEF folder itself. "
		"By default the game files are copied into the app's own folder in Application Support, "
		"so the original folder or CD is no longer needed.",
		[NSFont systemFontOfSize: [NSFont smallSystemFontSize]] );
	check = [NSButton checkboxWithTitle: @"Use BaseEF files in place rather than copy" target: self action: @selector(toggled:)];
	[check setState: NSControlStateValueOff];
	status = WrappingLabel( @"", [NSFont systemFontOfSize: [NSFont smallSystemFontSize]] );
	[status setTextColor: [NSColor secondaryLabelColor]];
	[self toggled: nil];

	stack = [NSStackView stackViewWithViews: @[ info, check, status ]];
	[stack setOrientation: NSUserInterfaceLayoutOrientationVertical];
	[stack setAlignment: NSLayoutAttributeLeading];
	[stack setSpacing: 8];
	[stack setCustomSpacing: 4 afterView: check];
	[stack setEdgeInsets: NSEdgeInsetsMake( 12, 20, 12, 20 )];
	[stack setTranslatesAutoresizingMaskIntoConstraints: NO];
	size = [stack fittingSize];

	// The panel stretches its accessory view to its own width; keep the
	// fixed-width block centred in it.
	view = [[NSView alloc] initWithFrame: NSMakeRect( 0, 0, size.width, size.height )];
	[view addSubview: stack];
	[[[stack centerXAnchor] constraintEqualToAnchor: [view centerXAnchor]] setActive: YES];
	[[[stack topAnchor] constraintEqualToAnchor: [view topAnchor]] setActive: YES];
	[[[stack bottomAnchor] constraintEqualToAnchor: [view bottomAnchor]] setActive: YES];
	return self;
}

- (void)dealloc { [view release]; [super dealloc]; }
- (NSView *)view { return view; }
- (BOOL)inPlace { return [check state] == NSControlStateValueOn; }

- (void)toggled: (id)sender
{
	[status setStringValue: [self inPlace]
		? @"Files will be read from this folder; nothing is copied."
		: @"Files will be copied into the app's data folder (about 600 MB for a standard install)."];
}
@end

// Ask for the install (or BaseEF) folder until a valid one is chosen.
// Returns NO on cancel. On success either *outBasepath is set (in place) or
// the files were copied into the homepath (*outBasepath = nil).
static BOOL RunChooser( NSString **outBasepath )
{
	NSString *testChoice = getenv( "VOYAGERSP_TEST_CHOOSE" ) ? [NSString stringWithUTF8String: getenv( "VOYAGERSP_TEST_CHOOSE" )] : nil;

	*outBasepath = nil;
	for( ;; )
	{
		NSString *chosen, *base, *problem = nil;
		BOOL inPlace;

		if( testChoice )
		{
			chosen = testChoice;
			inPlace = TestEnv( "VOYAGERSP_TEST_INPLACE" );
			MacLog( @"[test] chooser -> \"%@\" (in place: %d)", chosen, inPlace );
		}
		else
		{
			NSOpenPanel *op;
			VSPChooserOptions *opts;
			NSUserDefaults *ud = [NSUserDefaults standardUserDefaults];

			// Older builds had a one-line message that stretched the panel, and
			// AppKit remembers the panel size; forget it once so the panel
			// opens at its normal size. Later resizes by the user are kept.
			if( ![ud boolForKey: KEY_PANELSIZERESET] )
			{
				[ud removeObjectForKey: @"NSNavPanelExpandedSizeForOpenMode"];
				[ud setBool: YES forKey: KEY_PANELSIZERESET];
				[ud synchronize];
			}
			op = [NSOpenPanel openPanel];
			opts = [[[VSPChooserOptions alloc] init] autorelease];

			[op setCanChooseDirectories: YES];
			[op setCanChooseFiles: NO];
			[op setAllowsMultipleSelection: NO];
			[op setPrompt: @"Choose"];
			[op setMessage: @"Choose your Elite Force folder"];
			[op setAccessoryView: [opts view]];
			if( [op respondsToSelector: @selector(setAccessoryViewDisclosed:)] )
				[op setAccessoryViewDisclosed: YES];
			MacLog( @"[dialog] Choose your Elite Force folder (open panel, options %.0fx%.0f pt)",
				[[opts view] frame].size.width, [[opts view] frame].size.height );
			[NSApp activateIgnoringOtherApps: YES];
			if( getenv( "VOYAGERSP_TEST_PANEL_TIMEOUT" ) )
			{
				NSTimer *t = [NSTimer timerWithTimeInterval: atof( getenv( "VOYAGERSP_TEST_PANEL_TIMEOUT" ) ) repeats: NO block: ^( NSTimer *timer ) {
					MacLog( @"[test] open panel %.0fx%.0f pt, options %.0fx%.0f pt, message \"%@\"; cancelling",
						[op frame].size.width, [op frame].size.height,
						[[opts view] frame].size.width, [[opts view] frame].size.height, [op message] );
					[op cancel: nil];
				}];
				[[NSRunLoop currentRunLoop] addTimer: t forMode: NSModalPanelRunLoopMode];
			}
			if( [op runModal] != NSModalResponseOK || ![[op URLs] count] )
			{
				MacLog( @"Game folder selection cancelled" );
				return NO;
			}
			chosen = [[[op URLs] objectAtIndex: 0] path];
			inPlace = [opts inPlace];
			MacLog( @"Chosen game folder \"%@\" (in place: %d)", chosen, inPlace );
		}

		// accept the install folder or the BaseEF folder itself
		if( ( base = BaseIn( chosen ) ) && IsDir( base ) )
		{
			if( !Pak0In( base ) )
				problem = MSG_PAK0_ON_CD;
		}
		else if( Pak0In( chosen ) || [[chosen lastPathComponent] caseInsensitiveCompare: @BASEGAME] == NSOrderedSame )
		{
			base = chosen;
			if( !Pak0In( base ) )
				problem = MSG_PAK0_ON_CD;
		}
		else
			problem = @"This folder has no BaseEF folder. Choose the Elite Force installation folder "
				"(the one containing BaseEF), or the BaseEF folder itself.";

		if( !problem && inPlace && [[base lastPathComponent] caseInsensitiveCompare: @BASEGAME] != NSOrderedSame )
			problem = @"To use the files in place the folder must be called BaseEF. "
				"Rename it, or untick the checkbox to copy the files instead.";

		if( !problem && inPlace )
		{
			NSString *bp = [base stringByDeletingLastPathComponent];
			[[NSUserDefaults standardUserDefaults] setObject: bp forKey: KEY_GAMEFOLDER];
			[[NSUserDefaults standardUserDefaults] synchronize];
			MacLog( @"Using game files in place from \"%@\" (remembered)", base );
			*outBasepath = bp;
			return YES;
		}
		if( !problem )
		{
			if( CopyGameFiles( base ) )
			{
				// copied data wins; forget any external folder
				[[NSUserDefaults standardUserDefaults] removeObjectForKey: KEY_GAMEFOLDER];
				[[NSUserDefaults standardUserDefaults] synchronize];
				return YES;
			}
		}
		else
		{
			MacLog( @"Rejected \"%@\": %@", chosen, problem );
			ErrorAlert( @"That folder can't be used", [NSString stringWithFormat: @"%@\n\n%@", chosen, problem] );
		}
		if( testChoice )
			return NO;	// no endless loop in tests
	}
}

/* ------------------------------------------------------------------------
   Startup
   ------------------------------------------------------------------------ */

/*
=================
Sys_MacResolveGameData

Called from main() before Com_Init when running from the .app. Returns a
directory to use as fs_basepath, or NULL to keep the default. Quits when
the user cancels the chooser.
=================
*/
/*
=================
VSP_DataOverridden

qtrue when the command line sets the data paths itself (no lookup, no UI).
=================
*/
qboolean VSP_DataOverridden( int argc, char **argv )
{
	static const char *overrides[] = { "fs_basepath", "fs_homepath", "com_homepath",
		"fs_cdpath", "fs_steampath", "fs_gogpath", "com_basegame", NULL };
	int i, j;

	for( i = 1; i < argc; i++ )
		for( j = 0; overrides[j]; j++ )
			if( !Q_stricmp( argv[i], overrides[j] ) )
				return qtrue;
	return qfalse;
}

/*
=================
VSP_FindGameData

Looks for BaseEF/pak0.pk3 without any UI (a, b, c above). Returns NO if
none has it. On success *outBasepath is the fs_basepath to use (nil: the
homepath copy, the default) and *outBase the BaseEF folder found.
=================
*/
BOOL VSP_FindGameData( NSString **outBasepath, NSString **outBase )
{
	NSString *bundle = Sys_MacAppBundlePath();
	NSString *remembered, *base;

	*outBasepath = nil;
	*outBase = nil;

	// a) remembered in-place folder
	remembered = [[NSUserDefaults standardUserDefaults] stringForKey: KEY_GAMEFOLDER];
	if( remembered )
	{
		if( ( base = BaseIn( remembered ) ) && Pak0In( base ) )
		{
			MacLog( @"Game data: remembered game folder \"%@\"", base );
			*outBasepath = remembered;
			*outBase = base;
			return YES;
		}
		MacLog( @"Game data: remembered game folder \"%@\" no longer has %s/pak0.pk3", remembered, BASEGAME );
	}

	// b) copied into the homepath
	if( Pak0In( HomeBase() ) )
	{
		MacLog( @"Game data: \"%@\"", HomeBase() );
		*outBase = HomeBase();
		return YES;
	}

	// c) next to the app, unless translocated
	if( !bundle )
		return NO;
	if( IsTranslocated( bundle ) )
		MacLog( @"Game data: app is translocated (%@), not looking next to it", bundle );
	else if( ( base = BaseIn( [bundle stringByDeletingLastPathComponent] ) ) && Pak0In( base ) )
	{
		MacLog( @"Game data: next to the app, \"%@\"", base );
		*outBasepath = [bundle stringByDeletingLastPathComponent];
		*outBase = base;
		return YES;
	}
	return NO;
}

// The chooser for the launcher: same results as RunChooser.
BOOL VSP_ChooseGameData( NSString **outBasepath )
{
	return RunChooser( outBasepath );
}

NSString *VSP_HomeBase( void )
{
	return HomeBase();
}

void VSP_RevealFolder( NSString *path )
{
	MacLog( @"Showing \"%@\" in Finder", path );
	[[NSWorkspace sharedWorkspace] openURL: [NSURL fileURLWithPath: path isDirectory: YES]];
}

void VSP_Log( NSString *msg )
{
	MacLog( @"%@", msg );
}

const char *Sys_MacResolveGameData( int argc, char **argv )
{
	static char result[MAX_OSPATH];

	if( VSP_DataOverridden( argc, argv ) )
		return NULL;

	@autoreleasepool {
		NSString *found, *base, *chosen = nil;

		if( !Sys_MacAppBundlePath() )
			return NULL;
		if( VSP_FindGameData( &found, &base ) )
		{
			if( !found )
				return NULL;
			Q_strncpyz( result, [found fileSystemRepresentation], sizeof( result ) );
			return result;
		}

		// nothing found: ask. SDL's video init sets up NSApp (SDL's own
		// NSApplication subclass) so the panels work and SDL is happy later.
		MacLog( @"Game data not found (no %s/pak0.pk3 in the remembered folder, \"%@\" or next to the app)",
			BASEGAME, HomeBase() );
		SDL_InitSubSystem( SDL_INIT_VIDEO );
		if( !RunChooser( &chosen ) )
		{
			SDL_Quit( );
			exit( 0 );
		}
		SDL_QuitSubSystem( SDL_INIT_VIDEO );

		if( chosen )
		{
			Q_strncpyz( result, [chosen fileSystemRepresentation], sizeof( result ) );
			return result;
		}
		return NULL;
	}
}

/* ------------------------------------------------------------------------
   Menu actions
   ------------------------------------------------------------------------ */

static void RevealFolder( NSString *path )
{
	VSP_RevealFolder( path );
}

// After this process has exited (atexit, so the config is written and the
// PID file gone): start a new instance in mode (0 SP, 1 Holomatch) or, for
// -1, showing the launcher.
static void DoRelaunch( void )
{
	@autoreleasepool {
		NSString *bundle = Sys_MacAppBundlePath();
		const char *mode = pendingRelaunch ? "1" : "0";
		char *args[] = { "open", "-n", "-a", NULL, "--args", "+set", "com_holomatch", (char *)mode, NULL };
		pid_t pid;
		int status;

		if( pendingRelaunch < -1 || !bundle )
			return;
		args[3] = (char *)[bundle fileSystemRepresentation];
		if( pendingRelaunch < 0 )
			args[4] = NULL;		// no mode: the launcher
		fprintf( stderr, "Relaunching: open -n -a \"%s\"%s%s\n", args[3],
			args[4] ? " --args +set com_holomatch " : "", args[4] ? mode : "" );
		if( TestEnv( "VOYAGERSP_TEST_NORELAUNCH" ) )
			return;
		if( posix_spawn( &pid, "/usr/bin/open", NULL, NULL, args, environ ) == 0 )
			waitpid( pid, &status, 0 );	// open returns once Launch Services has it
		else
			fprintf( stderr, "Relaunch failed\n" );
	}
}

// Quit and come back in mode (see DoRelaunch).
static void RelaunchIn( int mode )
{
	if( pendingRelaunch == -2 )
		atexit( DoRelaunch );
	pendingRelaunch = mode;
	Cbuf_ExecuteText( EXEC_APPEND, "quit\n" );
}

static void ReturnToLauncher_f( void )
{
	if( Alert( NSAlertStyleInformational, @"Return to the launcher?",
			Com_Holomatch() ? @"This leaves the current game and restarts Elite Force at the launcher."
				: @"This restarts Elite Force at the launcher. Progress since your last save is lost.",
			@[ @"Return to Launcher", @"Cancel" ], NO ) == 0 )
		RelaunchIn( -1 );
}

static void SwitchMode_f( void )
{
	if( Alert( NSAlertStyleInformational,
			Com_Holomatch() ? @"Switch to Single Player?" : @"Switch to Holomatch?",
			Com_Holomatch() ? @"This leaves the current game and restarts Elite Force in single player."
				: @"This restarts Elite Force in Holomatch. Progress since your last save is lost.",
			@[ @"Switch", @"Cancel" ], NO ) == 0 )
		RelaunchIn( Com_Holomatch() ? 0 : 1 );
}

// The BaseEF folder whose pak0.pk3 the engine is using.
static NSString *CurrentGameBase( void )
{
	const char *bp = Cvar_VariableString( "fs_basepath" );
	NSString *base;

	if( bp && *bp && ( base = BaseIn( [NSString stringWithUTF8String: bp] ) ) && Pak0In( base ) )
		return base;
	if( Pak0In( HomeBase() ) )
		return HomeBase();
	return nil;
}

// External game folder in use (never inside the homepath), or nil.
static NSString *ExternalGameBase( void )
{
	NSString *base = CurrentGameBase();

	if( base && ![[[base stringByStandardizingPath] lowercaseString]
			hasPrefix: [[[HomeRoot() stringByStandardizingPath] lowercaseString] stringByAppendingString: @"/"]] )
		return base;
	return nil;
}

static void ShowSaves_f( void )
{
	NSString *saves = [HomeBase() stringByAppendingPathComponent: @"saves"];
	[[NSFileManager defaultManager] createDirectoryAtPath: saves withIntermediateDirectories: YES attributes: nil error: NULL];
	RevealFolder( saves );
}

static void ShowGameFolder_f( void )
{
	NSString *base = CurrentGameBase();

	if( base )
		RevealFolder( base );
	else
		ErrorAlert( @"No game folder", @"No BaseEF folder with pak0.pk3 is in use." );
}

static void ChangeGameFolder_f( void )
{
	NSString *chosen = nil;

	if( !RunChooser( &chosen ) )
		return;
	if( Alert( NSAlertStyleInformational, @"Game folder changed",
			@"The new game folder will be used the next time you start the game.",
			@[ @"Restart Now", @"Later" ], NO ) == 0 )
		RelaunchIn( Com_Holomatch() ? 1 : 0 );
}

static unsigned long long FolderSize( NSString *dir, int *files )
{
	NSDirectoryEnumerator *en = [[NSFileManager defaultManager] enumeratorAtPath: dir];
	unsigned long long n = 0;
	NSString *rel;

	if( files ) *files = 0;
	while( ( rel = [en nextObject] ) )
		if( [[[en fileAttributes] fileType] isEqualToString: NSFileTypeRegular] )
		{
			n += [[en fileAttributes] fileSize];
			if( files ) ( *files )++;
		}
	return n;
}

// HomeRoot() if it is a real directory exactly where expected (no symlink
// escapes), else nil. realpath() reports the on-disk case, and the default
// macOS filesystem is case-insensitive, so a folder created under an older
// spelling ("VoyagerSP-macOS") is the same folder: only the last component
// is compared case-insensitively; its parent must match exactly.
static NSString *SafeHomeRoot( void )
{
	NSString *root = HomeRoot();
	char parentReal[PATH_MAX], rootReal[PATH_MAX];
	const char *slash;
	size_t plen;
	struct stat st;

	if( lstat( [root fileSystemRepresentation], &st ) || !S_ISDIR( st.st_mode ) )
		return nil;		// missing, or a symlink
	if( !realpath( [[root stringByDeletingLastPathComponent] fileSystemRepresentation], parentReal ) ||
		!realpath( [root fileSystemRepresentation], rootReal ) )
		return nil;
	plen = strlen( parentReal );
	slash = strrchr( rootReal, '/' );
	if( !slash || (size_t)( slash - rootReal ) != plen || strncmp( rootReal, parentReal, plen ) )
		return nil;		// not directly inside Application Support
	if( strcasecmp( slash + 1, MACOS_HOMEPATH_NAME ) )
		return nil;
	return [NSString stringWithUTF8String: rootReal];
}

static void DoPendingDelete( void )
{
	@autoreleasepool {
		NSString *root;
		NSError *err = nil;

		if( !pendingDelete )
			return;
		if( !( root = SafeHomeRoot() ) )
		{
			fprintf( stderr, "Delete Saves & Settings: refusing, %s is not a plain directory\n",
				[HomeRoot() fileSystemRepresentation] );
			return;
		}
		if( TestEnv( "VOYAGERSP_TEST_NOTRASH" ) )
		{
			fprintf( stderr, "[test] would move \"%s\" to the Trash\n", [root fileSystemRepresentation] );
			return;
		}
		if( [[NSFileManager defaultManager] trashItemAtURL: [NSURL fileURLWithPath: root isDirectory: YES]
				resultingItemURL: nil error: &err] )
			fprintf( stderr, "Moved \"%s\" to the Trash\n", [root fileSystemRepresentation] );
		else
		{
			fprintf( stderr, "Could not move \"%s\" to the Trash: %s\n", [root fileSystemRepresentation],
				[[err localizedDescription] UTF8String] );
			Alert( NSAlertStyleWarning, @"Could not delete saves & settings",
				[NSString stringWithFormat: @"%@ could not be moved to the Trash: %@\n\nYou can delete it in Finder.",
					root, [err localizedDescription]], @[ @"OK" ], NO );
		}
	}
}

static void DeleteSettings_f( void )
{
	NSString *root = SafeHomeRoot(), *ext = ExternalGameBase(), *msg;
	NSString *base = [root stringByAppendingPathComponent: @BASEGAME];
	unsigned long long total, pk3Bytes = 0;
	int files = 0, saves = 0, pk3s = 0;

	if( !root )
	{
		ErrorAlert( @"Nothing to delete",
			[NSString stringWithFormat: @"%@ does not exist, or is a link to another location, so nothing was deleted.", HomeRoot()] );
		return;
	}
	total = FolderSize( root, &files );
	FolderSize( [base stringByAppendingPathComponent: @"saves"], &saves );
	for( NSString *e in [[NSFileManager defaultManager] contentsOfDirectoryAtPath: base error: NULL] )
		if( [[e pathExtension] caseInsensitiveCompare: @"pk3"] == NSOrderedSame )
		{
			pk3s++;
			pk3Bytes += [[[NSFileManager defaultManager] attributesOfItemAtPath:
				[base stringByAppendingPathComponent: e] error: NULL] fileSize];
		}

	msg = [NSString stringWithFormat:
		@"This quits the game and moves this folder to the Trash:\n%@\n\n"
		"• saved games (%d files)\n"
		"• settings and key bindings (efspconfig.cfg, hmconfig.cfg, keys, console history)\n"
		"%@"
		"\nTotal: %d files, %@.\n\n%@",
		root, saves,
		pk3s ? [NSString stringWithFormat: @"• game files copied by the app (%d .pk3, %@)\n", pk3s, FormatBytes( pk3Bytes )] : @"",
		files, FormatBytes( total ),
		ext ? [NSString stringWithFormat: @"Your game folder at %@ will not be touched.", ext]
			: @"Nothing outside this folder is touched."];

	if( Alert( NSAlertStyleCritical, @"Delete saves & settings?", msg,
			@[ @"Move to Trash and Quit", @"Cancel" ], YES ) != 0 )
		return;

	[[NSUserDefaults standardUserDefaults] removeObjectForKey: KEY_GAMEFOLDER];
	[[NSUserDefaults standardUserDefaults] synchronize];
	pendingDelete = qtrue;
	atexit( DoPendingDelete );	// runs after the engine has written its config and closed its files
	Cbuf_ExecuteText( EXEC_APPEND, "quit\n" );
}

@interface VSPMenuTarget : NSObject
@end

@implementation VSPMenuTarget
- (void)toLauncher: (id)sender		{ @autoreleasepool { ReturnToLauncher_f(); } }
- (void)switchMode: (id)sender		{ @autoreleasepool { SwitchMode_f(); } }
- (void)showSaves: (id)sender		{ @autoreleasepool { ShowSaves_f(); } }
- (void)showGame: (id)sender		{ @autoreleasepool { ShowGameFolder_f(); } }
- (void)changeGame: (id)sender		{ @autoreleasepool { ChangeGameFolder_f(); } }
- (void)deleteSettings: (id)sender	{ @autoreleasepool { DeleteSettings_f(); } }
@end

static void Cmd_ToLauncher( void )		{ @autoreleasepool { ReturnToLauncher_f(); } }
static void Cmd_SwitchMode( void )		{ @autoreleasepool { SwitchMode_f(); } }
static void Cmd_ShowSaves( void )		{ @autoreleasepool { ShowSaves_f(); } }
static void Cmd_ShowGame( void )		{ @autoreleasepool { ShowGameFolder_f(); } }
static void Cmd_ChangeGame( void )		{ @autoreleasepool { ChangeGameFolder_f(); } }
static void Cmd_DeleteSettings( void )	{ @autoreleasepool { DeleteSettings_f(); } }

/*
=================
Sys_MacDataPostInit

After Com_Init: replay the early log into the console and register the
console versions of the menu actions (bundle only).
=================
*/
void Sys_MacDataPostInit( void )
{
	@autoreleasepool {
		if( earlyLog )
		{
			Com_Printf( "%s", [earlyLog UTF8String] );
			[earlyLog release];
			earlyLog = nil;
		}
		// Opening a menu-bar menu stops the game loop until it closes: pause
		// the sound meanwhile instead of looping the last buffer.
		static BOOL menuObservers;
		if( !menuObservers )
		{
			NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];
			[nc addObserverForName: NSMenuDidBeginTrackingNotification object: nil queue: nil
				usingBlock: ^( NSNotification *n ) { SNDDMA_PauseOutput( qtrue ); }];
			[nc addObserverForName: NSMenuDidEndTrackingNotification object: nil queue: nil
				usingBlock: ^( NSNotification *n ) { SNDDMA_PauseOutput( qfalse ); }];
			menuObservers = YES;
		}
		if( !Sys_MacAppBundlePath() )
			return;
		Cmd_AddCommand( "macos_launcher", Cmd_ToLauncher );
		Cmd_AddCommand( "macos_switchmode", Cmd_SwitchMode );
		Cmd_AddCommand( "macos_showsaves", Cmd_ShowSaves );
		Cmd_AddCommand( "macos_showgamefolder", Cmd_ShowGame );
		Cmd_AddCommand( "macos_changegamefolder", Cmd_ChangeGame );
		Cmd_AddCommand( "macos_deletesettings", Cmd_DeleteSettings );
	}
}

// Top-level File menu with the game folder actions.
static NSMenuItem *BuildFileMenu( id target )
{
	NSMenu *menu = [[[NSMenu alloc] initWithTitle: @"File"] autorelease];
	NSMenuItem *top = [[[NSMenuItem alloc] initWithTitle: @"File" action: nil keyEquivalent: @""] autorelease];
	struct { NSString *title; SEL sel; } items[] = {
		{ @"Return to Launcher…", @selector(toLauncher:) },
		{ Com_Holomatch() ? @"Switch to Single Player" : @"Switch to Holomatch", @selector(switchMode:) },
		{ nil, NULL },
		{ @"Show Saves Folder", @selector(showSaves:) },
		{ @"Show Game Folder", @selector(showGame:) },
		{ nil, NULL },
		{ @"Change Game Folder…", @selector(changeGame:) },
		{ nil, NULL },
		{ @"Delete Saves & Settings…", @selector(deleteSettings:) },
	};
	size_t i;

	for( i = 0; i < ARRAY_LEN( items ); i++ )
	{
		NSMenuItem *item;

		if( !items[i].title )
		{
			[menu addItem: [NSMenuItem separatorItem]];
			continue;
		}
		item = [[[NSMenuItem alloc] initWithTitle: items[i].title action: items[i].sel keyEquivalent: @""] autorelease];
		[item setTarget: target];
		[menu addItem: item];
	}
	[top setSubmenu: menu];
	return top;
}

/*
=================
Sys_MacFrame

Called every frame. Every 16th frame, check that the menu bar SDL built has
our File menu (right after the application menu) and add it if not: once at
startup, and again should SDL ever rebuild the menu bar (bundle only).
=================
*/
void Sys_MacFrame( void )
{
	static VSPMenuTarget *target;
	static NSMenuItem *fileItem;
	static qboolean notBundle, logged;
	static int checks;

	if( notBundle || ( ++checks & 15 ) )
		return;

	@autoreleasepool {
		NSMenu *bar;

		if( !Sys_MacAppBundlePath() )
		{
			notBundle = qtrue;
			return;
		}
		if( !NSApp || !( bar = [NSApp mainMenu] ) || [bar numberOfItems] < 1 || ![[bar itemAtIndex: 0] submenu] )
			return;		// SDL has not built the menu bar yet
		if( fileItem && [bar indexOfItem: fileItem] >= 0 )
			return;		// already there

		if( !target )
			target = [[VSPMenuTarget alloc] init];
		if( !fileItem )
			fileItem = [BuildFileMenu( target ) retain];
		if( [fileItem menu] )
			[[fileItem menu] removeItem: fileItem];		// attached to an old menu bar
		[bar insertItem: fileItem atIndex: 1];
		Com_Printf( "macOS: %s the File menu (%s)\n", logged ? "re-added" : "added",
			[[[[bar itemArray] valueForKey: @"title"] componentsJoinedByString: @" | "] UTF8String] );
		logged = qtrue;
	}
}
