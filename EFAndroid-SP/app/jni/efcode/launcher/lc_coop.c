/*
===========================================================================
Launcher core: co-op, launcher side (query client, description, invite link,
the sheet's choice). Spec: launcher-data-coop.md 6.1, 6.2, 6.4, 10.9.
This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#include "lc_sock.h"
#include "lc_coop.h"
#include "lc_nat.h"
#include "lc_data.h"
#include "../../../../../efgame/src/game/coop_characters.h"	/* player 2's characters (coop_look) */

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#ifndef EF_APP_VERSION
#	define EF_APP_VERSION	"unknown"
#endif

#define COOP_OOB		"\xff\xff\xff\xff"
#define COOP_QUERY		"getcoopinfo"
#define COOP_RESPONSE	"coopInfoResponse"
#define COOP_NAME_MAX	31

const char *lc_coop_app_version( void )
{
	return EF_APP_VERSION;
}

/* a name the game can show and the command line can carry (LDC 6.2 CoopSafeName, without the
   transliteration: ASCII only, no quotes, backslash, semicolon or percent) */
static void safe_name( const char *in, char *out, size_t size )
{
	size_t o = 0;
	const unsigned char *p = (const unsigned char *)( in ? in : "" );

	for ( ; *p && o < COOP_NAME_MAX && o + 1 < size; p++ ) {
		if ( *p < ' ' || *p > '~' || *p == '"' || *p == '\\' || *p == ';' || *p == '%' )
			continue;
		if ( *p == ' ' && ( o == 0 || out[o - 1] == ' ' ) )
			continue;
		out[o++] = (char)*p;
	}
	out[o] = 0;
	lc_trim( out );
}

/* ---- infostring --------------------------------------------------------------------------- */

void lc_coop_info_value( const char *info, const char *key, char *out, size_t size )
{
	const char *p = info;
	size_t kl = strlen( key );

	if ( size )
		out[0] = 0;
	if ( !info || *info != '\\' )
		return;
	while ( *p == '\\' ) {
		const char *k = ++p, *v, *ke, *ve;
		while ( *p && *p != '\\' )
			p++;
		if ( !*p )
			break;
		ke = p;
		v = ++p;
		while ( *p && *p != '\\' )
			p++;
		ve = p;
		if ( (size_t)( ke - k ) == kl && !strncmp( k, key, kl ) ) {
			size_t n = (size_t)( ve - v );
			if ( n >= size )
				n = size ? size - 1 : 0;
			if ( size ) {
				memcpy( out, v, n );
				out[n] = 0;
			}
			return;
		}
	}
}

static int has_key( const char *info, const char *key )
{
	char tmp[2];
	const char *p = info;
	size_t kl = strlen( key );
	(void)tmp;
	if ( !info || *info != '\\' )
		return 0;
	while ( *p == '\\' ) {
		const char *k = ++p, *ke;
		while ( *p && *p != '\\' )
			p++;
		if ( !*p )
			break;
		ke = p;
		++p;
		while ( *p && *p != '\\' )
			p++;
		if ( (size_t)( ke - k ) == kl && !strncmp( k, key, kl ) )
			return 1;
	}
	return 0;
}

/* Latin-1 fallback for non-UTF-8 values */
static void fix_utf8( char *s, size_t size )
{
	if ( lc_utf8_valid( s ) )
		return;
	{
		char *tmp = (char *)malloc( strlen( s ) * 2 + 1 );
		size_t o = 0;
		const unsigned char *p = (const unsigned char *)s;
		if ( !tmp )
			return;
		for ( ; *p; p++ ) {
			if ( *p < 0x80 )
				tmp[o++] = (char)*p;
			else {
				tmp[o++] = (char)( 0xc0 | ( *p >> 6 ) );
				tmp[o++] = (char)( 0x80 | ( *p & 0x3f ) );
			}
		}
		tmp[o] = 0;
		lc_strlcpy( s, tmp, size );
		lc_utf8_truncate( s, size - 1 );
		free( tmp );
	}
}

static const char *skill_name( int skill, const char *raw, char *buf, size_t n )
{
	switch ( skill ) {
	case 0:		return "Easy";
	case 1:		return "Medium";
	case 2:		return "Hard";
	default:	lc_snprintf( buf, n, "Skill %s", raw ); return buf;
	}
}

