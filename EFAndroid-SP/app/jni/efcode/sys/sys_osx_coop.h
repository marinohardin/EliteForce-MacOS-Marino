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

#include "sys_coop_net.h"	// VSP_COOP_PORT, VSP_COOP_PROTOCOL and the connection

// The Single Player card's "Co-op…" button. launcher is the VSPLauncher.
NSButton *VSP_CoopButton( id launcher );
// Play's extra arguments: +set sp_coop 1 after Host, +set coop_connect <address> after
// Connect (single player only). Passwords don't go on the command line (Sys_CoopPassword).
void VSP_CoopAppendArgs( NSMutableString *args, BOOL holomatch );

// Ask address ("host" or "host:port", default port VSP_COOP_PORT) whether a
// co-op game runs there. Blocks up to timeout seconds. The result:
//   status   @"found", @"none" (no answer), @"mismatch" (answered, other
//            version) or @"bad" (the address couldn't be used)
//   address  the address asked ("1.2.3.4:29760"), when it resolved
//   info     the host's keys (map, mission, skill, players, version, ...)
//   error    a sentence for "bad" and "mismatch"
NSDictionary *VSP_CoopQuery( NSString *address, double timeout );

// stvef://coop/<host>[:<port>] (Copy Invite's link): the address; @"" for a co-op link without a
// usable one; nil for any other link
NSString *VSP_CoopLinkAddress( NSString *link );
// with the launcher up: open the Co-op… sheet with that address filled in and checked (NO when a
// sheet is already open)
BOOL VSP_CoopOpenJoin( NSString *address );

#else

#define VSP_CoopButton( launcher )			nil
#define VSP_CoopAppendArgs( args, holomatch )	( (void)0 )
#define VSP_CoopLinkAddress( link )			( (NSString *)nil )

#endif

#endif
