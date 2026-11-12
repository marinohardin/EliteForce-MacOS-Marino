/* lc_server.c: settings, modifier rules, masters, cfg text, the zip reader and inflate, the map
   catalog on a synthetic game folder (servers.md 2, 3, 5, 6) */
#include "lc_server.h"
#include "lc_net.h"
#include "lc_test.h"
#include "lc_test_deflate.h"
#include <stdlib.h>
#include <string.h>

static char g_work[LC_MAX_PATH];

/* ---- a minimal zip writer: stored and deflated entries, central directory, no crc ---- */
typedef struct { const char *name; const unsigned char *data; size_t csize, usize; int method; } zent;

static void put16( unsigned char *p, unsigned v ) { p[0] = (unsigned char)v; p[1] = (unsigned char)( v >> 8 ); }
static void put32( unsigned char *p, unsigned v ) { put16( p, v & 0xffff ); put16( p + 2, v >> 16 ); }

static int write_zip( const char *path, const zent *e, int n )
{
	FILE *f = lc_fopen( path, "wb" );
	unsigned offsets[16];
	unsigned char hdr[46];
	unsigned pos = 0, cdStart, cdSize = 0;
	int i;

	if ( !f )
		return 0;
	for ( i = 0; i < n; i++ ) {
		size_t nl = strlen( e[i].name );
		offsets[i] = pos;
		memset( hdr, 0, 30 );
		put32( hdr, 0x04034b50u );
		put16( hdr + 8, (unsigned)e[i].method );
		put32( hdr + 18, (unsigned)e[i].csize );
		put32( hdr + 22, (unsigned)e[i].usize );
		put16( hdr + 26, (unsigned)nl );
		put16( hdr + 28, 4 );		/* an extra field, to check it is skipped */
		fwrite( hdr, 1, 30, f );
		fwrite( e[i].name, 1, nl, f );
		fwrite( "\0\0\0\0", 1, 4, f );
		fwrite( e[i].data, 1, e[i].csize, f );
		pos += 30 + (unsigned)nl + 4 + (unsigned)e[i].csize;
	}
	cdStart = pos;
	for ( i = 0; i < n; i++ ) {
		size_t nl = strlen( e[i].name );
		memset( hdr, 0, 46 );
		put32( hdr, 0x02014b50u );
		put16( hdr + 10, (unsigned)e[i].method );
		put32( hdr + 20, (unsigned)e[i].csize );
		put32( hdr + 24, (unsigned)e[i].usize );
		put16( hdr + 28, (unsigned)nl );
		put32( hdr + 42, offsets[i] );
		fwrite( hdr, 1, 46, f );
		fwrite( e[i].name, 1, nl, f );
		cdSize += 46 + (unsigned)nl;
	}
	memset( hdr, 0, 22 );
	put32( hdr, 0x06054b50u );
	put16( hdr + 8, (unsigned)n );
	put16( hdr + 10, (unsigned)n );
	put32( hdr + 12, cdSize );
	put32( hdr + 16, cdStart );
	fwrite( hdr, 1, 22, f );
	fclose( f );
	return 1;
}

#define STORED( name, text ) { name, (const unsigned char *)( text ), strlen( text ), strlen( text ), 0 }

static void write_text( const char *path, const char *text )
{
	FILE *f = lc_fopen( path, "wb" );
	if ( f ) {
		fputs( text, f );
		fclose( f );
	}
}

static void counter_cb( const char *map, const char *longname, const char *type, void *u )
{
	int *n = (int *)u;
	(void)map; (void)longname; (void)type;
	( *n )++;
}

typedef struct { char map[64], longname[128], type[64]; int n; } arena_seen;
static void first_cb( const char *map, const char *longname, const char *type, void *u )
{
	arena_seen *a = (arena_seen *)u;
	if ( !a->n++ ) {
		lc_strlcpy( a->map, map, sizeof( a->map ) );
		lc_strlcpy( a->longname, longname, sizeof( a->longname ) );
		lc_strlcpy( a->type, type, sizeof( a->type ) );
	}
}

static char logs[4][256];
static int nlogs;
static void log_cb( const char *line, void *u )
{
	(void)u;
	if ( nlogs < 4 )
		lc_strlcpy( logs[nlogs++], line, 256 );
}

static int want_all( const char *l, void *u ) { (void)l; (void)u; return 1; }

