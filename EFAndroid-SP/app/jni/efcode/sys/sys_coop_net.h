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
// (1: the M0 check only; 2: M1, a guest can connect; 3: the guest mirrors the host's world;
// 4: one guest command per host frame, acknowledged for prediction; 5: the guest says which host
// level it has up, movies, cutscene skips; 6: each guest command says what host time the guest was
// looking at, for its shots; 7: the game's own paks by name and checksum in the check's answer,
// the connect and the accept)
#define VSP_COOP_PROTOCOL	7

// the longest player name co-op sends (coop_name; the launcher's "Your name")
#define COOP_NAME_MAX		31
#define COOP_LOOK_MAX		24	// a player 2 character id with its terminator (= COOP_CHARACTER_MAX)

// the biggest co-op packet (fits a usual Ethernet/Wi-Fi MTU with room to spare)
#define COOP_MAX_PACKET		1400

// A replica frame's slots: the game's entity images, the configstrings, the game's globals image,
// and the host's newest server commands.
#define COOP_SLOT_CS		1024
#define COOP_SLOT_GLOBALS	2048
#define COOP_SLOT_COMMANDS	2049
#define COOP_NUM_SLOTS		2050

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
// the host level (sp/sp_bridge.cpp's level id) the guest has loaded and runs, 0 for none yet
int			CoopNet_HostGuestLevel( void );
// Host's and guest's game files (FS_CoopPaks lists, "pak0.pk3:<checksum> ..."): the same,
// only the Expansion Pack's pak3.pk3 differs (a warning: they can still play), or pak0-pak2
// differ (the host refuses). text: sentences naming the packs, for the host (forHost) or guest.
#define COOP_PAKS_SAME		0
#define COOP_PAKS_EXPANSION	1
#define COOP_PAKS_DIFFER	2
int			CoopNet_ComparePaks( const char *hostPaks, const char *guestPaks, qboolean forHost, char *text, int size );
// the coop_status / coop_disconnect / coop_kick commands (once; the guest adds them itself)
void		CoopNet_AddCommands( void );

// For the host's game, once a game frame (sp/sp_bridge.cpp declares these itself, with the
// game's types): the guest's next command (one a frame, in order; with none waiting, the newest
// one's angles and no movement); 0 when no guest steers player 2 right now. angles are 0..65535
// like a usercmd's.
int			CoopNet_GuestInput( int *angles, int *buttons, int *weapon, signed char *forwardmove,
				signed char *rightmove, signed char *upmove );
// the host level time the guest's screen showed when it made the command CoopNet_GuestInput
// returned last (its shots are checked against the world as it was then); 0 when unknown
int			CoopNet_GuestViewTime( void );
// the guest's oldest console command for player 2 not run yet (a cheat; the bridge checks which),
// 0 when there is none
int			CoopNet_HostTakeCommand( char *out, int size );
// the guest's name, "" without a guest
const char	*CoopNet_GuestName( void );
// how the guest wants player 2 to look: "male", "female", or "" (like player 1, or no guest)
const char	*CoopNet_GuestSex( void );
// the character the guest chose for player 2 (an id from the game's coop_characters.h), "" for
// the default Munro or without a guest
const char	*CoopNet_GuestLook( void );
// qpath, as the game would load it, comes from the game's own pak0-pak2 (not from a folder or
// another pack)
qboolean	CoopNet_CorePakFile( const char *qpath );

// The host's replica frames (sp/sp_bridge.cpp, after every game frame while a guest is here):
// Begin returns 0 when no guest wants one; then every slot that isn't empty, then End sends it.
int			CoopNet_HostFrameBegin( int levelId, int levelTime, const char *map );
void		CoopNet_HostFrameSlot( int slot, const void *data, int len );
// ...or the slot as it was in the frame before (qfalse: there it was empty, or there is none)
qboolean	CoopNet_HostFrameSlotKeep( int slot );
void		CoopNet_HostFrameEnd( void );

// The guest's replica frames, for sp/sp_bridge.cpp: the newest one put together (0: none yet),
// a frame's header (qfalse: no longer kept), and a slot of it (its length, 0 when empty).
int			CoopNet_GuestNewestFrame( void );
qboolean	CoopNet_GuestFrameInfo( int frame, int *levelId, int *levelTime, int *ackCmd );
int			CoopNet_GuestFrameSlot( int frame, int slot, const unsigned char **data );
// the host level the guest's current map load is for (frames of other levels aren't applied)
int			CoopNet_GuestLevelId( void );
// the bridge applied a frame of host level levelId with its cgame up: the guest is in that level
// (the host holds a new level's start until then)
void		CoopNet_GuestLevelReady( int levelId );
// the host's game isn't going on: 1 the host paused it (menu, console), 2 its frames stopped
// coming for another reason (a load, the network); 0 it plays
int			CoopNet_GuestHostPaused( void );
// this player's command for one host frame, numbered by the bridge (a frame's ackCmd is the
// newest of these the host applied), with the host level time its screen showed (viewTime); sent
// at once; 0 when this player has no input now
int			CoopNet_GuestQueueCmd( int seq, const int *angles, int buttons, int weapon,
				int forwardmove, int rightmove, int upmove, int viewTime );
// a console command for player 2 on the host (a cheat), sent now; 0 when not connected
int			CoopNet_GuestSendCommand( const char *text );

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
// this Mac's name as the Finder shows it ("Mac" when there is none), for a player without coop_name
void		Sys_CoopMacName( char *out, int size );

#endif // EF_COOP

#endif // SYS_COOP_NET_H
