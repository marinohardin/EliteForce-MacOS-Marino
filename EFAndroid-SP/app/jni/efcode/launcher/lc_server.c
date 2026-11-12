/*
===========================================================================
Launcher core: Host a Server settings, the map catalog, the generated cfg files,
master servers. Spec: servers.md sections 2-6. This file is part of VoyagerSP
(GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#include "lc_server.h"
#include "lc_net.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ---- small helpers --------------------------------------------------------- */

static int lc_ci_ext( const char *name, const char *ext )
{
	size_t n = strlen( name ), e = strlen( ext );
	return n > e && lc_stricmp( name + n - e, ext ) == 0;
}

static void lc_lower( char *s )
{
	for ( ; *s; s++ )
		*s = (char)tolower( (unsigned char)*s );
}

/* growable string */
typedef struct { char *p; size_t len, cap; } lc_sb;

static void lc_sb_add( lc_sb *b, const char *s )
{
	size_t n = strlen( s );
	if ( b->len + n + 1 > b->cap ) {
		size_t nc = b->cap ? b->cap : 256;
		while ( nc < b->len + n + 1 )
			nc *= 2;
		b->p = (char *)realloc( b->p, nc );
		b->cap = nc;
	}
	memcpy( b->p + b->len, s, n + 1 );
	b->len += n;
}

static void lc_sb_addf( lc_sb *b, const char *fmt, ... )
#if defined( __GNUC__ )
	__attribute__ (( format( printf, 2, 3 ) ))
#endif
	;
static void lc_sb_addf( lc_sb *b, const char *fmt, ... )
{
	char tmp[2048];
	va_list ap;
	va_start( ap, fmt );
	vsnprintf( tmp, sizeof( tmp ), fmt, ap );
	va_end( ap );
	lc_sb_add( b, tmp );
}

/* ---- settings (2) ------------------------------------------------------------- */

void lc_clean_cvar_text( const char *in, size_t maxLen, char *out, size_t outSize )
{
	size_t n = 0;
	const unsigned char *p = (const unsigned char *)( in ? in : "" );

	if ( !outSize )
		return;
	for ( ; *p && n < maxLen && n + 1 < outSize; p++ ) {
		if ( *p < 32 || *p > 126 || *p == '"' || *p == ';' || *p == '\\' || *p == '%' )
			continue;
		out[n++] = (char)*p;
	}
	out[n] = 0;
	lc_trim( out );
}

static void default_hostname( const char *userFullName, char *out, size_t size )
{
	char word[64], clean[64];
	size_t i = 0;
	const char *p = userFullName ? userFullName : "";

	while ( *p == ' ' )
		p++;
	while ( *p && *p != ' ' && i + 1 < sizeof( word ) )
		word[i++] = *p++;
	word[i] = 0;
	lc_clean_cvar_text( word, 24, clean, sizeof( clean ) );
	if ( clean[0] )
		lc_snprintf( out, size, "%s's Server", clean );
	else
		lc_strlcpy( out, "Elite Force Server", size );
}

void lc_server_settings_defaults( lc_server_settings *s, const char *userFullName )
{
	static const char *const rot[] = { "hm_voy1", "hm_borg1", "hm_kln1", "hm_dn1", "hm_for1", "hm_cam" };
	int i;

	memset( s, 0, sizeof( *s ) );
	default_hostname( userFullName, s->hostname, sizeof( s->hostname ) );
	s->gametype = LC_GT_FFA;
	s->fraglimit = 20;
	s->capturelimit = 8;
	s->timelimit = 15;
	s->maxPlayers = 12;
	s->bots = 0;
	s->botSkill = 3;
	s->openPort = 1;
	s->allowDownload = 1;
	s->pure = 1;
	s->port = 27960;
	s->tickRate = 20;
	s->cmodGame = 1;
	s->unlagged = 1;
	s->smoothClients = 1;
	s->unlaggedPredict = 1;
	s->rotationCount = (int)( sizeof( rot ) / sizeof( rot[0] ) );
	for ( i = 0; i < s->rotationCount; i++ )
		lc_strlcpy( s->rotation[i], rot[i], sizeof( s->rotation[i] ) );
}

static int clampi( int v, int lo, int hi )
{
	return v < lo ? lo : v > hi ? hi : v;
}

void lc_server_settings_validate( lc_server_settings *s, const char *userFullName )
{
	char tmp[64];

	lc_clean_cvar_text( s->hostname, 48, tmp, sizeof( tmp ) );
	lc_strlcpy( s->hostname, tmp, sizeof( s->hostname ) );
	lc_clean_cvar_text( s->password, 32, tmp, sizeof( tmp ) );
	lc_strlcpy( s->password, tmp, sizeof( s->password ) );
	if ( s->gametype != 0 && s->gametype != 1 && s->gametype != 3 && s->gametype != 4 )
		s->gametype = 0;
	s->botSkill = clampi( s->botSkill, 1, 5 );
	s->maxPlayers = clampi( s->maxPlayers, 2, 32 );
	s->bots = clampi( s->bots, 0, 31 );
	if ( s->port < 1024 || s->port > 65535 )
		s->port = 27960;
	if ( s->tickRate != 40 )
		s->tickRate = 20;
	s->unlagged = clampi( s->unlagged, 0, 2 );
	if ( !s->hostname[0] )
		default_hostname( userFullName, s->hostname, sizeof( s->hostname ) );
	if ( s->rotationCount < 0 )
		s->rotationCount = 0;
	if ( s->rotationCount > 64 )
		s->rotationCount = 64;
	lc_server_settings_apply_modifier_rules( s, -1 );
}

void lc_server_settings_apply_modifier_rules( lc_server_settings *s, int favoredMod )
{
	/* excludes[m] = bitmask of modifiers that m turns off */
	static const int excludes[LC_MOD_COUNT] = {
		/* assimilation   */ ( 1 << LC_MOD_DISINTEGRATION ) | ( 1 << LC_MOD_ELIMINATION ) | ( 1 << LC_MOD_ACTIONHERO ),
		/* specialties    */ ( 1 << LC_MOD_DISINTEGRATION ) | ( 1 << LC_MOD_ACTIONHERO ),
		/* disintegration */ ( 1 << LC_MOD_ASSIMILATION ) | ( 1 << LC_MOD_ACTIONHERO ) | ( 1 << LC_MOD_SPECIALTIES ),
		/* elimination    */ ( 1 << LC_MOD_ASSIMILATION ) | ( 1 << LC_MOD_ACTIONHERO ),
		/* actionHero     */ ( 1 << LC_MOD_ASSIMILATION ) | ( 1 << LC_MOD_SPECIALTIES ) | ( 1 << LC_MOD_ELIMINATION ) | ( 1 << LC_MOD_DISINTEGRATION ),
	};
	int order[LC_MOD_COUNT], n = 0, i, m;

	if ( s->gametype != 3 )
		s->mods[LC_MOD_ASSIMILATION] = 0;
	if ( s->gametype >= 4 )
		s->mods[LC_MOD_ELIMINATION] = 0;
	if ( s->gametype >= 3 )
		s->mods[LC_MOD_ACTIONHERO] = 0;
	if ( favoredMod >= 0 && favoredMod < LC_MOD_COUNT )
		order[n++] = favoredMod;
	for ( i = 0; i < LC_MOD_COUNT; i++ )
		if ( i != favoredMod )
			order[n++] = i;
	for ( i = 0; i < LC_MOD_COUNT; i++ ) {
		m = order[i];
		if ( !s->mods[m] )
			continue;
		for ( n = 0; n < LC_MOD_COUNT; n++ )
			if ( excludes[m] & ( 1 << n ) )
				s->mods[n] = 0;
	}
}

