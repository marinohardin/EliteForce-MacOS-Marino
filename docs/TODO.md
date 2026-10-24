# TODO and proposals

Ideas that were discussed and parked. Each entry says what and why, and roughly how.

## Host sees their own internet server (router loopback)

**Status:** proposal, not started (2026-10-24).

**Problem.** When you host an internet server, the master server lists it (the server
window's **Listed** row confirms it), but the host's own Holomatch browser doesn't show it
under INTERNET2. The browser pings every listed server and hides the ones that don't
answer. The host's ping goes to its own public address, and most home routers don't route
traffic from the LAN back in to their own public address ("NAT loopback" / "hairpin NAT").
Players on other networks see the server normally. Today the host uses **Play on This
Mac** or the LOCAL list instead.

**Proposal.** In the client (Holomatch, macOS only):

1. At startup, ask the router for its public IPv4 address with an address-only NAT-PMP
   lookup (`DNSServiceNATPortMappingCreate` with protocol 0 and ports 0, as the server
   window's `startListCheck` already does). No port is opened.
2. When a master's server list arrives (`CL_ServersResponsePacket` in `cl_main.c`), rewrite
   any entry whose address is that public address to this Mac's LAN address (or
   `127.0.0.1`), keeping the port, before the browser pings it.

The host's server then shows up in their own INTERNET2 list with a normal ping, and Engage
connects over the local network. Nothing changes on the wire for anyone else.

**Notes.**
- Hard to test without a second network; a laptop on a phone hotspot works.
- Possibly also useful: show "(this Mac)" in the hostname, or prefer the LAN entry when the
  same server also appears in LOCAL.