static void test_settings( void )
{
	lc_server_settings s, t;
	char buf[64];
	lc_store *st;
	char path[LC_MAX_PATH];

	/* 2.2 CleanCvarText */
	lc_clean_cvar_text( "  He\"llo; wo\\rld%!\x01 caf\xc3\xa9  ", 48, buf, sizeof( buf ) );
	CHECK_STR( buf, "Hello world! caf" );
	lc_clean_cvar_text( "abcdefghij", 4, buf, sizeof( buf ) );
	CHECK_STR( buf, "abcd" );
	lc_clean_cvar_text( "ab  ", 4, buf, sizeof( buf ) );
	CHECK_STR( buf, "ab" );

	/* 2.1 defaults */
	lc_server_settings_defaults( &s, "Marino Hardin" );
	CHECK_STR( s.hostname, "Marino's Server" );
	CHECK( s.gametype == 0 && s.fraglimit == 20 && s.capturelimit == 8 && s.timelimit == 15 );
	CHECK( s.maxPlayers == 12 && s.bots == 0 && s.botSkill == 3 && s.port == 27960 && s.tickRate == 20 );
	CHECK( s.openPort && s.allowDownload && s.pure && !s.internet && !s.friendlyFire );
	CHECK( s.cmodGame && s.unlagged == 1 && s.smoothClients && s.unlaggedPredict );
	CHECK( s.rotationCount == 6 );
	CHECK_STR( s.rotation[0], "hm_voy1" );
	CHECK_STR( s.rotation[5], "hm_cam" );
	lc_server_settings_defaults( &t, NULL );
	CHECK_STR( t.hostname, "Elite Force Server" );
	lc_server_settings_defaults( &t, "  ;;\"  Foo" );
	CHECK_STR( t.hostname, "Elite Force Server" );

	/* 2.3 validation */
	s.gametype = 2; s.botSkill = 9; s.maxPlayers = 1; s.bots = 99; s.port = 80; s.tickRate = 30; s.unlagged = 7;
	s.hostname[0] = 0;
	lc_server_settings_validate( &s, "Zed Y" );
	CHECK( s.gametype == 0 && s.botSkill == 5 && s.maxPlayers == 2 && s.bots == 31 && s.port == 27960 );
	CHECK( s.tickRate == 20 && s.unlagged == 2 );
	CHECK_STR( s.hostname, "Zed's Server" );
	s.tickRate = 40; s.port = 65536;
	lc_server_settings_validate( &s, "Zed Y" );
	CHECK( s.tickRate == 40 && s.port == 27960 );

	/* 2.4 modifier rules */
	lc_server_settings_defaults( &s, NULL );
	s.mods[LC_MOD_ASSIMILATION] = s.mods[LC_MOD_SPECIALTIES] = s.mods[LC_MOD_ELIMINATION] = 1;
	s.gametype = LC_GT_FFA;
	lc_server_settings_apply_modifier_rules( &s, -1 );
	CHECK( !s.mods[LC_MOD_ASSIMILATION] );				/* only Team */
	CHECK( s.mods[LC_MOD_SPECIALTIES] && s.mods[LC_MOD_ELIMINATION] );
	s.gametype = LC_GT_TEAM;
	s.mods[LC_MOD_ASSIMILATION] = 1;
	s.mods[LC_MOD_ACTIONHERO] = 1;
	lc_server_settings_apply_modifier_rules( &s, -1 );
	CHECK( s.mods[LC_MOD_ASSIMILATION] && !s.mods[LC_MOD_ACTIONHERO] );	/* assimilation (first) wins */
	CHECK( !s.mods[LC_MOD_ELIMINATION] && s.mods[LC_MOD_SPECIALTIES] );
	s.gametype = LC_GT_CTF;
	lc_server_settings_apply_modifier_rules( &s, -1 );
	CHECK( !s.mods[LC_MOD_ASSIMILATION] && !s.mods[LC_MOD_ELIMINATION] && !s.mods[LC_MOD_ACTIONHERO] );
	/* the favourite wins */
	memset( s.mods, 0, sizeof( s.mods ) );
	s.gametype = LC_GT_FFA;
	s.mods[LC_MOD_DISINTEGRATION] = 1;
	s.mods[LC_MOD_ACTIONHERO] = 1;
	s.mods[LC_MOD_SPECIALTIES] = 1;
	lc_server_settings_apply_modifier_rules( &s, LC_MOD_ACTIONHERO );
	CHECK( s.mods[LC_MOD_ACTIONHERO] && !s.mods[LC_MOD_DISINTEGRATION] && !s.mods[LC_MOD_SPECIALTIES] );
	s.mods[LC_MOD_DISINTEGRATION] = 1;
	s.mods[LC_MOD_ACTIONHERO] = 1;
	lc_server_settings_apply_modifier_rules( &s, -1 );		/* no favourite: the fixed order, disintegration first */
	CHECK( s.mods[LC_MOD_DISINTEGRATION] && !s.mods[LC_MOD_ACTIONHERO] );
	/* 4.3 enabling */
	s.gametype = LC_GT_TEAM;
	CHECK( lc_server_settings_modifier_allowed( &s, LC_MOD_ASSIMILATION ) );
	CHECK( lc_server_settings_modifier_allowed( &s, LC_MOD_ELIMINATION ) );
	CHECK( !lc_server_settings_modifier_allowed( &s, LC_MOD_ACTIONHERO ) );
	s.gametype = LC_GT_CTF;
	CHECK( !lc_server_settings_modifier_allowed( &s, LC_MOD_ELIMINATION ) );
	CHECK( !lc_server_settings_modifier_allowed( &s, LC_MOD_ASSIMILATION ) );
	CHECK( lc_server_settings_modifier_allowed( &s, LC_MOD_SPECIALTIES ) );

	/* 2.5 names */
	CHECK_STR( lc_gametype_name( 0 ), "Free For All" );
	CHECK_STR( lc_gametype_name( 1 ), "Tournament" );
	CHECK_STR( lc_gametype_name( 3 ), "Team Holomatch" );
	CHECK_STR( lc_gametype_name( 4 ), "Capture the Flag" );
	CHECK_STR( lc_gametype_name( 2 ), "Free For All" );
	CHECK_STR( lc_browser_gametype_name( 2, buf, sizeof( buf ) ), "Single Player" );
	CHECK_STR( lc_browser_gametype_name( 7, buf, sizeof( buf ) ), "Type 7" );
	CHECK_STR( lc_browser_gametype_name( 4, buf, sizeof( buf ) ), "Capture the Flag" );
	CHECK_STR( lc_skill_name( 0 ), "Cadet" );
	CHECK_STR( lc_skill_name( 3 ), "Lieutenant" );
	CHECK_STR( lc_skill_name( 9 ), "Captain" );
	CHECK_STR( lc_modifier_name( LC_MOD_ACTIONHERO ), "Action Hero" );

	/* store round trip */
	lc_mkdir_p( g_work );
	lc_path_join( path, sizeof( path ), g_work, "settings.cfg" );
	lc_remove( path );
	st = lc_store_load( path );
	lc_server_settings_defaults( &s, "A B" );
	s.gametype = LC_GT_TEAM;
	s.mods[LC_MOD_ASSIMILATION] = 1;
	s.internet = 1; s.port = 27961; s.tickRate = 40; s.bots = 4; s.rotationCount = 2;
	lc_strlcpy( s.rotation[0], "hm_a", 64 );
	lc_strlcpy( s.rotation[1], "hm_b", 64 );
	lc_strlcpy( s.hostname, "My Server", sizeof( s.hostname ) );
	lc_strlcpy( s.password, "pw", sizeof( s.password ) );
	lc_server_settings_save( &s, st );
	lc_store_free( st );
	st = lc_store_load( path );
	lc_server_settings_load( &t, st, "A B" );
	CHECK( t.gametype == LC_GT_TEAM && t.mods[LC_MOD_ASSIMILATION] && t.internet && t.port == 27961 && t.tickRate == 40 && t.bots == 4 );
	CHECK( t.rotationCount == 2 );
	CHECK_STR( t.rotation[1], "hm_b" );
	CHECK_STR( t.hostname, "My Server" );
	CHECK_STR( t.password, "pw" );
	lc_store_free( st );
	/* an absent store is all defaults; an empty stored rotation stays empty */
	lc_remove( path );
	st = lc_store_load( path );
	lc_server_settings_load( &t, st, "Q" );
	CHECK( t.rotationCount == 6 && t.port == 27960 );
	s.rotationCount = 0;
	lc_server_settings_save( &s, st );
	lc_store_free( st );
	st = lc_store_load( path );
	lc_server_settings_load( &t, st, "Q" );
	CHECK( t.rotationCount == 0 );
	lc_store_free( st );
}

