// coop_characters.h -- single-player co-op: the characters a guest can choose for player 2.
//
// Shared by the game (coop/coop_look.cpp: what player 2 wears) and the launchers' Co-op sheet or
// dialog (the "Character" popup), so both know the same names. Plain C: included from C++ and
// Objective-C.
//
// Only characters from the game's own pak0-pak2 (the host has them too: co-op refuses a guest whose
// pak0-pak2 differ), each the crew member's own head, body and skins as the game's NPCs wear them.
// Every crew and Hazard Team body is a human-sized skeletal model with tag_head, tag_weapon and the
// animations a player moves with (stand, walk, run, jump, crouch, the weapon poses, death). Left
// out of the single-player models: the Borg, aliens, Klingons and Hirogen (taller, other animation
// sets), bots, and the disguises. Player 2 keeps
// the player's box, eye height and hit box whichever it wears, and the voice of the chosen sex.
//
// "experimental": Holomatch player models (models/players2/<name>/ in pak0 or pak2), named with the
// COOP_HOLOMATCH_PREFIX below. Their files are read as they are; a Holomatch model has only the
// two dozen Holomatch animations, so player 2 plays the nearest of them for each single-player
// animation (coop/coop_holomatch.cpp), with Munro's timing.
//
// The guest sends the id (the "look" key, coop_look); the host takes it only if it is listed here
// and every file it needs comes from its own pak0-pak2, else player 2 is the default Munro.

#ifndef COOP_CHARACTERS_H
#define COOP_CHARACTERS_H

typedef struct
{
	const char	*id;		// what the guest sends: lower case letters, digits and _
	const char	*name;		// for people
	const char	*group;		// "crew", "hazard" or "experimental"
	const char	*head;		// model/skin, as renderInfo names them (COOP_HOLOMATCH_PREFIX: models/players2/)
	const char	*torso;
	const char	*legs;
} coopCharacter_t;

// a model name that starts with this is a Holomatch model, models/players2/<rest>/, not models/players/
#define COOP_HOLOMATCH_PREFIX		"players2:"
#define COOP_HOLOMATCH_PREFIX_LEN	9

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
	// experimental: Holomatch models, grouped by species
	// Klingons
	{ "gowron",		"Gowron",						"experimental",	"players2:gowron/default",	"players2:gowron/default",	"players2:gowron/default" },
	{ "kahless",		"Kahless",						"experimental",	"players2:kahless/default",	"players2:kahless/default",	"players2:kahless/default" },
	{ "kehlyr",		"K'Ehleyr",						"experimental",	"players2:kehlyr/default",	"players2:kehlyr/default",	"players2:kehlyr/default" },
	{ "katarah",		"Katarah",						"experimental",	"players2:katarah/default",	"players2:katarah/default",	"players2:katarah/default" },
	// Romulans
	{ "sela",			"Sela",							"experimental",	"players2:sela/default",	"players2:sela/default",	"players2:sela/default" },
	{ "tolek",		"Tolek",						"experimental",	"players2:tolek/default",	"players2:tolek/default",	"players2:tolek/default" },
	{ "tlar",			"T'Lar",						"experimental",	"players2:tlar/default",	"players2:tlar/default",	"players2:tlar/default" },
	{ "seklom",		"Seklom",						"experimental",	"players2:seklom/default",	"players2:seklom/default",	"players2:seklom/default" },
	// a Ferengi
	{ "ferengi",		"Ferengi",						"experimental",	"players2:ferengi/default",	"players2:ferengi/default",	"players2:ferengi/default" },
	// the Borg
	{ "borgqueen",	"Borg Queen",					"experimental",	"players2:borgqueen/default",	"players2:borgqueen/default",	"players2:borgqueen/default" },
	{ "1_of_12",		"One of Twelve",				"experimental",	"players2:1_of_12/default",	"players2:1_of_12/default",	"players2:1_of_12/default" },
	{ "3_of_6",		"Three of Six",					"experimental",	"players2:3_of_6/default",	"players2:3_of_6/default",	"players2:3_of_6/default" },
	{ "5_of_9",		"Five of Nine",					"experimental",	"players2:5_of_9/default",	"players2:5_of_9/default",	"players2:5_of_9/default" },
	// assimilated crew
	{ "borg_janeway",	"Borg Janeway",					"experimental",	"players2:borg-janeway/default",	"players2:borg-janeway/default",	"players2:borg-janeway/default" },
	{ "borg_torres",	"Borg Torres",					"experimental",	"players2:borg-torres/default",	"players2:borg-torres/default",	"players2:borg-torres/default" },
	{ "borg_tuvok",	"Borg Tuvok",					"experimental",	"players2:borg-tuvok/default",	"players2:borg-tuvok/default",	"players2:borg-tuvok/default" },
	// the Captain Proton holodeck program
	{ "proton",		"Captain Proton",				"experimental",	"players2:proton/default",	"players2:proton/default",	"players2:proton/default" },
	{ "chaotica",		"Chaotica",						"experimental",	"players2:chaotica/default",	"players2:chaotica/default",	"players2:chaotica/default" },
	{ "arachnia",		"Arachnia",						"experimental",	"players2:arachnia/default",	"players2:arachnia/default",	"players2:arachnia/default" },
	{ "buster",		"Buster Kincaid",				"experimental",	"players2:buster/default",	"players2:buster/default",	"players2:buster/default" },
	// Hirogen
	{ "beta",			"Beta (Hirogen)",				"experimental",	"players2:beta/default",	"players2:beta/default",	"players2:beta/default" },
	{ "gamma",		"Gamma (Hirogen)",				"experimental",	"players2:gamma/default",	"players2:gamma/default",	"players2:gamma/default" },
	// holodeck characters
	{ "desperado",	"Desperado",					"experimental",	"players2:desperado/default",	"players2:desperado/default",	"players2:desperado/default" },
	{ "paladin",		"Paladin",						"experimental",	"players2:paladin/default",	"players2:paladin/default",	"players2:paladin/default" },
};

#define COOP_NUM_CHARACTERS		( (int)( sizeof( coop_characters ) / sizeof( coop_characters[0] ) ) )
#define COOP_CHARACTER_MAX		24		// the longest id the connection carries, with its terminator

#endif // COOP_CHARACTERS_H
