// coop_ping.cpp -- single-player co-op: the cgame's "look here" pings.
//
// A player double-taps Use with nothing to use (coop_use.cpp, on the host) and the host sends
// "cooping <player> <x> <y> <z> <name>" to both players' cgames. Each shows it for COOP_PING_TIME:
// a flare at the spot in the world, a marker with the name over it on screen (also through walls),
// an arrow at the edge of the screen while the spot is off screen, and the communicator chirp.
// One ping per player at a time; a new one replaces the old.

#include "../cgame/cg_local.h"
#include "../cgame/FX_Public.h"
#include "../game/coop_public.h"

#define COOP_PING_TIME	4000	// ms a ping shows
#define COOP_PING_FADE	1000	// ms of fading out at the end
#define COOP_PING_EDGE	28		// the arrow's distance from the screen's edge (640x480)

typedef struct
{
	int		time;		// cg.time it came, 0: none
	vec3_t	origin;
	char	name[36];
} coopPing_t;

static coopPing_t	coop_pings[2];

static const float *COOP_PingColor( int who )
{
	static const vec4_t colors[2] = { { 1.0f, 0.8f, 0.2f, 1.0f }, { 0.3f, 0.85f, 1.0f, 1.0f } };
	return colors[who & 1];
}

void COOP_PingCommand( void )
{
	int			who = atoi( CG_Argv( 1 ) ) & 1;
	coopPing_t	*p = &coop_pings[who];
	vec3_t		rgb;

	p->origin[0] = atof( CG_Argv( 2 ) );
	p->origin[1] = atof( CG_Argv( 3 ) );
	p->origin[2] = atof( CG_Argv( 4 ) );
	Q_strncpyz( p->name, CG_Argv( 5 ), sizeof( p->name ) );
	p->time = cg.time ? cg.time : 1;

	VectorCopy( COOP_PingColor( who ), rgb );
	FX_AddSprite( p->origin, NULL, NULL, 24.0f, 0.0f, 1.0f, 0.2f, rgb, rgb, 0.0f, 0.0f, COOP_PING_TIME,
		cgs.media.yellowParticleShader );
	FX_AddSprite( p->origin, NULL, NULL, 4.0f, 60.0f, 1.0f, 0.0f, rgb, rgb, 0.0f, 0.0f, 600,
		cgs.media.whiteRingShader );
	cgi_S_StartLocalSound( cgs.media.talkSound, CHAN_LOCAL_SOUND );
}

// a square of side s centred on (x, y), 640x480
static void COOP_Dot( float x, float y, float s, const float *color )
{
	CG_FillRect( x - s / 2, y - s / 2, s, s, color );
}

static void COOP_DrawPing( const coopPing_t *p, int who )
{
	vec3_t	local;
	vec4_t	color;
	float	f, r, u, sx, sy;
	int		age = cg.time - p->time;
	qboolean onScreen;

	Vector4Copy( COOP_PingColor( who ), color );
	if ( age > COOP_PING_TIME - COOP_PING_FADE )
	{
		color[3] = (float)( COOP_PING_TIME - age ) / COOP_PING_FADE;
	}

	// into the view: forward, right, up
	VectorSubtract( p->origin, cg.refdef.vieworg, local );
	f = DotProduct( local, cg.refdef.viewaxis[0] );
	r = -DotProduct( local, cg.refdef.viewaxis[1] );
	u = DotProduct( local, cg.refdef.viewaxis[2] );

	onScreen = qfalse;
	sx = sy = 0;
	if ( f > 1.0f )
	{
		float tx = tan( cg.refdef.fov_x * ( M_PI / 360.0f ) );
		float ty = tan( cg.refdef.fov_y * ( M_PI / 360.0f ) );
		sx = 320.0f + 320.0f * r / ( f * tx );
		sy = 240.0f - 240.0f * u / ( f * ty );
		onScreen = (qboolean)( sx > 12 && sx < 628 && sy > 12 && sy < 468 );
	}

	if ( onScreen )
	{
		// a diamond over the spot, pulsing, with the name above it
		float pulse = 1.0f + 0.25f * sin( age * 0.012f );
		for ( int i = -4; i <= 4; i++ )
		{
			float w = ( 5 - abs( i ) ) * 2.0f * pulse;
			CG_FillRect( sx - w / 2, sy + i * 2.0f * pulse - 1, w, 2.0f * pulse, color );
		}
		CG_DrawProportionalString( (int)sx, (int)sy - 30, p->name, UI_CENTER | UI_TINYFONT, color );
		return;
	}

	// off screen: an arrow on the screen's edge, pointing the way to turn
	float dx = r, dy = -u;
	if ( f <= 1.0f && fabs( dx ) < 1.0f && fabs( dy ) < 1.0f )
	{
		dy = 1.0f;	// straight behind: point down
	}
	if ( f <= 1.0f )
	{
		// behind the viewer: push the arrow to the side it is on
		if ( fabs( dx ) >= fabs( dy ) * 0.5f )
		{
			dx = dx < 0 ? -1.0f : 1.0f;
			dy = 0;
		}
	}
	float len = sqrt( dx * dx + dy * dy );
	dx /= len;
	dy /= len;
	float hw = 320.0f - COOP_PING_EDGE, hh = 240.0f - COOP_PING_EDGE;
	float t = ( fabs( dx ) * hh > fabs( dy ) * hw ) ? hw / fabs( dx ) : hh / fabs( dy );
	float ax = 320.0f + dx * t, ay = 240.0f + dy * t;

	// a filled arrowhead: rows of dots across the direction, wider further back from the tip
	for ( int k = 0; k <= 8; k++ )
	{
		float bx = ax - dx * k * 2.0f, by = ay - dy * k * 2.0f;
		for ( int j = -k; j <= k; j += 2 )
		{
			COOP_Dot( bx - dy * j * 1.0f, by + dx * j * 1.0f, 3.0f, color );
		}
	}
	// the name, inside the arrow
	float nx = ax - dx * 34.0f, ny = ay - dy * 34.0f - 6.0f;
	CG_DrawProportionalString( (int)nx, (int)ny, p->name, UI_CENTER | UI_TINYFONT, color );
}

// CG_Draw2D (through COOP_DrawStatus)
void COOP_DrawPings( void )
{
	for ( int i = 0; i < 2; i++ )
	{
		coopPing_t *p = &coop_pings[i];
		if ( !p->time )
		{
			continue;
		}
		if ( cg.time < p->time || cg.time - p->time >= COOP_PING_TIME )
		{
			p->time = 0;	// over (or a new level's clock)
			continue;
		}
		COOP_DrawPing( p, i );
	}
}
