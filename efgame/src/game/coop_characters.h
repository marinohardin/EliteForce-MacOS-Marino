// coop_characters.h -- single-player co-op: the characters a guest can choose for player 2.
//
// Shared by the game (coop/coop_look.cpp: what player 2 wears) and the Mac launcher's Co-op sheet
// (the "Character" popup), so both know the same names. Plain C: included from C++ and Objective-C.
//
// Only characters from the game's own pak0-pak2 (the host has them too: co-op refuses a guest whose
// pak0-pak2 differ), each the crew member's own head, body and skins as the game's NPCs wear them.
// Every body here is a human-sized skeletal model with tag_head, tag_weapon and the animations a
// player moves with (stand, walk, run, jump, crouch, the weapon poses, death). Left out: the Borg,
// aliens, Klingons and Hirogen (taller, other animation sets), bots, and the disguises. Player 2 keeps
// the player's box, eye height and hit box whichever it wears, and the voice of the chosen sex.
//
// The guest sends the id (the "look" key, coop_look); the host takes it only if it is listed here
// and every file it needs comes from its own pak0-pak2, else player 2 is the default Munro.

#ifndef COOP_CHARACTERS_H
#define COOP_CHARACTERS_H

typedef struct
{
	const char	*id;		// what the guest sends: lower case letters, digits and _
	const char	*name;		// for people
	const char	*group;		// "crew" or "hazard"
	const char	*head;		// model/skin, as renderInfo names them
	const char	*torso;
	const char	*legs;
} coopCharacter_t;

static const coopCharacter_t coop_characters[] =
{
	// Voyager's crew
	{ "janeway",		"Captain Janeway",				"crew",		"janeway/default",		"crewfemale/default",	"crewfemale/default" },
	{ "chakotay",		"Chakotay",						"crew",		"chakotay/default",		"crewthin/red",			"crewthin/default" },
	{ "tuvok",			"Tuvok",						"crew",		"tuvok/default",		"crewthin/tuvok",		"crewthin/default" },
	{ "paris",			"Tom Paris",					"crew",		"paris/default",		"crewthin/red",			"crewthin/default" },
	{ "torres",			"B'Elanna Torres",				"crew",		"torres/default",		"crewfemale/gold",		"crewfemale/default" },
	{ "kim",			"Harry Kim",					"crew",		"kim/default",			"crewthin/gold",		"crewthin/default" },
	{ "seven",			"Seven of Nine",				"crew",		"seven/default",		"seven/default",		"seven/default" },
	{ "doctor",			"The Doctor",					"crew",		"doctor/default",		"crewthin/blue",		"crewthin/default" },
	{ "neelix",			"Neelix",						"crew",		"neelix/default",		"crewthin/neelix",		"crewthin/neelix" },
	// the Hazard Team
	{ "tuvok_hazard",	"Tuvok (Hazard Team)",			"hazard",	"tuvok_h/default",		"hazard/default",		"hazard/default" },
	{ "seven_hazard",	"Seven of Nine (Hazard Team)",	"hazard",	"seven/hazard",			"seven/hazard",			"seven/hazard" },
	{ "biessman",		"Rick Biessman",				"hazard",	"biessman/default",		"hazard/default",		"hazard/default" },
	{ "chang",			"Austin Chang",					"hazard",	"chang/default",		"hazard/default",		"hazard/default" },
	{ "chell",			"Chell",						"hazard",	"chell/default",		"hazard/default",		"hazard/default" },
	{ "foster",			"Les Foster",					"hazard",	"foster/default",		"hazard/default",		"hazard/default" },
	{ "telsia",			"Telsia Murphy",				"hazard",	"telsia/default",		"hazardfemale/default",	"hazardfemale/default" },
	{ "jurot",			"Juliet Jurot",					"hazard",	"telsia/jurot",			"hazardfemale/default",	"hazardfemale/default" },
	{ "laird",			"Elizabeth Laird",				"hazard",	"mackey/default",		"hazardfemale/default",	"hazardfemale/default" },
	{ "kenn",			"Kenn Lathrop",					"hazard",	"munro/kenn",			"hazard/default",		"hazard/default" },
	{ "odell",			"Thomas Odell",					"hazard",	"chang/tom",			"hazard/default",		"hazard/default" },
	{ "nelson",			"Jeffrey Nelson",				"hazard",	"chell/long",			"hazard/default",		"hazard/default" },
	{ "oviedo",			"Perfecto Oviedo",				"hazard",	"oviedo_h/default",		"hazard/default",		"hazard/default" },
	{ "csatlos",		"Mitch Csatlos",				"hazard",	"oviedo_h/jaworski",	"hazard/default",		"hazard/default" },
	{ "jaworski",		"Michael Jaworski",				"hazard",	"oviedo_h/csatlos",		"hazard/default",		"hazard/default" },
};

#define COOP_NUM_CHARACTERS		( (int)( sizeof( coop_characters ) / sizeof( coop_characters[0] ) ) )
#define COOP_CHARACTER_MAX		24		// the longest id the connection carries, with its terminator

#endif // COOP_CHARACTERS_H
