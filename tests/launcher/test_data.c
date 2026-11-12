/* game data: lookup, status, patch/expansion checks, choice validation, copy and add jobs */
#include "lc_data.h"
#include "lc_settings.h"
#include "lc_test.h"
#include "lc_test_util.h"
#include <stdlib.h>
#ifndef _WIN32
#	include <unistd.h>
#endif

static void Le16( unsigned char *p, unsigned v ) { p[0] = (unsigned char)v; p[1] = (unsigned char)( v >> 8 ); }
static void Le32( unsigned char *p, unsigned v ) { Le16( p, v & 0xFFFF ); Le16( p + 2, v >> 16 ); }

/* a stored (uncompressed) zip with the given entry names, each holding "x" */
static void MakeZip( const char *dir, const char *file, const char *const *names, int n, const char *comment )
{
	unsigned char buf[8192];
	size_t len = 0, cdStart, cdLen;
	int i;

	for ( i = 0; i < n; i++ ) {
		size_t nl = strlen( names[i] );
		memset( buf + len, 0, 30 );
		memcpy( buf + len, "PK\3\4", 4 );
		Le16( buf + len + 8, 0 );
		Le32( buf + len + 18, 1 );
		Le32( buf + len + 22, 1 );
		Le16( buf + len + 26, (unsigned)nl );
		memcpy( buf + len + 30, names[i], nl );
		buf[len + 30 + nl] = 'x';
		len += 30 + nl + 1;
	}
	cdStart = len;
	{
		size_t off = 0;
		for ( i = 0; i < n; i++ ) {
			size_t nl = strlen( names[i] );
			memset( buf + len, 0, 46 );
			memcpy( buf + len, "PK\1\2", 4 );
			Le32( buf + len + 20, 1 );
			Le32( buf + len + 24, 1 );
			Le16( buf + len + 28, (unsigned)nl );
			Le32( buf + len + 42, (unsigned)off );
			memcpy( buf + len + 46, names[i], nl );
			len += 46 + nl;
			off += 30 + nl + 1;
		}
	}
	cdLen = len - cdStart;
	memset( buf + len, 0, 22 );
	memcpy( buf + len, "PK\5\6", 4 );
	Le16( buf + len + 8, (unsigned)n );
	Le16( buf + len + 10, (unsigned)n );
	Le32( buf + len + 12, (unsigned)cdLen );
	Le32( buf + len + 16, (unsigned)cdStart );
	Le16( buf + len + 20, (unsigned)strlen( comment ) );
	len += 22;
	memcpy( buf + len, comment, strlen( comment ) );
	len += strlen( comment );
	tu_write( dir, file, buf, len );
}

static void TestZip( void )
{
	const char *dir = tu_dir( "data-zip" );
	const char *names[] = { "ui/menu.txt", "Maps/Tour/DECK02.BSP", "maps/tour/deck03.bsp" };
	const char *one[] = { "other.txt" };

	MakeZip( dir, "a.pk3", names, 3, "" );
	CHECK( lc_zip_has_entry( tu_path( dir, "a.pk3" ), "maps/tour/deck02.bsp" ) );		/* case-insensitive */
	CHECK( lc_zip_has_entry( tu_path( dir, "a.pk3" ), "ui/menu.txt" ) );
	CHECK( !lc_zip_has_entry( tu_path( dir, "a.pk3" ), "maps/tour/deck04.bsp" ) );
	CHECK( !lc_zip_has_entry( tu_path( dir, "a.pk3" ), "maps/tour/deck0" ) );
	MakeZip( dir, "b.pk3", one, 1, "a comment after the end record" );
	CHECK( !lc_zip_has_entry( tu_path( dir, "b.pk3" ), "maps/tour/deck02.bsp" ) );
	CHECK( lc_zip_has_entry( tu_path( dir, "b.pk3" ), "other.txt" ) );
	tu_text( dir, "junk.pk3", "this is not a zip file at all, just text that is long enough to scan" );
	CHECK( !lc_zip_has_entry( tu_path( dir, "junk.pk3" ), "x" ) );
	tu_text( dir, "tiny.pk3", "PK" );
	CHECK( !lc_zip_has_entry( tu_path( dir, "tiny.pk3" ), "x" ) );
	CHECK( !lc_zip_has_entry( tu_path( dir, "missing.pk3" ), "x" ) );
}

