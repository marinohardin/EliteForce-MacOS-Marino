/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

#ifndef __APPLE__
#error This file is for Mac OS X only. You probably should not compile it.
#endif

// Please note that this file is just some Mac-specific bits. Most of the
// Mac OS X code is shared with other Unix platforms in sys_unix.c ...

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "sys_local.h"

#import <Carbon/Carbon.h>
#import <Cocoa/Cocoa.h>

#ifndef MACOS_HOMEPATH_NAME
// ~/Library/Application Support/<this> holds macOS saves/config (see sys_osx.m)
#define MACOS_HOMEPATH_NAME "VoyagerSP-macOS"
#endif

/*
==============
Sys_DisablePressAndHold

Holding a key (e.g. WASD) while SDL has text input enabled pops up the macOS
accent picker. SDL2 used to register this default itself; sdl2-compat (SDL3)
does not, so do it here before the window is created.
==============
*/
void Sys_DisablePressAndHold( void )
{
	@autoreleasepool {
		[[NSUserDefaults standardUserDefaults] registerDefaults:@{ @"ApplePressAndHoldEnabled": @NO }];
	}
}

/*
==============
Sys_Dialog

Display an OS X dialog box
==============
*/
dialogResult_t Sys_Dialog( dialogType_t type, const char *message, const char *title )
{
	dialogResult_t result = DR_OK;
	NSAlert *alert = [NSAlert new];

	[alert setMessageText: [NSString stringWithUTF8String: title]];
	[alert setInformativeText: [NSString stringWithUTF8String: message]];

	if( type == DT_ERROR )
		[alert setAlertStyle: NSCriticalAlertStyle];
	else
		[alert setAlertStyle: NSWarningAlertStyle];

	switch( type )
	{
		default:
			[alert runModal];
			result = DR_OK;
			break;

		case DT_YES_NO:
			[alert addButtonWithTitle: @"Yes"];
			[alert addButtonWithTitle: @"No"];
			switch( [alert runModal] )
			{
				default:
				case NSAlertFirstButtonReturn: result = DR_YES; break;
				case NSAlertSecondButtonReturn: result = DR_NO; break;
			}
			break;

		case DT_OK_CANCEL:
			[alert addButtonWithTitle: @"OK"];
			[alert addButtonWithTitle: @"Cancel"];

			switch( [alert runModal] )
			{
				default:
				case NSAlertFirstButtonReturn: result = DR_OK; break;
				case NSAlertSecondButtonReturn: result = DR_CANCEL; break;
			}
			break;
	}

	[alert release];

	return result;
}

/*
=================
Sys_StripAppBundle

Discovers if passed dir is suffixed with the directory structure of a Mac OS X
.app bundle. If it is, the .app directory structure is stripped off the end and
the result is returned. If not, dir is returned untouched.
=================
*/
char *Sys_StripAppBundle( char *dir )
{
	static char cwd[MAX_OSPATH];

	Q_strncpyz(cwd, dir, sizeof(cwd));
	if(strcmp(Sys_Basename(cwd), "MacOS"))
		return dir;
	Q_strncpyz(cwd, Sys_Dirname(cwd), sizeof(cwd));
	if(strcmp(Sys_Basename(cwd), "Contents"))
		return dir;
	Q_strncpyz(cwd, Sys_Dirname(cwd), sizeof(cwd));
	if(!strstr(Sys_Basename(cwd), ".app"))
		return dir;
	Q_strncpyz(cwd, Sys_Dirname(cwd), sizeof(cwd));
	return cwd;
}

/*
=================
Sys_MacAppBundlePath

Returns the path of the enclosing .app bundle, or NULL when the executable is
not running from inside one (e.g. a dev build in build-macos/).
=================
*/
static NSString *Sys_MacAppBundlePath( void )
{
	NSString *path = [[NSBundle mainBundle] bundlePath];

	if( path && [[path pathExtension] caseInsensitiveCompare: @"app"] == NSOrderedSame )
		return path;
	return nil;
}

static const char *bundleVulkanLibrary;
static const char *bundleVulkanICD;
static char homepathMigrationMsg[MAX_OSPATH * 3];

