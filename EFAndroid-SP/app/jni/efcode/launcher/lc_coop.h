/*
===========================================================================
Launcher core: co-op (launcher side). Spec: launcher-data-coop.md §6, §10.9.
Owner: core-b. The engine side (Sys_CoopFrame etc.) is
efcode/sys/sys_coop_host.c, not part of this library.

The Co-op… button is hidden unless lc_coop_ui_enabled() (lc_common.h), but all
of this is built and tested.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#ifndef LC_COOP_H
#define LC_COOP_H

#include "lc_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LC_COOP_PORT		29760
#define LC_COOP_PROTOCOL	6

typedef struct {
	/* "ok" (a co-op game of this version answered), "mismatch" (answered, other co-op/app version:
	   `error` has the sentence, `info` is filled), "noanswer" (silence), "error" (silence and the
	   send failed: show `error` like the no-answer text), "bad" (address problem: `error`).
	   The sheet's found / mismatch / none / bad of §6.1 map to ok / mismatch / noanswer+error / bad. */
	char status[12];
	char address[64];			/* normalized a.b.c.d:port */
	char error[512];			/* the sentence to show (§6.4) */
	char info[1024];			/* the coopInfoResponse infostring */
} lc_coop_query;

/* §6.4, blocking (run on a thread) */
void lc_coop_query_run( const char *address, double timeoutSeconds, lc_coop_query *out );
/* infostring value; "" if absent */
void lc_coop_info_value( const char *info, const char *key, char *out, size_t size );
/* the sheet's description lines for a host's answer (§6.1) */
void lc_coop_describe( const char *info, char *out, size_t size );
/* LAN IPv4 addresses, primary first; returns the count */
int lc_coop_lan_addresses( char addrs[][16], char ifnames[][64], int max );
/* stvef://coop/… invite link for an address (§6.1, §7.1) */
void lc_coop_invite_link( const char *address, char *out, size_t size );

/* the sheet's choice, kept until Play (§6.2) */
void lc_coop_choose_none( void );
void lc_coop_choose_host( int background, const char *name, const char *password );
void lc_coop_choose_join( const char *address, const char *sex, const char *name, const char *password );
int lc_coop_choice( void );		/* 0 none, 1 host, 2 join */
/* fills the co-op fields of an lc_launch (lc_launch.h) from the choice. The password
   never goes on the command line (§6.2): Windows (launcher in the game process)
   calls Sys_CoopSetLaunchPassword(lc_coop_password()) directly; Linux ef-launcher
   puts it in env EF_COOP_PASSWORD for the exec, and sys_coop_host.c takes it from
   there (and unsets it) on startup. */
struct lc_launch_coop_fields { int coopHost, coopBackground; const char *coopConnect, *coopSex, *coopName; };
void lc_coop_launch_fields( struct lc_launch_coop_fields *out );
const char *lc_coop_password( void );	/* "" if none */

#ifdef __cplusplus
}
#endif

#endif
