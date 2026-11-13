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

// ---- game/ui module loading ----
// Android resolves the bare soname from the APK's native lib dir. Desktop: the modules sit next to
// the executable (Sys_BinaryPath()): libef*.dylib (macOS), libef*.so (Linux), ef*.dll (Windows).
// The callers still ask for "libefgame.so"/"libefui.so"; this maps the name per OS.
#ifdef __ANDROID__
const char *SP_ModulePath( const char *soname ){ return soname; }
#else
extern char *Sys_BinaryPath( void );
const char *SP_ModulePath( const char *soname ){
    static char path[1024];
    char base[256];
    const char *name = soname;
    const char *ext;
    size_t n;
    snprintf( base, sizeof(base), "%s", name );
    ext = strrchr( name, '.' );
    if( ext && !strcmp( ext, ".so" ) ){
        n = (size_t)( ext - name );
        base[n] = '\0';
#if defined(_WIN32)
        if( !strncmp( base, "lib", 3 ) ) memmove( base, base + 3, strlen( base + 3 ) + 1 );   // libefgame -> efgame
        strncat( base, ".dll", sizeof(base) - strlen(base) - 1 );
#elif defined(__APPLE__)
        strncat( base, ".dylib", sizeof(base) - strlen(base) - 1 );
#else
        strncat( base, ".so", sizeof(base) - strlen(base) - 1 );
#endif
    }
    snprintf( path, sizeof(path), "%s/%s", Sys_BinaryPath(), base );
    return path;
}
#endif

// Thin dlopen/LoadLibrary wrapper (kept local rather than SDL_LoadObject so the flags stay explicit:
// RTLD_NOW and a REAL unload per map -- retail relies on a fresh module image after every unload).
#ifdef _WIN32
#include <windows.h>
static char s_modErr[256];
void *SP_ModuleLoad( const char *path, int global ){
    wchar_t wpath[1024];
    HMODULE h;
    (void)global;
    if( !MultiByteToWideChar( CP_UTF8, 0, path, -1, wpath, 1024 ) ) wpath[0] = 0;
    h = LoadLibraryW( wpath );
    if( !h ) snprintf( s_modErr, sizeof(s_modErr), "LoadLibrary(%s) failed, error %lu", path, (unsigned long)GetLastError() );
    return (void *)h;
}
void *SP_ModuleSym( void *h, const char *name ){ return (void *)GetProcAddress( (HMODULE)h, name ); }
void  SP_ModuleUnload( void *h ){ if( h ) FreeLibrary( (HMODULE)h ); }
const char *SP_ModuleError( void ){ return s_modErr; }
#else
#include <dlfcn.h>
void *SP_ModuleLoad( const char *path, int global ){ return dlopen( path, RTLD_NOW | ( global ? RTLD_GLOBAL : 0 ) ); }
void *SP_ModuleSym( void *h, const char *name ){ return dlsym( h, name ); }
void  SP_ModuleUnload( void *h ){ if( h ) dlclose( h ); }
const char *SP_ModuleError( void ){ const char *e = dlerror(); return e ? e : ""; }
#endif

// Main-thread test for the android/log.h shim (pthread_main_np is Apple-only). The engine
// executable's constructor runs on the main thread.
#ifdef _WIN32
static DWORD s_mainThread;
__attribute__((constructor)) static void sp_initMainThread( void ){ s_mainThread = GetCurrentThreadId(); }
int SP_IsMainThread( void ){ return GetCurrentThreadId() == s_mainThread; }
#else
#include <pthread.h>
#ifdef __APPLE__
int SP_IsMainThread( void ){ return pthread_main_np() != 0; }
#else
static pthread_t s_mainThread;
__attribute__((constructor)) static void sp_initMainThread( void ){ s_mainThread = pthread_self(); }
int SP_IsMainThread( void ){ return pthread_equal( pthread_self(), s_mainThread ); }
#endif
#endif