/*
=================
Sys_MacSetupBundleEnv

When running from the .app, point SDL and the Vulkan loader at the copies of
libvulkan / MoltenVK shipped in Contents/Frameworks, so no Homebrew install is
needed. Must run before SDL loads Vulkan. Variables already set in the
environment win, so they can still be overridden for debugging.
Returns qtrue when running from an app bundle.
=================
*/
qboolean Sys_MacSetupBundleEnv( void )
{
	@autoreleasepool {
		NSString *bundle = Sys_MacAppBundlePath();
		NSFileManager *fm = [NSFileManager defaultManager];
		NSString *lib, *icd;

		if( !bundle )
			return qfalse;

		lib = [[[NSBundle mainBundle] privateFrameworksPath] stringByAppendingPathComponent: @"libvulkan.1.dylib"];
		icd = [[[NSBundle mainBundle] resourcePath] stringByAppendingPathComponent: @"vulkan/icd.d/MoltenVK_icd.json"];

		if( [fm fileExistsAtPath: lib] )
		{
			setenv( "SDL_VULKAN_LIBRARY", [lib fileSystemRepresentation], 0 );
			bundleVulkanLibrary = strdup( getenv( "SDL_VULKAN_LIBRARY" ) );
		}
		if( [fm fileExistsAtPath: icd] )
		{
			// VK_DRIVER_FILES is the current name, VK_ICD_FILENAMES the legacy one
			setenv( "VK_DRIVER_FILES", [icd fileSystemRepresentation], 0 );
			setenv( "VK_ICD_FILENAMES", [icd fileSystemRepresentation], 0 );
			bundleVulkanICD = strdup( getenv( "VK_DRIVER_FILES" ) );
		}
		return qtrue;
	}
}

/*
=================
Sys_MacPrintStartupInfo

Log what Sys_MacSetupBundleEnv / Sys_MacPrepareHomepath did (called once the
console is up).
=================
*/
void Sys_MacPrintStartupInfo( void )
{
	if( homepathMigrationMsg[0] )
		Com_Printf( "%s\n", homepathMigrationMsg );

	@autoreleasepool {
		NSString *bundle = Sys_MacAppBundlePath();

		if( !bundle )
			return;
		Com_Printf( "macOS app bundle: %s\n", [bundle fileSystemRepresentation] );
		Com_Printf( "  SDL_VULKAN_LIBRARY = %s\n", bundleVulkanLibrary ? bundleVulkanLibrary : "(not bundled)" );
		Com_Printf( "  VK_DRIVER_FILES    = %s\n", bundleVulkanICD ? bundleVulkanICD : "(not bundled)" );
	}
}

/*
=================
Sys_MacPrepareHomepath

macOS saves/config live in ~/Library/Application Support/<MACOS_HOMEPATH_NAME>
instead of the shared HOMEPATH_NAME ("Lilium Voyager"). The caller makes that
the default by passing "+set com_homepath MACOS_HOMEPATH_NAME" to Com_Init.

One-time migration: if the new baseEF folder does not exist yet but the old
~/Library/Application Support/<HOMEPATH_NAME>/baseEF does, COPY it over
(saves, efspconfig.cfg, keys, screenshots). The original is never moved or
deleted. A stale pid file in the copy is removed so the first start does not
report an abnormal exit.

Returns qfalse (and does nothing) when the command line sets fs_homepath or
com_homepath itself.
=================
*/
qboolean Sys_MacPrepareHomepath( int argc, char **argv )
{
	int i;

	for( i = 1; i < argc; i++ )
		if( !Q_stricmp( argv[i], "fs_homepath" ) || !Q_stricmp( argv[i], "com_homepath" ) )
			return qfalse;

	@autoreleasepool {
		NSFileManager *fm = [NSFileManager defaultManager];
		// $HOME, like Sys_DefaultHomePath(), so both agree on where the homepath is
		const char *home = getenv( "HOME" );
		NSString *support = [( home && *home ? [NSString stringWithUTF8String: home] : NSHomeDirectory() )
			stringByAppendingPathComponent: @"Library/Application Support"];
		NSString *oldBase = [[support stringByAppendingPathComponent: @HOMEPATH_NAME] stringByAppendingPathComponent: @BASEGAME];
		NSString *newRoot = [support stringByAppendingPathComponent: @MACOS_HOMEPATH_NAME];
		NSString *newBase = [newRoot stringByAppendingPathComponent: @BASEGAME];
		NSError *err = nil;
		BOOL isDir = NO;

		if( [fm fileExistsAtPath: newBase] )
			return qtrue;
		if( ![fm fileExistsAtPath: oldBase isDirectory: &isDir] || !isDir )
			return qtrue;

		if( [fm createDirectoryAtPath: newRoot withIntermediateDirectories: YES attributes: nil error: &err] &&
			[fm copyItemAtPath: oldBase toPath: newBase error: &err] )
		{
			NSString *pid = [newBase stringByAppendingPathComponent: @PRODUCT_NAME ".pid"];
			if( [fm fileExistsAtPath: pid] )
				[fm removeItemAtPath: pid error: NULL];
			Com_sprintf( homepathMigrationMsg, sizeof( homepathMigrationMsg ),
				"Copied saves/config from \"%s\" to \"%s\" (original left in place)",
				[oldBase fileSystemRepresentation], [newBase fileSystemRepresentation] );
		}
		else
		{
			Com_sprintf( homepathMigrationMsg, sizeof( homepathMigrationMsg ),
				"WARNING: could not copy \"%s\" to \"%s\": %s",
				[oldBase fileSystemRepresentation], [newBase fileSystemRepresentation],
				[[err localizedDescription] UTF8String] );
		}
		fprintf( stderr, "%s\n", homepathMigrationMsg );
	}
	return qtrue;
}