int lc_server_settings_modifier_allowed( const lc_server_settings *s, int mod )
{
	switch ( mod ) {
	case LC_MOD_ASSIMILATION:	return s->gametype == 3;
	case LC_MOD_ELIMINATION:	return s->gametype < 4;
	case LC_MOD_ACTIONHERO:		return s->gametype < 3;
	default:					return 1;
	}
}

const char *lc_gametype_name( int gametype )
{
	switch ( gametype ) {
	case 1:		return "Tournament";
	case 3:		return "Team Holomatch";
	case 4:		return "Capture the Flag";
	default:	return "Free For All";
	}
}

const char *lc_browser_gametype_name( int gametype, char *buf, size_t n )
{
	if ( gametype == 2 )
		lc_strlcpy( buf, "Single Player", n );
	else if ( gametype > 4 )
		lc_snprintf( buf, n, "Type %d", gametype );
	else
		lc_strlcpy( buf, lc_gametype_name( gametype ), n );
	return buf;
}

const char *lc_skill_name( int skill )
{
	static const char *const names[] = { "Cadet", "Ensign", "Lieutenant", "Commander", "Captain" };
	return names[clampi( skill, 1, 5 ) - 1];
}

const char *lc_modifier_name( int mod )
{
	static const char *const names[LC_MOD_COUNT] = { "Assimilation", "Specialties", "Disintegration", "Elimination", "Action Hero" };
	return mod >= 0 && mod < LC_MOD_COUNT ? names[mod] : "";
}

/* store: one key per field, "serversettings.<field>" */
#define SS_PREFIX "serversettings."

static int ss_get_int( lc_store *st, const char *field, int *out )
{
	char key[96];
	const char *v;
	char *end;
	long n;

	lc_snprintf( key, sizeof( key ), SS_PREFIX "%s", field );
	v = lc_store_get( st, key );
	if ( !v || !*v )
		return 0;
	n = strtol( v, &end, 10 );
	if ( *end )
		return 0;
	*out = (int)n;
	return 1;
}

static int ss_get_bool( lc_store *st, const char *field, int *out )
{
	int v;
	if ( !ss_get_int( st, field, &v ) )
		return 0;
	*out = v != 0;
	return 1;
}

static void ss_set_int( lc_store *st, const char *field, int v )
{
	char key[96];
	lc_snprintf( key, sizeof( key ), SS_PREFIX "%s", field );
	lc_store_set_int( st, key, v );
}

#define SS_INTS( X ) \
	X( gametype ) X( fraglimit ) X( capturelimit ) X( timelimit ) X( maxPlayers ) X( bots ) \
	X( botSkill ) X( port ) X( tickRate ) X( unlagged )
#define SS_BOOLS( X ) \
	X( friendlyFire ) X( internet ) X( openPort ) X( allowDownload ) X( pure ) X( cmodGame ) \
	X( smoothClients ) X( unlaggedPredict )

void lc_server_settings_load( lc_server_settings *s, lc_store *st, const char *userFullName )
{
	char key[96];
	const char *v;
	static const char *const modKeys[LC_MOD_COUNT] = { "assimilation", "specialties", "disintegration", "elimination", "actionHero" };
	int i;

	lc_server_settings_defaults( s, userFullName );
	lc_snprintf( key, sizeof( key ), SS_PREFIX "hostname" );
	if ( ( v = lc_store_get( st, key ) ) != NULL )
		lc_strlcpy( s->hostname, v, sizeof( s->hostname ) );
	lc_snprintf( key, sizeof( key ), SS_PREFIX "password" );
	if ( ( v = lc_store_get( st, key ) ) != NULL )
		lc_strlcpy( s->password, v, sizeof( s->password ) );
#define X( f ) ss_get_int( st, #f, &s->f );
	SS_INTS( X )
#undef X
#define X( f ) ss_get_bool( st, #f, &s->f );
	SS_BOOLS( X )
#undef X
	for ( i = 0; i < LC_MOD_COUNT; i++ )
		ss_get_bool( st, modKeys[i], &s->mods[i] );
	lc_snprintf( key, sizeof( key ), SS_PREFIX "rotation" );
	if ( lc_store_has_list( st, key ) )
		s->rotationCount = lc_store_get_list( st, key, s->rotation[0], sizeof( s->rotation[0] ), 64 );
	lc_server_settings_validate( s, userFullName );
}

void lc_server_settings_save( const lc_server_settings *s, lc_store *st )
{
	char key[96];
	static const char *const modKeys[LC_MOD_COUNT] = { "assimilation", "specialties", "disintegration", "elimination", "actionHero" };
	int i;

	lc_snprintf( key, sizeof( key ), SS_PREFIX "hostname" );
	lc_store_set( st, key, s->hostname );
	lc_snprintf( key, sizeof( key ), SS_PREFIX "password" );
	lc_store_set( st, key, s->password );
#define X( f ) ss_set_int( st, #f, s->f );
	SS_INTS( X )
	SS_BOOLS( X )
#undef X
	for ( i = 0; i < LC_MOD_COUNT; i++ )
		ss_set_int( st, modKeys[i], s->mods[i] ? 1 : 0 );
	lc_snprintf( key, sizeof( key ), SS_PREFIX "rotation" );
	lc_store_set_list( st, key, s->rotation[0], sizeof( s->rotation[0] ), s->rotationCount );
	lc_store_save( st );
}

/* ---- pk3 zip reading (3.3) and a small inflate ------------------------------------- */