void lc_coop_describe( const char *info, char *out, size_t size )
{
	char mission[160], map[96], state[32], skillRaw[16], skillBuf[40], timeRaw[16], player[160], safe[64], guest[8], needPw[8],
		ver[64], proto[16], pl[16], mp[16];
	int players, maxPlayers;
	size_t o;

	if ( !size )
		return;
	out[0] = 0;
	lc_coop_info_value( info, "mission", mission, sizeof( mission ) );
	lc_coop_info_value( info, "map", map, sizeof( map ) );
	lc_coop_info_value( info, "state", state, sizeof( state ) );
	lc_coop_info_value( info, "skill", skillRaw, sizeof( skillRaw ) );
	lc_coop_info_value( info, "time", timeRaw, sizeof( timeRaw ) );
	lc_coop_info_value( info, "player", player, sizeof( player ) );
	lc_coop_info_value( info, "guest", guest, sizeof( guest ) );
	lc_coop_info_value( info, "needPassword", needPw, sizeof( needPw ) );
	lc_coop_info_value( info, "version", ver, sizeof( ver ) );
	lc_coop_info_value( info, "coopProtocol", proto, sizeof( proto ) );
	lc_coop_info_value( info, "players", pl, sizeof( pl ) );
	lc_coop_info_value( info, "maxPlayers", mp, sizeof( mp ) );
	fix_utf8( mission, sizeof( mission ) );
	fix_utf8( map, sizeof( map ) );
	players = atoi( pl );
	maxPlayers = atoi( mp );

	if ( strcmp( state, "playing" ) || !map[0] )
		lc_strlcat( out, "The host is in the menus: no level is loaded yet.", size );
	else if ( mission[0] ) {
		lc_strlcat( out, "Mission: \xe2\x80\x9c", size );
		lc_strlcat( out, mission, size );
		lc_strlcat( out, "\xe2\x80\x9d (map ", size );
		lc_strlcat( out, map, size );
		lc_strlcat( out, ")", size );
	} else {
		lc_strlcat( out, "Map: ", size );
		lc_strlcat( out, map, size );
	}
	lc_strlcat( out, "\nDifficulty: ", size );
	lc_strlcat( out, skill_name( atoi( skillRaw ), skillRaw, skillBuf, sizeof( skillBuf ) ), size );
	if ( has_key( info, "time" ) ) {
		int t = atoi( timeRaw );
		char tmp[64];
		if ( t < 60 )
			lc_strlcat( out, " \xc2\xb7 on this level for under a minute", size );
		else {
			lc_snprintf( tmp, sizeof( tmp ), " \xc2\xb7 on this level for %d min", t / 60 );
			lc_strlcat( out, tmp, size );
		}
	}
	o = strlen( out );
	lc_snprintf( out + o, size - o, "\nPlayers: %d of %d%s", players > 1 ? players : 1, maxPlayers > 0 ? maxPlayers : 2,
		!strcmp( guest, "1" ) ? " (the host, and a friend as player 2: the game is full)" :
		players >= 2 ? " (the host, and player 2 as a stand-in on the host\xe2\x80\x99s " LC_PLATFORM_NOUN_LC " until a friend joins)" :
		" (the host)" );
	if ( player[0] ) {
		safe_name( player, safe, sizeof( safe ) );
		lc_strlcat( out, "\nHost: ", size );
		lc_strlcat( out, safe, size );
	}
	if ( !strcmp( needPw, "1" ) )
		lc_strlcat( out, "\nThe host set a password.", size );
	o = strlen( out );
	lc_snprintf( out + o, size - o, "\nHost\xe2\x80\x99s app: Elite Force %s \xc2\xb7 co-op version %s", ver[0] ? ver : "?", proto[0] ? proto : "?" );
}

/* ---- the query (6.4) ------------------------------------------------------------------------- */

static void set_result( lc_coop_query *out, const char *status, const char *address, const char *error )
{
	lc_strlcpy( out->status, status, sizeof( out->status ) );
	lc_strlcpy( out->address, address ? address : "", sizeof( out->address ) );
	lc_strlcpy( out->error, error ? error : "", sizeof( out->error ) );
}