static void test_masters( void )
{
	char m[65], out[LC_MAX_MASTERS][65];
	const char *in[] = { "  Master.EXAMPLE.org ", "x", "master.example.org", "", "bad host$!", "[::1]:27953", "a:b-c_d" };
	char path[LC_MAX_PATH], list[3][65];
	lc_store *st;
	int n, i;
	char *text;

	lc_clean_master( "  Master.Stef1.Daggolin.DE\t", m );
	CHECK_STR( m, "master.stef1.daggolin.de" );
	lc_clean_master( "a b$c", m );
	CHECK_STR( m, "abc" );
	lc_clean_master( "0123456789012345678901234567890123456789012345678901234567890123456789", m );
	CHECK( strlen( m ) == 64 );
	n = lc_clean_masters( in, 7, out );
	CHECK( n == 5 );
	CHECK_STR( out[0], "master.example.org" );
	CHECK_STR( out[1], "x" );
	CHECK_STR( out[2], "badhost" );
	CHECK_STR( out[3], "[::1]:27953" );
	CHECK_STR( out[4], "a:b-c_d" );

	lc_path_join( path, sizeof( path ), g_work, "masters.cfg" );
	lc_remove( path );
	st = lc_store_load( path );
	n = lc_masters_load( st, out );
	CHECK( n == 5 );
	CHECK_STR( out[0], "master.stef1.daggolin.de" );
	CHECK_STR( out[4], "ipv6.tjps.eu" );
	/* saving the recommended list removes the key (follows future defaults) */
	lc_masters_save( st, (const char (*)[65])out, 5 );
	CHECK( !lc_store_has_list( st, "masters" ) );
	lc_strlcpy( list[0], "one.example", 65 );
	lc_strlcpy( list[1], "two.example", 65 );
	lc_masters_save( st, (const char (*)[65])list, 2 );
	CHECK( lc_store_has_list( st, "masters" ) );
	n = lc_masters_load( st, out );
	CHECK( n == 2 && !strcmp( out[1], "two.example" ) );
	lc_masters_save( st, (const char (*)[65])list, 0 );		/* the user removed them all: an empty list, not the defaults */
	CHECK( lc_store_has_list( st, "masters" ) );
	CHECK( lc_masters_load( st, out ) == 0 );
	lc_store_free( st );

	/* 5.2: sixteen lines, blanks past the end */
	lc_masters_cfg_text( (const char (*)[65])list, 2, 0, &text );
	CHECK( strstr( text, "set sv_master1 \"one.example\"\nset sv_master2 \"two.example\"\nset sv_master3 \"\"\n" ) != NULL );
	CHECK( strstr( text, "set sv_master16 \"\"\n" ) != NULL );
	{
		int lines = 0;
		const char *p = text;
		while ( ( p = strchr( p, '\n' ) ) != NULL ) { lines++; p++; }
		CHECK( lines == 16 );
	}
	free( text );
	lc_masters_cfg_text( (const char (*)[65])list, 2, 1, &text );
	CHECK( strstr( text, "one.example" ) == NULL );
	free( text );
	for ( i = 0; i < 5; i++ )
		CHECK( lc_default_masters[i] != NULL );
	CHECK_STR( lc_original_masters[0], "master.stef1.ravensoft.com" );
	CHECK_STR( lc_original_masters[1], "efmaster.tjps.eu" );
}

