/*
===========================================================================
Launcher core: Hosting: the dedicated-server child process, its console parser and state, the running-servers registry. Spec: servers.md §9, §15.
Owner: core-b. Derived from servers.md §15.2 (names vsp_ → lc_); the owner may
add declarations and adjust details, see docs/launcher-spec/ARCHITECTURE.md.
Persistence goes through lc_store (lc_common.h) instead of the vsp_store bridge.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#ifndef LC_HOST_H
#define LC_HOST_H

#include "lc_common.h"
#include "lc_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- dedicated server host (9) ---- */
typedef enum { LC_SRV_STARTING, LC_SRV_RUNNING, LC_SRV_IDLE, LC_SRV_STOPPING,
               LC_SRV_STOPPED, LC_SRV_FAILED } lc_srv_state;
typedef struct { char name[64], score[12], ping[12], address[64], num[8]; int bot; } lc_srv_player;
typedef struct lc_srv lc_srv;          /* console parser + state machine, no I/O of its own */
typedef struct {
    void (*send)(const char *cmd, void *u);                    /* write cmd + "\n" to stdin */
    void (*log)(const char *line, int color /* 0 normal,1 orange,2 red,3 green,4 secondary,5 accent */, void *u);
    void (*state)(lc_srv_state s, const char *detail, void *u);
    void (*players)(const lc_srv_player *p, int n, void *u);
    void (*info)(const char *currentMap, int boundPort, void *u);
    void (*ready)(int boundPort, void *u);                     /* once */
    void *user;
} lc_srv_callbacks;
lc_srv *lc_srv_new(const lc_srv_callbacks *cb, int configuredPort);
void lc_srv_feed(lc_srv *s, const char *bytes, size_t n);    /* line assembly + 9.7 */
void lc_srv_poll_tick(lc_srv *s, double nowSeconds);         /* every 2 s, 9.6 */
void lc_srv_user_command(lc_srv *s, const char *cmd);        /* counts "status" */
void lc_srv_exited(lc_srv *s, int signaled, int code);       /* 9.14 messages + state */
const char *lc_srv_addbot_command(lc_srv *s, int skill, char *buf, size_t n);
void lc_srv_say_command(const char *text, char *buf, size_t n);   /* sanitise, 150 chars */
/* process spawning: small portable helper (posix_spawn / CreateProcessW + Job Object) */
typedef struct lc_proc lc_proc;
lc_proc *lc_proc_spawn(const char *exe, const char *const *argv, const char *const *envp,
                         void (*out)(const char *b, size_t n, void *u),
                         void (*exited)(int signaled, int code, void *u), void *u);
int  lc_proc_write(lc_proc *p, const char *b, size_t n);
void lc_proc_terminate(lc_proc *p);   /* SIGTERM / TerminateProcess */
void lc_proc_kill(lc_proc *p);        /* SIGKILL */
int  lc_proc_pid(lc_proc *p);

/* ---- running-servers registry (9.13) ---- */
struct lc_running { int pid; char name[64]; int port; long long started; };
int  lc_running_register(const char *homeRoot, const char *name, int port);
void lc_running_unregister(const char *homeRoot);
int  lc_running_list(const char *homeRoot, struct lc_running *out, int max); /* prunes stale */

#ifdef __cplusplus
}
#endif

#endif
