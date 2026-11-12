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
int lc_data_find( int argc, char **argv, lc_data *out );	/* no UI, never exits */
/* the Game Files group's status line and detail (§2.9) */
void lc_data_status_text( const lc_data *d, char *line, size_t lineSize, char *detail, size_t detailSize );
int lc_data_missing_patch_paks( const char *homeBase, const char *base, char names[2][16] );
int lc_data_has_expansion( const char *homeBase, const char *base );
int lc_zip_has_entry( const char *pk3, const char *name );

/* ---- the chooser (§4.2) ------------------------------------------------------------ */
int lc_is_installer( const char *path );		/* a file ending ".exe" (GOG setup) */
/* 0 ok (base filled); else nonzero and problem = the sentence to show */
int lc_validate_choice( const char *chosen, int inPlace, char *base, size_t baseSize,
	char *problem, size_t problemSize );
/* remember an in-place folder (the folder holding BaseEF); NULL clears */
int lc_remember_game_folder( const char *folderHoldingBase );
/* Linux in-place with a differently named BaseEF: sys_data.c's .gamedata symlink
   logic; returns the fs_basepath to use (static buffer) */
const char *lc_use_found_basepath( const char *folder, const char *base );

/* ---- copy / unpack jobs (§4.3, §4.4) ------------------------------------------------- */
typedef struct lc_copyjob_s lc_copyjob;
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
/* the progress panel's texts (§4.4) */
void lc_copyjob_title( const lc_copyjob *j, char *out, size_t size );
void lc_copyjob_progress_text( const lc_copyjob *j, unsigned long long done, char *out, size_t size );

#ifdef __cplusplus
}
#endif

#endif
