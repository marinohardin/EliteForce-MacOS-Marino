/*
===========================================================================
Launcher core: co-op (launcher side). Spec: launcher-data-coop.md §6, §10.9.
Owner: core-b. The engine side (Sys_CoopFrame etc.) is
efcode/sys/sys_coop_host.c, not part of this library.

The Co-op… button shows in co-op builds (EF_COOP: lc_coop_ui_enabled(), lc_common.h).

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
#define LC_COOP_PROTOCOL	7	// = VSP_COOP_PROTOCOL (sys_coop_net.h)

typedef struct {
	/* "ok" (a co-op game of this version answered), "mismatch" (answered, other co-op/app version:
	   `error` has the sentence, `info` is filled), "noanswer" (silence), "error" (silence and the
	   send failed: show `error` like the no-answer text), "bad" (address problem: `error`).
	   The sheet's found / mismatch / none / bad of §6.1 map to ok / mismatch / noanswer+error / bad. */
	/* After lc_coop_check_paks, an "ok" may become "paks" (pak0-pak2 differ from the host's: `error`
	   has the sentences, no Connect; the host would refuse), or stay "ok" with `warning` set (only
	   the Expansion Pack's pak3 differs: the sheet offers "Join Anyway"). */
	char status[12];
	char address[64];			/* normalized a.b.c.d:port */
	char error[512];			/* the sentence to show (§6.4) */
	char info[1024];			/* the coopInfoResponse infostring */
	char warning[512];			/* lc_coop_check_paks: the Expansion Pack differs ("" otherwise) */
} lc_coop_query;

/* the app version this build was compiled with (EF_APP_VERSION): co-op needs host and guest to match */
const char *lc_coop_app_version( void );
/* §6.4, blocking (run on a thread) */
void lc_coop_query_run( const char *address, double timeoutSeconds, lc_coop_query *out );
/* infostring value; "" if absent */
void lc_coop_info_value( const char *info, const char *key, char *out, size_t size );
/* the sheet's description lines for a host's answer (§6.1) */
void lc_coop_describe( const char *info, char *out, size_t size );
/* ---- the game's own paks (protocol 7; sys_coop_net.c CoopNet_ComparePaks) ---- */
/* which of the game's own paks a pk3 with this checksum is: 0-3 for pak0.pk3-pak3.pk3 (pak3 is the
   Expansion Pack's), -1 for any other (= qcommon/files.c FS_CoopCorePakIndex) */
int lc_coop_core_pak_index( unsigned checksum );
/* this computer's own paks as the engine will list them once it runs (FS_CoopPaks): every .pk3 in
   the homepath's baseEF and in dataFolder (the BaseEF in use, may be NULL), by checksum:
   "pak0.pk3:<checksum> pak1.pk3:<checksum> ..." */
void lc_coop_local_paks( const char *dataFolder, char *out, size_t size );
#define LC_COOP_PAKS_SAME		0
#define LC_COOP_PAKS_DIFFER		1	/* pak0-pak2 differ: the host refuses */
#define LC_COOP_PAKS_EXPANSION	2	/* only pak3 differs: a warning */
/* = CoopNet_ComparePaks: text gets the sentences naming each pack that differs, for the host
   (forHost) or the guest */
int lc_coop_compare_paks( const char *hostPaks, const char *guestPaks, int forHost, char *text, size_t size );
/* after lc_coop_query_run: compare an "ok" answer's paks with this computer's (see lc_coop_query) */
void lc_coop_check_paks( lc_coop_query *q, const char *dataFolder );

/* ---- player 2's character (efgame/src/game/coop_characters.h, the table the game checks) ---- */
int lc_coop_character_count( void );
/* i-th character: id (what coop_look carries), name (for people), group ("crew" or "hazard");
   0 when i is out of range */
int lc_coop_character( int i, const char **id, const char **name, const char **group );
/* 1 when id is one of them ("" and NULL are Munro: 0) */
int lc_coop_character_valid( const char *id );

/* LAN IPv4 addresses, primary first; returns the count */
int lc_coop_lan_addresses( char addrs[][16], char ifnames[][64], int max );
/* stvef://coop/… invite link for an address (§6.1, §7.1) */
void lc_coop_invite_link( const char *address, char *out, size_t size );

/* the sheet's choice, kept until Play (§6.2) */
void lc_coop_choose_none( void );
void lc_coop_choose_host( int background, const char *name, const char *password );
/* look: a character id (lc_coop_character), "" or NULL for Munro */
void lc_coop_choose_join( const char *address, const char *sex, const char *look, const char *name, const char *password );
int lc_coop_choice( void );		/* 0 none, 1 host, 2 join */
/* fills the co-op fields of an lc_launch (lc_launch.h) from the choice. The password
   never goes on the command line (§6.2): Windows (launcher in the game process)
   calls Sys_CoopSetLaunchPassword(lc_coop_password()) directly; Linux ef-launcher
   puts it in env EF_COOP_PASSWORD for the exec, and sys_coop_host.c takes it from
   there (and unsets it) on startup. */
struct lc_launch_coop_fields { int coopHost, coopBackground; const char *coopConnect, *coopSex, *coopLook, *coopName; };
void lc_coop_launch_fields( struct lc_launch_coop_fields *out );
const char *lc_coop_password( void );	/* "" if none */

#ifdef __cplusplus
}
#endif

#endif
