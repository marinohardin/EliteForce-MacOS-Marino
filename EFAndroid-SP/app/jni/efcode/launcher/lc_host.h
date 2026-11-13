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
void lc_srv_free(lc_srv *s);
lc_srv_state lc_srv_get_state(const lc_srv *s);
int  lc_srv_is_ready(const lc_srv *s);       /* "Server ready" seen (9.8 becameReady happened) */
int  lc_srv_is_stopping(const lc_srv *s);
int  lc_srv_bound_port(const lc_srv *s);     /* the configured port until the engine reports the bound one */
void lc_srv_feed(lc_srv *s, const char *bytes, size_t n);    /* line assembly + 9.7 */
void lc_srv_poll_tick(lc_srv *s, double nowSeconds);         /* every 2 s while a child exists, 9.6 */
/* A command typed or from a button (9.12): echoes "] <cmd>" (log colour 5), counts a typed
   "status" so its answer is shown, and sends it. Typed "quit" (any case) is lc_srv_begin_stop
   instead (nothing echoed twice). Empty/blank commands are ignored. The UI never needs to
   echo, count or send by itself. */
void lc_srv_user_command(lc_srv *s, const char *cmd);
/* Stop (9.14): state Stopping, "] quit", sends quit. lc_srv_stop_tick (call it with a
   monotonic time every ~250 ms while Stopping) logs the escalation lines and returns 1 when
   the UI should lc_proc_terminate (5 s after quit) and 2 when it should lc_proc_kill (3 s later). */
void lc_srv_begin_stop(lc_srv *s);
int  lc_srv_stop_tick(lc_srv *s, double nowSeconds);
/* the child ended: flushes the last line, logs "Server: exited (...)", then Stopped / Failed
   with the 9.14 messages and details */
void lc_srv_exited(lc_srv *s, int signaled, int code);
/* the child couldn't be started (cfg write failed / spawn failed): logs the line in red (may be
   NULL), state Failed "couldn't start" */
void lc_srv_start_failed(lc_srv *s, const char *logLine);
/* the exact commands of the server controls (9.12); all go through lc_srv_user_command */
void lc_srv_next_map(lc_srv *s);                       /* vstr nextmap */
void lc_srv_restart_map(lc_srv *s);                    /* map_restart */
void lc_srv_change_map_slot(lc_srv *s, int slot);      /* vstr vsp_map<slot>  (slot is 1-based) */
void lc_srv_change_map(lc_srv *s, const char *map);    /* map <name> */
void lc_srv_add_bot(lc_srv *s, int skill);             /* addbot <random free name> <skill> */
void lc_srv_remove_bots(lc_srv *s);                    /* set bot_minplayers 0, kick allbots + the secondary note */
void lc_srv_kick(lc_srv *s, const char *num);          /* clientkick <num>; the person-confirmation is the UI's */
void lc_srv_say(lc_srv *s, const char *text);          /* say "<sanitised>" (nothing if it ends up empty) */
void lc_srv_heartbeat(lc_srv *s);                      /* the Check Again's "heartbeat" (not echoed) */
const char *lc_srv_addbot_command(lc_srv *s, int skill, char *buf, size_t n);
void lc_srv_say_command(const char *text, char *buf, size_t n);   /* sanitise, 150 chars; "" if empty */
/* process spawning: small portable helper (posix_spawn / CreateProcessW + Job Object) */
typedef struct lc_proc lc_proc;
/* argv includes argv[0] and is NULL-terminated; envp = the complete environment ("NAME=value",
   NULL-terminated) or NULL to inherit ours minus every VOYAGERSP_TEST_* variable. stdout and
   stderr come to `out` on a reader thread (stdin is a pipe: lc_proc_write); `exited` is called from
   that thread after the output ended and the process was reaped (signaled = POSIX signal, or a
   Windows crash code >= 0xC0000000; code = exit status / signal number). Windows: no console
   window, a Job Object with KILL_ON_JOB_CLOSE; Linux: PR_SET_PDEATHSIG SIGTERM. NULL on failure
   (errno set on POSIX). */
lc_proc *lc_proc_spawn(const char *exe, const char *const *argv, const char *const *envp,
                         void (*out)(const char *b, size_t n, void *u),
                         void (*exited)(int signaled, int code, void *u), void *u);
void lc_proc_free(lc_proc *p);        /* kills it if still running, joins the reader, frees; call from the UI thread, not from `exited` */
int  lc_proc_running(lc_proc *p);
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
