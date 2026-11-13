/* tiny test helpers for the launcher core tests */
#ifndef LC_TEST_H
#define LC_TEST_H
#include <stdio.h>
#include <string.h>
static int lc_test_failures;
#define CHECK( cond ) do { if ( !( cond ) ) { fprintf( stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond ); lc_test_failures++; } } while ( 0 )
#define CHECK_STR( a, b ) do { const char *_a = ( a ), *_b = ( b ); if ( !_a || !_b || strcmp( _a, _b ) ) { fprintf( stderr, "%s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, _a ? _a : "(null)", _b ? _b : "(null)" ); lc_test_failures++; } } while ( 0 )
#define TEST_DONE( ) ( lc_test_failures ? ( fprintf( stderr, "%d failure(s)\n", lc_test_failures ), 1 ) : 0 )
#endif