static void TestPatchAndExpansion( void )
{
	const char *home = tu_dir( "data-pe-home" ), *base = tu_dir( "data-pe-base" );
	char names[2][16];
	const char *deck[] = { "maps/tour/deck02.bsp" }, *other[] = { "x.txt" };

	CHECK( lc_data_missing_patch_paks( home, base, names ) == 2 );
	CHECK_STR( names[0], "pak1.pk3" );
	CHECK_STR( names[1], "pak2.pk3" );
	tu_text( base, "PAK1.PK3", "x" );
	CHECK( lc_data_missing_patch_paks( home, base, names ) == 1 );
	CHECK_STR( names[0], "pak2.pk3" );
	tu_text( home, "pak2.pk3", "x" );
	CHECK( lc_data_missing_patch_paks( home, base, names ) == 0 );
	CHECK( lc_data_missing_patch_paks( "", base, names ) == 1 );

	CHECK( !lc_data_has_expansion( home, base ) );
	MakeZip( base, "pak0.pk3", other, 1, "" );
	CHECK( !lc_data_has_expansion( home, base ) );
	MakeZip( home, "pak3.pk3", deck, 1, "" );		/* in either folder */
	CHECK( lc_data_has_expansion( home, base ) );
	tu_rmtree( tu_path( home, "pak3.pk3" ) );
	CHECK( !lc_data_has_expansion( home, base ) );
	tu_text( base, "Maps/Tour/Deck02.bsp", "bsp" );			/* loose */
	CHECK( lc_data_has_expansion( home, base ) );
	tu_rmtree( tu_path( base, "Maps" ) );
	MakeZip( base, ".hidden.pk3", deck, 1, "" );			/* dot-files don't count */
	CHECK( !lc_data_has_expansion( home, base ) );
}

