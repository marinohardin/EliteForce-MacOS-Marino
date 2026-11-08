/*
===========================================================================
macOS app bundle: single-player co-op in the launcher (sys_osx_coop.m).

Only EF_COOP builds (the engine CMakeLists' option) have it. Without
EF_COOP these hooks are nothing, so the launcher is exactly as before.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#ifndef SYS_OSX_COOP_H
#define SYS_OSX_COOP_H

#ifdef EF_COOP

#import <Cocoa/Cocoa.h>

// the UDP port a co-op host answers co-op checks on (and, from M1, guests on)
#define VSP_COOP_PORT		29760
// bumped whenever co-op's network messages change; host and guest must match
#define VSP_COOP_PROTOCOL	1

// The Single Player card's "Co-op…" button. launcher is the VSPLauncher.
NSButton *VSP_CoopButton( id launcher );
// Play's extra arguments: +set sp_coop 1 after Host (single player only).
void VSP_CoopAppendArgs( NSMutableString *args, BOOL holomatch );

// Ask address ("host" or "host:port", default port VSP_COOP_PORT) whether a
// co-op game runs there. Blocks up to timeout seconds. The result:
//   status   @"found", @"none" (no answer), @"mismatch" (answered, other
//            version) or @"bad" (the address couldn't be used)
//   address  the address asked ("1.2.3.4:29760"), when it resolved
//   info     the host's keys (map, mission, skill, players, version, ...)
//   error    a sentence for "bad" and "mismatch"
NSDictionary *VSP_CoopQuery( NSString *address, double timeout );

#else

#define VSP_CoopButton( launcher )			nil
#define VSP_CoopAppendArgs( args, holomatch )	( (void)0 )

#endif

#endif
