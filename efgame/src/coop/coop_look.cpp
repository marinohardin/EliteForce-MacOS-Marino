// coop_look.cpp -- single-player co-op: how player 2 looks, and both players' names.
//
// Without a guest, P2 looks like P1 (the same models). A guest chooses male or female Munro in
// the launcher (coop_sex, sent when it connects): P2 then wears P1's outfit in that sex, with its
// voice (pain, death, jump sounds). The guest may also choose a crew member or Hazard Team
// member (coop_look, an id from game/coop_characters.h): P2 then wears that character's head,
// body and skins, still with the chosen sex's voice and the player's box. The host takes only a
// listed character whose files all come from its own pak0-pak2; anything else is the default
// Munro. The guest's replica registers the same models from P2's replicated model names
// (coop_replica.cpp), so both screens agree.
//
// Names are co-op-only state: P2's client is ours, and P1's name for co-op messages comes from
// coop_name. P1's own netname, g_sex and everything the story scripts read stay the host's.

#include "g_local.h"
#include "../cgame/cg_local.h"
#include "coop_public.h"
#include "coop_local.h"
#include "coop_characters.h"

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

// "dir/skin" (or "dir": the default skin) -> each file the cgame loads for that part, all from
// pak0-pak2; part: "head", "upper" or "lower" (the legs also bring the animation file)
static qboolean COOP_CorePart( const char *model, const char *part )
{
	char		dir[MAX_QPATH], skin[MAX_QPATH];
	const char	*slash = strchr( model, '/' );

	Q_strncpyz( dir, model, sizeof( dir ) );
	if ( slash && slash - model < (int)sizeof( dir ) )
	{
		dir[slash - model] = 0;
		Q_strncpyz( skin, slash + 1, sizeof( skin ) );
	}
	else
	{
		Q_strncpyz( skin, "default", sizeof( skin ) );
	}
	if ( !COOP_CorePakFile( va( "models/players/%s/%s.%s", dir, part, part[0] == 'h' ? "md3" : "mdr" ) ) ||
		!COOP_CorePakFile( va( "models/players/%s/%s_%s.skin", dir, part, skin ) ) )
	{
		return qfalse;
	}
	return ( part[0] != 'l' || COOP_CorePakFile( va( "models/players/%s/animation.cfg", dir ) ) ) ? qtrue : qfalse;
}

/*
COOP_GuestCharacter

The character the guest chose, when the host can dress P2 in it: one of coop_characters.h whose
files all come from this copy's pak0-pak2 (the guest's pak0-pak2 match them, so it has them too).
NULL for the default Munro: no choice, an unknown name, or a file missing or replaced by another
pack. Checked again only when the choice changes.
*/
static const coopCharacter_t *COOP_GuestCharacter( void )
{
	static char						checkedId[COOP_CHARACTER_MAX];
	static const coopCharacter_t	*checked;
	const char						*id = COOP_GuestConnected() ? COOP_GuestLook() : "";

	if ( !strcmp( id, checkedId ) )
	{
		return id[0] ? checked : NULL;
	}
	Q_strncpyz( checkedId, id, sizeof( checkedId ) );
	checked = NULL;
	if ( !id[0] )
	{
		return NULL;
	}
	for ( int i = 0; i < COOP_NUM_CHARACTERS; i++ )
	{
		const coopCharacter_t *c = &coop_characters[i];

		if ( strcmp( id, c->id ) )
		{
			continue;
		}
		if ( COOP_CorePart( c->head, "head" ) && COOP_CorePart( c->torso, "upper" ) && COOP_CorePart( c->legs, "lower" ) )
		{
			checked = c;
		}
		else
		{
			gi.Printf( "co-op: player 2's character %s isn't all in this game's pak0-pak2: default Munro\n", c->name );
		}
		return checked;
	}
	gi.Printf( "co-op: player 2 asked for an unknown character \"%s\": default Munro\n", id );
	return NULL;
}

// a client's models changed: the cgame's animation state for it still points into the old
// models' animation table (it looks again only when the animation number changes), so the new
// body would play the old one's frame numbers; start its legs and torso over
void COOP_ForgetAnimFrames( int num )
{
	if ( num >= 0 && num < MAX_GENTITIES )
	{
		memset( &cg_entities[num].pe.legs, 0, sizeof( cg_entities[num].pe.legs ) );
		memset( &cg_entities[num].pe.torso, 0, sizeof( cg_entities[num].pe.torso ) );
	}
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

Every frame and at every (re)spawn of P2: the guest's name, character and Munro, or P1's look
without a guest. Registers new models and sounds only when they change.
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
	const coopCharacter_t	*character = COOP_GuestCharacter();

	Q_strncpyz( cl->pers.netname, name, sizeof( cl->pers.netname ) );
	Q_strncpyz( cl->clientInfo.name, name, sizeof( cl->clientInfo.name ) );

	if ( !p1->client )
	{
		return;
	}
	if ( character )
	{
		Q_strncpyz( head, character->head, sizeof( head ) );
		Q_strncpyz( torso, character->torso, sizeof( torso ) );
		Q_strncpyz( legs, character->legs, sizeof( legs ) );
	}
	else if ( female == p1Female )
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
		COOP_ForgetAnimFrames( ent->s.number );
		gi.Printf( "co-op: player 2 (%s) is %s%s: %s, %s, %s\n", name, character ? character->name : female ? "female Munro" : "male Munro",
			character ? ( female ? " with a female voice" : " with a male voice" ) : "", head, torso, legs );
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