static void TestFind( void )
{
	const char *home = lc_homepath( ), *root = tu_dir( "data-find" );
	char homeBase[LC_MAX_PATH], inst[LC_MAX_PATH], target[LC_MAX_PATH];
	char *argv[] = { (char *)"x" };
	lc_data d;
	lc_store *s;

	/* a clean homepath */
	tu_rmtree( tu_path( home, "baseEF" ) );
	tu_rmtree( tu_path( home, ".gamedata" ) );
	tu_rmtree( tu_path( home, "launcher.cfg" ) );
	lc_path_join( homeBase, sizeof( homeBase ), home, "baseEF" );

	/* nothing remembered, nothing copied: not found here (or found next to the exe / in a known install) */
	lc_data_find( 1, argv, &d );
	CHECK( !d.found || ( strcmp( d.how, "homepath" ) && strcmp( d.how, "remembered game folder" ) ) );

	/* the command line sets the paths itself */
	{
		char *ov[] = { (char *)"x", (char *)"+set", (char *)"FS_BASEPATH", (char *)"/x" };
		CHECK( lc_data_overridden( 4, ov ) );
		CHECK( !lc_data_overridden( 1, argv ) );
		CHECK( lc_data_find( 4, ov, &d ) == 1 );
		CHECK_STR( d.how, "the command line" );
		CHECK_STR( d.basepath, "" );
		CHECK_STR( d.base, "" );
	}

	/* a copy in the homepath, any case */
	tu_text( home, "baseEF/PAK0.PK3", "x" );
	CHECK( lc_data_find( 1, argv, &d ) == 1 );
	CHECK( d.found && !d.inPlace );
	CHECK_STR( d.how, "homepath" );
	CHECK_STR( d.basepath, "" );
	CHECK_STR( d.base, homeBase );
	{
		char line[256], detail[512];
		lc_data_status_text( &d, line, sizeof( line ), detail, sizeof( detail ) );
		CHECK( strncmp( line, "Patch 1.2 missing \xc2\xb7 add pak1.pk3 and pak2.pk3 with Add Game Files\xe2\x80\xa6", 20 ) == 0 );
		CHECK_STR( line, "Patch 1.2 missing \xc2\xb7 add pak1.pk3 and pak2.pk3 with Add Game Files\xe2\x80\xa6" );
		CHECK( strstr( detail, "These game files are from the original CD (version 1.0). The game also needs pak1.pk3 and pak2.pk3 from the 1.1 and 1.2 patches" ) );
		tu_text( home, "baseEF/pak1.pk3", "x" );
		lc_data_status_text( &d, line, sizeof( line ), detail, sizeof( detail ) );
		CHECK( strstr( line, "add pak2.pk3 with Add" ) != NULL );
		tu_text( home, "baseEF/pak2.pk3", "x" );
		lc_data_status_text( &d, line, sizeof( line ), detail, sizeof( detail ) );
		CHECK_STR( line, "Ready \xc2\xb7 copied into the app's own folder" );
		CHECK_STR( detail, "" );
	}

	/* a remembered folder wins; Windows and the exact name BaseEF use it directly */
	lc_path_join( inst, sizeof( inst ), root, "Install" );
	tu_text( inst, "baseEF/pak0.pk3", "x" );
	CHECK( lc_remember_game_folder( inst ) );
	CHECK( lc_data_find( 1, argv, &d ) == 1 );
	CHECK( d.found && d.inPlace );
	CHECK_STR( d.how, "remembered game folder" );
	CHECK_STR( d.basepath, inst );
	{
		char want[LC_MAX_PATH];
		lc_path_join( want, sizeof( want ), inst, "baseEF" );
		CHECK_STR( d.base, want );
		/* the file sys_data.c reads */
		s = lc_store_load( NULL );
		CHECK_STR( lc_store_get( s, "gamefolder" ), inst );
		lc_store_free( s );
		{
			char *text = lc_read_file( tu_path( home, "launcher.cfg" ), NULL );
			CHECK( text && strstr( text, "gamefolder \"" ) == text );
			free( text );
		}
	}
	{
		char line[256];
		lc_data_status_text( &d, line, sizeof( line ), NULL, 0 );
		CHECK( strstr( line, "Patch 1.2 missing" ) == NULL );		/* pak1/pak2 are in the homepath copy */
		tu_rmtree( tu_path( home, "baseEF/pak2.pk3" ) );
		tu_rmtree( tu_path( home, "baseEF/pak1.pk3" ) );
		lc_data_status_text( &d, line, sizeof( line ), NULL, 0 );
		CHECK( strstr( line, "Patch 1.2 missing" ) != NULL );
		tu_text( inst, "baseEF/pak1.pk3", "x" );
		tu_text( inst, "baseEF/pak2.pk3", "x" );
		lc_data_status_text( &d, line, sizeof( line ), NULL, 0 );
		CHECK_STR( line, "Ready \xc2\xb7 using the files in this folder" );
	}

	/* the folder holding the install named differently: BaseEF on Linux needs the .gamedata link */
	{
		char inst2[LC_MAX_PATH];
		lc_path_join( inst2, sizeof( inst2 ), root, "Install2" );
		tu_text( inst2, "BaseEF/pak0.pk3", "x" );
		CHECK( lc_remember_game_folder( inst2 ) );
		CHECK( lc_data_find( 1, argv, &d ) == 1 );
		CHECK_STR( d.how, "remembered game folder" );
#ifdef _WIN32
		CHECK_STR( d.basepath, inst2 );
#else
		{
			char link[LC_MAX_PATH], want[LC_MAX_PATH];
			ssize_t n;

			lc_path_join( want, sizeof( want ), home, ".gamedata" );
			CHECK_STR( d.basepath, want );
			lc_path_join( link, sizeof( link ), want, "baseEF" );
			n = readlink( link, target, sizeof( target ) - 1 );
			CHECK( n > 0 );
			if ( n > 0 ) {
				target[n] = 0;
				lc_path_join( want, sizeof( want ), inst2, "BaseEF" );
				CHECK_STR( target, want );
			}
			CHECK_STR( lc_use_found_basepath( inst2, want ), d.basepath );
		}
#endif
	}

	/* the BaseEF folder itself remembered: its parent is the folder */
	{
		char be[LC_MAX_PATH];
		lc_path_join( be, sizeof( be ), inst, "baseEF" );
		CHECK( lc_remember_game_folder( be ) );
		CHECK( lc_data_find( 1, argv, &d ) == 1 );
		CHECK_STR( d.basepath, inst );
		CHECK_STR( d.base, be );
		CHECK( lc_data_external_base( target, sizeof( target ) ) );
	}

	/* a remembered folder without pak0 falls back to the homepath copy; clearing forgets it */
	tu_text( home, "baseEF/pak0.pk3", "x" );
	CHECK( lc_remember_game_folder( tu_path( root, "gone" ) ) );
	CHECK( lc_data_find( 1, argv, &d ) == 1 );
	CHECK_STR( d.how, "homepath" );
	CHECK( lc_remember_game_folder( NULL ) );
	s = lc_store_load( NULL );
	CHECK( lc_store_get( s, "gamefolder" ) == NULL );
	lc_store_free( s );
	CHECK( lc_data_external_base( target, sizeof( target ) ) == 0 );
	CHECK_STR( target, "" );
	CHECK( lc_data_pak0_in_homebase( ) );

	/* the status of nothing found */
	{
		lc_data none;
		char line[256];
		memset( &none, 0, sizeof( none ) );
		lc_data_status_text( &none, line, sizeof( line ), NULL, 0 );
		CHECK_STR( line, "Choose the Elite Force folder (the one with BaseEF), or the GOG setup .exe." );
	}
}

