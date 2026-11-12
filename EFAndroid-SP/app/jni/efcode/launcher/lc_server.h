/*
===========================================================================
Launcher core: Host a Server settings, the map catalog, the generated cfg files, master servers. Spec: servers.md §2–6, §15.
Owner: core-b. Derived from servers.md §15.2 (names vsp_ → lc_); the owner may
add declarations and adjust details, see docs/launcher-spec/ARCHITECTURE.md.
Persistence goes through lc_store (lc_common.h) instead of the vsp_store bridge.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#ifndef LC_SERVER_H
#define LC_SERVER_H

#include "lc_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- settings (2) ---- */
typedef enum { LC_GT_FFA = 0, LC_GT_TOURNAMENT = 1, LC_GT_TEAM = 3, LC_GT_CTF = 4 } lc_gametype;
enum { LC_MOD_ASSIMILATION, LC_MOD_SPECIALTIES, LC_MOD_DISINTEGRATION,
       LC_MOD_ELIMINATION, LC_MOD_ACTIONHERO, LC_MOD_COUNT };

typedef struct {
    char hostname[49], password[33];
    int  gametype;
    int  mods[LC_MOD_COUNT];          /* booleans, in the fixed order */
    int  fraglimit, capturelimit, timelimit, maxPlayers, bots, botSkill;
    int  friendlyFire, internet, openPort, allowDownload, pure;
    int  port, tickRate;
    int  cmodGame, unlagged, smoothClients, unlaggedPredict;
    int  rotationCount;
    char rotation[64][64];             /* or a dynamic list */
} lc_server_settings;

void lc_server_settings_defaults(lc_server_settings *s, const char *userFullName);
void lc_server_settings_validate(lc_server_settings *s, const char *userFullName); /* 2.3 step 3-4 */
void lc_server_settings_apply_modifier_rules(lc_server_settings *s, int favoredMod /* -1: none */);
int  lc_server_settings_modifier_allowed(const lc_server_settings *s, int mod);    /* 4.3 enabling */
void lc_clean_cvar_text(const char *in, size_t maxLen, char *out, size_t outSize);
const char *lc_gametype_name(int gametype);         /* 2.5 */
const char *lc_browser_gametype_name(int gametype, char *buf, size_t n); /* adds "Single Player", "Type N" */
const char *lc_skill_name(int skill);
const char *lc_modifier_name(int mod);
void lc_server_settings_load(lc_server_settings *s, lc_store *st, const char *userFullName);
void lc_server_settings_save(const lc_server_settings *s, lc_store *st);

/* ---- pk3 / map catalog (3) ---- */
typedef struct { char name[64]; unsigned method, csize, usize, offset; } lc_zip_entry;
int  lc_zip_list(const char *path, int (*want)(const char *lowerName, void *u), void *u,
                  lc_zip_entry **out, int *count);          /* free with free() */
unsigned char *lc_zip_read(const char *path, const lc_zip_entry *e, size_t *len);

typedef struct {
    char name[64], longName[128], types[64], pk3[128];
    int  retail, expansion;
    char shotPath[1024]; int shotInZip; lc_zip_entry shotEntry;  /* levelshot location */
} lc_map_info;
typedef struct lc_map_catalog lc_map_catalog;               /* opaque, cached by stamp */
const lc_map_catalog *lc_map_catalog_get(const char *dataFolder, const char *homeBase);
int  lc_map_catalog_count(const lc_map_catalog *c);
const lc_map_info *lc_map_catalog_at(const lc_map_catalog *c, int i);
const lc_map_info *lc_map_find(const lc_map_catalog *c, const char *name);
const char *lc_map_display_name(const lc_map_info *m);
int  lc_map_supports_gametype(const lc_map_info *m, int gametype);
void lc_map_types_description(const lc_map_info *m, char *out, size_t n);
unsigned char *lc_map_levelshot_bytes(const lc_map_info *m, size_t *len, int *isTga); /* UI decodes */
int  lc_is_expansion_map(const char *lowerName);              /* 3.8 list */
void lc_arena_parse(const char *text, void (*block)(const char *map, const char *longname,
                     const char *type, void *u), void *u);

/* ---- cfg generation (5) ---- */
int  lc_server_cfg_text(const lc_server_settings *s, const lc_map_catalog *c,
                         const char (*masters)[65], int masterCount, int blankMasters,
                         char **outText, char *err, size_t errSize,
                         void (*log)(const char *line, void *u), void *u);
void lc_masters_cfg_text(const char (*masters)[65], int count, int blank, char **outText);
/* write with lc_write_file_atomic (lc_common.h) */

/* ---- masters (6) ---- */
#define LC_MAX_MASTERS 16
extern const char *const lc_default_masters[5];
extern const char *const lc_original_masters[2];
void lc_clean_master(const char *in, char out[65]);
int  lc_clean_masters(const char *const *in, int n, char out[][65]);   /* returns count */
int  lc_masters_load(lc_store *st, char out[][65]);             /* defaults if absent */
void lc_masters_save(lc_store *st, const char (*list)[65], int n); /* removes key if == defaults */
/* writes <homeBase>/vsp_masters.cfg (§5.2); 1 ok — the launcher passes +exec vsp_masters.cfg then */
int  lc_masters_write_cfg(const char *homeBase, const char (*list)[65], int n, int blank);
/* store keys: "masters" (list), "serversettings.*" (one key per field, rotation as a list),
   "browserfavorites" (list), "browseronlyfavorites", "browseronlypeople" */

#ifdef __cplusplus
}
#endif

#endif