static void test_zip_and_inflate( void )
{
	char path[LC_MAX_PATH];
	lc_zip_entry *ents;
	int n, i, found;
	unsigned char *b;
	size_t len;
	zent z[3];
	arena_seen seen;
	int count = 0;
	unsigned char out[8192];
	unsigned char corrupt[sizeof( BIG_RAW )];

	/* raw inflate against zlib-made streams: fixed and dynamic blocks */
	CHECK( lc_inflate_raw( SMALL_RAW, sizeof( SMALL_RAW ), out, sizeof( out ) ) == 59 );
	CHECK( memcmp( out, "{\nmap \"hm_test1\"", 16 ) == 0 );
	CHECK( lc_inflate_raw( BIG_RAW, sizeof( BIG_RAW ), out, sizeof( out ) ) == 4338 );
	CHECK( lc_inflate_raw( BIG_RAW, sizeof( BIG_RAW ), out, 100 ) < 0 );		/* too small an output */
	CHECK( lc_inflate_raw( BIG_RAW, 100, out, sizeof( out ) ) < 0 );			/* truncated input */
	memcpy( corrupt, BIG_RAW, sizeof( corrupt ) );
	for ( i = 20; i < 40; i++ )
		corrupt[i] ^= 0x5a;
	/* corrupt data must never crash; most corruptions are detected */
	(void)lc_inflate_raw( corrupt, sizeof( corrupt ), out, sizeof( out ) );

	z[0] = (zent)STORED( "scripts/arenas.txt", "{ map hm_x longname \"X\" type \"ctf\" }" );
	z[1] = (zent){ "scripts/small.arena", SMALL_RAW, sizeof( SMALL_RAW ), 59, 8 };
	z[2] = (zent){ "scripts/big.arena", BIG_RAW, sizeof( BIG_RAW ), 4338, 8 };
	lc_path_join( path, sizeof( path ), g_work, "test.pk3" );
	CHECK( write_zip( path, z, 3 ) );
	CHECK( lc_zip_list( path, want_all, NULL, &ents, &n ) );
	CHECK( n == 3 );
	found = 0;
	for ( i = 0; i < n; i++ ) {
		b = lc_zip_read( path, &ents[i], &len );
		CHECK( b != NULL );
		if ( !b )
			continue;
		if ( !strcmp( ents[i].name, "scripts/small.arena" ) ) {
			CHECK( len == 59 );
			memset( &seen, 0, sizeof( seen ) );
			b = (unsigned char *)realloc( b, len + 1 );
			b[len] = 0;
			lc_arena_parse( (const char *)b, first_cb, &seen );
			CHECK( seen.n == 1 );
			CHECK_STR( seen.map, "hm_test1" );
			CHECK_STR( seen.longname, "^1Test ^7One" );
			CHECK_STR( seen.type, "ffa team" );
			found++;
		} else if ( !strcmp( ents[i].name, "scripts/big.arena" ) ) {
			CHECK( len == 4338 );
			b = (unsigned char *)realloc( b, len + 1 );
			b[len] = 0;
			count = 0;
			lc_arena_parse( (const char *)b, counter_cb, &count );
			CHECK( count == 60 );
			found++;
		} else if ( !strcmp( ents[i].name, "scripts/arenas.txt" ) ) {
			CHECK( len == strlen( "{ map hm_x longname \"X\" type \"ctf\" }" ) );
			found++;
		}
		free( b );
	}
	CHECK( found == 3 );
	free( ents );
	/* a damaged central directory, a non-zip, a missing file: no entries, no crash */
	CHECK( !lc_zip_list( "/nonexistent/none.pk3", want_all, NULL, &ents, &n ) );
	lc_path_join( path, sizeof( path ), g_work, "notzip.pk3" );
	write_text( path, "this is not a zip file, just some text of a decent length, more than twenty-two bytes" );
	CHECK( !lc_zip_list( path, want_all, NULL, &ents, &n ) );
	/* the "want" filter */
	lc_path_join( path, sizeof( path ), g_work, "test.pk3" );
}