static unsigned LE16( const unsigned char *p ) { return (unsigned)p[0] | (unsigned)p[1] << 8; }
static unsigned LE32( const unsigned char *p ) { return (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24; }

static int fseek64( FILE *f, long long off, int whence )
{
#ifdef _WIN32
	return _fseeki64( f, off, whence );
#else
	return fseeko( f, (off_t)off, whence );
#endif
}

/* raw inflate (RFC 1951), after Mark Adler's puff.c (zlib license). Returns bytes written or -1. */
#define LC_MAXBITS 15
#define LC_MAXLCODES 286
#define LC_MAXDCODES 30
#define LC_FIXLCODES 288

typedef struct {
	unsigned char *out;
	size_t outlen, outcnt;
	const unsigned char *in;
	size_t inlen, incnt;
	int bitbuf, bitcnt;
	int err;
} inf_state;

typedef struct { short count[LC_MAXBITS + 1]; short symbol[LC_MAXLCODES]; } inf_huff;

static int inf_bits( inf_state *s, int need )
{
	long val = s->bitbuf;

	while ( s->bitcnt < need ) {
		if ( s->incnt == s->inlen ) {
			s->err = 1;
			return 0;
		}
		val |= (long)s->in[s->incnt++] << s->bitcnt;
		s->bitcnt += 8;
	}
	s->bitbuf = (int)( val >> need );
	s->bitcnt -= need;
	return (int)( val & ( ( 1L << need ) - 1 ) );
}

static int inf_decode( inf_state *s, const inf_huff *h )
{
	int len, code = 0, first = 0, index = 0, count;

	for ( len = 1; len <= LC_MAXBITS; len++ ) {
		code |= inf_bits( s, 1 );
		if ( s->err )
			return -1;
		count = h->count[len];
		if ( code - count < first )
			return h->symbol[index + ( code - first )];
		index += count;
		first += count;
		first <<= 1;
		code <<= 1;
	}
	return -1;
}

static int inf_construct( inf_huff *h, const short *length, int n )
{
	int symbol, len, left;
	short offs[LC_MAXBITS + 1];

	for ( len = 0; len <= LC_MAXBITS; len++ )
		h->count[len] = 0;
	for ( symbol = 0; symbol < n; symbol++ )
		h->count[length[symbol]]++;
	if ( h->count[0] == n )
		return 0;
	left = 1;
	for ( len = 1; len <= LC_MAXBITS; len++ ) {
		left <<= 1;
		left -= h->count[len];
		if ( left < 0 )
			return left;
	}
	offs[1] = 0;
	for ( len = 1; len < LC_MAXBITS; len++ )
		offs[len + 1] = (short)( offs[len] + h->count[len] );
	for ( symbol = 0; symbol < n; symbol++ )
		if ( length[symbol] != 0 )
			h->symbol[offs[length[symbol]]++] = (short)symbol;
	return left;
}

static int inf_codes( inf_state *s, const inf_huff *lencode, const inf_huff *distcode )
{
	static const short lens[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
	static const short lext[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
	static const short dists[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
	static const short dext[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
	int symbol, len;
	unsigned dist;

	do {
		symbol = inf_decode( s, lencode );
		if ( symbol < 0 )
			return -1;
		if ( symbol < 256 ) {
			if ( s->outcnt >= s->outlen )
				return -1;
			s->out[s->outcnt++] = (unsigned char)symbol;
		} else if ( symbol > 256 ) {
			symbol -= 257;
			if ( symbol >= 29 )
				return -1;
			len = lens[symbol] + inf_bits( s, lext[symbol] );
			symbol = inf_decode( s, distcode );
			if ( symbol < 0 || symbol >= 30 )
				return -1;
			dist = (unsigned)( dists[symbol] + inf_bits( s, dext[symbol] ) );
			if ( s->err || dist > s->outcnt || s->outcnt + (size_t)len > s->outlen )
				return -1;
			while ( len-- ) {
				s->out[s->outcnt] = s->out[s->outcnt - dist];
				s->outcnt++;
			}
		}
	} while ( symbol != 256 );
	return 0;
}

static int inf_fixed( inf_state *s )
{
	static int virgin = 1;
	static inf_huff lencode, distcode;

	if ( virgin ) {
		short lengths[LC_FIXLCODES];
		int symbol;
		for ( symbol = 0; symbol < 144; symbol++ ) lengths[symbol] = 8;
		for ( ; symbol < 256; symbol++ ) lengths[symbol] = 9;
		for ( ; symbol < 280; symbol++ ) lengths[symbol] = 7;
		for ( ; symbol < LC_FIXLCODES; symbol++ ) lengths[symbol] = 8;
		inf_construct( &lencode, lengths, LC_FIXLCODES );
		for ( symbol = 0; symbol < LC_MAXDCODES; symbol++ ) lengths[symbol] = 5;
		inf_construct( &distcode, lengths, LC_MAXDCODES );
		virgin = 0;
	}
	return inf_codes( s, &lencode, &distcode );
}

static int inf_dynamic( inf_state *s )
{
	static const short order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
	int nlen, ndist, ncode, index, err;
	short lengths[LC_MAXLCODES + LC_MAXDCODES];
	inf_huff lencode, distcode;

	nlen = inf_bits( s, 5 ) + 257;
	ndist = inf_bits( s, 5 ) + 1;
	ncode = inf_bits( s, 4 ) + 4;
	if ( s->err || nlen > LC_MAXLCODES || ndist > LC_MAXDCODES )
		return -1;
	for ( index = 0; index < ncode; index++ )
		lengths[order[index]] = (short)inf_bits( s, 3 );
	for ( ; index < 19; index++ )
		lengths[order[index]] = 0;
	err = inf_construct( &lencode, lengths, 19 );
	if ( err != 0 )
		return -1;
	index = 0;
	while ( index < nlen + ndist ) {
		int symbol, len;
		symbol = inf_decode( s, &lencode );
		if ( symbol < 0 )
			return -1;
		if ( symbol < 16 )
			lengths[index++] = (short)symbol;
		else {
			len = 0;
			if ( symbol == 16 ) {
				if ( index == 0 )
					return -1;
				len = lengths[index - 1];
				symbol = 3 + inf_bits( s, 2 );
			} else if ( symbol == 17 )
				symbol = 3 + inf_bits( s, 3 );
			else
				symbol = 11 + inf_bits( s, 7 );
			if ( index + symbol > nlen + ndist )
				return -1;
			while ( symbol-- )
				lengths[index++] = (short)len;
		}
	}
	if ( lengths[256] == 0 )
		return -1;
	err = inf_construct( &lencode, lengths, nlen );
	if ( err && ( err < 0 || nlen != lencode.count[0] + lencode.count[1] ) )
		return -1;
	err = inf_construct( &distcode, lengths + nlen, ndist );
	if ( err && ( err < 0 || ndist != distcode.count[0] + distcode.count[1] ) )
		return -1;
	return inf_codes( s, &lencode, &distcode );
}

/* returns the number of bytes written to out, or -1 */
long lc_inflate_raw( const unsigned char *in, size_t inLen, unsigned char *out, size_t outLen )
{
	inf_state s;
	int last, type, err = 0;

	memset( &s, 0, sizeof( s ) );
	s.out = out;
	s.outlen = outLen;
	s.in = in;
	s.inlen = inLen;
	do {
		last = inf_bits( &s, 1 );
		type = inf_bits( &s, 2 );
		if ( s.err )
			return -1;
		if ( type == 0 ) {
			unsigned len, nlen;
			s.bitbuf = 0;
			s.bitcnt = 0;
			if ( s.incnt + 4 > s.inlen )
				return -1;
			len = s.in[s.incnt++];
			len |= (unsigned)s.in[s.incnt++] << 8;
			nlen = s.in[s.incnt++];
			nlen |= (unsigned)s.in[s.incnt++] << 8;
			if ( len != ( ~nlen & 0xffff ) )
				return -1;
			if ( s.incnt + len > s.inlen || s.outcnt + len > s.outlen )
				return -1;
			memcpy( s.out + s.outcnt, s.in + s.incnt, len );
			s.outcnt += len;
			s.incnt += len;
		} else if ( type == 1 )
			err = inf_fixed( &s );
		else if ( type == 2 )
			err = inf_dynamic( &s );
		else
			err = -1;
		if ( err )
			return -1;
	} while ( !last );
	return (long)s.outcnt;
}

int lc_zip_list( const char *path, int (*want)( const char *lowerName, void *u ), void *u,
	lc_zip_entry **out, int *count )
{
	FILE *f;
	long long size;
	size_t readLen, i;
	unsigned char *buf, *cd;
	lc_zip_entry *list = NULL;
	int n = 0, cap = 0;
	unsigned cdSize, cdOffset;
	long long tailStart;
	const unsigned char *eocd = NULL;

	*out = NULL;
	*count = 0;
	f = lc_fopen( path, "rb" );
	if ( !f )
		return 0;
	fseek64( f, 0, SEEK_END );
	size = (long long)ftell( f );
#ifdef _WIN32
	size = _ftelli64( f );
#endif
	if ( size < 22 ) {
		fclose( f );
		return 0;
	}
	readLen = (size_t)( size < 65536 + 22 ? size : 65536 + 22 );
	tailStart = size - (long long)readLen;
	buf = (unsigned char *)malloc( readLen );
	if ( !buf || fseek64( f, tailStart, SEEK_SET ) || fread( buf, 1, readLen, f ) != readLen ) {
		free( buf );
		fclose( f );
		return 0;
	}
	for ( i = readLen - 22 + 1; i-- > 0; )
		if ( buf[i] == 0x50 && buf[i + 1] == 0x4b && buf[i + 2] == 5 && buf[i + 3] == 6 ) {
			eocd = buf + i;
			break;
		}
	if ( !eocd ) {
		free( buf );
		fclose( f );
		return 0;
	}
	cdSize = LE32( eocd + 12 );
	cdOffset = LE32( eocd + 16 );
	free( buf );
	if ( (long long)cdOffset + cdSize > size ) {
		fclose( f );
		return 0;
	}
	cd = (unsigned char *)malloc( cdSize ? cdSize : 1 );
	if ( !cd || fseek64( f, cdOffset, SEEK_SET ) || fread( cd, 1, cdSize, f ) != cdSize ) {
		free( cd );
		fclose( f );
		return 0;
	}
	fclose( f );

	for ( i = 0; i + 46 <= cdSize && LE32( cd + i ) == 0x02014b50u; ) {
		unsigned nameLen = LE16( cd + i + 28 ), extra = LE16( cd + i + 30 ), comment = LE16( cd + i + 32 );
		char name[64], lower[64];
		lc_zip_entry *e;

		if ( i + 46 + nameLen > cdSize )
			break;
		if ( nameLen < sizeof( name ) ) {
			memcpy( name, cd + i + 46, nameLen );
			name[nameLen] = 0;
			/* ISO-8859-1 names: keep bytes as they are, entries are matched by ASCII anyway */
			lc_strlcpy( lower, name, sizeof( lower ) );
			lc_lower( lower );
			if ( !want || want( lower, u ) ) {
				if ( n == cap ) {
					cap = cap ? cap * 2 : 32;
					list = (lc_zip_entry *)realloc( list, sizeof( *list ) * (size_t)cap );
				}
				e = &list[n++];
				memset( e, 0, sizeof( *e ) );
				lc_strlcpy( e->name, name, sizeof( e->name ) );
				e->method = LE16( cd + i + 10 );
				e->csize = LE32( cd + i + 20 );
				e->usize = LE32( cd + i + 24 );
				e->offset = LE32( cd + i + 42 );
			}
		}
		i += 46 + nameLen + extra + comment;
	}
	free( cd );
	*out = list;
	*count = n;
	return 1;
}

unsigned char *lc_zip_read( const char *path, const lc_zip_entry *e, size_t *len )
{
	FILE *f;
	unsigned char hdr[30], *comp, *out;
	long long dataStart;

	if ( len )
		*len = 0;
	if ( e->usize > 32u * 1024 * 1024 || ( e->method != 0 && e->method != 8 ) )
		return NULL;
	f = lc_fopen( path, "rb" );
	if ( !f )
		return NULL;
	if ( fseek64( f, e->offset, SEEK_SET ) || fread( hdr, 1, 30, f ) != 30 || LE32( hdr ) != 0x04034b50u ) {
		fclose( f );
		return NULL;
	}
	dataStart = (long long)e->offset + 30 + LE16( hdr + 26 ) + LE16( hdr + 28 );
	comp = (unsigned char *)malloc( e->csize ? e->csize : 1 );
	if ( !comp || fseek64( f, dataStart, SEEK_SET ) || fread( comp, 1, e->csize, f ) != e->csize ) {
		free( comp );
		fclose( f );
		return NULL;
	}
	fclose( f );
	if ( e->method == 0 ) {
		if ( len )
			*len = e->csize;
		return comp;
	}
	out = (unsigned char *)malloc( e->usize ? e->usize : 1 );
	if ( out ) {
		long n = lc_inflate_raw( comp, e->csize, out, e->usize );
		if ( n < 0 ) {
			free( out );
			out = NULL;
		} else if ( len )
			*len = (size_t)n;
	}
	free( comp );
	return out;
}

/* ---- arena files (3.4) ---------------------------------------------------------------- */

/* Quake 3 style tokens; returns the next token in tok (size), or 0 at the end */
static int arena_token( const char **pp, char *tok, size_t size )
{
	const char *p = *pp;
	size_t n = 0;

	for ( ;; ) {
		while ( *p && (unsigned char)*p <= ' ' )
			p++;
		if ( p[0] == '/' && p[1] == '/' ) {
			while ( *p && *p != '\n' )
				p++;
			continue;
		}
		if ( p[0] == '/' && p[1] == '*' ) {
			const char *e = strstr( p + 2, "*/" );
			p = e ? e + 2 : p + strlen( p );
			continue;
		}
		break;
	}
	if ( !*p ) {
		*pp = p;
		return 0;
	}
	if ( *p == '"' ) {
		p++;
		while ( *p && *p != '"' ) {
			if ( n + 1 < size )
				tok[n++] = *p;
			p++;
		}
		if ( *p == '"' )
			p++;
	} else if ( *p == '{' || *p == '}' ) {
		tok[n++] = *p++;
	} else {
		while ( *p && (unsigned char)*p > ' ' && *p != '{' && *p != '}' ) {
			if ( n + 1 < size )
				tok[n++] = *p;
			p++;
		}
	}
	tok[n] = 0;
	*pp = p;
	return 1;
}

void lc_arena_parse( const char *text, void (*block)( const char *map, const char *longname, const char *type, void *u ), void *u )
{
	const char *p = text;
	char tok[512], key[512], val[512];

	while ( arena_token( &p, tok, sizeof( tok ) ) ) {
		char map[256] = "", longname[256] = "", type[256] = "";

		if ( strcmp( tok, "{" ) )
			continue;
		for ( ;; ) {
			if ( !arena_token( &p, key, sizeof( key ) ) || !strcmp( key, "}" ) )
				break;
			if ( !arena_token( &p, val, sizeof( val ) ) )
				break;
			lc_lower( key );
			if ( !strcmp( key, "map" ) )
				lc_strlcpy( map, val, sizeof( map ) );
			else if ( !strcmp( key, "longname" ) )
				lc_strlcpy( longname, val, sizeof( longname ) );
			else if ( !strcmp( key, "type" ) )
				lc_strlcpy( type, val, sizeof( type ) );
		}
		if ( map[0] )
			block( map, longname, type, u );
	}
}

/* ---- map catalog (3) ------------------------------------------------------------------- */

struct lc_map_catalog {
	lc_map_info *maps;
	int count;
	char *stamp;
	struct lc_map_catalog *older;	/* superseded catalogs stay alive: callers may still hold pointers */
};

typedef struct { char key[64]; int idx; } kv_index;

typedef struct {
	lc_map_info *maps; char (*mapKey)[64]; int nMaps, capMaps;
	struct { char key[64], longName[128], types[64]; } *arenas; int nArenas, capArenas;
	struct { char key[64]; char path[1024]; int inZip; lc_zip_entry entry; } *shots; int nShots, capShots;
} scan_t;

static void latin1_to_utf8( const unsigned char *in, size_t n, char *out /* 2n+1 */ )
{
	size_t i, o = 0;
	for ( i = 0; i < n; i++ ) {
		if ( in[i] < 0x80 )
			out[o++] = (char)in[i];
		else {
			out[o++] = (char)( 0xc0 | ( in[i] >> 6 ) );
			out[o++] = (char)( 0x80 | ( in[i] & 0x3f ) );
		}
	}
	out[o] = 0;
}

/* NUL-terminated UTF-8 text (Latin-1 fallback) from raw bytes; free() it */
static char *decode_text( const unsigned char *b, size_t n )
{
	char *t = (char *)malloc( n * 2 + 1 );
	if ( !t )
		return NULL;
	memcpy( t, b, n );
	t[n] = 0;
	if ( !lc_utf8_valid( t ) )
		latin1_to_utf8( b, n, t );
	return t;
}

typedef struct { scan_t *sc; } arena_ctx;

static void arena_block( const char *map, const char *longname, const char *type, void *u )
{
	scan_t *sc = ( (arena_ctx *)u )->sc;
	char key[64];
	int i;

	lc_strlcpy( key, map, sizeof( key ) );
	lc_lower( key );
	for ( i = 0; i < sc->nArenas; i++ )
		if ( !strcmp( sc->arenas[i].key, key ) )
			break;
	if ( i == sc->nArenas ) {
		if ( sc->nArenas == sc->capArenas ) {
			sc->capArenas = sc->capArenas ? sc->capArenas * 2 : 64;
			sc->arenas = realloc( sc->arenas, sizeof( *sc->arenas ) * (size_t)sc->capArenas );
		}
		sc->nArenas++;
	}
	lc_strlcpy( sc->arenas[i].key, key, sizeof( sc->arenas[i].key ) );
	lc_strlcpy( sc->arenas[i].longName, longname, sizeof( sc->arenas[i].longName ) );
	lc_strlcpy( sc->arenas[i].types, type, sizeof( sc->arenas[i].types ) );
}

static void scan_parse_arena_text( scan_t *sc, const unsigned char *bytes, size_t n )
{
	char *text = decode_text( bytes, n );
	arena_ctx c;
	if ( !text )
		return;
	c.sc = sc;
	lc_arena_parse( text, arena_block, &c );
	free( text );
}

static void scan_add_map( scan_t *sc, const char *name, const char *pk3, int retail, int expansion )
{
	char key[64];
	int i;

	lc_strlcpy( key, name, sizeof( key ) );
	lc_lower( key );
	for ( i = 0; i < sc->nMaps; i++ )
		if ( !strcmp( sc->mapKey[i], key ) )
			break;
	if ( i == sc->nMaps ) {
		if ( sc->nMaps == sc->capMaps ) {
			sc->capMaps = sc->capMaps ? sc->capMaps * 2 : 128;
			sc->maps = (lc_map_info *)realloc( sc->maps, sizeof( *sc->maps ) * (size_t)sc->capMaps );
			sc->mapKey = realloc( sc->mapKey, sizeof( *sc->mapKey ) * (size_t)sc->capMaps );
		}
		sc->nMaps++;
	}
	memset( &sc->maps[i], 0, sizeof( sc->maps[i] ) );
	lc_strlcpy( sc->mapKey[i], key, sizeof( sc->mapKey[i] ) );
	lc_strlcpy( sc->maps[i].name, name, sizeof( sc->maps[i].name ) );
	lc_strlcpy( sc->maps[i].pk3, pk3, sizeof( sc->maps[i].pk3 ) );
	sc->maps[i].retail = retail;
	sc->maps[i].expansion = expansion;
}

static void scan_add_shot( scan_t *sc, const char *base, const char *path, int inZip, const lc_zip_entry *e, int isTga, int loose )
{
	char key[64];
	int i;

	lc_strlcpy( key, base, sizeof( key ) );
	lc_lower( key );
	for ( i = 0; i < sc->nShots; i++ )
		if ( !strcmp( sc->shots[i].key, key ) )
			break;
	if ( i < sc->nShots && !loose && isTga ) {
		/* a pk3 .tga never replaces an existing shot */
		return;
	}
	if ( i == sc->nShots ) {
		if ( sc->nShots == sc->capShots ) {
			sc->capShots = sc->capShots ? sc->capShots * 2 : 128;
			sc->shots = realloc( sc->shots, sizeof( *sc->shots ) * (size_t)sc->capShots );
		}
		sc->nShots++;
	}
	memset( &sc->shots[i], 0, sizeof( sc->shots[i] ) );
	lc_strlcpy( sc->shots[i].key, key, sizeof( sc->shots[i].key ) );
	lc_strlcpy( sc->shots[i].path, path, sizeof( sc->shots[i].path ) );
	sc->shots[i].inZip = inZip;
	if ( e )
		sc->shots[i].entry = *e;
}

/* is lowerName one of the pk3 entries the catalog wants? */
static int want_entry( const char *l, void *u )
{
	size_t n = strlen( l );
	(void)u;
	if ( !strncmp( l, "maps/", 5 ) )
		return n > 9 && !strcmp( l + n - 4, ".bsp" ) && !strchr( l + 5, '/' );
	if ( !strncmp( l, "scripts/", 8 ) )
		return !strcmp( l, "scripts/arenas.txt" ) || ( n > 14 && !strcmp( l + n - 6, ".arena" ) );
	if ( !strncmp( l, "levelshots/", 11 ) )
		return n > 15 && ( !strcmp( l + n - 4, ".jpg" ) || !strcmp( l + n - 4, ".tga" ) );
	return 0;
}

typedef struct { char (*names)[256]; int n, cap; } name_list;

static int collect_name( const char *name, int isDir, void *user )
{
	name_list *l = (name_list *)user;
	(void)isDir;
	if ( l->n == l->cap ) {
		l->cap = l->cap ? l->cap * 2 : 64;
		l->names = realloc( l->names, sizeof( *l->names ) * (size_t)l->cap );
	}
	lc_strlcpy( l->names[l->n++], name, 256 );
	return 0;
}

static int cmp_names_ci( const void *a, const void *b )
{
	return lc_stricmp( (const char *)a, (const char *)b );
}

static void list_sorted( const char *dir, name_list *l )
{
	memset( l, 0, sizeof( *l ) );
	lc_list_dir( dir, collect_name, l );
	if ( l->n > 1 )
		qsort( l->names, (size_t)l->n, sizeof( l->names[0] ), cmp_names_ci );
}

static void scan_directory( scan_t *sc, const char *dir )
{
	name_list files;
	int i, j;
	char sub[LC_MAX_PATH], path[LC_MAX_PATH];

	list_sorted( dir, &files );
	for ( i = 0; i < files.n; i++ ) {
		const char *fn = files.names[i];
		char base[256], lbase[256];
		int retail, expansion;
		lc_zip_entry *ents;
		int nEnts;

		if ( fn[0] == '.' || !lc_ci_ext( fn, ".pk3" ) )
			continue;
		lc_path_join( path, sizeof( path ), dir, fn );
		if ( !lc_is_file( path ) )
			continue;
		lc_strlcpy( base, fn, sizeof( base ) );
		base[strlen( base ) - 4] = 0;
		lc_strlcpy( lbase, base, sizeof( lbase ) );
		lc_lower( lbase );
		retail = ( lbase[0] == 'p' && lbase[1] == 'a' && lbase[2] == 'k' && lbase[3] >= '0' && lbase[3] <= '3' && !lbase[4] );
		expansion = !strcmp( lbase, "pak3" );
		if ( !lc_zip_list( path, want_entry, NULL, &ents, &nEnts ) )
			continue;
		for ( j = 0; j < nEnts; j++ ) {
			lc_zip_entry *e = &ents[j];
			char lower[64];
			size_t n;

			lc_strlcpy( lower, e->name, sizeof( lower ) );
			lc_lower( lower );
			n = strlen( lower );
			if ( !strncmp( lower, "maps/", 5 ) ) {
				char mname[64];
				lc_strlcpy( mname, e->name + 5, sizeof( mname ) );
				mname[strlen( mname ) - 4] = 0;
				scan_add_map( sc, mname, fn, retail, expansion );
			} else if ( !strncmp( lower, "scripts/", 8 ) ) {
				size_t len;
				unsigned char *b = lc_zip_read( path, e, &len );
				if ( b ) {
					scan_parse_arena_text( sc, b, len );
					free( b );
				}
			} else if ( !strncmp( lower, "levelshots/", 11 ) ) {
				char sbase[64];
				lc_strlcpy( sbase, e->name + 11, sizeof( sbase ) );
				sbase[strlen( sbase ) - 4] = 0;
				scan_add_shot( sc, sbase, path, 1, e, !strcmp( lower + n - 4, ".tga" ), 0 );
			}
		}
		free( ents );
	}
	free( files.names );

	/* loose files */
	if ( lc_child_ci( dir, "maps", sub, sizeof( sub ) ) && lc_is_dir( sub ) ) {
		list_sorted( sub, &files );
		for ( i = 0; i < files.n; i++ )
			if ( files.names[i][0] != '.' && lc_ci_ext( files.names[i], ".bsp" ) ) {
				char mname[64];
				lc_strlcpy( mname, files.names[i], sizeof( mname ) );
				mname[strlen( mname ) - 4] = 0;
				scan_add_map( sc, mname, "", 0, 0 );
			}
		free( files.names );
	}
	if ( lc_child_ci( dir, "scripts", sub, sizeof( sub ) ) && lc_is_dir( sub ) ) {
		list_sorted( sub, &files );
		for ( i = 0; i < files.n; i++ )
			if ( files.names[i][0] != '.' && ( lc_ci_ext( files.names[i], ".arena" ) || !lc_stricmp( files.names[i], "arenas.txt" ) ) ) {
				size_t len;
				char *b;
				lc_path_join( path, sizeof( path ), sub, files.names[i] );
				b = lc_read_file( path, &len );
				if ( b ) {
					scan_parse_arena_text( sc, (unsigned char *)b, len );
					free( b );
				}
			}
		free( files.names );
	}
	if ( lc_child_ci( dir, "levelshots", sub, sizeof( sub ) ) && lc_is_dir( sub ) ) {
		list_sorted( sub, &files );
		for ( i = 0; i < files.n; i++ )
			if ( files.names[i][0] != '.' && ( lc_ci_ext( files.names[i], ".jpg" ) || lc_ci_ext( files.names[i], ".tga" ) ) ) {
				char sbase[64];
				lc_path_join( path, sizeof( path ), sub, files.names[i] );
				lc_strlcpy( sbase, files.names[i], sizeof( sbase ) );
				sbase[strlen( sbase ) - 4] = 0;
				scan_add_shot( sc, sbase, path, 0, NULL, 0, 1 );
			}
		free( files.names );
	}
}

static int cmp_maps( const void *a, const void *b )
{
	const lc_map_info *x = (const lc_map_info *)a, *y = (const lc_map_info *)b;
	int c = lc_stricmp( lc_map_display_name( x ), lc_map_display_name( y ) );
	return c ? c : lc_stricmp( x->name, y->name );
}

static void stamp_dir( lc_sb *sb, const char *dir )
{
	name_list l;
	int i;
	char path[LC_MAX_PATH], sub[LC_MAX_PATH];
	static const char *const subs[] = { "maps", "scripts", "levelshots" };
	size_t k;

	list_sorted( dir, &l );
	for ( i = 0; i < l.n; i++ ) {
		lc_path_join( path, sizeof( path ), dir, l.names[i] );
		lc_sb_addf( sb, "%s:%lld:%lld|", path, (long long)lc_file_size( path ), (long long)lc_file_mtime( path ) );
	}
	free( l.names );
	for ( k = 0; k < sizeof( subs ) / sizeof( subs[0] ); k++ ) {
		if ( !lc_child_ci( dir, subs[k], sub, sizeof( sub ) ) || !lc_is_dir( sub ) )
			continue;
		list_sorted( sub, &l );
		for ( i = 0; i < l.n; i++ ) {
			lc_path_join( path, sizeof( path ), sub, l.names[i] );
			lc_sb_addf( sb, "%s:%lld:%lld|", path, (long long)lc_file_size( path ), (long long)lc_file_mtime( path ) );
		}
		free( l.names );
	}
}

static int same_dir( const char *a, const char *b )
{
	char x[LC_MAX_PATH], y[LC_MAX_PATH];
	size_t n;
	lc_strlcpy( x, a, sizeof( x ) );
	lc_strlcpy( y, b, sizeof( y ) );
	for ( n = strlen( x ); n > 1 && ( x[n - 1] == '/' || x[n - 1] == '\\' ); n-- )
		x[n - 1] = 0;
	for ( n = strlen( y ); n > 1 && ( y[n - 1] == '/' || y[n - 1] == '\\' ); n-- )
		y[n - 1] = 0;
#ifdef _WIN32
	return !lc_stricmp( x, y );
#else
	return !strcmp( x, y );
#endif
}

static lc_mutex *catalog_mutex( void )
{
	static lc_mutex *m;
	if ( !m )
		m = lc_mutex_new( );
	return m;
}

static struct lc_map_catalog *g_catalog;

const lc_map_catalog *lc_map_catalog_get( const char *dataFolder, const char *homeBase )
{
	lc_sb stamp = { NULL, 0, 0 };
	const char *dirs[2];
	int nDirs = 0, i;
	struct lc_map_catalog *c;
	lc_mutex *mu = catalog_mutex( );

	if ( dataFolder && *dataFolder )
		dirs[nDirs++] = dataFolder;
	if ( homeBase && *homeBase && ( nDirs == 0 || !same_dir( dataFolder, homeBase ) ) )
		dirs[nDirs++] = homeBase;
	for ( i = 0; i < nDirs; i++ ) {
		lc_sb_addf( &stamp, "[%s]", dirs[i] );
		stamp_dir( &stamp, dirs[i] );
	}
	if ( !stamp.p )
		lc_sb_add( &stamp, "" );

	lc_mutex_lock( mu );
	if ( g_catalog && !strcmp( g_catalog->stamp, stamp.p ) ) {
		free( stamp.p );
		lc_mutex_unlock( mu );
		return g_catalog;
	}
	{
		scan_t sc;
		double t0 = lc_now( );
		int k, a;

		memset( &sc, 0, sizeof( sc ) );
		for ( i = 0; i < nDirs; i++ )
			scan_directory( &sc, dirs[i] );
		c = (struct lc_map_catalog *)calloc( 1, sizeof( *c ) );
		c->maps = (lc_map_info *)calloc( (size_t)( sc.nMaps ? sc.nMaps : 1 ), sizeof( lc_map_info ) );
		for ( k = 0; k < sc.nMaps; k++ ) {
			lc_map_info *m = &sc.maps[k];
			int found = 0, s;
			for ( a = 0; a < sc.nArenas; a++ )
				if ( !strcmp( sc.arenas[a].key, sc.mapKey[k] ) ) {
					found = 1;
					break;
				}
			if ( !found && ( m->retail || m->expansion ) )
				continue;
			if ( found ) {
				lc_strip_colors( sc.arenas[a].longName, m->longName, sizeof( m->longName ) );
				lc_trim( m->longName );
				lc_strlcpy( m->types, sc.arenas[a].types, sizeof( m->types ) );
				lc_lower( m->types );
				lc_trim( m->types );
			}
			for ( s = 0; s < sc.nShots; s++ )
				if ( !strcmp( sc.shots[s].key, sc.mapKey[k] ) ) {
					lc_strlcpy( m->shotPath, sc.shots[s].path, sizeof( m->shotPath ) );
					m->shotInZip = sc.shots[s].inZip;
					m->shotEntry = sc.shots[s].entry;
					break;
				}
			c->maps[c->count++] = *m;
		}
		if ( c->count > 1 )
			qsort( c->maps, (size_t)c->count, sizeof( lc_map_info ), cmp_maps );
		free( sc.maps );
		free( sc.mapKey );
		free( sc.arenas );
		free( sc.shots );
		lc_log( "Server: %d Holomatch maps in the game files (%d ms)", c->count, (int)( ( lc_now( ) - t0 ) * 1000 ) );
	}
	c->stamp = stamp.p;
	c->older = g_catalog;
	g_catalog = c;
	lc_mutex_unlock( mu );
	return c;
}

int lc_map_catalog_count( const lc_map_catalog *c )
{
	return c ? c->count : 0;
}

const lc_map_info *lc_map_catalog_at( const lc_map_catalog *c, int i )
{
	return c && i >= 0 && i < c->count ? &c->maps[i] : NULL;
}

const lc_map_info *lc_map_find( const lc_map_catalog *c, const char *name )
{
	int i;
	if ( !c || !name )
		return NULL;
	for ( i = 0; i < c->count; i++ )
		if ( !lc_stricmp( c->maps[i].name, name ) )
			return &c->maps[i];
	return NULL;
}

const char *lc_map_display_name( const lc_map_info *m )
{
	return m->longName[0] ? m->longName : m->name;
}

static int types_has( const char *types, const char *word )
{
	size_t n = strlen( word );
	const char *p = types;

	while ( *p ) {
		const char *e;
		while ( *p == ' ' || *p == '\t' )
			p++;
		e = p;
		while ( *e && *e != ' ' && *e != '\t' )
			e++;
		if ( (size_t)( e - p ) == n && !strncmp( p, word, n ) )
			return 1;
		p = e;
	}
	return 0;
}

int lc_map_supports_gametype( const lc_map_info *m, int gametype )
{
	const char *want;
	if ( !m->types[0] )
		return 1;
	want = gametype == LC_GT_TOURNAMENT ? "tourney" : gametype == LC_GT_TEAM ? "team" : gametype == LC_GT_CTF ? "ctf" : "ffa";
	return types_has( m->types, want );
}

void lc_map_types_description( const lc_map_info *m, char *out, size_t n )
{
	static const struct { const char *word, *label; } t[] = { { "ffa", "FFA" }, { "tourney", "Tourney" }, { "team", "Team" }, { "ctf", "CTF" } };
	size_t i;

	if ( !n )
		return;
	out[0] = 0;
	if ( !m->types[0] )
		return;
	for ( i = 0; i < 4; i++ )
		if ( types_has( m->types, t[i].word ) ) {
			if ( out[0] )
				lc_strlcat( out, ", ", n );
			lc_strlcat( out, t[i].label, n );
		}
}

unsigned char *lc_map_levelshot_bytes( const lc_map_info *m, size_t *len, int *isTga )
{
	unsigned char *b;
	const char *nm;

	if ( len )
		*len = 0;
	if ( isTga )
		*isTga = 0;
	if ( !m->shotPath[0] )
		return NULL;
	if ( m->shotInZip ) {
		b = lc_zip_read( m->shotPath, &m->shotEntry, len );
		nm = m->shotEntry.name;
	} else {
		size_t l = 0;
		b = (unsigned char *)lc_read_file( m->shotPath, &l );
		if ( len )
			*len = l;
		nm = m->shotPath;
	}
	if ( b && isTga )
		*isTga = lc_ci_ext( nm, ".tga" );
	return b;
}

int lc_is_expansion_map( const char *lowerName )
{
	static const char *const list[] = {
		"ctf_breach", "ctf_dn1", "ctf_for1", "ctf_neptune", "ctf_oldwest", "ctf_reservoir", "ctf_singularity",
		"ctf_spyglass2", "ctf_stasis", "hm_altar", "hm_blastradius", "hm_borgattack", "hm_for2", "hm_raven",
		"hm_temple", "hm_voy3" };
	size_t i;
	char l[64];

	if ( !lowerName )
		return 0;
	lc_strlcpy( l, lowerName, sizeof( l ) );
	lc_lower( l );
	for ( i = 0; i < sizeof( list ) / sizeof( list[0] ); i++ )
		if ( !strcmp( l, list[i] ) )
			return 1;
	return 0;
}

/* ---- masters (6) ------------------------------------------------------------------------ */

const char *const lc_default_masters[5] = {
	"master.stef1.daggolin.de", "efmaster.tjps.eu", "master.stvef.org", "master.stef1.ravensoft.com", "ipv6.tjps.eu" };
const char *const lc_original_masters[2] = { "master.stef1.ravensoft.com", "efmaster.tjps.eu" };

void lc_clean_master( const char *in, char out[65] )
{
	char tmp[1024];
	size_t n = 0;
	const char *p;

	lc_strlcpy( tmp, in ? in : "", sizeof( tmp ) );
	lc_trim( tmp );
	for ( p = tmp; *p && n < 64; p++ ) {
		int c = tolower( (unsigned char)*p );
		if ( ( c >= 'a' && c <= 'z' ) || ( c >= '0' && c <= '9' ) || c == '.' || c == '-' || c == ':' || c == '[' || c == ']' || c == '_' )
			out[n++] = (char)c;
	}
	out[n] = 0;
}

int lc_clean_masters( const char *const *in, int n, char out[][65] )
{
	int i, j, count = 0;

	for ( i = 0; i < n && count < LC_MAX_MASTERS; i++ ) {
		char m[65];
		lc_clean_master( in[i], m );
		if ( !m[0] )
			continue;
		for ( j = 0; j < count; j++ )
			if ( !strcmp( out[j], m ) )
				break;
		if ( j < count )
			continue;
		memcpy( out[count++], m, 65 );
	}
	return count;
}

int lc_masters_load( lc_store *st, char out[][65] )
{
	if ( lc_store_has_list( st, "masters" ) ) {
		char raw[LC_MAX_MASTERS * 4][256];
		const char *ptrs[LC_MAX_MASTERS * 4];
		int n = lc_store_get_list( st, "masters", raw[0], sizeof( raw[0] ), LC_MAX_MASTERS * 4 ), i;
		for ( i = 0; i < n; i++ )
			ptrs[i] = raw[i];
		return lc_clean_masters( ptrs, n, out );
	} else {
		int i;
		for ( i = 0; i < 5; i++ )
			lc_clean_master( lc_default_masters[i], out[i] );
		return 5;
	}
}

void lc_masters_save( lc_store *st, const char (*list)[65], int n )
{
	char clean[LC_MAX_MASTERS][65];
	const char *ptrs[LC_MAX_MASTERS * 4];
	int i, count, isDefault;
	char joined[2048] = "";

	if ( !list ) {
		lc_store_remove_list( st, "masters" );
		lc_store_save( st );
		return;
	}
	if ( n > LC_MAX_MASTERS * 4 )
		n = LC_MAX_MASTERS * 4;
	for ( i = 0; i < n; i++ )
		ptrs[i] = list[i];
	count = lc_clean_masters( ptrs, n, clean );
	isDefault = count == 5;
	for ( i = 0; isDefault && i < 5; i++ )
		if ( strcmp( clean[i], lc_default_masters[i] ) )
			isDefault = 0;
	if ( isDefault )
		lc_store_remove_list( st, "masters" );
	else
		lc_store_set_list( st, "masters", clean[0], sizeof( clean[0] ), count );
	lc_store_save( st );
	for ( i = 0; i < count; i++ ) {
		if ( i )
			lc_strlcat( joined, ", ", sizeof( joined ) );
		lc_strlcat( joined, clean[i], sizeof( joined ) );
	}
	lc_log( "Master servers: %s", joined );
}

static void masters_block( lc_sb *sb, const char (*masters)[65], int count, int blank )
{
	int i;
	for ( i = 0; i < LC_MAX_MASTERS; i++ )
		lc_sb_addf( sb, "set sv_master%d \"%s\"\n", i + 1, !blank && masters && i < count ? masters[i] : "" );
}

void lc_masters_cfg_text( const char (*masters)[65], int count, int blank, char **outText )
{
	lc_sb sb = { NULL, 0, 0 };
	masters_block( &sb, masters, count, blank );
	*outText = sb.p;
}

int lc_masters_write_cfg( const char *homeBase, const char (*list)[65], int n, int blank )
{
	lc_sb sb = { NULL, 0, 0 };
	char path[LC_MAX_PATH];
	int ok;

	lc_sb_add( &sb, "// Written by the Elite Force launcher at each start: its Servers\xe2\x80\xa6 list.\n"
		"// sv_master1-5 are Holomatch's Internet 1-5. Edits here are overwritten.\n\n" );
	masters_block( &sb, list, n, blank );
	lc_mkdir_p( homeBase );
	lc_path_join( path, sizeof( path ), homeBase, "vsp_masters.cfg" );
	ok = lc_write_file_atomic( path, sb.p, sb.len );
	if ( !ok )
		lc_log( "Couldn't write %s", path );
	free( sb.p );
	return ok;
}

/* ---- vsp_server.cfg (5.1) ------------------------------------------------------------------- */

int lc_server_cfg_text( const lc_server_settings *s, const lc_map_catalog *c, const char (*masters)[65], int masterCount,
	int blankMasters, char **outText, char *err, size_t errSize, void (*log)( const char *line, void *u ), void *u )
{
	char rot[64][64];
	int nRot = 0, i;
	lc_sb sb = { NULL, 0, 0 };
	char line[256];

	*outText = NULL;
	for ( i = 0; i < s->rotationCount && i < 64; i++ ) {
		const lc_map_info *m = lc_map_find( c, s->rotation[i] );
		if ( m )
			lc_strlcpy( rot[nRot++], m->name, sizeof( rot[0] ) );
		else if ( log ) {
			lc_snprintf( line, sizeof( line ), "Server: map %s is not in the game files, skipped", s->rotation[i] );
			log( line, u );
		}
	}
	if ( !nRot ) {
		for ( i = 0; i < lc_map_catalog_count( c ); i++ ) {
			const lc_map_info *m = lc_map_catalog_at( c, i );
			if ( lc_map_supports_gametype( m, s->gametype ) ) {
				lc_strlcpy( rot[nRot++], m->name, sizeof( rot[0] ) );
				break;
			}
		}
		if ( !nRot ) {
			if ( err )
				lc_strlcpy( err, "There are no Holomatch maps in your game files.", errSize );
			return 0;
		}
		if ( log ) {
			lc_snprintf( line, sizeof( line ), "Server: no maps in the rotation, using %s", rot[0] );
			log( line, u );
		}
	}

	lc_sb_add( &sb, "// Written by the Elite Force launcher (Host a Server) at each start.\n"
		"// Change these settings in the launcher's Config\xe2\x80\xa6 sheet; edits here are overwritten.\n\n" );
	lc_sb_addf( &sb, "set sv_hostname \"%s\"\n", s->hostname );
	lc_sb_addf( &sb, "set g_password \"%s\"\n", s->password );
	lc_sb_addf( &sb, "set sv_maxclients %d\n", s->maxPlayers );
	lc_sb_addf( &sb, "set sv_allowDownload %d\n", s->allowDownload ? 1 : 0 );
	lc_sb_addf( &sb, "set sv_pure %d\n", s->pure ? 1 : 0 );
	if ( s->tickRate == 40 )
		lc_sb_add( &sb, "set sv_fps 40\n" );
	lc_sb_add( &sb, "\n" );
	lc_sb_add( &sb, "// cMod's game code (by Noah Metzger) and its lag compensation\n" );
	lc_sb_addf( &sb, "set sv_cmodGame %d\n", s->cmodGame ? 1 : 0 );
	lc_sb_addf( &sb, "set g_unlagged %d\n", s->unlagged );
	lc_sb_addf( &sb, "set g_smoothClients %d\n", s->smoothClients ? 1 : 0 );
	lc_sb_addf( &sb, "set g_unlaggedPredict %d\n\n", s->unlaggedPredict ? 1 : 0 );
	lc_sb_addf( &sb, "set g_gametype %d\n", s->gametype );
	lc_sb_addf( &sb, "set g_pModAssimilation %d\n", s->mods[LC_MOD_ASSIMILATION] ? 1 : 0 );
	lc_sb_addf( &sb, "set g_pModDisintegration %d\n", s->mods[LC_MOD_DISINTEGRATION] ? 1 : 0 );
	lc_sb_addf( &sb, "set g_pModActionHero %d\n", s->mods[LC_MOD_ACTIONHERO] ? 1 : 0 );
	lc_sb_addf( &sb, "set g_pModSpecialties %d\n", s->mods[LC_MOD_SPECIALTIES] ? 1 : 0 );
	lc_sb_addf( &sb, "set g_pModElimination %d\n", s->mods[LC_MOD_ELIMINATION] ? 1 : 0 );
	lc_sb_addf( &sb, "set fraglimit %d\n", s->fraglimit );
	lc_sb_addf( &sb, "set capturelimit %d\n", s->capturelimit );
	lc_sb_addf( &sb, "set timelimit %d\n", s->timelimit );
	lc_sb_addf( &sb, "set g_friendlyFire %d\n\n", s->friendlyFire ? 1 : 0 );
	lc_sb_add( &sb, "// bots fill the game up to this many players (per team in team games)\n" );
	lc_sb_add( &sb, "set bot_enable 1\n" );
	lc_sb_addf( &sb, "set bot_minplayers %d\n", s->bots );
	lc_sb_addf( &sb, "set g_spSkill %d\n\n", s->botSkill );
	lc_sb_add( &sb, "// the master servers it announces itself to (the launcher's Servers\xe2\x80\xa6 sheet)\n" );
	masters_block( &sb, masters, masterCount, blankMasters );
	lc_sb_add( &sb, "\n// the map rotation\n" );
	for ( i = 0; i < nRot; i++ )
		lc_sb_addf( &sb, "set vsp_map%d \"map %s ; set nextmap vstr vsp_map%d\"\n", i + 1, rot[i], i + 1 < nRot ? i + 2 : 1 );
	lc_sb_add( &sb, "vstr vsp_map1\n" );
	*outText = sb.p;
	return 1;
}
