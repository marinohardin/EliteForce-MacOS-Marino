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
lc_nat *lc_nat_public_address(void (*cb)(lc_nat_result r, const char *ip, int err, void *u), void *u);
lc_nat *lc_nat_map_udp(int port, void (*cb)(lc_nat_result r, const char *ip, int extPort,
                         int err, void *u), void *u);          /* renews until released */
void lc_nat_release(lc_nat *n);                              /* deletes the mapping */
int  lc_ipv4_private(unsigned hostOrderAddr);
int  lc_lan_address(char *out, size_t n);                     /* platform #ifdefs inside */
typedef struct { int listed[LC_MAX_MASTERS], missing[LC_MAX_MASTERS], down[LC_MAX_MASTERS]; } lc_list_result;
void lc_list_check(const char (*masters)[65], int n, const char *want /* ip:port or NULL */,
                    lc_list_result *out);                     /* blocking, parallel */

#ifdef __cplusplus
}
#endif

#endif
