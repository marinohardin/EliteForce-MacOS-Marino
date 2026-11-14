/*
===========================================================================
Launcher core: the update checker (GitHub releases). Spec:
launcher-data-coop.md §8, §10.7. Owner: core-a.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#ifndef LC_UPDATE_H
#define LC_UPDATE_H

#include "lc_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	int ok;					/* 0: couldn't check (problem filled) */
	char version[64];		/* "" = no release */
	char url[512];			/* release page (or the releases list) */
	char problem[256];
} lc_update_result;

/* blocking fetch + parse (VOYAGERSP_TEST_UPDATE honoured): the newest release tagged
   LC_UPDATE_TAG_PREFIX (or LC_UPDATE_LEGACY_TAG_PREFIX) that has one of this platform's
   LC_UPDATE_ASSET_SUFFIXES downloads; url is its release page */
void lc_update_fetch( const char *appVersion, lc_update_result *out );
/* parse the releases JSON: 1 version found, 0 none, -1 not a JSON array. A release counts
   when its tag starts with one of tagPrefixes (NULL-terminated; version = the rest) and,
   unless assetSuffixes is NULL, one of its assets' names ends with one of assetSuffixes
   (NULL-terminated, case-insensitive). asset = that asset's name, the earliest suffix first. */
int lc_update_parse( const char *json, size_t len, const char *const *tagPrefixes,
	const char *const *assetSuffixes, const char *repo,
	char *version, size_t vSize, char *url, size_t uSize, char *asset, size_t aSize );
int lc_version_compare( const char *a, const char *b );		/* NSNumericSearch semantics */
int lc_version_is_newer( const char *v, const char *appVersion );
int lc_update_due( long long lastCheck, long long now );
/* automatic check state from the store: returns 1 when a fetch is due; fills the
   stored latest (badge shown at once) */
int lc_update_auto_state( lc_store *s, char *storedVersion, size_t vSize, char *storedUrl, size_t uSize );
/* after a successful fetch: save timestamp + latest (not under the test hook); logs */
void lc_update_save( lc_store *s, const lc_update_result *r, const char *appVersion );
/* the alert texts of Check for Updates… (platform-worded: "Elite Force for Windows/Linux") */
void lc_update_alert_texts( const lc_update_result *r, const char *appVersion,
	char *message, size_t mSize, char *info, size_t iSize, int *newer );

#ifdef __cplusplus
}
#endif

#endif
