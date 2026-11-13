/*
 * Module-load smoke test: load the game modules the way the engine does
 * (dlopen / LoadLibrary) and resolve every exported entry point.
 *
 *   modload <path/to/efgame-module> [<path/to/efui-module> ...]
 *
 * A module whose file name contains "efui" is checked against the UI list,
 * anything else against the game list. Returns 0 if all load and resolve,
 * nonzero otherwise. The names match efgame/android/efgame.ver and efui.ver
 * (Itanium C++ mangling; the same on Linux, macOS and Windows x64 MinGW).
 */
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define LOAD(p) ((void *)LoadLibraryA(p))
#define SYM(h, n) ((void *)GetProcAddress((HMODULE)(h), (n)))
#define CLOSE(h) FreeLibrary((HMODULE)(h))
static const char *lasterr(void) { static char b[64]; snprintf(b, sizeof b, "GetLastError=%lu", GetLastError()); return b; }
#else
#include <dlfcn.h>
#define LOAD(p) dlopen((p), RTLD_NOW)
#define SYM(h, n) dlsym((h), (n))
#define CLOSE(h) dlclose(h)
static const char *lasterr(void) { const char *e = dlerror(); return e ? e : "unknown"; }
#endif

static const char *game_required[] = {
	"_Z10GetGameAPIP13game_import_t",	/* GetGameAPI */
	"_Z6vmMainiiiiiiiii",			/* vmMain (cgame) */
	"_Z8dllEntryPFiizE",			/* dllEntry (cgame) */
	"_Z15CG_GetCameraPosPf",		/* CG_GetCameraPos */
	NULL
};
/* in efgame.exp (macOS) but not the GNU version script: warn only */
static const char *game_optional[] = { "G_RegressDump", NULL };
static const char *ui_required[] = { "_Z8GetUIAPIv", NULL };

static int check(const char *path)
{
	const char **req, **opt = NULL, **s;
	int bad = 0;
	void *h;

	if (strstr(path, "efui")) req = ui_required;
	else { req = game_required; opt = game_optional; }

	h = LOAD(path);
	if (!h) { printf("FAIL: cannot load %s: %s\n", path, lasterr()); return 1; }
	printf("loaded %s\n", path);
	for (s = req; *s; s++) {
		if (SYM(h, *s)) printf("  ok   %s\n", *s);
		else { printf("  FAIL missing export %s\n", *s); bad++; }
	}
	for (s = opt; s && *s; s++) {
		if (SYM(h, *s)) printf("  ok   %s (optional)\n", *s);
		else printf("  note optional export %s absent\n", *s);
	}
	CLOSE(h);
	return bad;
}

int main(int argc, char **argv)
{
	int i, bad = 0;
	if (argc < 2) { fprintf(stderr, "usage: %s module...\n", argv[0]); return 2; }
	for (i = 1; i < argc; i++) bad += check(argv[i]);
	printf(bad ? "modload: FAILED\n" : "modload: OK\n");
	return bad ? 1 : 0;
}