/* 3.4: the tokeniser's corners */
static void test_arena_parse( void )
{
	arena_seen a;
	int count = 0;

	memset( &a, 0, sizeof( a ) );
	lc_arena_parse( "// comment { map nope }\n/* block\n{ map nope2 } */ {\n\"map\" \"hm_q\"\nLongName \"Quoted Name\"\n type\tffa\n}\n"
		"{ map \"\" }\n{ longname only }\n", first_cb, &a );
	CHECK( a.n == 1 );
	CHECK_STR( a.map, "hm_q" );
	CHECK_STR( a.longname, "Quoted Name" );
	CHECK_STR( a.type, "ffa" );
	lc_arena_parse( "{ map a } { map b", counter_cb, &count );		/* unterminated block: its pairs are lost... */
	CHECK( count >= 1 );
	count = 0;
	lc_arena_parse( "{ map \"unterminated string", counter_cb, &count );
	CHECK( count == 0 || count == 1 );
}

static void test_catalog( void )
{
	char data[LC_MAX_PATH], home[LC_MAX_PATH], p[LC_MAX_PATH], sub[LC_MAX_PATH];
	zent z[8];
	const lc_map_catalog *c, *c2;
	const lc_map_info *m;
	char buf[64];
	char *cfg;
	char err[128];
	lc_server_settings s;
	char masters[2][65];
	int i;
	unsigned char *shot;
	size_t len;
	int isTga;

	lc_path_join( data, sizeof( data ), g_work, "game" );
	lc_path_join( home, sizeof( home ), g_work, "homebase" );
	lc_mkdir_p( data );
	lc_mkdir_p( home );

	/* pak0 (retail): a Holomatch map with arena info, a single-player map without */
	z[0] = (zent)STORED( "maps/hm_retail1.bsp", "BSP" );
	z[1] = (zent)STORED( "maps/sp_only.bsp", "BSP" );
	z[2] = (zent)STORED( "scripts/arenas.txt", "{\nmap \"hm_retail1\"\nlongname \"^2Retail One\"\ntype \"ffa tourney\"\n}\n" );
	z[3] = (zent)STORED( "levelshots/hm_retail1.tga", "TGAdata" );
	z[4] = (zent)STORED( "levelshots/hm_retail1.jpg", "JPGdata" );
	z[5] = (zent)STORED( "maps/sub/ignored.bsp", "BSP" );		/* three path components: not a map */
	lc_path_join( p, sizeof( p ), data, "pak0.pk3" );
	CHECK( write_zip( p, z, 6 ) );
	/* pak3 (Expansion Pack) */
	z[0] = (zent)STORED( "maps/hm_voy3.bsp", "BSP" );
	z[1] = (zent)STORED( "scripts/exp.arena", "{ map hm_voy3 longname \"Voyager Three\" type \"ctf team\" }" );
	lc_path_join( p, sizeof( p ), data, "PAK3.PK3" );
	CHECK( write_zip( p, z, 2 ) );
	/* an add-on pk3: arena text deflated, a map without arena info, upper-case names */
	z[0] = (zent)STORED( "maps/hm_test1.bsp", "BSP" );
	z[1] = (zent)STORED( "maps/HM_NoArena.bsp", "BSP" );
	z[2] = (zent){ "scripts/small.arena", SMALL_RAW, sizeof( SMALL_RAW ), 59, 8 };
	z[3] = (zent)STORED( "levelshots/hm_test1.jpg", "JPEG" );
	lc_path_join( p, sizeof( p ), data, "addon.pk3" );
	CHECK( write_zip( p, z, 4 ) );
	/* loose files */
	lc_path_join( sub, sizeof( sub ), data, "maps" );
	lc_mkdir_p( sub );
	lc_path_join( p, sizeof( p ), sub, "hm_loose.bsp" );
	write_text( p, "BSP" );
	lc_path_join( sub, sizeof( sub ), data, "scripts" );
	lc_mkdir_p( sub );
	lc_path_join( p, sizeof( p ), sub, "loose.arena" );
	write_text( p, "{ map hm_loose longname \"The Loose One\" type ffa }" );
	lc_path_join( sub, sizeof( sub ), data, "levelshots" );
	lc_mkdir_p( sub );
	lc_path_join( p, sizeof( p ), sub, "hm_loose.jpg" );
	write_text( p, "LOOSEJPG" );
	/* the home folder overrides: the same map name in another pk3 */
	z[0] = (zent)STORED( "maps/hm_test1.bsp", "BSP" );
	z[1] = (zent)STORED( "scripts/over.arena", "{ map hm_test1 longname \"Overridden\" type team }" );
	lc_path_join( p, sizeof( p ), home, "zz_home.pk3" );
	CHECK( write_zip( p, z, 2 ) );

	c = lc_map_catalog_get( data, home );
	CHECK( lc_map_catalog_count( c ) == 5 );
	/* sorted by display name, case-insensitively; no longname: the name itself */
	CHECK_STR( lc_map_display_name( lc_map_catalog_at( c, 0 ) ), "HM_NoArena" );
	CHECK_STR( lc_map_display_name( lc_map_catalog_at( c, 1 ) ), "Overridden" );
	CHECK_STR( lc_map_display_name( lc_map_catalog_at( c, 2 ) ), "Retail One" );
	CHECK_STR( lc_map_display_name( lc_map_catalog_at( c, 3 ) ), "The Loose One" );
	CHECK_STR( lc_map_display_name( lc_map_catalog_at( c, 4 ) ), "Voyager Three" );
	CHECK( lc_map_find( c, "sp_only" ) == NULL );
	CHECK( lc_map_find( c, "HM_SUB" ) == NULL );
	m = lc_map_find( c, "HM_RETAIL1" );
	CHECK( m && m->retail && !m->expansion );
	CHECK_STR( m->longName, "Retail One" );						/* colour codes stripped */
	CHECK_STR( m->types, "ffa tourney" );
	CHECK_STR( m->pk3, "pak0.pk3" );
	CHECK( lc_map_supports_gametype( m, LC_GT_FFA ) && lc_map_supports_gametype( m, LC_GT_TOURNAMENT ) );
	CHECK( !lc_map_supports_gametype( m, LC_GT_TEAM ) && !lc_map_supports_gametype( m, LC_GT_CTF ) );
	lc_map_types_description( m, buf, sizeof( buf ) );
	CHECK_STR( buf, "FFA, Tourney" );
	shot = lc_map_levelshot_bytes( m, &len, &isTga );
	CHECK( shot && len == 7 && !isTga && !memcmp( shot, "JPGdata", 7 ) );	/* the .jpg beats the .tga */
	free( shot );
	m = lc_map_find( c, "hm_voy3" );
	CHECK( m && m->expansion && m->retail );
	CHECK_STR( m->pk3, "PAK3.PK3" );
	lc_map_types_description( m, buf, sizeof( buf ) );
	CHECK_STR( buf, "Team, CTF" );
	m = lc_map_find( c, "hm_test1" );
	CHECK( m && !m->retail );
	CHECK_STR( m->longName, "Overridden" );			/* the later directory wins */
	CHECK_STR( m->pk3, "zz_home.pk3" );
	CHECK_STR( m->types, "team" );
	shot = lc_map_levelshot_bytes( m, &len, &isTga );
	CHECK( shot && len == 4 && !memcmp( shot, "JPEG", 4 ) );
	free( shot );
	m = lc_map_find( c, "hm_noarena" );
	CHECK( m && m->types[0] == 0 && m->longName[0] == 0 );
	CHECK( lc_map_supports_gametype( m, LC_GT_CTF ) );
	lc_map_types_description( m, buf, sizeof( buf ) );
	CHECK_STR( buf, "" );
	CHECK_STR( m->name, "HM_NoArena" );
	m = lc_map_find( c, "hm_loose" );
	CHECK( m && m->pk3[0] == 0 && !m->retail );
	shot = lc_map_levelshot_bytes( m, &len, &isTga );
	CHECK( shot && len == 8 && !memcmp( shot, "LOOSEJPG", 8 ) );
	free( shot );
	/* cached by stamp: the same pointer until something changes */
	c2 = lc_map_catalog_get( data, home );
	CHECK( c2 == c );
	lc_path_join( p, sizeof( p ), data, "maps" );
	lc_path_join( sub, sizeof( sub ), p, "hm_newloose.bsp" );
	write_text( sub, "BSP" );
	c2 = lc_map_catalog_get( data, home );
	CHECK( c2 != c && lc_map_catalog_count( c2 ) == 6 );
	c = c2;
	/* a missing data folder */
	CHECK( lc_map_catalog_count( lc_map_catalog_get( "/nonexistent/folder", home ) ) == 1 );
	CHECK( lc_map_catalog_count( lc_map_catalog_get( NULL, NULL ) ) == 0 );
	c = lc_map_catalog_get( data, home );

	/* 3.8 */
	CHECK( lc_is_expansion_map( "ctf_dn1" ) && lc_is_expansion_map( "hm_voy3" ) && !lc_is_expansion_map( "hm_voy1" ) );

	/* 5.1: the exact vsp_server.cfg text */
	lc_server_settings_defaults( &s, "Pat Q" );
	s.rotationCount = 4;
	lc_strlcpy( s.rotation[0], "HM_retail1", 64 );
	lc_strlcpy( s.rotation[1], "hm_missing", 64 );
	lc_strlcpy( s.rotation[2], "hm_loose", 64 );
	lc_strlcpy( s.rotation[3], "hm_retail1", 64 );		/* duplicates are allowed */
	s.gametype = LC_GT_FFA;
	s.tickRate = 20;
	s.bots = 3;
	s.botSkill = 2;
	s.mods[LC_MOD_SPECIALTIES] = 1;
	lc_strlcpy( masters[0], "master.one", 65 );
	lc_strlcpy( masters[1], "master.two", 65 );
	{
		nlogs = 0;
		CHECK( lc_server_cfg_text( &s, c, (const char (*)[65])masters, 2, 0, &cfg, err, sizeof( err ), log_cb, NULL ) );
		CHECK( nlogs == 1 );
		CHECK_STR( logs[0], "Server: map hm_missing is not in the game files, skipped" );
	}
	CHECK_STR( cfg,
		"// Written by the Elite Force launcher (Host a Server) at each start.\n"
		"// Change these settings in the launcher's Config\xe2\x80\xa6 sheet; edits here are overwritten.\n"
		"\n"
		"set sv_hostname \"Pat's Server\"\n"
		"set g_password \"\"\n"
		"set sv_maxclients 12\n"
		"set sv_allowDownload 1\n"
		"set sv_pure 1\n"
		"\n"
		"// cMod's game code (by Noah Metzger) and its lag compensation\n"
		"set sv_cmodGame 1\n"
		"set g_unlagged 1\n"
		"set g_smoothClients 1\n"
		"set g_unlaggedPredict 1\n"
		"\n"
		"set g_gametype 0\n"
		"set g_pModAssimilation 0\n"
		"set g_pModDisintegration 0\n"
		"set g_pModActionHero 0\n"
		"set g_pModSpecialties 1\n"
		"set g_pModElimination 0\n"
		"set fraglimit 20\n"
		"set capturelimit 8\n"
		"set timelimit 15\n"
		"set g_friendlyFire 0\n"
		"\n"
		"// bots fill the game up to this many players (per team in team games)\n"
		"set bot_enable 1\n"
		"set bot_minplayers 3\n"
		"set g_spSkill 2\n"
		"\n"
		"// the master servers it announces itself to (the launcher's Servers\xe2\x80\xa6 sheet)\n"
		"set sv_master1 \"master.one\"\n"
		"set sv_master2 \"master.two\"\n"
		"set sv_master3 \"\"\nset sv_master4 \"\"\nset sv_master5 \"\"\nset sv_master6 \"\"\nset sv_master7 \"\"\n"
		"set sv_master8 \"\"\nset sv_master9 \"\"\nset sv_master10 \"\"\nset sv_master11 \"\"\nset sv_master12 \"\"\n"
		"set sv_master13 \"\"\nset sv_master14 \"\"\nset sv_master15 \"\"\nset sv_master16 \"\"\n"
		"\n"
		"// the map rotation\n"
		"set vsp_map1 \"map hm_retail1 ; set nextmap vstr vsp_map2\"\n"
		"set vsp_map2 \"map hm_loose ; set nextmap vstr vsp_map3\"\n"
		"set vsp_map3 \"map hm_retail1 ; set nextmap vstr vsp_map1\"\n"
		"vstr vsp_map1\n" );
	free( cfg );
	/* High tick rate writes sv_fps; a one-map rotation points at itself; blank masters */
	s.tickRate = 40;
	s.rotationCount = 1;
	CHECK( lc_server_cfg_text( &s, c, (const char (*)[65])masters, 2, 1, &cfg, err, sizeof( err ), NULL, NULL ) );
	CHECK( strstr( cfg, "set sv_pure 1\nset sv_fps 40\n\n// cMod" ) != NULL );
	CHECK( strstr( cfg, "set vsp_map1 \"map hm_retail1 ; set nextmap vstr vsp_map1\"\nvstr vsp_map1\n" ) != NULL );
	CHECK( strstr( cfg, "master.one" ) == NULL );
	free( cfg );
	/* an empty rotation: the first catalog map that supports the game type; Tournament skips the others */
	s.rotationCount = 0;
	s.gametype = LC_GT_TOURNAMENT;
	CHECK( lc_server_cfg_text( &s, c, NULL, 0, 0, &cfg, err, sizeof( err ), NULL, NULL ) );
	for ( i = 0; i < 2; i++ ) {}
	CHECK( strstr( cfg, "set vsp_map1 \"map hm_newloose ; set nextmap vstr vsp_map1\"" ) != NULL );		/* first in catalog order; no arena: supports everything */
	free( cfg );
	/* no maps at all */
	CHECK( !lc_server_cfg_text( &s, lc_map_catalog_get( NULL, NULL ), NULL, 0, 0, &cfg, err, sizeof( err ), NULL, NULL ) );
	CHECK_STR( err, "There are no Holomatch maps in your game files." );

	/* vsp_masters.cfg */
	CHECK( lc_masters_write_cfg( home, (const char (*)[65])masters, 2, 0 ) );
	lc_path_join( p, sizeof( p ), home, "vsp_masters.cfg" );
	cfg = lc_read_file( p, &len );
	CHECK( cfg != NULL );
	if ( cfg ) {
		CHECK( !strncmp( cfg, "// Written by the Elite Force launcher at each start: its Servers\xe2\x80\xa6 list.\n"
			"// sv_master1-5 are Holomatch's Internet 1-5. Edits here are overwritten.\n\nset sv_master1 \"master.one\"\n", 156 ) ||
			strstr( cfg, "\n\nset sv_master1 \"master.one\"\nset sv_master2 \"master.two\"\nset sv_master3 \"\"\n" ) != NULL );
		free( cfg );
	}
}

int main( void )
{
	char name[64];
	lc_snprintf( name, sizeof( name ), "servertest-%ld", lc_getpid( ) );		/* a fresh folder every run */
	lc_path_join( g_work, sizeof( g_work ), lc_homepath( ), name );
	lc_mkdir_p( g_work );
	test_settings( );
	test_masters( );
	test_zip_and_inflate( );
	test_arena_parse( );
	test_catalog( );
	return TEST_DONE( );
}
