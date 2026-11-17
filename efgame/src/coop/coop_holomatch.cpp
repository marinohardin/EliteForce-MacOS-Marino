// coop_holomatch.cpp -- single-player co-op: player 2 in a Holomatch player model.
//
// coop_characters.h's "experimental" characters are Holomatch models (models/players2/<name>/ in
// the game's pak0), named "players2:<name>/<skin>" in P2's renderInfo. Their files are read as
// they are: upper.mdr, lower.mdr, head.md3, the skins, and the Holomatch animation.cfg, a plain list
// of 25 frame ranges in the Holomatch animation order (below) after a few keys (sex, soundpath).
//
// The game runs P2 as always: its animation numbers, timers and sounds come from Munro's body
// (COOP_HOLOMATCH_TIMING), so P2 moves, fires and goes down exactly as in Munro's suit. Only the
// drawing changes: CG_Player hands us the frames it worked out for Munro's body and we draw the
// nearest Holomatch animation instead (coop_holoRules), from the time the single-player animation
// started. A Holomatch skeleton (Gowron: 15 bones in the legs, 33 in the torso) isn't Munro's
// (11 and 27) and each .mdr carries its own frames, so none of Munro's frames can play on it.
//
// A Holomatch model's legs file leaves out the torso-only frames: its leg animations start that
// many frames earlier in lower.mdr than the list says (as the Holomatch game reads it).

#include "g_local.h"
#include "../cgame/cg_local.h"
#include "anims.h"
#include "coop_public.h"
#include "coop_local.h"
#include "coop_characters.h"

extern qboolean		G_ParseAnimFileSet( const char *filename, int *animFileIndex, qboolean cacheSounds );
extern qboolean		ValidAnimFileIndex( int index );
extern animFileSet_t	knownAnimFileSets[MAX_ANIM_FILES];

#define COOP_HOLOMATCH_TIMING	"hazard"	// the body whose animation file times P2's animations
#define COOP_HOLO_BLEND			100			// ms from the last drawn pose into a new animation

// the Holomatch animations, in the order of its animation.cfg
enum
{
	HM_DEATH1, HM_DEAD1, HM_DEATH2, HM_DEAD2, HM_DEATH3, HM_DEAD3,
	HM_TORSO_GESTURE, HM_TORSO_ATTACK, HM_TORSO_ATTACK2, HM_TORSO_DROP, HM_TORSO_RAISE,
	HM_TORSO_STAND, HM_TORSO_STAND2,
	HM_LEGS_WALKCR, HM_LEGS_WALK, HM_LEGS_RUN, HM_LEGS_BACK, HM_LEGS_SWIM,
	HM_LEGS_JUMP, HM_LEGS_LAND, HM_LEGS_JUMPB, HM_LEGS_LANDB,
	HM_LEGS_IDLE, HM_LEGS_IDLECR, HM_LEGS_TURN,
	HM_NUM,
	HM_DEATH = 100,		// in coop_holoRules: the death (or HM_DEAD: lying dead) numbered like the
	HM_DEAD				// single-player one, 1-3, the others in turn
};

typedef struct
{
	int		firstFrame;
	int		numFrames;
	int		loopFrames;		// the Holomatch way: 0 doesn't loop, n loops the last n frames
	int		frameLerp;		// ms
} holoAnim_t;

/*
==============================================================================
The translation: each single-player animation -> the nearest Holomatch one, for the legs and for
the torso. The first rule whose text is in the animation's name wins (exact names first);
anything left is standing. Holomatch has no pain, ladder, crawl, sitting or lying animations.
==============================================================================
*/
typedef struct
{
	const char	*text;		// in the single-player animation's name
	int			legs, torso;
	qboolean	reverse;	// play it backwards
} holoRule_t;

