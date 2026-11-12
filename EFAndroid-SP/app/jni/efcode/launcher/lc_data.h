/*
===========================================================================
Launcher core: finding, choosing, copying and unpacking the game data.
Spec: launcher-data-coop.md §2.9, §4, §10.5. Owner: core-a.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
The lookup must give the same answer as sys_data.c's Sys_DataResolve (which the
engine keeps using); share code with it where practical but do not change its
behaviour.
===========================================================================
*/

#ifndef LC_DATA_H
#define LC_DATA_H

#include "lc_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	int found;
	char basepath[LC_MAX_PATH];		/* "" = default (the homepath copy) */
	char base[LC_MAX_PATH];			/* the BaseEF folder in use */
	char how[64];					/* "remembered game folder", "homepath", "next to the game", "GOG install", … */
	int inPlace;					/* used where it is (not a homepath copy) */
} lc_data;

int lc_data_overridden( int argc, char **argv );			/* = Sys_DataOverridden */
/* no UI, never exits; the same answer as Sys_DataResolve (which calls it). Logs each step
   through lc_log ("Game data: ..."). When the command line sets the data paths itself it
   returns 1 with how "the command line", basepath/base empty (check lc_data_overridden
   first: the launcher shows LC_DATA_OVERRIDDEN_* then). */
int lc_data_find( int argc, char **argv, lc_data *out );
/* §2.9's fixed texts */
#define LC_DATA_OVERRIDDEN_PATH		"Set on the command line"
#define LC_DATA_OVERRIDDEN_STATUS	"fs_basepath / fs_homepath given on the command line"
#define LC_DATA_NONE_PATH			"No game files yet"
#define LC_DATA_NONE_STATUS			"Choose the Elite Force folder (the one with BaseEF), or the GOG setup .exe."
/* path with the home folder shown as "~" (Linux; Windows: unchanged) */
void lc_data_display_path( const char *path, char *out, size_t size );
/* BaseEF of the remembered in-place folder (the "external" base of lc_plan_add_files), "" if none */
int lc_data_external_base( char *out, size_t size );
int lc_data_pak0_in_homebase( void );
/* the Game Files group's status line and detail (§2.9, case B): found with a missing patch:
   line "Patch 1.2 missing · add … with Add Game Files…" and detail = its tooltip;
   found: "Ready · using the files in this folder" / "Ready · copied into the app's own
   folder"; not found: LC_DATA_NONE_STATUS. The UI picks the icon (the patch line is a warning). */
void lc_data_status_text( const lc_data *d, char *line, size_t lineSize, char *detail, size_t detailSize );
int lc_data_missing_patch_paks( const char *homeBase, const char *base, char names[2][16] );
int lc_data_has_expansion( const char *homeBase, const char *base );
int lc_zip_has_entry( const char *pk3, const char *name );

/* §4.2's wording; MSG_USE_BASEEF is reworded for this platform */
#define LC_MSG_PAK0_ON_CD	"pak0.pk3 is missing \xe2\x80\x94 minimum CD installs leave it on the CD; copy it from BaseEF on the CD into this BaseEF folder, or choose a full installation."
#ifdef _WIN32
#	define LC_MSG_USE_BASEEF	"Run the installer to install the game on this PC instead, then choose the folder it installed to."
#else
#	define LC_MSG_USE_BASEEF	"Run GOG's installer with Wine instead, then choose the folder it installed to."
#endif
#define LC_MSG_PAK0_NOT_COPIED	"pak0.pk3 did not end up in the destination folder."

/* ---- the chooser (§4.2) ------------------------------------------------------------ */
int lc_is_installer( const char *path );		/* a file ending ".exe" (GOG setup) */
/* 0 ok (base filled); else nonzero and problem = the sentence to show */
int lc_validate_choice( const char *chosen, int inPlace, char *base, size_t baseSize,
	char *problem, size_t problemSize );
/* the "That folder can't be used" alert */
void lc_choice_alert( const char *chosen, const char *problem, char *title, size_t titleSize, char *text, size_t textSize );
/* remember an in-place folder (the folder holding BaseEF); NULL clears */
int lc_remember_game_folder( const char *folderHoldingBase );
/* Linux in-place with a differently named BaseEF: sys_data.c's .gamedata symlink
   logic; returns the fs_basepath to use (static buffer) */
const char *lc_use_found_basepath( const char *folder, const char *base );

/* ---- copy / unpack jobs (§4.3, §4.4) ------------------------------------------------- */
typedef struct lc_copyjob_s lc_copyjob;
/* The plan functions log like the Mac code ("Copying N .pk3 files and …", "Add game files: …",
   "GOG installer: …"). A NULL result means nothing to do (copy: no memory); for the unpack
   planner err is filled when the installer can't be used (alert: lc_installer_error_alert)
   and empty when everything was already there (lc_nothing_to_add_alert(1, present, …)).
   Next the UI checks the disk (lc_copyjob_check_space), runs the job on a worker thread
   behind a progress panel (lc_copyjob_title / lc_copyjob_progress_text), and on 0 shows
   lc_copyjob_failure_texts (not on -1, a cancel). For a copy, run also checks that pak0.pk3
   arrived (else returns 0 with LC_MSG_PAK0_NOT_COPIED). Picked .exe files are never added by
   lc_plan_add_files: the UI sends each one to lc_plan_unpack_installer (onlyNew = 1). */
lc_copyjob *lc_plan_copy_game_files( const char *srcBase, const char *homeBase,
	int *pk3s, int *saves, int *kept );
lc_copyjob *lc_plan_add_files( const char *const *picked, int n, const char *homeBase,
	const char *externalBase, int *present );
lc_copyjob *lc_plan_unpack_installer( const char *exe, int onlyNew, const char *homeBase,
	const char *externalBase, int *present, char *err, size_t errSize );
unsigned long long lc_copyjob_total( const lc_copyjob *j );
int lc_copyjob_count( const lc_copyjob *j );
int lc_copyjob_is_unpack( const lc_copyjob *j );
/* run on a worker thread; *progress = bytes done; stops when *cancel; rolls back
   the files it created on failure or cancel. 1 ok, 0 failed (err), -1 cancelled */
int lc_copyjob_run( lc_copyjob *j, volatile unsigned long long *progress,
	volatile signed char *cancel, char *err, size_t errSize );
void lc_copyjob_free( lc_copyjob *j );
int lc_have_gog( void );		/* 0: built without GOG unpacking (lc_plan_unpack_installer fails) */
/* 1 enough free space; else 0 with the "Not enough disk space" alert texts (creates HomeBase) */
int lc_copyjob_check_space( const lc_copyjob *j, char *title, size_t titleSize, char *text, size_t textSize );
void lc_copyjob_failure_texts( const lc_copyjob *j, const char *err, char *title, size_t titleSize,
	char *text, size_t textSize );
void lc_installer_error_alert( const char *exe, const char *phrase, char *title, size_t titleSize,
	char *text, size_t textSize );
void lc_nothing_to_add_alert( int unpackInstaller, int present, char *title, size_t titleSize,
	char *text, size_t textSize );
/* the progress panel's texts (§4.4) */
void lc_copyjob_title( const lc_copyjob *j, char *out, size_t size );
void lc_copyjob_progress_text( const lc_copyjob *j, unsigned long long done, char *out, size_t size );

#ifdef __cplusplus
}
#endif

#endif