static void TestValidate( void )
{
	const char *root = tu_dir( "data-validate" );
	char base[LC_MAX_PATH], problem[512], want[LC_MAX_PATH];

	/* the installation folder with BaseEF/pak0.pk3 */
	tu_text( root, "Inst/BaseEF/pak0.pk3", "x" );
	CHECK( lc_validate_choice( tu_path( root, "Inst" ), 0, base, sizeof( base ), problem, sizeof( problem ) ) == 0 );
	lc_path_join( want, sizeof( want ), tu_path( root, "Inst" ), "BaseEF" );
	CHECK( !lc_stricmp( base, want ) );		/* (Windows reports the spelling it was asked for) */
	CHECK_STR( problem, "" );
	CHECK( lc_validate_choice( tu_path( root, "Inst" ), 1, base, sizeof( base ), problem, sizeof( problem ) ) == 0 );
	/* a trailing separator */
	{
		char slash[LC_MAX_PATH];
		lc_snprintf( slash, sizeof( slash ), "%s%c", tu_path( root, "Inst" ), LC_PATH_SEP );
		CHECK( lc_validate_choice( slash, 0, base, sizeof( base ), problem, sizeof( problem ) ) == 0 );
	}
	/* the BaseEF folder itself */
	CHECK( lc_validate_choice( tu_path( root, "Inst/BaseEF" ), 1, base, sizeof( base ), problem, sizeof( problem ) ) == 0 );
	CHECK_STR( base, tu_path( root, "Inst/BaseEF" ) );
	/* a folder with pak0.pk3 but another name: copy yes, in place no */
	tu_text( root, "Other/pak0.pk3", "x" );
	CHECK( lc_validate_choice( tu_path( root, "Other" ), 0, base, sizeof( base ), problem, sizeof( problem ) ) == 0 );
	CHECK_STR( base, tu_path( root, "Other" ) );
	CHECK( lc_validate_choice( tu_path( root, "Other" ), 1, base, sizeof( base ), problem, sizeof( problem ) ) != 0 );
	CHECK_STR( problem, "To use the files in place the folder must be called BaseEF. Rename it, or untick the checkbox to copy the files instead." );
	/* pak0 left on the CD */
	tu_text( root, "Cd/BaseEF/pak1.pk3", "x" );
	CHECK( lc_validate_choice( tu_path( root, "Cd" ), 0, base, sizeof( base ), problem, sizeof( problem ) ) != 0 );
	CHECK_STR( problem, "pak0.pk3 is missing \xe2\x80\x94 minimum CD installs leave it on the CD; copy it from BaseEF on the CD into this BaseEF folder, or choose a full installation." );
	CHECK( lc_validate_choice( tu_path( root, "Cd/BaseEF" ), 0, base, sizeof( base ), problem, sizeof( problem ) ) != 0 );
	CHECK( strncmp( problem, "pak0.pk3 is missing", 19 ) == 0 );
	/* neither */
	tu_text( root, "Empty/readme.txt", "x" );
	CHECK( lc_validate_choice( tu_path( root, "Empty" ), 0, base, sizeof( base ), problem, sizeof( problem ) ) != 0 );
	CHECK_STR( problem, "This folder has no BaseEF folder. Choose the Elite Force installation folder (the one containing BaseEF), or the BaseEF folder itself." );
	CHECK( lc_validate_choice( tu_path( root, "missing" ), 0, base, sizeof( base ), problem, sizeof( problem ) ) != 0 );
	CHECK( lc_validate_choice( tu_path( root, "Empty/readme.txt" ), 0, base, sizeof( base ), problem, sizeof( problem ) ) != 0 );

	{
		char title[128], text[1024];
		lc_choice_alert( "/x/y", "Problem.", title, sizeof( title ), text, sizeof( text ) );
		CHECK_STR( title, "That folder can't be used" );
		CHECK_STR( text, "/x/y\n\nProblem." );
	}

	/* installers */
	tu_text( root, "setup_x.EXE", "MZ" );
	CHECK( lc_is_installer( tu_path( root, "setup_x.EXE" ) ) );
	CHECK( !lc_is_installer( tu_path( root, "Inst" ) ) );
	CHECK( !lc_is_installer( tu_path( root, "Other/pak0.pk3" ) ) );
	tu_dir( "data-validate/dir.exe" );
	CHECK( !lc_is_installer( tu_path( root, "dir.exe" ) ) );
}