static const holoRule_t coop_holoRules[] =
{
	// exact names, before the wider rules below catch them
	{ "BOTH_RUN1STOP",		HM_LEGS_IDLE,	HM_TORSO_STAND },
	{ "BOTH_UNCROUCH1",		HM_LEGS_IDLE,	HM_TORSO_STAND },
	{ "LEGS_KNEELUP1",		HM_LEGS_IDLE,	HM_TORSO_STAND },
	{ "BOTH_STANDTOWALK1",	HM_LEGS_WALK,	HM_TORSO_STAND },
	{ "BOTH_CRAWLBACK1",	HM_LEGS_WALKCR,	HM_TORSO_STAND,	qtrue },
	{ "BOTH_CROUCH1WALK",	HM_LEGS_WALKCR,	HM_TORSO_STAND },
	// getting up from the ground or a seat: crouched
	{ "TOSTAND",			HM_LEGS_IDLECR,	HM_TORSO_STAND },
	{ "GETUP",				HM_LEGS_IDLECR,	HM_TORSO_STAND },
	{ "GET_UP",				HM_LEGS_IDLECR,	HM_TORSO_STAND },
	{ "STANDUP",			HM_LEGS_IDLECR,	HM_TORSO_STAND },
	{ "2STAND",				HM_LEGS_IDLECR,	HM_TORSO_STAND },
	{ "SIT1STAND",			HM_LEGS_IDLECR,	HM_TORSO_STAND },
	// dying (P2 going down is BOTH_DEATH1) and dead
	{ "DEATH",				HM_DEATH,		HM_DEATH },
	{ "DEAD",				HM_DEAD,		HM_DEAD },
	// lying on the ground: dead
	{ "LYING",				HM_DEAD,		HM_DEAD },
	{ "LIE_DOWN",			HM_DEAD,		HM_DEAD },
	{ "WRITH",				HM_DEAD,		HM_DEAD },
	{ "PRONE",				HM_DEAD,		HM_DEAD },
	{ "SLEEP",				HM_DEAD,		HM_DEAD },
	{ "INJURED",			HM_DEAD,		HM_DEAD },
	// ladders: walking (down: backwards), still on it: standing
	{ "LADDER_UP",			HM_LEGS_WALK,	HM_TORSO_STAND },
	{ "LADDER_DWN",			HM_LEGS_WALK,	HM_TORSO_STAND,	qtrue },
	{ "LADDER",				HM_LEGS_IDLE,	HM_TORSO_STAND },
	// crouched, kneeling, sitting: crouched
	{ "CRAWL",				HM_LEGS_WALKCR,	HM_TORSO_STAND },
	{ "CROUCH",				HM_LEGS_IDLECR,	HM_TORSO_STAND },
	{ "KNEEL",				HM_LEGS_IDLECR,	HM_TORSO_STAND },
	{ "CRLEAN",				HM_LEGS_IDLECR,	HM_TORSO_STAND },
	{ "SIT",				HM_LEGS_IDLECR,	HM_TORSO_STAND },
	{ "TABLE_",				HM_LEGS_IDLECR,	HM_TORSO_STAND },
	{ "COVERUP",			HM_LEGS_IDLECR,	HM_TORSO_STAND },
	// jumping and landing
	{ "JUMPBACK",			HM_LEGS_JUMPB,	HM_TORSO_STAND },
	{ "INAIRBACK",			HM_LEGS_JUMPB,	HM_TORSO_STAND },
	{ "LANDBACK",			HM_LEGS_LANDB,	HM_TORSO_STAND },
	{ "JUMP",				HM_LEGS_JUMP,	HM_TORSO_STAND },
	{ "INAIR",				HM_LEGS_JUMP,	HM_TORSO_STAND },
	{ "DIVE",				HM_LEGS_JUMP,	HM_TORSO_STAND },
	{ "ROLL",				HM_LEGS_JUMP,	HM_TORSO_STAND },
	{ "LAND",				HM_LEGS_LAND,	HM_TORSO_STAND },
	// swimming, flying, floating
	{ "SWIM",				HM_LEGS_SWIM,	HM_TORSO_STAND },
	{ "FLY_",				HM_LEGS_SWIM,	HM_TORSO_STAND },
	{ "FLOAT",				HM_LEGS_SWIM,	HM_TORSO_STAND },
	// moving
	{ "WALKBACK",			HM_LEGS_BACK,	HM_TORSO_STAND },
	{ "RUNBACK",			HM_LEGS_BACK,	HM_TORSO_STAND },
	{ "WALK",				HM_LEGS_WALK,	HM_TORSO_STAND },
	{ "RUN",				HM_LEGS_RUN,	HM_TORSO_STAND },
	{ "STRAFE",				HM_LEGS_RUN,	HM_TORSO_STAND },
	{ "TURN",				HM_LEGS_TURN,	HM_TORSO_STAND },
	// the weapon
	{ "MELEE",				HM_LEGS_IDLE,	HM_TORSO_ATTACK2 },
	{ "ATTACK",				HM_LEGS_IDLE,	HM_TORSO_ATTACK },
	{ "DROPWEAP",			HM_LEGS_IDLE,	HM_TORSO_DROP },
	{ "RAISEWEAP",			HM_LEGS_IDLE,	HM_TORSO_RAISE },
	// a gesture
	{ "GESTURE",			HM_LEGS_IDLE,	HM_TORSO_GESTURE },
};