void lc_coop_query_run( const char *address, double timeout, lc_coop_query *out )
{
	char a[128], host[128], msg[512], challenge[16], q[64], shown[64];
	char *firstColon, *secondColon;
	int port = LC_COOP_PORT, sends = 0, ql, sendErr = 0;
	struct addrinfo hints, *res = NULL;
	struct sockaddr_in to;
	lc_sock fd;
	double deadline;
	unsigned rnd;

	memset( out, 0, sizeof( *out ) );
	lc_net_init( );
	lc_strlcpy( a, address ? address : "", sizeof( a ) );
	lc_trim( a );
	if ( !a[0] ) {
		set_result( out, "bad", NULL, "Type the host\xe2\x80\x99s address, such as 192.168.1.20." );
		return;
	}
	lc_strlcpy( host, a, sizeof( host ) );
	firstColon = strchr( a, ':' );
	secondColon = firstColon ? strchr( firstColon + 1, ':' ) : NULL;
	if ( secondColon ) {
		set_result( out, "bad", NULL, "IPv6 addresses aren\xe2\x80\x99t supported yet. Use the host\xe2\x80\x99s IPv4 address, such as 192.168.1.20." );
		return;
	}
	if ( firstColon ) {
		const char *ps = firstColon + 1;
		size_t i, pl = strlen( ps );
		int digits = 1;
		for ( i = 0; i < pl; i++ )
			if ( !isdigit( (unsigned char)ps[i] ) )
				digits = 0;
		if ( !pl || pl > 5 || !digits || atoi( ps ) < 1 || atoi( ps ) > 65535 ) {
			lc_snprintf( msg, sizeof( msg ), "\xe2\x80\x9c%s\xe2\x80\x9d isn\xe2\x80\x99t a port. Leave it out for the co-op port (%d), or give a number from 1 to 65535.", ps, LC_COOP_PORT );
			set_result( out, "bad", NULL, msg );
			return;
		}
		port = atoi( ps );
		*firstColon = 0;
		lc_strlcpy( host, a, sizeof( host ) );
	}
	if ( !host[0] ) {
		set_result( out, "bad", NULL, "Type the host\xe2\x80\x99s address before the port." );
		return;
	}
	memset( &hints, 0, sizeof( hints ) );
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	if ( getaddrinfo( host, NULL, &hints, &res ) != 0 || !res ) {
		lc_snprintf( msg, sizeof( msg ), "\xe2\x80\x9c%s\xe2\x80\x9d couldn\xe2\x80\x99t be found. Check the address.", host );
		set_result( out, "bad", NULL, msg );
		return;
	}
	memcpy( &to, res->ai_addr, sizeof( to ) );
	freeaddrinfo( res );
	to.sin_port = htons( (unsigned short)port );
	{
		char ipb[32];
		inet_ntop( AF_INET, &to.sin_addr, ipb, sizeof( ipb ) );
		lc_snprintf( shown, sizeof( shown ), "%s:%d", ipb, port );
	}
	fd = socket( AF_INET, SOCK_DGRAM, 0 );
	if ( fd == LC_BADSOCK ) {
		set_result( out, "bad", shown, "A network socket couldn\xe2\x80\x99t be opened." );
		return;
	}
	lc_random_bytes( &rnd, sizeof( rnd ) );
	lc_snprintf( challenge, sizeof( challenge ), "%08x", rnd );
	ql = lc_snprintf( q, sizeof( q ), COOP_OOB COOP_QUERY " %s", challenge );

	deadline = lc_now( ) + timeout;
	for ( ;; ) {
		double left = deadline - lc_now( );
		struct sockaddr_in from;
		socklen_t fl = sizeof( from );
		char buf[1500];
		int n, wait;
		static const char prefix[] = COOP_OOB COOP_RESPONSE "\n";

		if ( left <= 0 )
			break;
		if ( sends < 3 && left <= timeout * ( 3 - sends ) / 3 + 0.001 ) {
			if ( sendto( fd, q, ql, 0, (struct sockaddr *)&to, sizeof( to ) ) < 0 )
				sendErr = lc_sock_errno( );
			sends++;
		}
		wait = (int)( ( left < timeout / 3 ? left : timeout / 3 ) * 1000 ) + 1;
		if ( lc_sock_wait_readable( fd, wait ) <= 0 )
			continue;
		n = (int)recvfrom( fd, buf, sizeof( buf ) - 1, 0, (struct sockaddr *)&from, &fl );
		if ( n <= 0 || from.sin_addr.s_addr != to.sin_addr.s_addr || from.sin_port != to.sin_port )
			continue;
		buf[n] = 0;
		if ( (size_t)n < sizeof( prefix ) - 1 || memcmp( buf, prefix, sizeof( prefix ) - 1 ) )
			continue;
		{
			const char *info = buf + sizeof( prefix ) - 1;
			char chal[64], version[64], protocol[16];
			if ( *info != '\\' )
				continue;
			lc_coop_info_value( info, "challenge", chal, sizeof( chal ) );
			if ( has_key( info, "challenge" ) && strcmp( chal, challenge ) )
				continue;
			lc_sock_close( fd );
			lc_strlcpy( out->info, info, sizeof( out->info ) );
			lc_coop_info_value( info, "version", version, sizeof( version ) );
			lc_coop_info_value( info, "coopProtocol", protocol, sizeof( protocol ) );
			if ( !version[0] )
				lc_strlcpy( version, "?", sizeof( version ) );
			if ( !protocol[0] )
				lc_strlcpy( protocol, "?", sizeof( protocol ) );
			if ( atoi( protocol ) != LC_COOP_PROTOCOL || strcmp( version, EF_APP_VERSION ) ) {
				lc_snprintf( msg, sizeof( msg ), "The host runs Elite Force %s (co-op version %s), and this %s runs %s (co-op version %d). Both need the same version to play together.",
					version, protocol, LC_PLATFORM_NOUN, EF_APP_VERSION, LC_COOP_PROTOCOL );
				set_result( out, "mismatch", shown, msg );
			} else
				set_result( out, "ok", shown, NULL );
			return;
		}
	}
	lc_sock_close( fd );
	if ( sendErr ) {
		char reason[64];
#ifdef _WIN32
		lc_snprintf( reason, sizeof( reason ), "error %d", sendErr );
		lc_snprintf( msg, sizeof( msg ), "Windows couldn\xe2\x80\x99t send to it (%s). Check that the firewall isn\xe2\x80\x99t blocking Elite Force.", reason );
#else
		lc_strlcpy( reason, strerror( sendErr ), sizeof( reason ) );
		lc_snprintf( msg, sizeof( msg ), "This computer couldn\xe2\x80\x99t send to it (%s). Check that a firewall isn\xe2\x80\x99t blocking Elite Force.", reason );
#endif
		set_result( out, "error", shown, msg );
		return;
	}
	set_result( out, "noanswer", shown, NULL );
}

