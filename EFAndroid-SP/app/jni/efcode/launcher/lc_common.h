/*
===========================================================================
Launcher core (Windows / Linux): the base layer every other lc_*.h uses.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).

Plain C99, no SDL, no engine headers. All strings are UTF-8; on Windows every
file/process call converts to UTF-16 inside. See docs/launcher-spec/ARCHITECTURE.md.
===========================================================================
*/

#ifndef LC_COMMON_H
#define LC_COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- product constants ------------------------------------------------- */

#ifdef _WIN32
#	define LC_PLATFORM_NOUN		"PC"			/* "Play on This PC", "on this PC" */
#	define LC_PLATFORM_NOUN_LC	"PC"
#	define LC_TRASH_NAME		"Recycle Bin"
#	define LC_FILE_MANAGER		"File Explorer"
#	define LC_UPDATE_TAG_PREFIX	"windows-v"
#	define LC_PATH_SEP			'\\'
#else
#	define LC_PLATFORM_NOUN		"Computer"		/* "Play on This Computer" */
#	define LC_PLATFORM_NOUN_LC	"computer"		/* "on this computer" */
#	define LC_TRASH_NAME		"Trash"
#	define LC_FILE_MANAGER		"the file manager"
#	define LC_UPDATE_TAG_PREFIX	"linux-v"
#	define LC_PATH_SEP			'/'
#endif

#define LC_BASEGAME			"baseEF"
#define LC_GITHUB_REPO		"marinohardin/EliteForce-MacOS-Marino"
#define LC_HOST_SERVER_ARG	"--host-server"
#define LC_CHOOSE_FOLDER_ARG	"--choose-game-folder"
#define LC_LAUNCHER_ARG		"--launcher"
#define LC_MAX_PATH			1024
#define LC_CMDLINE_SIZE		1024	/* MAX_STRING_CHARS: the engine's command-line buffer */

/* Co-op button in the launcher: code is complete, button hidden by default.
   lc_coop_ui_enabled() is 1 when this is 1 or env EF_SHOW_COOP=1. */
#ifndef LC_COOP_UI_ENABLED
#	define LC_COOP_UI_ENABLED	0
#endif
int lc_coop_ui_enabled( void );

/* ---- logging ----------------------------------------------------------------
   Core code logs through lc_log (printf-style). The default sink is stderr; the
   launcher may install its own (e.g. to replay into the engine console later). */
typedef void (*lc_log_sink)( const char *line, void *user );
void lc_set_log_sink( lc_log_sink sink, void *user );
void lc_log( const char *fmt, ... )
#if defined( __GNUC__ )
	__attribute__ (( format( printf, 1, 2 ) ))
#endif
	;

/* Env helpers: lc_env returns NULL when unset or empty; lc_test_env: set and not "0". */
const char *lc_env( const char *name );
int lc_test_env( const char *name );

/* ---- strings ---------------------------------------------------------------- */
size_t lc_strlcpy( char *dst, const char *src, size_t size );	/* always terminates */
size_t lc_strlcat( char *dst, const char *src, size_t size );
int lc_stricmp( const char *a, const char *b );
int lc_strnicmp( const char *a, const char *b, size_t n );
int lc_snprintf( char *dst, size_t size, const char *fmt, ... )	/* always terminates */
#if defined( __GNUC__ )
	__attribute__ (( format( printf, 3, 4 ) ))
#endif
	;
char *lc_strdup( const char *s );					/* NULL-safe, free() it */
void lc_trim( char *s );							/* in place: leading/trailing whitespace */
int lc_utf8_valid( const char *s );
/* truncate to at most maxBytes without splitting a UTF-8 sequence */
void lc_utf8_truncate( char *s, size_t maxBytes );

/* ---- paths and files (UTF-8 paths) --------------------------------------------- */
void lc_path_join( char *out, size_t size, const char *a, const char *b );
void lc_path_dirname( char *out, size_t size, const char *path );
const char *lc_path_basename( const char *path );
int lc_is_dir( const char *path );
int lc_is_file( const char *path );
int lc_is_symlink( const char *path );				/* Windows: reparse point */
int64_t lc_file_size( const char *path );			/* -1 if missing */
int64_t lc_file_mtime( const char *path );			/* unix seconds, -1 if missing */
FILE *lc_fopen( const char *path, const char *mode );
int lc_mkdir( const char *path );					/* 1 ok or exists */
int lc_mkdir_p( const char *path );
int lc_remove( const char *path );					/* file or empty dir */
int lc_rename( const char *from, const char *to );	/* replaces an existing file */
/* read a whole file (NUL-terminated, *len without the NUL); free() it */
char *lc_read_file( const char *path, size_t *len );
/* write via <path>.tmp + rename */
int lc_write_file_atomic( const char *path, const void *data, size_t len );
/* case-insensitive child lookup: out = dir/<actual name of name>; 1 found */
int lc_child_ci( const char *dir, const char *name, char *out, size_t size );
/* directory listing: callback per entry name (not . / ..); return nonzero to stop */
int lc_list_dir( const char *dir, int (*cb)( const char *name, int isDir, void *user ), void *user );
/* free bytes on the volume of dir, 0 = unknown */
unsigned long long lc_free_space( const char *dir );
/* the user's home folder; Windows %USERPROFILE% */
const char *lc_home_dir( void );
/* the app's homepath (%APPDATA%\EliteForce-Marino, $XDG_DATA_HOME/eliteforce-marino);
   same rules as sys_data.c's Sys_DataHomePath (name lower-cased, spaces → '-' on Linux) */
