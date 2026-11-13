// coop_hud.cpp -- single-player co-op: the cgame's co-op status text (downed, revive progress).
//
// The host's game sends "coopmsg <text>" to one player (coop_down.cpp; '|' separates lines),
// several times a second while it applies; the text goes away soon after the last one. The
// game's own centre print is not drawn in single player, so this has its own spot on screen.

#include "../cgame/cg_local.h"
#include "../game/coop_public.h"

#define COOP_HUD_HOLD	600		// ms a message stays after the last update

static char	coop_hudText[256];
static int	coop_hudTime;

qboolean COOP_ServerCommand( const char *cmd )
{
	if ( strcmp( cmd, "coopmsg" ) )
	{
		return qfalse;
	}
	Q_strncpyz( coop_hudText, CG_Argv( 1 ), sizeof( coop_hudText ) );
	coop_hudTime = cg.time;
	return qtrue;
}

void COOP_DrawStatus( void )
{
	static const vec4_t	color = { 1.0f, 0.85f, 0.4f, 1.0f };
	char				line[256];
	const char			*s = coop_hudText;
	int					y = SCREEN_HEIGHT * 0.3f;

	if ( !coop_hudText[0] || cg.time < coop_hudTime || cg.time - coop_hudTime > COOP_HUD_HOLD )
	{
		return;
	}
	while ( *s )
	{
		int n = 0;
		while ( s[n] && s[n] != '|' && n < (int)sizeof( line ) - 1 )
		{
			line[n] = s[n];
			n++;
		}
		line[n] = 0;
		CG_DrawProportionalString( SCREEN_WIDTH / 2, y, line, UI_CENTER | UI_SMALLFONT, color );
		y += 20;
		s += n;
		if ( *s == '|' )
		{
			s++;
		}
	}
}