static signed char	coop_holoLegs[MAX_ANIMATIONS], coop_holoTorso[MAX_ANIMATIONS];
static byte			coop_holoReverse[MAX_ANIMATIONS];
static qboolean		coop_holoMapped;

static void COOP_HoloMap( void )
{
	int deaths = 0, deads = 0;

	for ( int i = 0; i < MAX_ANIMATIONS; i++ )
	{
		const char	*name = animTable[i].name;
		int			legs = HM_LEGS_IDLE, torso = HM_TORSO_STAND;

		coop_holoReverse[i] = 0;
		for ( int r = 0; name && r < (int)( sizeof( coop_holoRules ) / sizeof( coop_holoRules[0] ) ); r++ )
		{
			if ( strstr( name, coop_holoRules[r].text ) )
			{
				legs = coop_holoRules[r].legs;
				torso = coop_holoRules[r].torso;
				coop_holoReverse[i] = coop_holoRules[r].reverse ? 1 : 0;
				break;
			}
		}
		if ( legs == HM_DEATH || legs == HM_DEAD )
		{
			// BOTH_DEATH1-3 / BOTH_DEAD1-3 -> the Holomatch one of the same number, the rest in turn
			int n = ( legs == HM_DEATH ) ? deaths++ : deads++;

			if ( !strcmp( name, "BOTH_DEATH1" ) || !strcmp( name, "BOTH_DEAD1" ) )			n = 0;
			else if ( !strcmp( name, "BOTH_DEATH2" ) || !strcmp( name, "BOTH_DEAD2" ) )		n = 1;
			else if ( !strcmp( name, "BOTH_DEATH3" ) || !strcmp( name, "BOTH_DEAD3" ) )		n = 2;
			legs = torso = ( n % 3 ) * 2 + ( legs == HM_DEAD ? 1 : 0 );		// HM_DEATH1/HM_DEAD1...
		}
		coop_holoLegs[i] = (signed char)legs;
		coop_holoTorso[i] = (signed char)torso;
	}
	coop_holoMapped = qtrue;
}

// the Holomatch animation a single-player one is drawn as (legs: qtrue for the legs); for the console
int COOP_HoloAnimFor( int anim, qboolean legs )
{
	if ( !coop_holoMapped )
	{
		COOP_HoloMap();
	}
	if ( anim < 0 || anim >= MAX_ANIMATIONS )
	{
		return HM_LEGS_IDLE;
	}
	return legs ? coop_holoLegs[anim] : coop_holoTorso[anim];
}

/*
==============================================================================
The models: registered from models/players2/, their animation lists kept here
==============================================================================
*/
#define COOP_MAX_HOLO_MODELS	4

typedef struct
{
	char		dir[MAX_QPATH];			// "gowron"
	qboolean	valid;					// its animation.cfg read
	holoAnim_t	legs[HM_NUM];			// frames in lower.mdr
	holoAnim_t	torso[HM_NUM];			// frames in upper.mdr
	qhandle_t	legsModel, torsoModel;	// what the cgame registered for it last
} holoModel_t;

static holoModel_t	coop_holoModels[COOP_MAX_HOLO_MODELS];
static int			coop_numHoloModels;

static qboolean COOP_IsHoloName( const char *model )
{
	return ( model && !Q_stricmpn( model, COOP_HOLOMATCH_PREFIX, COOP_HOLOMATCH_PREFIX_LEN ) ) ? qtrue : qfalse;
}

