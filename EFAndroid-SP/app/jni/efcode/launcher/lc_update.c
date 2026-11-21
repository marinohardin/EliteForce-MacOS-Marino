/*
===========================================================================
Launcher core: the update checker (lc_update.h). Spec: launcher-data-coop.md
§8, §10.7.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#include "lc_update.h"
#include "lc_settings.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#	define LC_PLATFORM_NAME	"Windows"
#	define LC_UPDATE_HOW	"Download the new version from its release page on GitHub, then run its installer, or replace the files you unzipped."
#else
#	define LC_PLATFORM_NAME	"Linux"
#	define LC_UPDATE_HOW	"Download the new version from its release page on GitHub, then replace this app with it."
#endif

/* ===================================================================== */
/* versions                                                              */

/* NSNumericSearch: digit runs compare as numbers, anything else by character code;
   a string that is a prefix of the other is the smaller */
int lc_version_compare( const char *a, const char *b )
{
	if ( !a )
		a = "";
	if ( !b )
		b = "";
	while ( *a && *b ) {
		if ( isdigit( (unsigned char)*a ) && isdigit( (unsigned char)*b ) ) {
			const char *ea = a, *eb = b;
			size_t la, lb;
			int c;

			while ( *a == '0' && isdigit( (unsigned char)a[1] ) )
				a++;
			while ( *b == '0' && isdigit( (unsigned char)b[1] ) )
				b++;
			ea = a;
			eb = b;
			while ( isdigit( (unsigned char)*ea ) )
				ea++;
			while ( isdigit( (unsigned char)*eb ) )
				eb++;
			la = (size_t)( ea - a );
			lb = (size_t)( eb - b );
			if ( la != lb )
				return la < lb ? -1 : 1;
			c = memcmp( a, b, la );
			if ( c )
				return c < 0 ? -1 : 1;
			a = ea;
			b = eb;
			continue;
		}
		if ( (unsigned char)*a != (unsigned char)*b )
			return (unsigned char)*a < (unsigned char)*b ? -1 : 1;
		a++;
		b++;
	}
	if ( *a )
		return 1;
	if ( *b )
		return -1;
	return 0;
}

int lc_version_is_newer( const char *v, const char *appVersion )
{
	return v && *v && appVersion && *appVersion && lc_version_compare( v, appVersion ) > 0;
}

int lc_update_due( long long lastCheck, long long now )
{
	long long d = now - lastCheck;
	return !( d >= 0 && d < 86400 );
}

/* ===================================================================== */
/* a small JSON scanner: an array of objects with tag_name, draft, prerelease, html_url,
   assets[].name */

typedef struct {
	const char *p, *end;
} json;

