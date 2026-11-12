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
void lc_browser_fetch(lc_browser *b, int source, const char *publicIP /* or NULL */,
                       void (*changed)(int source, void *u), void *u);   /* spawns a worker */
void lc_browser_cancel_all(lc_browser *b);
int  lc_browser_rows(lc_browser *b, int source, int onlyFav, int onlyPeople,
                      const char *const *favorites, int nFav,
                      const lc_sort_key *keys, int nKeys, const lc_browser_row **out);
const char *lc_browser_status_text(lc_browser *b, int source);   /* exact strings of 8.4 */
int  lc_map_state(const lc_map_catalog *c, const char *map);     /* HAVE / MISSING / EXPANSION */
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