// models/players2/<dir>/animation.cfg, the Holomatch way
static qboolean COOP_ReadHoloAnims( holoModel_t *m )
{
	char			text[8192];
	char			*p = text;
	const char		*filename = va( "models/players2/%s/animation.cfg", m->dir );
	fileHandle_t	f;
	int				len = gi.FS_FOpenFile( filename, &f, FS_READ );
	int				i, skip = 0;

	if ( len <= 0 )
	{
		return qfalse;
	}
	if ( len >= (int)sizeof( text ) )
	{
		gi.FS_FCloseFile( f );
		return qfalse;
	}
	gi.FS_Read( text, len, f );
	gi.FS_FCloseFile( f );
	text[len] = 0;

	// keys before the list: sex, soundpath, footsteps (one value), headoffset (three)
	for ( ;; )
	{
		char	*prev = p;
		char	*token = COM_Parse( &p );

		if ( !token || !token[0] )
		{
			return qfalse;
		}
		if ( token[0] >= '0' && token[0] <= '9' )
		{
			p = prev;
			break;
		}
		for ( int n = Q_stricmp( token, "headoffset" ) ? 1 : 3; n > 0; n-- )
		{
			COM_Parse( &p );
		}
	}
	for ( i = 0; i < HM_NUM; i++ )
	{
		int		v[4];
		float	fps;

		for ( int k = 0; k < 4; k++ )
		{
			char *token = COM_Parse( &p );
			if ( !token || !token[0] )
			{
				gi.Printf( S_COLOR_YELLOW "co-op: %s has only %d of the %d Holomatch animations\n", filename, i, HM_NUM );
				return qfalse;
			}
			v[k] = ( k == 3 ) ? 0 : atoi( token );
			if ( k == 3 )
			{
				fps = atof( token );
				v[3] = (int)( fps > 0 ? 1000.0f / fps : 100 );
			}
		}
		if ( i == HM_LEGS_WALKCR )
		{
			skip = v[0] - m->torso[HM_TORSO_GESTURE].firstFrame;	// lower.mdr has no torso-only frames
		}
		holoAnim_t a = { v[0], v[1] < 0 ? -v[1] : v[1], v[2], v[3] > 0 ? v[3] : 100 };
		if ( a.numFrames < 1 )
		{
			a.numFrames = 1;
		}
		if ( a.loopFrames < 0 || a.loopFrames > a.numFrames )
		{
			a.loopFrames = 0;
		}
		m->torso[i] = a;
		m->legs[i] = a;
		if ( i >= HM_LEGS_WALKCR )
		{
			m->legs[i].firstFrame -= skip;
		}
	}
	// a torso doesn't play the leg animations, nor legs the torso's: the nearest instead
	for ( i = HM_TORSO_GESTURE; i <= HM_TORSO_STAND2; i++ )
	{
		m->legs[i] = m->legs[HM_LEGS_IDLE];
	}
	for ( i = HM_LEGS_WALKCR; i < HM_NUM; i++ )
	{
		m->torso[i] = m->torso[HM_TORSO_STAND];
	}
	return qtrue;
}

static holoModel_t *COOP_HoloModel( const char *name, qboolean load )
{
	char	dir[MAX_QPATH];
	char	*slash;

	if ( !COOP_IsHoloName( name ) )
	{
		return NULL;
	}
	Q_strncpyz( dir, name + COOP_HOLOMATCH_PREFIX_LEN, sizeof( dir ) );
	if ( ( slash = strchr( dir, '/' ) ) != NULL )
	{
		*slash = 0;
	}
	for ( int i = 0; i < coop_numHoloModels; i++ )
	{
		if ( !Q_stricmp( coop_holoModels[i].dir, dir ) )
		{
			return &coop_holoModels[i];
		}
	}
	if ( !load || coop_numHoloModels == COOP_MAX_HOLO_MODELS )
	{
		return NULL;
	}
	holoModel_t *m = &coop_holoModels[coop_numHoloModels++];
	memset( m, 0, sizeof( *m ) );
	Q_strncpyz( m->dir, dir, sizeof( m->dir ) );
	m->valid = COOP_ReadHoloAnims( m );
	if ( !m->valid )
	{
		gi.Printf( S_COLOR_YELLOW "co-op: can't read models/players2/%s/animation.cfg\n", dir );
	}
	return m;
}

// "players2:dir" -> "models/players2/dir", anything else -> "models/players/<name>"
static const char *COOP_PartDir( const char *model )
{
	return COOP_IsHoloName( model ) ? va( "models/players2/%s", model + COOP_HOLOMATCH_PREFIX_LEN ) : va( "models/players/%s", model );
}