static void Fill( const char *dir, const char *name, size_t n, char c )
{
	char *b = malloc( n );
	memset( b, c, n );
	tu_write( dir, name, b, n );
	free( b );
}

static void TestCopyJob( void )
{
	const char *root = tu_dir( "data-copy" );
	char src[LC_MAX_PATH], dst[LC_MAX_PATH], err[512], title[128], text[1024], label[1024];
	lc_copyjob *j;
	int pk3s = -1, saves = -1, kept = -1;
	volatile unsigned long long progress = 0;
	volatile signed char cancel = 0;
	char *c;

	lc_path_join( src, sizeof( src ), root, "src/BaseEF" );
	lc_path_join( dst, sizeof( dst ), root, "dst/baseEF" );
	Fill( src, "pak0.pk3", 2500000, 'a' );			/* more than one copy block */
	Fill( src, "PAK1.PK3", 100, 'b' );
	Fill( src, ".hidden.pk3", 10, 'h' );
	Fill( src, "notes.txt", 10, 'n' );
	Fill( src, "level.bsp", 10, 'l' );
	Fill( src, "saves/a.sav", 30, 's' );
	Fill( src, "saves/sub/b.sav", 40, 't' );
	Fill( src, "saves/.DS_Store", 5, 'x' );
	/* an existing save in the destination is never overwritten, an existing pak is */
	tu_text( dst, "saves/a.sav", "mine" );
	Fill( dst, "pak0.pk3", 5, 'z' );

	j = lc_plan_copy_game_files( src, dst, &pk3s, &saves, &kept );
	CHECK( j != NULL );
	CHECK( pk3s == 2 && saves == 1 && kept == 1 );
	CHECK( lc_copyjob_count( j ) == 3 );
	CHECK( lc_copyjob_total( j ) == 2500000 + 100 + 40 );
	CHECK( !lc_copyjob_is_unpack( j ) );
	lc_copyjob_title( j, title, sizeof( title ) );
	CHECK_STR( title, "Copying game files" );
	lc_copyjob_progress_text( j, 0, label, sizeof( label ) );
	lc_snprintf( text, sizeof( text ), "Copying game files to %s \xe2\x80\xa6", dst );
	CHECK_STR( label, text );
	CHECK( lc_copyjob_check_space( j, title, sizeof( title ), text, sizeof( text ) ) );
	CHECK( lc_copyjob_run( j, &progress, &cancel, err, sizeof( err ) ) == 1 );
	CHECK( progress == 2500140 );
	CHECK( lc_file_size( tu_path( dst, "pak0.pk3" ) ) == 2500000 );
	CHECK( lc_file_size( tu_path( dst, "PAK1.PK3" ) ) == 100 );
	CHECK( lc_file_size( tu_path( dst, "saves/sub/b.sav" ) ) == 40 );
	CHECK( !lc_is_file( tu_path( dst, "notes.txt" ) ) && !lc_is_file( tu_path( dst, "level.bsp" ) ) );
	CHECK( !lc_is_file( tu_path( dst, ".hidden.pk3" ) ) && !lc_is_file( tu_path( dst, "saves/.DS_Store" ) ) );
	CHECK( !lc_is_file( tu_path( dst, "pak0.pk3.part" ) ) );
	c = lc_read_file( tu_path( dst, "saves/a.sav" ), NULL );
	CHECK( c && !strcmp( c, "mine" ) );
	free( c );
	c = lc_read_file( tu_path( dst, "pak0.pk3" ), NULL );
	CHECK( c && c[0] == 'a' && c[2499999] == 'a' );
	free( c );
	CHECK( lc_file_mtime( tu_path( dst, "PAK1.PK3" ) ) == lc_file_mtime( tu_path( src, "PAK1.PK3" ) ) );
	lc_copyjob_free( j );

	/* cancelled before it starts: -1, nothing created, what existed stays */
	{
		const char *d2 = tu_dir( "data-copy2" );
		tu_text( d2, "keep.pk3", "keep" );
		j = lc_plan_copy_game_files( src, d2, NULL, NULL, NULL );
		CHECK( j != NULL );
		cancel = 1;
		progress = 0;
		CHECK( lc_copyjob_run( j, &progress, &cancel, err, sizeof( err ) ) == -1 );
		CHECK_STR( err, "Cancelled." );
		CHECK( !lc_is_file( tu_path( d2, "pak0.pk3" ) ) && !lc_is_file( tu_path( d2, "PAK1.PK3" ) ) );
		CHECK( lc_is_file( tu_path( d2, "keep.pk3" ) ) );
		lc_copyjob_free( j );
		cancel = 0;
	}

	/* a failure in the second item rolls the first back (files that existed stay) */
	{
		const char *s3 = tu_dir( "data-copy3-src" ), *d3 = tu_dir( "data-copy3" );
		lc_copyjob *k;

		Fill( s3, "a.pk3", 100, '1' );
		Fill( s3, "b.pk3", 100, '2' );
		tu_text( d3, "b.pk3", "old" );				/* will be replaced, but not removed on rollback */
		k = lc_plan_copy_game_files( s3, d3, NULL, NULL, NULL );
		CHECK( k && lc_copyjob_count( k ) == 2 );
		lc_remove( tu_path( s3, "b.pk3" ) );		/* now unreadable */
		/* the order of items is the directory's; either way one is created before the failure or none */
		CHECK( lc_copyjob_run( k, &progress, &cancel, err, sizeof( err ) ) == 0 );
		CHECK( strncmp( err, "Cannot read ", 12 ) == 0 );
		CHECK( !lc_is_file( tu_path( d3, "a.pk3" ) ) );
		CHECK( lc_is_file( tu_path( d3, "b.pk3" ) ) );
		lc_copyjob_failure_texts( k, err, title, sizeof( title ), text, sizeof( text ) );
		CHECK_STR( title, "Copying the game files failed" );
		CHECK( strstr( text, "\n\nThe partial copy was removed." ) != NULL );
		lc_copyjob_free( k );
	}

	/* space check wording (can't fake the disk: only the texts with a huge plan) */
	{
		lc_copyjob *k = lc_plan_copy_game_files( tu_dir( "data-copy4-src" ), tu_dir( "data-copy4" ), NULL, NULL, NULL );
		CHECK( k != NULL );
		CHECK( lc_copyjob_count( k ) == 0 );
		CHECK( lc_copyjob_check_space( k, title, sizeof( title ), text, sizeof( text ) ) );
		lc_copyjob_free( k );
	}
}