/* ---- the game's own paks (protocol 7) -------------------------------------------------------------- */

/* = qcommon/files.c pak_checksums (ELITEFORCE): pak0.pk3, the 1.1 and 1.2 patches' pak1/pak2.pk3, the
   Expansion Pack's pak3.pk3 */
static const unsigned coop_pak_checksums[4] = { 3376297517u, 596947475u, 3960871590u, 1592359207u };

int lc_coop_core_pak_index( unsigned checksum )
{
	int i;
	for ( i = 0; i < 4; i++ )
		if ( checksum == coop_pak_checksums[i] )
			return i;
	return -1;
}

typedef struct { const char *dir; unsigned found[4]; } pakScan;

static int PakScanCB( const char *name, int isDir, void *user )
{
	pakScan *c = (pakScan *)user;
	size_t n = strlen( name );
	char path[LC_MAX_PATH];
	unsigned sum;
	int i;

	if ( isDir || name[0] == '.' || n < 5 || lc_stricmp( name + n - 4, ".pk3" ) )
		return 0;
	lc_path_join( path, sizeof( path ), c->dir, name );
	if ( !lc_pak_checksum( path, &sum ) )
		return 0;
	i = lc_coop_core_pak_index( sum );
	if ( i >= 0 )
		c->found[i] = sum;
	return 0;
}

void lc_coop_local_paks( const char *dataFolder, char *out, size_t size )
{
	pakScan c;
	const char *home = lc_homebase( );
	int i;

	memset( &c, 0, sizeof( c ) );
	c.dir = home;
	lc_list_dir( home, PakScanCB, &c );
	if ( dataFolder && *dataFolder && strcmp( dataFolder, home ) ) {
		c.dir = dataFolder;
		lc_list_dir( dataFolder, PakScanCB, &c );
	}
	if ( size )
		out[0] = 0;
	for ( i = 0; i < 4; i++ )
		if ( c.found[i] ) {
			char one[32];
			lc_snprintf( one, sizeof( one ), "%spak%d.pk3:%u", out[0] ? " " : "", i, c.found[i] );
			lc_strlcat( out, one, size );
		}
}

/* a FS_CoopPaks list as the checksum of each of pak0-pak3, 0 for one that isn't there */
static void ParsePaks( const char *list, unsigned sums[4] )
{
	memset( sums, 0, 4 * sizeof( sums[0] ) );
	while ( list && *list ) {
		while ( *list == ' ' )
			list++;
		if ( !lc_strnicmp( list, "pak", 3 ) && list[3] >= '0' && list[3] <= '3' && !lc_strnicmp( list + 4, ".pk3:", 5 ) )
			sums[list[3] - '0'] = (unsigned)strtoul( list + 9, NULL, 10 );
		while ( *list && *list != ' ' )
			list++;
	}
}