const char *lc_homepath( void );
/* <homepath>/baseEF */
const char *lc_homebase( void );
/* "<n> bytes" … "600 MB" (decimal units, like macOS NSByteCountFormatter file style) */
void lc_format_bytes( unsigned long long n, char *out, size_t size );

/* ---- the running executable --------------------------------------------------- */
/* absolute path of the current executable */
const char *lc_exe_path( void );
/* folder of the current executable */
const char *lc_exe_dir( void );
/* what to run to start a new top-level instance: $APPIMAGE when set (Linux AppImage),
   else lc_exe_path(). On Linux a sibling: lc_sibling_exe("efsp") etc. */
const char *lc_self_command( void );
int lc_sibling_exe( const char *name, char *out, size_t size );	/* 1 if it exists */
/* user's full name (Windows: GetUserNameExW(NameDisplay), else GetUserNameW;
   Linux: GECOS of getpwuid, else $USER); "" if unknown */
const char *lc_user_full_name( void );
/* computer name (Windows GetComputerNameExW(ComputerNameDnsHostname); Linux
   gethostname); LC_PLATFORM_NOUN if unknown */
const char *lc_computer_name( void );
/* Steam Deck / gamescope session */
int lc_is_steam_deck( void );

/* ---- processes ---------------------------------------------------------------- */
/* Start a detached process (no console window on Windows; new session on Linux).
   argv[0] is the program; envAdd = extra "NAME=value" strings (NULL-terminated, may
   be NULL); envRemovePrefix: drop every variable whose name starts with it (e.g.
   "VOYAGERSP_TEST_"), NULL for none. Returns the pid, or -1 (err filled). */
long lc_spawn_detached( const char *const *argv, const char *const *envAdd,
	const char *envRemovePrefix, char *err, size_t errSize );
/* Replace the current process (Linux execv; Windows: spawn detached + exit(0)).
   Returns only on failure. */
int lc_exec( const char *const *argv, const char *const *envAdd );
int lc_pid_alive( long pid );
long lc_getpid( void );

/* ---- desktop integration -------------------------------------------------- */
int lc_open_url( const char *url );					/* default browser */
int lc_open_folder( const char *path );				/* file manager */
/* move a file/folder to the Recycle Bin / freedesktop Trash; 0 with err on failure */
int lc_move_to_trash( const char *path, char *err, size_t errSize );

/* ---- threads and time ---------------------------------------------------------- */
typedef struct lc_thread_s lc_thread;
lc_thread *lc_thread_start( void (*fn)( void *user ), void *user );	/* NULL on failure */
void lc_thread_join( lc_thread *t );				/* also frees it */
void lc_thread_detach( lc_thread *t );				/* frees the handle, thread runs on */
typedef struct lc_mutex_s lc_mutex;
lc_mutex *lc_mutex_new( void );
void lc_mutex_lock( lc_mutex *m );
void lc_mutex_unlock( lc_mutex *m );
void lc_mutex_free( lc_mutex *m );
void lc_sleep_ms( int ms );
double lc_now( void );								/* monotonic seconds */
int64_t lc_time( void );							/* wall clock, unix seconds */

/* ---- sockets (UDP helpers for lc_net / lc_nat / lc_coop) ---------------------------- */
int lc_net_init( void );							/* WSAStartup once; 1 ok */
/* fill out with a random byte string (OS CSPRNG) */
void lc_random_bytes( void *out, size_t n );

/* ---- settings store: <homepath>/launcher.cfg ---------------------------------------
   One `key "value"` per line (no escapes: the value is between the first and the last
   quote, as sys_data.c reads it; newlines become spaces), last wins, keys are
   case-insensitive, unknown keys and their order kept on save. sys_data.c reads the
   `gamefolder` line, so that key's format must not change. Lists: key.n + key.0 … */
typedef struct lc_store_s lc_store;
lc_store *lc_store_load( const char *path );		/* NULL path: <homepath>/launcher.cfg; never NULL */
const char *lc_store_get( lc_store *s, const char *key );		/* NULL if absent */
int lc_store_has( lc_store *s, const char *key );
int lc_store_get_int( lc_store *s, const char *key, int def );
int lc_store_get_bool( lc_store *s, const char *key, int def );
long long lc_store_get_int64( lc_store *s, const char *key, long long def );
void lc_store_set( lc_store *s, const char *key, const char *value );	/* NULL removes */
void lc_store_set_int( lc_store *s, const char *key, int v );
void lc_store_set_int64( lc_store *s, const char *key, long long v );
void lc_store_remove( lc_store *s, const char *key );
/* lists: returns the count; out may be NULL to only count; items truncated to itemSize */
int lc_store_get_list( lc_store *s, const char *key, char *out, size_t itemSize, int max );
int lc_store_has_list( lc_store *s, const char *key );
void lc_store_set_list( lc_store *s, const char *key, const char *items, size_t itemSize, int n );
void lc_store_remove_list( lc_store *s, const char *key );
int lc_store_save( lc_store *s );					/* atomic; 1 ok */
void lc_store_free( lc_store *s );
/* the shared instance for <homepath>/launcher.cfg (loaded on first use) */
lc_store *lc_store_shared( void );

/* ---- HTTP (libcurl loaded at run time) --------------------------------------------- */
#define LC_HTTP_OK				0
#define LC_HTTP_UNAVAILABLE		(-1)	/* no libcurl */
#define LC_HTTP_FAILED			(-2)	/* network error / timeout; err filled */
/* GET url; *body (NUL-terminated, free() it), *len, *status (HTTP code).
   headers: NULL-terminated "Name: value" list or NULL. */
int lc_http_get( const char *url, const char *const *headers, int timeoutSeconds,
	char **body, size_t *len, long *status, char *err, size_t errSize );

#ifdef __cplusplus
}
#endif

#endif
