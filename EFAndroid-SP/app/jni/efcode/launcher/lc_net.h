/*
===========================================================================
Launcher core: Server queries (getservers/getinfo/getstatus), colour codes, the server browser model, efservers.com. Spec: servers.md §7, §8, §15.
Owner: core-b. Derived from servers.md §15.2 (names vsp_ → lc_); the owner may
add declarations and adjust details, see docs/launcher-spec/ARCHITECTURE.md.
Persistence goes through lc_store (lc_common.h) instead of the vsp_store bridge.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#ifndef LC_NET_H
#define LC_NET_H

#include "lc_common.h"
#include "lc_server.h"
#ifdef _WIN32
#include <winsock2.h>
#else
#include <netinet/in.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---- protocol (7) ---- */
typedef struct { unsigned char ip[4]; unsigned short port; } lc_addr4;
int  lc_parse_server_list(const unsigned char *buf, size_t n,
                           void (*entry)(lc_addr4 a, void *u), void *u);     /* 7.1 */
int  lc_query_master(const char *master, int timeoutMs,
                      void (*entry)(lc_addr4 a, void *u), void *u);          /* blocking; -1 no answer, else #packets */
/* number of distinct ip:port entries the master lists, -1 when it didn't answer (6.1, VSP_QueryMasterCount) */
int  lc_query_master_count(const char *master);
typedef struct {
    char address[24], hostname[128], name[128], map[64];
    int  players, maxPlayers, humans /* -1 unknown */, gametype, ping, needPass;
} lc_server_info;
typedef struct { char name[64]; int score, ping; } lc_player;
typedef struct {
    int humans; int isCmod; char cmodVersion[128];
    int playerCount; lc_player players[64]; long long when;
} lc_server_status;
int  lc_parse_info_response(const unsigned char *buf, size_t n, const char *from, lc_server_info *out);
int  lc_parse_status_response(const unsigned char *buf, size_t n, lc_server_status *out);
int  lc_parse_address4(const char *s, int defaultPort, struct sockaddr_in *out);
typedef struct {
    void (*got_info)(const lc_server_info *i, void *u);
    void (*got_status)(const char *address, const lc_server_status *st, void *u);
    void (*done)(int answered, void *u);
    void *user;
} lc_query_callbacks;
void lc_query_servers(const char *const *addresses, int n, int broadcast,
                       const volatile int *cancel, const lc_query_callbacks *cb); /* blocking, 7.4 */
int  lc_query_status(const char *address, lc_server_status *out);            /* blocking, 7.5 */
void lc_decode_colors(const char *in, char *out, size_t n, unsigned char *colorIdx /* may be NULL */);
void lc_strip_colors(const char *in, char *out, size_t n);
int  lc_cmod_number(const char *cmodVersion, char *out, size_t n);           /* "\bv(\d+\.\d+)" */
double lc_cmod_rank(int isCmod, const char *cmodVersion);                     /* 0, version, or 0.01 */

/* ---- browser model (8) ---- */
typedef enum { LC_COL_FAV, LC_COL_NAME, LC_COL_LOCK, LC_COL_MAP, LC_COL_PLAYERS,
               LC_COL_HUMANS, LC_COL_TYPE, LC_COL_CMOD, LC_COL_PING } lc_column;
typedef struct { lc_column column; int ascending; } lc_sort_key;   /* 8.3 default: FAV desc, HUMANS desc, PLAYERS desc, PING asc */
typedef struct {
    lc_server_info info;
    int  favorite, thisComputer, isCmod;
    char cmodVersion[128];
    char listedOn[LC_MAX_MASTERS][16];  int listedOnCount;   /* "Internet N" labels (Combined) */
} lc_browser_row;
typedef struct lc_browser lc_browser;   /* sources, results, merge, loopback rewrite, filters, sort */
lc_browser *lc_browser_new(const char (*masters)[65], int n, const lc_map_catalog *c);
void lc_browser_free(lc_browser *b);                 /* cancels every fetch and waits for the workers */
/* Source index = segment index: 0 Local, 1 Combined (only when there are masters), 2 + i = master i
   (so with no masters the only source is 0). */
int  lc_browser_source_count(lc_browser *b);
void lc_browser_source_label(lc_browser *b, int source, char *out, size_t n);   /* "Local", "Combined", "Internet 3" or "3" (> 7 masters) */
/* Loopback Host Fix (8.4 step 3-4); default on. publicIP (8.4): when non-NULL it is the
   address to rewrite to 127.0.0.1; when NULL and the fix is on, an internet fetch waits up to 1 s
   for lc_public_address() (lc_nat.h) and uses that. */
void lc_browser_set_loopback_fix(lc_browser *b, int on);
/* starts (or restarts: the old fetch is cancelled and the results cleared) a fetch on a worker
   thread; `changed` is called from the worker whenever the results or the status change
   (the UI coalesces its reloads, 8.4); also called once synchronously when the fetch starts. */
void lc_browser_fetch(lc_browser *b, int source, const char *publicIP /* or NULL */,
                       void (*changed)(int source, void *u), void *u);   /* spawns a worker */
void lc_browser_cancel_all(lc_browser *b);
int  lc_browser_is_busy(lc_browser *b, int source);      /* the source's fetch is running (spinner) */
int  lc_browser_fetched(lc_browser *b, int source);      /* fetched at least once in this browser (8.4: fetch only if not) */
/* a filtered, sorted (stable) snapshot of the source's rows, with `favorite` set from the favorites
   list (8.5). *out points to an array of that many rows in display order, in a private snapshot
   valid until the next lc_browser_rows call on this browser (copy what you keep). Returns the count. */
int  lc_browser_rows(lc_browser *b, int source, int onlyFav, int onlyPeople,
                      const char *const *favorites, int nFav,
                      const lc_sort_key *keys, int nKeys, const lc_browser_row **out);
/* the default sort (8.3) into keys[4]; lc_sort_click applies a header click (new key first,
   direction toggled when it was already first, else the column's first-click direction;
   older keys follow, without a repeat); returns the new key count (<= max) */
void lc_browser_default_sort(lc_sort_key keys[4]);
int  lc_sort_click(lc_sort_key *keys, int nKeys, int max, lc_column col);
const char *lc_browser_status_text(lc_browser *b, int source);   /* exact strings of 8.4; copy it */
/* the getstatus answers the fetches stored per address, for the players panel (8.6) */
int  lc_browser_get_status(lc_browser *b, const char *address, lc_server_status *out);   /* 1 if stored */
/* after the UI's own lc_query_status answered (8.6): stores it and sets cMod on matching rows */
void lc_browser_store_status(lc_browser *b, const char *address, const lc_server_status *st);
void lc_browser_forget_status(lc_browser *b, const char *address);
#define LC_MAP_HAVE        0
#define LC_MAP_MISSING     1
#define LC_MAP_EXPANSION   2
int  lc_map_state(const lc_map_catalog *c, const char *map);     /* LC_MAP_*; 8.8 */
/* efservers.com has no page for retail / Expansion Pack maps: Map Info is hidden for them (8.6) */
int  lc_map_is_builtin(const lc_map_catalog *c, const char *map);
void lc_sort_players(lc_player *p, int n);                       /* people first, score desc */
int  lc_tag_bots(const lc_player *p, int n, int humans);

/* ---- efservers (7.8) ---- */
void lc_efservers_info_url(const char *map, char *out, size_t n);
void lc_efservers_3d_lookup_url(const char *map, char *out, size_t n);
int  lc_efservers_parse_3d(const char *html, char *url, size_t n);   /* 1 found, 0 none */

#ifdef __cplusplus
}
#endif

#endif
