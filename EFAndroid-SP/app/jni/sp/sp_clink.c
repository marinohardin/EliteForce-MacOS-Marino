// sp_clink.c (compiled as C) — hands the bridge the engine's Com_Printf/Com_Error with
// C linkage. (The SP game headers declare these with C++ linkage, which would mangle and
// not resolve against the C engine symbols; the bridge gets them through here instead.)
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

extern void Com_Printf( const char *fmt, ... );
extern void Com_Error( int level, const char *fmt, ... );

typedef void (*sp_printf_t)( const char *fmt, ... );

sp_printf_t SP_ComPrintf( void ){ return Com_Printf; }

void SP_ComError( int level, const char *fmt, ... ){
    char b[2048]; va_list a; va_start(a,fmt); vsnprintf(b,sizeof(b),fmt,a); va_end(a);
    Com_Error( level, "%s", b );
}

// Module path for dlopen. Android resolves the bare soname from the APK's native lib
// dir; on macOS the modules are dylibs next to the executable.
#ifdef __APPLE__
extern char *Sys_BinaryPath( void );
const char *SP_ModulePath( const char *soname ){
    static char path[1024];
    char base[256];
    const char *ext;
    snprintf( base, sizeof(base), "%s", soname );
    ext = strrchr( soname, '.' );
    if( ext && !strcmp( ext, ".so" ) ) base[ ext - soname ] = '\0', strncat( base, ".dylib", sizeof(base) - strlen(base) - 1 );
    snprintf( path, sizeof(path), "%s/%s", Sys_BinaryPath(), base );
    return path;
}
#else
const char *SP_ModulePath( const char *soname ){ return soname; }
#endif