/*
COOP_RegisterHoloModels

CG_RegisterClientModelname, first: -1 when no part is a Holomatch model (the cgame's own way then),
else the models, skins and animation set registered here (qtrue) or not (qfalse: the cgame falls
back to the default models). The names come without their skins, as CG_RegisterClientModelname
has them.
*/
int COOP_RegisterHoloModels( clientInfo_t *ci, const char *headModel, const char *headSkin,
	const char *torsoModel, const char *torsoSkin, const char *legsModel, const char *legsSkin )
{
	holoModel_t *legsHolo, *torsoHolo;

	if ( !COOP_IsHoloName( legsModel ) && !COOP_IsHoloName( torsoModel ) && !COOP_IsHoloName( headModel ) )
	{
		return -1;
	}
	legsHolo = COOP_HoloModel( legsModel, qtrue );
	torsoHolo = COOP_HoloModel( torsoModel, qtrue );
	if ( ( legsHolo && !legsHolo->valid ) || ( torsoHolo && !torsoHolo->valid ) )
	{
		return qfalse;
	}

	ci->legsModel = cgi_R_RegisterModel( va( "%s/lower.mdr", COOP_PartDir( legsModel ) ) );
	ci->legsSkin = cgi_R_RegisterSkin( va( "%s/lower_%s.skin", COOP_PartDir( legsModel ), legsSkin ) );
	ci->torsoModel = ( torsoModel && torsoModel[0] ) ? cgi_R_RegisterModel( va( "%s/upper.mdr", COOP_PartDir( torsoModel ) ) ) : 0;
	ci->torsoSkin = ( torsoModel && torsoModel[0] ) ? cgi_R_RegisterSkin( va( "%s/upper_%s.skin", COOP_PartDir( torsoModel ), torsoSkin ) ) : 0;
	ci->headModel = ( headModel && headModel[0] ) ? cgi_R_RegisterModel( va( "%s/head.md3", COOP_PartDir( headModel ) ) ) : 0;
	ci->headSkin = 0;
	ci->extensions = qfalse;
	if ( headModel && headModel[0] )
	{
		ci->headSkin = cgi_R_RegisterSkin( va( "%s/head_%s.skin", COOP_PartDir( headModel ), headSkin ) );
		if ( ci->headSkin < 0 )
		{
			ci->extensions = qtrue;
			ci->headSkin = -ci->headSkin;
		}
	}
	if ( !ci->legsModel || !ci->legsSkin || ( torsoModel && torsoModel[0] && ( !ci->torsoModel || !ci->torsoSkin ) ) ||
		( headModel && headModel[0] && ( !ci->headModel || !ci->headSkin ) ) )
	{
		gi.Printf( S_COLOR_YELLOW "co-op: a Holomatch model's files didn't load: %s, %s, %s\n", headModel, torsoModel, legsModel );
		return qfalse;
	}
	// the game times the animations with Munro's body (its animation file and its sounds)
	if ( !G_ParseAnimFileSet( legsHolo ? COOP_HOLOMATCH_TIMING : legsModel, &ci->animFileIndex, qtrue ) )
	{
		return qfalse;
	}
	if ( legsHolo )
	{
		legsHolo->legsModel = ci->legsModel;
	}
	if ( torsoHolo )
	{
		torsoHolo->torsoModel = ci->torsoModel;
	}
	return qtrue;
}

/*
==============================================================================
Drawing: the Holomatch frames for the single-player animation running
==============================================================================
*/
typedef struct
{
	qboolean	started;
	int		animNum;		// the lerpFrame's animation, with its toggle bit (a restart changes it)
	int		start;			// cg.time it started
	int		lastFrame;		// the frame drawn last, to blend from into the next animation
} holoPart_t;

static holoPart_t	coop_holoParts[MAX_GENTITIES][2];