static void PakListAdd( char *list, size_t size, int pak )
{
	char one[24];
	lc_snprintf( one, sizeof( one ), "%spak%d.pk3", list[0] ? ", " : "", pak );
	lc_strlcat( list, one, size );
}

int lc_coop_compare_paks( const char *hostPaks, const char *guestPaks, int forHost, char *text, size_t size )
{
	unsigned host[4], guest[4];
	char hostOnly[64] = "", guestOnly[64] = "", differ[64] = "", tmp[256];
	int i;

	ParsePaks( hostPaks, host );
	ParsePaks( guestPaks, guest );
	if ( size )
		text[0] = 0;
	for ( i = 0; i < 3; i++ ) {
		if ( host[i] == guest[i] )
			continue;
		if ( !guest[i] )
			PakListAdd( hostOnly, sizeof( hostOnly ), i );
		else if ( !host[i] )
			PakListAdd( guestOnly, sizeof( guestOnly ), i );
		else
			PakListAdd( differ, sizeof( differ ), i );
	}
	if ( hostOnly[0] || guestOnly[0] || differ[0] ) {
		if ( hostOnly[0] ) {
			lc_snprintf( tmp, sizeof( tmp ), forHost ? "Player 2 is missing %s (you have it). " : "You're missing %s (the host has it). ", hostOnly );
			lc_strlcat( text, tmp, size );
		}
		if ( guestOnly[0] ) {
			lc_snprintf( tmp, sizeof( tmp ), forHost ? "You're missing %s (player 2 has it). " : "The host is missing %s (you have it). ", guestOnly );
			lc_strlcat( text, tmp, size );
		}
		if ( differ[0] ) {
			lc_snprintf( tmp, sizeof( tmp ), forHost ? "Player 2's %s differs from yours. " : "Your %s differs from the host's. ", differ );
			lc_strlcat( text, tmp, size );
		}
		lc_strlcat( text, "Both need the game's original pak0.pk3, pak1.pk3 and pak2.pk3 (other packs don't matter).", size );
		return LC_COOP_PAKS_DIFFER;
	}
	if ( host[3] != guest[3] ) {
		if ( !guest[3] )
			lc_strlcat( text, forHost ? "Player 2 doesn't have the Expansion Pack (pak3.pk3); you do." :
				"You don't have the Expansion Pack (pak3.pk3); the host does.", size );
		else if ( !host[3] )
			lc_strlcat( text, forHost ? "Player 2 has the Expansion Pack (pak3.pk3); you don't." :
				"The host doesn't have the Expansion Pack (pak3.pk3); you do.", size );
		else
			lc_strlcat( text, forHost ? "Player 2's Expansion Pack (pak3.pk3) differs from yours." :
				"Your Expansion Pack (pak3.pk3) differs from the host's.", size );
		lc_strlcat( text, " You can still play together: the host runs the game, so the difference is mostly "
			"cosmetic on player 2's screen (some texts, the tricorder, a few weapon and NPC details).", size );
		return LC_COOP_PAKS_EXPANSION;
	}
	return LC_COOP_PAKS_SAME;
}

/* straight apostrophes as the typographic one the sheets use */
static void CurlyApostrophes( const char *in, char *out, size_t size )
{
	size_t o = 0;
	for ( ; *in && o + 4 < size; in++ ) {
		if ( *in == '\'' ) {
			memcpy( out + o, "\xe2\x80\x99", 3 );
			o += 3;
		} else
			out[o++] = *in;
	}
	if ( size )
		out[o] = 0;
}

void lc_coop_check_paks( lc_coop_query *q, const char *dataFolder )
{
	char hostPaks[256], ours[256], text[1024];
	int match;

	q->warning[0] = 0;
	if ( strcmp( q->status, "ok" ) )
		return;
	lc_coop_info_value( q->info, "paks", hostPaks, sizeof( hostPaks ) );
	lc_coop_local_paks( dataFolder, ours, sizeof( ours ) );
	match = lc_coop_compare_paks( hostPaks, ours, 0, text, sizeof( text ) );
	lc_log( "Co-op: host's paks \"%s\", this %s's \"%s\": %s", hostPaks, LC_PLATFORM_NOUN_LC, ours,
		match == LC_COOP_PAKS_SAME ? "the same" : text );
	if ( match == LC_COOP_PAKS_DIFFER ) {
		lc_strlcpy( q->status, "paks", sizeof( q->status ) );
		CurlyApostrophes( text, q->error, sizeof( q->error ) );
	} else if ( match == LC_COOP_PAKS_EXPANSION )
		CurlyApostrophes( text, q->warning, sizeof( q->warning ) );
}

