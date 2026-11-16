/*
===========================================================================
Launcher core: engine config reading, the graphics model, quality and
brightness presets, cMod values, player names. Spec: launcher-data-coop.md
§2.4–2.10, §10.2–10.3. Owner: core-a.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#ifndef LC_SETTINGS_H
#define LC_SETTINGS_H

#include "lc_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* store keys (launcher.cfg) for LDC §1.1's NSUserDefaults keys */
#define LC_KEY_MODE				"launchermode"		/* 0 SP, 1 HM, 2 Host */
#define LC_KEY_CHECK_UPDATES	"checkforupdates"	/* bool, absent = 1 */
#define LC_KEY_UPDATE_LAST		"updatelastcheck"	/* unix seconds */
#define LC_KEY_UPDATE_VERSION	"updatelatestversion"
#define LC_KEY_UPDATE_URL		"updatelatesturl"
#define LC_KEY_HUD_WIDESCREEN	"hudwidescreen"		/* bool, absent = 1 */
#define LC_KEY_GAMEFOLDER		"gamefolder"		/* read by sys_data.c too */
#define LC_KEY_COOP_BACKGROUND	"coopkeepplayinginbackground"	/* bool, absent = 1 */
#define LC_KEY_COOP_NAME		"coopplayername"
#define LC_KEY_COOP_MUNRO		"coopguestmunro"	/* "male" | "female" */
#define LC_KEY_COOP_LOOK		"coopguestcharacter"	/* a coop_characters.h id, "" = Munro */
#define LC_KEY_LOOPBACK_FIX		"loopbackhostfix"	/* bool, absent = 1 */
#define LC_KEY_AUTOLAUNCH		"autolaunch"		/* Linux Steam Deck addition, bool, absent = 0 */

enum { LC_MODE_SP = 0, LC_MODE_HOLOMATCH = 1, LC_MODE_HOST = 2 };

/* ---- engine config files (§2.5) ----------------------------------------------------
   Parses `seta name "value"` / `set …` lines like the engine would read them. */
typedef struct {
	int count, cap;
	char (*names)[64];			/* lower-cased */
	char (*values)[256];
} lc_cfg;
int lc_cfg_read( const char *path, lc_cfg *out );		/* 1 if the file existed */
const char *lc_cfg_get( const lc_cfg *c, const char *name );	/* case-insensitive; NULL absent */
int lc_cfg_int( const lc_cfg *c, const char *name, int def );
float lc_cfg_float( const lc_cfg *c, const char *name, float def );
void lc_cfg_free( lc_cfg *c );
/* <homeBase>/efspconfig.cfg (SP) or hmconfig.cfg (Holomatch and Host) */
void lc_config_path( const char *homeBase, int holomatch, char *out, size_t size );

/* ---- graphics (§2.4, §2.5) ------------------------------------------------------- */
typedef enum { LC_PRESET_PERFORMANCE, LC_PRESET_BALANCED, LC_PRESET_BEST, LC_PRESET_CUSTOM } lc_preset;
#define LC_BRIGHTNESS_CUSTOM	(-1)
typedef struct {
	int fullscreen, width, height, highdpi, msaa /* 0,2,4,8 */, picmip /* 0..2 */, vsync;
	int brightness;				/* preset index, or LC_BRIGHTNESS_CUSTOM */
} lc_gfx;

/* load the mode's values (fallback to the other mode's file, then defaults) as §2.5;
   *fromConfig = 1 when a config file supplied them. screenW/H = the usable screen size
   (logical pixels, 0 = unknown): when the config gives no custom size, the 1920x1080
   default becomes the biggest listed size that fits (a small laptop screen), else 1024x768.
   (A deliberate addition to the Mac rule, where every screen is big enough.) */
void lc_gfx_load( const char *homeBase, int holomatch, int screenW, int screenH,
	lc_gfx *g, int *fromConfig );
lc_preset lc_gfx_match_preset( const lc_gfx *g );
void lc_gfx_apply_preset( lc_gfx *g, lc_preset p );
const char *lc_preset_title( lc_preset p );				/* "Performance", … , "Custom" */
const char *lc_preset_help( lc_preset p );		/* (the spec has no text for these: short tooltips) */
/* window size list for a usable screen area (§2.4); returns the count. maxW/maxH = the
   usable area (work area, no taskbar) with maxH ALREADY minus the title bar (Mac: 28);
   the current size is always listed, appended at the end if it isn't a candidate. */
int lc_window_sizes( int maxW, int maxH, int curW, int curH, int (*out)[2], int max );

/* ---- brightness presets (§2.6, from cMod) -------------------------------------------- */
int lc_brightness_count( void );
const char *lc_brightness_title( int b );
const char *lc_brightness_help( int b );					/* b = LC_BRIGHTNESS_CUSTOM: the Custom hint */
const char *lc_brightness_test_id( int b );
int lc_brightness_match( const lc_cfg *cfg );				/* preset index or LC_BRIGHTNESS_CUSTOM */
/* the +set list for preset b if it fits in room bytes; else writes
   <homeBase>/vsp_brightness.cfg and returns "+exec vsp_brightness.cfg "; Custom → "".
   Returns 0 on success. */
int lc_brightness_args( int b, size_t room, const char *homeBase, char *out, size_t outSize );

/* ---- cMod (§2.8, §2.10) ---------------------------------------------------------- */
typedef struct {
	int available;				/* the cMod modules pk3 is bundled (lc_cmod_pak_path) */
	int on;						/* cl_cmodModules */
	int hudAspect;				/* cg_aspectCorrect: from the store's hudwidescreen */
	char hudCenter[16];			/* cg_aspectCorrectCenterHud */
	int predictWeapons;			/* cg_predictWeapons */
} lc_cmod;
/* the bundled pk3: Windows <exe dir>\cmod\pakcmod-release-*.pk3, Linux
   <exe dir>/../share/eliteforce-marino/cmod/…; 1 if found */
int lc_cmod_pak_path( char *out, size_t size );
void lc_cmod_load( const lc_cfg *hm, lc_store *s, lc_cmod *out );
void lc_cmod_save_store( const lc_cmod *c, lc_store *s );	/* hudwidescreen */

/* ---- names (§2.8, §6.2) ---------------------------------------------------------------- */
void lc_clean_name( const char *in, char *out, size_t size );	/* VSP_CleanName, "UnnamedPlayer" fallback */
void lc_first_name( const char *fullName, char *out, size_t size );	/* first word, else "Crewman" */
void lc_coop_safe_name( const char *in, char *out, size_t size );
/* name field's initial value: hmconfig's `name`, else first name of the user */
void lc_player_name_from_config( const lc_cfg *hm, const char *fullUserName, char *out, size_t size );

#ifdef __cplusplus
}
#endif

#endif