static void TestAddFiles( void )
{
	const char *root = tu_dir( "data-add" );
	char home[LC_MAX_PATH], ext[LC_MAX_PATH], exp[LC_MAX_PATH], picked1[LC_MAX_PATH], picked2[LC_MAX_PATH], err[256];
	const char *picks[3];
	lc_copyjob *j;
	int present = -1;
	volatile unsigned long long progress = 0;
	volatile signed char cancel = 0;
	char title[128], text[512];

	lc_path_join( home, sizeof( home ), root, "home/baseEF" );
	lc_path_join( ext, sizeof( ext ), root, "game/BaseEF" );
	lc_path_join( exp, sizeof( exp ), root, "expansion" );
	Fill( exp, "BaseEF/pak3.pk3", 200, '3' );
	Fill( exp, "BaseEF/pak4.pk3", 300, '4' );
	Fill( exp, "BaseEF/readme.txt", 5, 'r' );
	Fill( home, "pak3.pk3", 200, 'x' );				/* same size: present */
	Fill( ext, "pak4.PK3", 300, 'y' );				/* in the external base, same size: present */
	lc_path_join( picked1, sizeof( picked1 ), root, "loose/Extra.pk3" );
	Fill( root, "loose/Extra.pk3", 70, 'e' );
	Fill( root, "loose2/extra.PK3", 99, 'f' );		/* the same name again: queued once */
	lc_path_join( picked2, sizeof( picked2 ), root, "loose2/extra.PK3" );
	picks[0] = exp;
	picks[1] = picked1;
	picks[2] = picked2;

	j = lc_plan_add_files( picks, 3, home, ext, &present );
	CHECK( j != NULL && present == 2 );
	CHECK( lc_copyjob_count( j ) == 1 && lc_copyjob_total( j ) == 70 );
	CHECK( lc_copyjob_check_space( j, title, sizeof( title ), text, sizeof( text ) ) );
	CHECK( lc_copyjob_run( j, &progress, &cancel, err, sizeof( err ) ) == 1 );
	CHECK( lc_file_size( tu_path( home, "Extra.pk3" ) ) == 70 );
	lc_copyjob_free( j );

	/* everything is there now */
	present = -1;
	j = lc_plan_add_files( picks, 2, home, ext, &present );
	CHECK( j && lc_copyjob_count( j ) == 0 && present == 3 );
	lc_copyjob_free( j );
	lc_nothing_to_add_alert( 0, 3, title, sizeof( title ), text, sizeof( text ) );
	CHECK_STR( title, "Nothing to add" );
	CHECK_STR( text, "The game already has all of those .pk3 files." );
	lc_nothing_to_add_alert( 0, 1, title, sizeof( title ), text, sizeof( text ) );
	CHECK_STR( text, "The game already has that .pk3 file." );
	lc_nothing_to_add_alert( 0, 0, title, sizeof( title ), text, sizeof( text ) );
	CHECK_STR( text, "No .pk3 files were found there. Choose .pk3 files, or the BaseEF folder that contains them." );
	lc_nothing_to_add_alert( 1, 0, title, sizeof( title ), text, sizeof( text ) );
	CHECK_STR( text, "The game already has all of this installer's .pk3 files." );

	/* a different size replaces the file, keeping the existing spelling */
	lc_rename( tu_path( home, "pak3.pk3" ), tu_path( home, "PAK3.PK3" ) );
	Fill( exp, "BaseEF/pak3.pk3", 250, '9' );
	picks[0] = exp;
	j = lc_plan_add_files( picks, 1, home, ext, &present );
	CHECK( j && lc_copyjob_count( j ) == 1 );
	CHECK( lc_copyjob_run( j, &progress, &cancel, err, sizeof( err ) ) == 1 );
	{
		char found[LC_MAX_PATH];
		/* the same file whatever its spelling (a case-insensitive volume may keep either) */
		CHECK( lc_child_ci( home, "pak3.pk3", found, sizeof( found ) ) && lc_file_size( found ) == 250 );
		CHECK( !lc_child_ci( home, "pak3.pk3.part", found, sizeof( found ) ) );
	}
	lc_copyjob_free( j );
}

