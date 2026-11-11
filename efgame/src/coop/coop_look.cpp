// coop_look.cpp -- single-player co-op: how player 2 looks, and both players' names.
//
// Without a guest, P2 looks like P1 (the same models). A guest chooses male or female Munro in
// the launcher (coop_sex, sent when it connects): P2 then wears P1's outfit in that sex, with its
// voice (pain, death, jump sounds). The guest's replica registers the same models from P2's
// replicated model names (coop_replica.cpp), so both screens agree.
//
// Names are co-op-only state: P2's client is ours, and P1's name for co-op messages comes from
// coop_name. P1's own netname, g_sex and everything the story scripts read stay the host's.

#include "g_local.h"
#include "coop_public.h"
#include "coop_local.h"

extern cvar_t		*g_sex;
extern void			CG_RegisterClientRenderInfo( clientInfo_t *ci, renderInfo_t *ri );
extern sfxHandle_t	cgi_S_RegisterSound( const char *sample );
extern const char	*cg_customBasicSoundNames[MAX_CUSTOM_BASIC_SOUNDS];

// The same outfit as male and female models. A model of P1's that has no partner here becomes
// the other sex's standard uniform (the hazard suit).
static const char *coop_outfits[][2] =
{
	// male					female
	{ "munro/default",		"alexandria/default" },
	{ "munro/default",		"alexa/default" },			// a script's name for her head
	{ "hazard/default",		"hazardfemale/default" },
	{ "munro_lt/default",	"alexandria_lt/default" },
	{ "munroscav/default",	"alexascav/default" },
	{ "imperial/raider",	"impfem/default" },
};

// "munro" and "munro/default" are the same model
static qboolean COOP_SameModel( const char *a, const char *b )
{
	int la = strlen( a ), lb = strlen( b );

	if ( la > 8 && !Q_stricmp( a + la - 8, "/default" ) )
	{
		la -= 8;
	}
	if ( lb > 8 && !Q_stricmp( b + lb - 8, "/default" ) )
	{
		lb -= 8;
	}
	return ( la == lb && !Q_stricmpn( a, b, la ) ) ? qtrue : qfalse;
}

// P1's model for one body part, worn by the other sex
static void COOP_OtherSexModel( char *out, int size, const char *p1Model, qboolean female, const char *standard )
{
	for ( int i = 0; i < (int)( sizeof( coop_outfits ) / sizeof( coop_outfits[0] ) ); i++ )
	{
		if ( COOP_SameModel( p1Model, coop_outfits[i][female ? 0 : 1] ) )
		{
			Q_strncpyz( out, coop_outfits[i][female ? 1 : 0], size );
			return;
		}
	}
	Q_strncpyz( out, standard, size );
}

void COOP_RegisterP2Sounds( clientInfo_t *ci )
{
	// straight from the voice's folder: the cgame's own NPC path would turn "munro" into "alexa"
	// when the host plays Alexandria
	for ( int i = 0; i < MAX_CUSTOM_BASIC_SOUNDS && cg_customBasicSoundNames[i]; i++ )
	{
		ci->sounds[i] = cgi_S_RegisterSound( va( "sound/voice/%s/misc/%s", ci->customBasicSoundDir,
			cg_customBasicSoundNames[i] + 1 ) );
	}
}