static void Ws( json *j )
{
	while ( j->p < j->end && ( *j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r' ) )
		j->p++;
}

/* a string at p (p on the opening quote) into out (truncated); advances; 0 on error */
static int Str( json *j, char *out, size_t size )
{
	size_t o = 0;

	if ( j->p >= j->end || *j->p != '"' )
		return 0;
	j->p++;
	while ( j->p < j->end && *j->p != '"' ) {
		unsigned char c = (unsigned char)*j->p++;

		if ( c == '\\' ) {
			if ( j->p >= j->end )
				return 0;
			c = (unsigned char)*j->p++;
			switch ( c ) {
			case 'n': c = '\n'; break;
			case 't': c = '\t'; break;
			case 'r': c = '\r'; break;
			case 'b': c = '\b'; break;
			case 'f': c = '\f'; break;
			case 'u': {
				unsigned cp = 0;
				int i;

				if ( j->end - j->p < 4 )
					return 0;
				for ( i = 0; i < 4; i++ ) {
					int h = j->p[i];
					cp <<= 4;
					cp |= (unsigned)( h >= '0' && h <= '9' ? h - '0' : ( h | 32 ) >= 'a' && ( h | 32 ) <= 'f' ? ( h | 32 ) - 'a' + 10 : 0 );
				}
				j->p += 4;
				if ( out && size ) {
					char enc[4];
					int n = 0, k;

					if ( cp < 0x80 )
						enc[n++] = (char)cp;
					else if ( cp < 0x800 ) {
						enc[n++] = (char)( 0xC0 | ( cp >> 6 ) );
						enc[n++] = (char)( 0x80 | ( cp & 0x3F ) );
					} else {
						enc[n++] = (char)( 0xE0 | ( cp >> 12 ) );
						enc[n++] = (char)( 0x80 | ( ( cp >> 6 ) & 0x3F ) );
						enc[n++] = (char)( 0x80 | ( cp & 0x3F ) );
					}
					for ( k = 0; k < n && o + 1 < size; k++ )
						out[o++] = enc[k];
				}
				continue;
			}
			default: break;		/* \" \\ \/ */
			}
		}
		if ( out && size && o + 1 < size )
			out[o++] = (char)c;
	}
	if ( j->p >= j->end )
		return 0;
	j->p++;
	if ( out && size )
		out[o] = 0;
	return 1;
}

static int Skip( json *j, int depth );

static int SkipContainer( json *j, int depth, char close )
{
	if ( depth > 64 )
		return 0;
	j->p++;
	Ws( j );
	if ( j->p < j->end && *j->p == close ) {
		j->p++;
		return 1;
	}
	for ( ;; ) {
		Ws( j );
		if ( close == '}' ) {
			if ( !Str( j, NULL, 0 ) )
				return 0;
			Ws( j );
			if ( j->p >= j->end || *j->p != ':' )
				return 0;
			j->p++;
		}
		if ( !Skip( j, depth + 1 ) )
			return 0;
		Ws( j );
		if ( j->p >= j->end )
			return 0;
		if ( *j->p == ',' ) {
			j->p++;
			continue;
		}
		if ( *j->p == close ) {
			j->p++;
			return 1;
		}
		return 0;
	}
}

/* skip one value */
static int Skip( json *j, int depth )
{
	Ws( j );
	if ( j->p >= j->end )
		return 0;
	if ( *j->p == '"' )
		return Str( j, NULL, 0 );
	if ( *j->p == '{' )
		return SkipContainer( j, depth, '}' );
	if ( *j->p == '[' )
		return SkipContainer( j, depth, ']' );
	if ( *j->p == '-' || isdigit( (unsigned char)*j->p ) || isalpha( (unsigned char)*j->p ) ) {
		while ( j->p < j->end && ( isalnum( (unsigned char)*j->p ) || *j->p == '-' || *j->p == '+' || *j->p == '.' ) )
			j->p++;
		return 1;
	}
	return 0;
}

typedef struct {
	char tag[128], url[512], asset[256];
	int draft, prerelease, haveTag;
	int assetRank;			/* index in the suffix list of asset; -1 none */
} release;

/* the index of the first suffix name ends with (case-insensitive), or -1 */
static int SuffixRank( const char *name, const char *const *suffixes )
{
	size_t n = strlen( name );
	int i;

	for ( i = 0; suffixes && suffixes[i]; i++ ) {
		size_t k = strlen( suffixes[i] );

		if ( n > k && !lc_stricmp( name + n - k, suffixes[i] ) )
			return i;
	}
	return -1;
}

/* an "assets" array at p: the best-ranked asset name into r; 0 on a syntax error */
static int Assets( json *j, release *r, const char *const *suffixes )
{
	j->p++;				/* [ */
	Ws( j );
	if ( j->p < j->end && *j->p == ']' ) {
		j->p++;
		return 1;
	}
	for ( ;; ) {
		Ws( j );
		if ( j->p >= j->end )
			return 0;
		if ( *j->p == '{' ) {
			j->p++;
			Ws( j );
			if ( j->p < j->end && *j->p == '}' )
				j->p++;
			else for ( ;; ) {
				char key[64];

				Ws( j );
				if ( !Str( j, key, sizeof( key ) ) )
					return 0;
				Ws( j );
				if ( j->p >= j->end || *j->p != ':' )
					return 0;
				j->p++;
				Ws( j );
				if ( !strcmp( key, "name" ) && j->p < j->end && *j->p == '"' ) {
					char name[256];
					int rank;

					if ( !Str( j, name, sizeof( name ) ) )
						return 0;
					rank = SuffixRank( name, suffixes );
					if ( rank >= 0 && ( r->assetRank < 0 || rank < r->assetRank ) ) {
						r->assetRank = rank;
						lc_strlcpy( r->asset, name, sizeof( r->asset ) );
					}
				} else if ( !Skip( j, 2 ) )
					return 0;
				Ws( j );
				if ( j->p >= j->end )
					return 0;
				if ( *j->p == ',' ) {
					j->p++;
					continue;
				}
				if ( *j->p == '}' ) {
					j->p++;
					break;
				}
				return 0;
			}
		} else if ( !Skip( j, 2 ) )
			return 0;
		Ws( j );
		if ( j->p >= j->end )
			return 0;
		if ( *j->p == ',' ) {
			j->p++;
			continue;
		}
		if ( *j->p == ']' ) {
			j->p++;
			return 1;
		}
		return 0;
	}
}

/* an object at p; fills r; 0 on a syntax error */
static int Release( json *j, release *r, const char *const *suffixes )
{
	memset( r, 0, sizeof( *r ) );
	r->assetRank = -1;
	j->p++;				/* { */
	Ws( j );
	if ( j->p < j->end && *j->p == '}' ) {
		j->p++;
		return 1;
	}
	for ( ;; ) {
		char key[64];

		Ws( j );
		if ( !Str( j, key, sizeof( key ) ) )
			return 0;
		Ws( j );
		if ( j->p >= j->end || *j->p != ':' )
			return 0;
		j->p++;
		Ws( j );
		if ( j->p >= j->end )
			return 0;
		if ( !strcmp( key, "tag_name" ) && *j->p == '"' ) {
			if ( !Str( j, r->tag, sizeof( r->tag ) ) )
				return 0;
			r->haveTag = 1;
		} else if ( !strcmp( key, "html_url" ) && *j->p == '"' ) {
			if ( !Str( j, r->url, sizeof( r->url ) ) )
				return 0;
		} else if ( !strcmp( key, "assets" ) && *j->p == '[' ) {
			if ( !Assets( j, r, suffixes ) )
				return 0;
		} else if ( ( !strcmp( key, "draft" ) || !strcmp( key, "prerelease" ) ) && ( *j->p == 't' || *j->p == 'f' ) ) {
			int v = *j->p == 't';

			if ( !Skip( j, 0 ) )
				return 0;
			if ( !strcmp( key, "draft" ) )
				r->draft = v;
			else
				r->prerelease = v;
		} else if ( !Skip( j, 1 ) )
			return 0;
		Ws( j );
		if ( j->p >= j->end )
			return 0;
		if ( *j->p == ',' ) {
			j->p++;
			continue;
		}
		if ( *j->p == '}' ) {
			j->p++;
			return 1;
		}
		return 0;
	}
}

/* the prefix of tag among prefixes (with a version after it), or NULL */
static const char *TagPrefix( const char *tag, const char *const *prefixes )
{
	int i;

	for ( i = 0; prefixes && prefixes[i]; i++ ) {
		size_t n = strlen( prefixes[i] );

		if ( !strncmp( tag, prefixes[i], n ) && tag[n] )
			return prefixes[i];
	}
	return NULL;
}

int lc_update_parse( const char *text, size_t len, const char *const *tagPrefixes,
	const char *const *assetSuffixes, const char *repo,
	char *version, size_t vSize, char *url, size_t uSize, char *asset, size_t aSize )
{
	json j;
	char best[128] = "", bestUrl[512] = "", bestAsset[256] = "", releases[256];
	int found = 0;

	j.p = text;
	j.end = text + len;
	lc_snprintf( releases, sizeof( releases ), "https://github.com/%s/releases", repo );
	if ( version && vSize )
		version[0] = 0;
	if ( url && uSize )
		lc_strlcpy( url, releases, uSize );
	if ( asset && aSize )
		asset[0] = 0;
	Ws( &j );
	if ( j.p >= j.end || *j.p != '[' )
		return -1;
	j.p++;
	Ws( &j );
	if ( j.p < j.end && *j.p == ']' )
		return 0;
	for ( ;; ) {
		Ws( &j );
		if ( j.p >= j.end )
			return -1;
		if ( *j.p == '{' ) {
			release r;
			const char *prefix;

			if ( !Release( &j, &r, assetSuffixes ) )
				return -1;
			if ( r.haveTag && !r.draft && !r.prerelease && ( prefix = TagPrefix( r.tag, tagPrefixes ) ) != NULL
				&& ( !assetSuffixes || r.assetRank >= 0 ) ) {
				const char *v = r.tag + strlen( prefix );

				if ( !found || lc_version_compare( v, best ) > 0 ) {
					lc_strlcpy( best, v, sizeof( best ) );
					lc_strlcpy( bestUrl, r.url[0] ? r.url : releases, sizeof( bestUrl ) );
					lc_strlcpy( bestAsset, r.asset, sizeof( bestAsset ) );
					found = 1;
				}
			}
		} else if ( !Skip( &j, 1 ) )
			return -1;
		Ws( &j );
		if ( j.p >= j.end )
			return -1;
		if ( *j.p == ',' ) {
			j.p++;
			continue;
		}
		if ( *j.p == ']' )
			break;
		return -1;
	}
	if ( !found )
		return 0;
	if ( version && vSize )
		lc_strlcpy( version, best, vSize );
	if ( url && uSize )
		lc_strlcpy( url, bestUrl, uSize );
	if ( asset && aSize )
		lc_strlcpy( asset, bestAsset, aSize );
	return 1;
}

/* ===================================================================== */
/* fetch                                                                 */

static void Fail( lc_update_result *out, const char *problem )
{
	char p[256];
	size_t n;

	lc_strlcpy( p, problem, sizeof( p ) );
	n = strlen( p );
	if ( n && p[n - 1] != '.' && p[n - 1] != '!' && p[n - 1] != '?' )
		lc_strlcat( p, ".", sizeof( p ) );
	out->ok = 0;
	out->version[0] = 0;
	lc_strlcpy( out->problem, p, sizeof( out->problem ) );
	lc_log( "Updates: couldn't check (%s)", out->problem );
}

void lc_update_fetch( const char *appVersion, lc_update_result *out )
{
	const char *test = lc_env( "VOYAGERSP_TEST_UPDATE" );
	char url[256], ua[128], err[256], *body = NULL;
	const char *headers[3];
	size_t len = 0;
	long status = 0;
	int r;

	memset( out, 0, sizeof( *out ) );
	if ( test ) {
		out->ok = 1;
		if ( !lc_stricmp( test, "none" ) ) {
			lc_snprintf( out->url, sizeof( out->url ), "https://github.com/%s/releases", LC_GITHUB_REPO );
		} else {
			lc_strlcpy( out->version, test, sizeof( out->version ) );
			lc_snprintf( out->url, sizeof( out->url ), "https://github.com/%s/releases/tag/%s%s",
				LC_GITHUB_REPO, LC_UPDATE_TAG_PREFIX, test );
		}
		return;
	}
	lc_snprintf( url, sizeof( url ), "https://api.github.com/repos/%s/releases?per_page=100", LC_GITHUB_REPO );
	lc_snprintf( ua, sizeof( ua ), "User-Agent: EliteForce-Marino/%s", appVersion ? appVersion : "" );
	headers[0] = "Accept: application/vnd.github+json";
	headers[1] = ua;
	headers[2] = NULL;
	err[0] = 0;
	r = lc_http_get( url, headers, 15, &body, &len, &status, err, sizeof( err ) );
	if ( r == LC_HTTP_UNAVAILABLE ) {
		Fail( out, "This computer can't make secure connections (libcurl isn't available)" );
	} else if ( r != LC_HTTP_OK ) {
		Fail( out, err[0] ? err : "GitHub couldn't be reached" );
	} else if ( status != 200 ) {
		char p[64];

		lc_snprintf( p, sizeof( p ), "GitHub answered %ld.", status );
		Fail( out, p );
	} else {
		static const char *const prefixes[] = { LC_UPDATE_TAG_PREFIX, LC_UPDATE_LEGACY_TAG_PREFIX, NULL };
		static const char *const suffixes[] = { LC_UPDATE_ASSET_SUFFIXES, NULL };
		char version[64], asset[256];
		int n = lc_update_parse( body ? body : "", len, prefixes, suffixes, LC_GITHUB_REPO,
			version, sizeof( version ), out->url, sizeof( out->url ), asset, sizeof( asset ) );

		if ( n < 0 )
			Fail( out, "GitHub's answer wasn't a list of releases" );
		else {
			out->ok = 1;
			if ( n > 0 ) {
				lc_strlcpy( out->version, version, sizeof( out->version ) );
				lc_log( "Updates: release %s has %s", version, asset );
			}
		}
	}
	free( body );
}

/* ===================================================================== */
/* state                                                                 */

int lc_update_auto_state( lc_store *s, char *storedVersion, size_t vSize, char *storedUrl, size_t uSize )
{
	const char *v, *u;
	long long last;

	if ( storedVersion && vSize )
		storedVersion[0] = 0;
	if ( storedUrl && uSize )
		storedUrl[0] = 0;
	if ( !lc_store_get_bool( s, LC_KEY_CHECK_UPDATES, 1 ) )
		return 0;
	v = lc_store_get( s, LC_KEY_UPDATE_VERSION );
	u = lc_store_get( s, LC_KEY_UPDATE_URL );
	if ( v && *v ) {
		if ( storedVersion )
			lc_strlcpy( storedVersion, v, vSize );
		if ( storedUrl )
			lc_strlcpy( storedUrl, u ? u : "", uSize );
	}
	if ( lc_env( "VOYAGERSP_TEST_UPDATE" ) )
		return 1;				/* bypasses the throttle */
	if ( !lc_store_has( s, LC_KEY_UPDATE_LAST ) )
		return 1;
	last = lc_store_get_int64( s, LC_KEY_UPDATE_LAST, 0 );
	return lc_update_due( last, lc_time( ) );
}

void lc_update_save( lc_store *s, const lc_update_result *r, const char *appVersion )
{
	if ( !r->ok )
		return;
	lc_log( "Updates: latest release %s, this is %s", r->version[0] ? r->version : "(none)", appVersion ? appVersion : "" );
	if ( lc_env( "VOYAGERSP_TEST_UPDATE" ) )
		return;					/* nothing is saved under the test hook */
	lc_store_set_int64( s, LC_KEY_UPDATE_LAST, lc_time( ) );
	if ( r->version[0] ) {
		lc_store_set( s, LC_KEY_UPDATE_VERSION, r->version );
		lc_store_set( s, LC_KEY_UPDATE_URL, r->url );
	} else {
		lc_store_remove( s, LC_KEY_UPDATE_VERSION );
		lc_store_remove( s, LC_KEY_UPDATE_URL );
	}
	lc_store_save( s );
}

void lc_update_alert_texts( const lc_update_result *r, const char *appVersion,
	char *message, size_t mSize, char *info, size_t iSize, int *newer )
{
	if ( newer )
		*newer = 0;
	if ( !r->ok ) {
		lc_strlcpy( message, "Couldn't check for updates.", mSize );
		lc_snprintf( info, iSize, "%s Check your internet connection and try again.", r->problem );
	} else if ( lc_version_is_newer( r->version, appVersion ) ) {
		lc_snprintf( message, mSize, "Elite Force for " LC_PLATFORM_NAME " %s is available.", r->version );
		lc_snprintf( info, iSize, "You have version %s. " LC_UPDATE_HOW " Your settings and saves are kept.", appVersion );
		if ( newer )
			*newer = 1;
	} else {
		lc_strlcpy( message, "You're up to date.", mSize );
		lc_snprintf( info, iSize, "Elite Force for " LC_PLATFORM_NAME " %s is the newest version.", appVersion ? appVersion : "" );
	}
}