static void COOP_HoloPartFrames( holoPart_t *st, const holoAnim_t *table, const lerpFrame_t *lf,
	int animFileIndex, qboolean legsPart, int *oldFrame, int *frame, float *backlerp )
{
	int		anim = lf->animationNumber & ~ANIM_TOGGLEBIT;
	int		hm = COOP_HoloAnimFor( anim, legsPart );

	if ( hm < 0 || hm >= HM_NUM )
	{
		hm = legsPart ? HM_LEGS_IDLE : HM_TORSO_STAND;
	}
	const holoAnim_t *a = &table[hm];

	if ( !st->started || st->animNum != lf->animationNumber || cg.time < st->start )
	{
		// (the first time, or a new level: straight into it)
		st->start = ( !st->started || cg.time < st->start ) ? cg.time - COOP_HOLO_BLEND : cg.time;
		st->started = qtrue;
		st->animNum = lf->animationNumber;
	}

	int			lerp = a->frameLerp;
	int			elapsed = cg.time - st->start - COOP_HOLO_BLEND;
	const animation_t *sp = ValidAnimFileIndex( animFileIndex ) ? &knownAnimFileSets[animFileIndex].animations[anim] : NULL;

	// an animation that plays once in both games takes as long as the single-player one
	if ( !a->loopFrames && sp && sp->loopFrames == -1 && sp->numFrames > 0 )
	{
		lerp = (int)( sp->numFrames * fabs( (float)sp->frameLerp ) ) / a->numFrames;
		if ( lerp < 10 )
		{
			lerp = 10;
		}
	}
	if ( elapsed < 0 )
	{
		// blending from the last pose into the first frame
		*oldFrame = st->lastFrame;
		*frame = a->firstFrame + ( coop_holoReverse[anim] ? a->numFrames - 1 : 0 );
		*backlerp = (float)-elapsed / COOP_HOLO_BLEND;
	}
	else
	{
		int f = elapsed / lerp, f2;
		int frames[2];

		for ( int k = 0; k < 2; k++ )
		{
			f2 = f + k;
			if ( f2 >= a->numFrames )
			{
				if ( a->loopFrames )
				{
					f2 = ( f2 - a->numFrames ) % a->loopFrames + a->numFrames - a->loopFrames;
				}
				else
				{
					f2 = a->numFrames - 1;
				}
			}
			frames[k] = a->firstFrame + ( coop_holoReverse[anim] ? a->numFrames - 1 - f2 : f2 );
		}
		*oldFrame = frames[0];
		*frame = frames[1];
		*backlerp = 1.0f - (float)( elapsed - f * lerp ) / lerp;
	}
	if ( *backlerp < 0.0f )
	{
		*backlerp = 0.0f;
	}
	else if ( *backlerp > 1.0f )
	{
		*backlerp = 1.0f;
	}
	st->lastFrame = ( *backlerp < 0.5f ) ? *frame : *oldFrame;
}

/*
COOP_HoloFrames

CG_Player, after CG_PlayerAnimation: a client in a Holomatch model draws the nearest Holomatch
animation's frames in place of the ones worked out for Munro's body (which the game keeps using).
Nothing changes for any other model, or when the Holomatch model didn't load (the default one is
drawn then).
*/
void COOP_HoloFrames( centity_t *cent, int *legsOld, int *legs, float *legsBackLerp, int *torsoOld, int *torso, float *torsoBackLerp )
{
	gclient_t		*cl = cent->gent ? cent->gent->client : NULL;
	holoModel_t		*m;
	int				num = cent->currentState.number;

	if ( !cl || num < 0 || num >= MAX_GENTITIES )
	{
		return;
	}
	m = COOP_HoloModel( cl->renderInfo.legsModelName, qfalse );
	if ( m && m->valid && m->legsModel && cl->clientInfo.legsModel == m->legsModel )
	{
		COOP_HoloPartFrames( &coop_holoParts[num][0], m->legs, &cent->pe.legs, cl->clientInfo.animFileIndex, qtrue,
			legsOld, legs, legsBackLerp );
	}
	else
	{
		coop_holoParts[num][0].started = qfalse;
	}
	m = COOP_HoloModel( cl->renderInfo.torsoModelName, qfalse );
	if ( m && m->valid && m->torsoModel && cl->clientInfo.torsoModel == m->torsoModel )
	{
		COOP_HoloPartFrames( &coop_holoParts[num][1], m->torso, &cent->pe.torso, cl->clientInfo.animFileIndex, qfalse,
			torsoOld, torso, torsoBackLerp );
	}
	else
	{
		coop_holoParts[num][1].started = qfalse;
	}
}