/*
COOP_UpdateP2Look

Every frame and at every (re)spawn of P2: the guest's name and Munro, or P1's look without a
guest. Registers new models and sounds only when they change.
*/
void COOP_UpdateP2Look( gentity_t *ent )
{
	gentity_t		*p1 = &g_entities[0];
	gclient_t		*cl = ent->client;
	renderInfo_t	*ri = &cl->renderInfo;
	const char		*guestSex = COOP_GuestConnected() ? COOP_GuestSex() : "";
	const char		*name = COOP_GuestConnected() && COOP_GuestName()[0] ? COOP_GuestName() : "Player 2";
	qboolean		p1Female = ( g_sex->string[0] == 'f' ) ? qtrue : qfalse;
	qboolean		female = guestSex[0] ? ( guestSex[0] == 'f' ? qtrue : qfalse ) : p1Female;
	char			head[sizeof( ri->headModelName )], torso[sizeof( ri->torsoModelName )], legs[sizeof( ri->legsModelName )];
	const char		*voice = female ? "alexa" : "munro";

	Q_strncpyz( cl->pers.netname, name, sizeof( cl->pers.netname ) );
	Q_strncpyz( cl->clientInfo.name, name, sizeof( cl->clientInfo.name ) );

	if ( !p1->client )
	{
		return;
	}
	if ( female == p1Female )
	{
		Q_strncpyz( head, p1->client->renderInfo.headModelName, sizeof( head ) );
		Q_strncpyz( torso, p1->client->renderInfo.torsoModelName, sizeof( torso ) );
		Q_strncpyz( legs, p1->client->renderInfo.legsModelName, sizeof( legs ) );
	}
	else
	{
		COOP_OtherSexModel( head, sizeof( head ), p1->client->renderInfo.headModelName, female,
			female ? "alexandria/default" : "munro/default" );
		COOP_OtherSexModel( torso, sizeof( torso ), p1->client->renderInfo.torsoModelName, female,
			female ? "hazardfemale/default" : "hazard/default" );
		COOP_OtherSexModel( legs, sizeof( legs ), p1->client->renderInfo.legsModelName, female,
			female ? "hazardfemale/default" : "hazard/default" );
	}

	if ( strcmp( head, ri->headModelName ) || strcmp( torso, ri->torsoModelName ) || strcmp( legs, ri->legsModelName ) )
	{
		Q_strncpyz( ri->headModelName, head, sizeof( ri->headModelName ) );
		Q_strncpyz( ri->torsoModelName, torso, sizeof( ri->torsoModelName ) );
		Q_strncpyz( ri->legsModelName, legs, sizeof( ri->legsModelName ) );
		CG_RegisterClientRenderInfo( &cl->clientInfo, ri );
		cl->clientInfo.infoValid = qtrue;
		gi.Printf( "co-op: player 2 (%s) is %s Munro: %s, %s, %s\n", name, female ? "female" : "male", head, torso, legs );
		cl->clientInfo.customBasicSoundDir = NULL;		// registered again below
	}
	// the voice, again when the cgame registered P2's sounds its own way (a video or sound restart)
	if ( !cl->clientInfo.customBasicSoundDir || strcmp( cl->clientInfo.customBasicSoundDir, voice ) ||
		( female != p1Female && coop_p2.levelFrames % 20 == 0 ) )
	{
		cl->clientInfo.customBasicSoundDir = (char *)voice;
		COOP_RegisterP2Sounds( &cl->clientInfo );
	}
}

// P1's name in co-op messages: the host player's coop_name ("Your name" in the launcher; only
// printable characters, no quotes, \ ; or %), else P1's netname ("Munro")
const char *COOP_HostName( void )
{
	static char	name[32];
	char		raw[128];
	gentity_t	*p1 = &g_entities[0];
	int			n = 0;

	gi.Cvar_VariableStringBuffer( "coop_name", raw, sizeof( raw ) );
	for ( const char *s = raw; *s && n < (int)sizeof( name ) - 1; s++ )
	{
		unsigned char c = (unsigned char)*s;
		if ( ( c > ' ' && c <= '~' && c != '"' && c != '\\' && c != ';' && c != '%' ) || ( c == ' ' && n ) )
		{
			name[n++] = (char)c;
		}
	}
	while ( n && name[n - 1] == ' ' )
	{
		n--;
	}
	name[n] = 0;
	if ( name[0] )
	{
		return name;
	}
	return ( p1->client && p1->client->pers.netname[0] ) ? p1->client->pers.netname : "Player 1";
}
