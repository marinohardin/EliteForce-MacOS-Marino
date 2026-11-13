/*
===========================================================================
Launcher core: Router port mapping (NAT-PMP + UPnP IGD), public address, LAN address, the internet-listing check. Spec: servers.md §7.10, §7.11, §9.9, §9.10, §10.
Owner: core-b. Derived from servers.md §15.2 (names vsp_ → lc_); the owner may
add declarations and adjust details, see docs/launcher-spec/ARCHITECTURE.md.
Persistence goes through lc_store (lc_common.h) instead of the vsp_store bridge.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#ifndef LC_NAT_H
#define LC_NAT_H

#include "lc_common.h"
#include "lc_server.h"
#ifdef _WIN32
#include <winsock2.h>
#else
#include <netinet/in.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---- NAT-PMP / PCP / UPnP (7.11) and listing check (9.10) ---- */
typedef enum { LC_NAT_OK, LC_NAT_UNSUPPORTED, LC_NAT_DISABLED, LC_NAT_DOUBLE,
               LC_NAT_PRIVATE_EXTERNAL, LC_NAT_TIMEOUT, LC_NAT_OTHER } lc_nat_result;
typedef struct lc_nat lc_nat;
/* Both start a worker thread and call cb exactly once from it (never while the caller holds
   a lock of its own). Result mapping (7.11, 9.9):
     LC_NAT_OK               success: ip = the router's public address, extPort the mapped port
     LC_NAT_UNSUPPORTED      no gateway, or the router says unsupported (NAT-PMP result 1/5, no
                             UPnP IGD with a WAN connection service)
     LC_NAT_DISABLED         the router refuses (NAT-PMP result 2, UPnP 606): port opening is off
     LC_NAT_PRIVATE_EXTERNAL mapped, but the router's external address (ip) is private (7.11)
     LC_NAT_TIMEOUT          nobody answered (NAT-PMP and UPnP both silent)
     LC_NAT_OTHER            err = the protocol's error number
   (LC_NAT_DOUBLE is kept for the contract; this implementation reports PRIVATE_EXTERNAL instead.)
   The UI keeps its own 15 s give-up timer (9.9); the worker finishes within ~12 s. */
lc_nat *lc_nat_public_address(void (*cb)(lc_nat_result r, const char *ip, int err, void *u), void *u);
lc_nat *lc_nat_map_udp(int port, void (*cb)(lc_nat_result r, const char *ip, int extPort,
                         int err, void *u), void *u);          /* renews until released */
/* stops the worker, deletes the mapping (NAT-PMP lifetime 0 / UPnP DeletePortMapping) and frees
   the handle. Blocks for up to ~2 s. Safe to call from inside the callback (the cleanup then
   happens when the callback returns). */
void lc_nat_release(lc_nat *n);                              /* deletes the mapping */
int  lc_ipv4_private(unsigned hostOrderAddr);
int  lc_lan_address(char *out, size_t n);                     /* platform #ifdefs inside */
/* the co-op sheet's list (LDC 6.1): IPv4 addresses of interfaces that are up, not loopback, not
   169.254/16; the one with the default route first (macOS: en0). Returns the count. */
int  lc_lan_addresses(char addrs[][16], char ifnames[][64], int max);
/* default gateway (host byte order) or 0 */
unsigned lc_default_gateway(void);

/* The public address of this network, from the router (servers.md 10). lc_public_address_start
   is called once at launcher start: an address-only lookup, silent on errors, never retried;
   logs "Public address (from the router): <ip>". Env VOYAGERSP_TEST_NAT=<ipv4>|fail sets it
   directly. lc_public_address returns NULL while unknown. lc_public_address_wait pumps nothing:
   it sleeps in 50 ms steps while a lookup is in progress, up to ms; returns 1 when known.
   (VSP_LoopbackArgs' 1.5 s wait on the UI thread: call this with 1500 from a worker, or poll.) */
void lc_public_address_start(void);
const char *lc_public_address(void);
int  lc_public_address_wait(int ms);
int  lc_public_address_pending(void);
typedef struct { int listed[LC_MAX_MASTERS], missing[LC_MAX_MASTERS], down[LC_MAX_MASTERS]; } lc_list_result;
void lc_list_check(const char (*masters)[65], int n, const char *want /* ip:port or NULL */,
                    lc_list_result *out);                     /* blocking, parallel */

#ifdef __cplusplus
}
#endif

#endif
