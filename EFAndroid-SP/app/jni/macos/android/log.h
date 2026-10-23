/* macOS stand-in for <android/log.h>: the sp/ bridge files include it
 * unconditionally for logcat output.
 *
 * On desktop the messages go through the engine console (Com_Printf), so they
 * reach the in-game console and qconsole.log, and con_tty can hide and redraw
 * its "tty]" prompt around them. Writing straight to stderr without a trailing
 * newline is what mangled lines like
 *   tty][ui] SP UI loaded (UI_API_VERSIO--- Common Initialization Complete ---
 *
 *   ANDROID_LOG_DEBUG  -> only when 'developer' is set (bridge chatter)
 *   ANDROID_LOG_INFO   -> always
 *   ANDROID_LOG_WARN   -> always, yellow
 *   ANDROID_LOG_ERROR  -> always, red
 *
 * Off the main (game) thread Com_Printf is not safe, so those messages fall
 * back to one newline-terminated stderr write. Not async-signal-safe: the
 * crash backtrace (sp_integration.c) writes to stderr itself. */
#ifndef EF_MACOS_ANDROID_LOG_H
#define EF_MACOS_ANDROID_LOG_H

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

enum { ANDROID_LOG_DEBUG = 3, ANDROID_LOG_INFO = 4, ANDROID_LOG_WARN = 5, ANDROID_LOG_ERROR = 6 };

#ifdef __cplusplus
extern "C" {
#endif
/* engine services with C linkage (sp_clink.c, qcommon/cvar.c) */
void (*SP_ComPrintf( void ))( const char *fmt, ... );
int  Cvar_VariableIntegerValue( const char *var_name );
#ifdef __cplusplus
}
#endif

static inline int ef_desktop_log( int prio, const char *tag, const char *fmt, va_list ap ) {
	char buf[4096];
	const char *color = "";
	size_t len;
	int r = vsnprintf( buf, sizeof( buf ), fmt, ap );

	len = strlen( buf );
	while ( len > 0 && ( buf[len - 1] == '\n' || buf[len - 1] == '\r' ) )
		buf[--len] = '\0';

	if ( !pthread_main_np() ) {
		fprintf( stderr, "[%s] %s\n", tag, buf );
		return r;
	}
	if ( prio <= ANDROID_LOG_DEBUG && !Cvar_VariableIntegerValue( "developer" ) )
		return r;
	if ( prio == ANDROID_LOG_WARN )
		color = "^3";
	else if ( prio >= ANDROID_LOG_ERROR )
		color = "^1";
	SP_ComPrintf()( "%s[%s] %s\n", color, tag, buf );
	return r;
}

static inline int __android_log_vprint( int prio, const char *tag, const char *fmt, va_list ap ) {
	return ef_desktop_log( prio, tag, fmt, ap );
}

static inline int __android_log_print( int prio, const char *tag, const char *fmt, ... ) {
	va_list ap;
	int r;
	va_start( ap, fmt );
	r = ef_desktop_log( prio, tag, fmt, ap );
	va_end( ap );
	return r;
}

#endif