static void TestInstaller( void )
{
	const char *root = tu_dir( "data-gog" );
	char err[256] = "", title[128], text[1024];
	int present = -1;

	tu_text( root, "setup_junk.exe", "this is not an installer" );
	if ( !lc_have_gog( ) ) {
		CHECK( lc_plan_unpack_installer( tu_path( root, "setup_junk.exe" ), 0, tu_dir( "data-gog-home" ), NULL, &present, err, sizeof( err ) ) == NULL );
		CHECK( err[0] != 0 );
		return;
	}
	CHECK( lc_plan_unpack_installer( tu_path( root, "setup_junk.exe" ), 0, tu_dir( "data-gog-home" ), NULL, &present, err, sizeof( err ) ) == NULL );
	CHECK_STR( err, "it isn't a Windows installer" );
	lc_installer_error_alert( tu_path( root, "setup_junk.exe" ), err, title, sizeof( title ), text, sizeof( text ) );
	CHECK_STR( title, "This installer can't be used" );
	CHECK( strncmp( text, "setup_junk.exe\n\nThis app can't unpack it: it isn't a Windows installer.\n\n", 71 ) == 0 );
	/* an MZ file that is not Inno Setup */
	tu_write( root, "setup_mz.exe", "MZ\x90\x00 padding padding padding padding", 36 );
	CHECK( lc_plan_unpack_installer( tu_path( root, "setup_mz.exe" ), 1, tu_dir( "data-gog-home" ), NULL, &present, err, sizeof( err ) ) == NULL );
	CHECK( err[0] != 0 );
	CHECK( lc_plan_unpack_installer( tu_path( root, "missing.exe" ), 1, tu_dir( "data-gog-home" ), NULL, &present, err, sizeof( err ) ) == NULL );
	CHECK( strncmp( err, "it can't be read", 16 ) == 0 );
}

int main( void )
{
	TestZip( );
	TestPatchAndExpansion( );
	TestFind( );
	TestValidate( );
	TestCopyJob( );
	TestAddFiles( );
	TestInstaller( );
	return TEST_DONE( );
}
