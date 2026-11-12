/*
===========================================================================
Launcher core: stvef:// links and their registration. Spec:
launcher-data-coop.md §7.1, §10.8; servers.md §8.9. Owner: core-a.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#ifndef LC_LINKS_H
#define LC_LINKS_H

#include "lc_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* a server link → "host[:port]"; 0 if it isn't one (password filled if the link has one) */
int lc_link_address( const char *link, char *out, size_t size, char *password, size_t pwSize );
/* -1 not a co-op link, 0 co-op without an address, 1 ok */
int lc_coop_link_address( const char *link, char *out, size_t size );
/* 1 if argv[i] looks like a link we handle */
int lc_is_link_arg( const char *arg );

/* registration as the stvef: handler.
   Windows: HKCU\Software\Classes\stvef (URL Protocol, shell\open\command =
   "<exe>" "%1"). Linux: ~/.local/share/applications/eliteforce-marino-stvef.desktop
   with MimeType=x-scheme-handler/stvef; + xdg-mime default (Exec = lc_self_command()).
   lc_link_handler_state: 1 = this app, 0 = none, 2 = another app (name filled). */
int lc_link_handler_state( char *otherApp, size_t size );
int lc_link_register( char *err, size_t errSize );

#ifdef __cplusplus
}
#endif

#endif
