/*
===========================================================================
Single-player co-op: the connection between a host and its guest
(sys_coop_net.c). Only EF_COOP builds have it.

The host side shares the UDP socket of the co-op checks (sys_osx_coop.m),
which hands it every packet that isn't a check and calls its frame function.
The guest side has a socket of its own.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#ifndef SYS_COOP_NET_H
#define SYS_COOP_NET_H

#ifdef EF_COOP

#include <netinet/in.h>

// the UDP port a co-op host answers co-op checks and its guest on
#define VSP_COOP_PORT		29760
// bumped whenever co-op's network messages change; host and guest must match
// (1: the M0 check only; 2: M1, a guest can connect)
#define VSP_COOP_PROTOCOL	2

// the biggest co-op packet (fits a usual Ethernet/Wi-Fi MTU with room to spare)
#define COOP_MAX_PACKET		1400

// The host (sys_osx_coop.m calls these while it has the co-op socket open).
// A packet that isn't a co-op check:
void		CoopNet_HostPacket( int sock, const unsigned char *data, int len, const struct sockaddr_in *from );
// once a frame, after the packets: timeouts and the host's status messages
void		CoopNet_HostFrame( int sock );
// the socket is about to close: tell the guest, forget it
void		CoopNet_HostShutdown( int sock, const char *reason );
// keys for the co-op check's answer: "needPassword" (1 when the host has a password)
// and "guest" (1 while a guest is connected)
int			CoopNet_HostNeedsPassword( void );
int			CoopNet_HostHasGuest( void );
// the coop_status / coop_disconnect / coop_kick commands (once; the guest adds them itself)
void		CoopNet_AddCommands( void );

// For the host's game, once a game frame (sp/sp_bridge.cpp declares these itself, with the
// game's types): the guest's newest input; 0 when no guest steers player 2 right now.
// angles are 0..65535 like a usercmd's; buttons include every press since the last call.
int			CoopNet_GuestInput( int *angles, int *buttons, int *weapon, signed char *forwardmove,
				signed char *rightmove, signed char *upmove );
// the guest's name, "" without a guest
const char	*CoopNet_GuestName( void );

// The guest. Once a frame (sys_osx_coop.m, before anything of the host): connects when
// the coop_connect cvar names a host, sends this player's input, disconnects when the
// cvar is cleared. Does nothing (one cvar read) while coop_connect is empty.
void		CoopNet_GuestFrame( void );
// coop_connect is set: this copy is a guest, so it doesn't host
int			CoopNet_GuestActive( void );

// The launcher's Join password for the guest, and its Host password for the host (both
// stay out of the command line and the launcher's log). The coop_password cvar wins when set.
void		Sys_CoopSetLaunchPassword( const char *password );
const char	*Sys_CoopPassword( void );

#endif // EF_COOP

#endif // SYS_COOP_NET_H