/* ---- player 2's character ------------------------------------------------------------------------- */

int lc_coop_character_count( void )
{
	return COOP_NUM_CHARACTERS;
}

int lc_coop_character( int i, const char **id, const char **name, const char **group )
{
	if ( i < 0 || i >= COOP_NUM_CHARACTERS )
		return 0;
	if ( id )
		*id = coop_characters[i].id;
	if ( name )
		*name = coop_characters[i].name;
	if ( group )
		*group = coop_characters[i].group;
	return 1;
}

int lc_coop_character_valid( const char *id )
{
	int i;
	if ( !id || !*id )
		return 0;
	for ( i = 0; i < COOP_NUM_CHARACTERS; i++ )
		if ( !strcmp( id, coop_characters[i].id ) )
			return 1;
	return 0;
}

/* ---- addresses and the invite --------------------------------------------------------------------- */

int lc_coop_lan_addresses( char addrs[][16], char ifnames[][64], int max )
{
	return lc_lan_addresses( addrs, ifnames, max );
}

void lc_coop_invite_link( const char *address, char *out, size_t size )
{
	if ( address && *address ) {
		lc_snprintf( out, size, "stvef://coop/%s", address );
		return;
	}
	{
		char addrs[8][16];
		if ( lc_lan_addresses( addrs, NULL, 8 ) > 0 )
			lc_snprintf( out, size, "stvef://coop/%s:%d", addrs[0], LC_COOP_PORT );
		else if ( size )
			out[0] = 0;
	}
}

/* ---- the sheet's choice ---------------------------------------------------------------------------- */

static struct {
	int choice, background;
	char address[64], sex[16], look[COOP_CHARACTER_MAX], name[40], password[72];
} g_choice;

void lc_coop_choose_none( void )
{
	memset( &g_choice, 0, sizeof( g_choice ) );
}

void lc_coop_choose_host( int background, const char *name, const char *password )
{
	memset( &g_choice, 0, sizeof( g_choice ) );
	g_choice.choice = 1;
	g_choice.background = background != 0;
	safe_name( name, g_choice.name, sizeof( g_choice.name ) );
	lc_strlcpy( g_choice.password, password ? password : "", sizeof( g_choice.password ) );
}

void lc_coop_choose_join( const char *address, const char *sex, const char *look, const char *name, const char *password )
{
	memset( &g_choice, 0, sizeof( g_choice ) );
	g_choice.choice = 2;
	lc_strlcpy( g_choice.address, address ? address : "", sizeof( g_choice.address ) );
	lc_strlcpy( g_choice.sex, sex && !lc_stricmp( sex, "female" ) ? "female" : "male", sizeof( g_choice.sex ) );
	if ( lc_coop_character_valid( look ) )
		lc_strlcpy( g_choice.look, look, sizeof( g_choice.look ) );
	safe_name( name, g_choice.name, sizeof( g_choice.name ) );
	lc_strlcpy( g_choice.password, password ? password : "", sizeof( g_choice.password ) );
}

int lc_coop_choice( void )
{
	return g_choice.choice;
}

void lc_coop_launch_fields( struct lc_launch_coop_fields *out )
{
	const char *p;
	int ok = 1;

	memset( out, 0, sizeof( *out ) );
	if ( g_choice.choice == 1 ) {
		out->coopHost = 1;
		out->coopBackground = g_choice.background;
	} else if ( g_choice.choice == 2 ) {
		/* only 0-9 . : may go on the command line */
		for ( p = g_choice.address; *p; p++ )
			if ( !isdigit( (unsigned char)*p ) && *p != '.' && *p != ':' )
				ok = 0;
		if ( ok && g_choice.address[0] ) {
			out->coopConnect = g_choice.address;
			out->coopSex = g_choice.sex;
			if ( g_choice.look[0] )
				out->coopLook = g_choice.look;
		}
	}
	if ( g_choice.choice && g_choice.name[0] )
		out->coopName = g_choice.name;
}

const char *lc_coop_password( void )
{
	return g_choice.password;
}
