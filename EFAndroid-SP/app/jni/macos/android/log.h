/* macOS stand-in for <android/log.h>: the sp/ bridge files include it
 * unconditionally for logcat output. Route it to stderr instead. */
#ifndef EF_MACOS_ANDROID_LOG_H
#define EF_MACOS_ANDROID_LOG_H

#include <stdarg.h>
#include <stdio.h>

enum { ANDROID_LOG_DEBUG = 3, ANDROID_LOG_INFO = 4, ANDROID_LOG_WARN = 5, ANDROID_LOG_ERROR = 6 };

static inline int __android_log_vprint( int prio, const char *tag, const char *fmt, va_list ap ) {
	(void)prio;
	fprintf( stderr, "[%s] ", tag );
	return vfprintf( stderr, fmt, ap );
}

static inline int __android_log_print( int prio, const char *tag, const char *fmt, ... ) {
	va_list ap;
	int r;
	va_start( ap, fmt );
	r = __android_log_vprint( prio, tag, fmt, ap );
	va_end( ap );
	fputc( '\n', stderr );
	return r;
}

#endif
