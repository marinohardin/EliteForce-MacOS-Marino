// g_regress.cpp -- the regression harness's view of the single-player game state (macos/regress.sh).
//
// The engine's SP bridge (sp/sp_bridge.cpp) looks this up with dlsym and calls it after every
// G_RunFrame, but only when the harness is on (the regress_log cvar is set): it is never called
// otherwise, so it changes nothing in normal play. It formats each in-use entity's game state as
// one line of "field=value" pairs; the engine logs a line only when it changed since the last frame
// (qcommon/regress.c), and the harness compares the logs of two builds. Times that follow the clock
// every frame (nextthink, commandTime) are logged relative to level.time, so they don't change a
// line on every frame.

#include "g_local.h"
#include <stdio.h>
#include <stdarg.h>

static int R_Append( char *buf, int len, int size, const char *fmt, ... ) {
	va_list ap;
	if ( len >= size ) return len;
	va_start( ap, fmt );
	int n = vsnprintf( buf + len, size - len, fmt, ap );
	va_end( ap );
	return n < 0 ? len : len + n;
}

#define V3(v)	(v)[0], (v)[1], (v)[2]

// emit( kind, num, fields ): kind 0 = entity, 1 = player state, 2 = level
extern "C" void G_RegressDump( void (*emit)( int kind, int num, const char *fields ) ) {
	char	buf[2048];
	int		len, i, j;

	len = R_Append( buf, 0, sizeof( buf ), "time=%d framenum=%d", level.time, level.framenum );
	emit( 2, 0, buf );

	for ( i = 0; i < globals.num_entities; i++ ) {
		gentity_t *e = &g_entities[i];
		if ( !e->inuse ) continue;
		len = 0;
		len = R_Append( buf, len, sizeof( buf ),
			"class=%s eType=%d eFlags=%d org=%.8g,%.8g,%.8g ang=%.8g,%.8g,%.8g health=%d flags=%d "
			"takedamage=%d contents=%d svFlags=%d linked=%d think=%d mover=%d model=%d frame=%d "
			"legs=%d torso=%d event=%d count=%d enemy=%d delayScript=%d tasks=",
			e->classname ? e->classname : "-", e->s.eType, e->s.eFlags, V3( e->currentOrigin ),
			V3( e->currentAngles ), e->health, e->flags, e->takedamage, e->contents, e->svFlags,
			e->linked, e->nextthink ? e->nextthink - level.time : 0, (int)e->moverState, e->s.modelindex, e->s.frame,
			e->s.legsAnim, e->s.torsoAnim, e->s.event, e->count,
			e->enemy ? e->enemy->s.number : -1, e->delayScriptTime );
		for ( j = 0; j < NUM_TIDS; j++ ) {
			len = R_Append( buf, len, sizeof( buf ), j ? ",%d" : "%d", e->taskID[j] );
		}
		if ( e->NPC ) {
			len = R_Append( buf, len, sizeof( buf ), " bState=%d squad=%d",
				(int)e->NPC->behaviorState, e->NPC->squadState );
		}
		emit( 0, i, buf );

		if ( e->client ) {
			playerState_t *ps = &e->client->ps;
			len = R_Append( buf, 0, sizeof( buf ),
				"cmdAge=%d pm_type=%d pm_flags=%d org=%.8g,%.8g,%.8g vel=%.8g,%.8g,%.8g "
				"view=%.8g,%.8g,%.8g weapon=%d wstate=%d legs=%d torso=%d stats=",
				level.time - ps->commandTime, ps->pm_type, ps->pm_flags, V3( ps->origin ), V3( ps->velocity ),
				V3( ps->viewangles ), ps->weapon, ps->weaponstate, ps->legsAnim, ps->torsoAnim );
			for ( j = 0; j < MAX_STATS; j++ ) {
				len = R_Append( buf, len, sizeof( buf ), j ? ",%d" : "%d", ps->stats[j] );
			}
			len = R_Append( buf, len, sizeof( buf ), " ammo=" );
			for ( j = 0; j < MAX_AMMO; j++ ) {
				len = R_Append( buf, len, sizeof( buf ), j ? ",%d" : "%d", ps->ammo[j] );
			}
			emit( 1, i, buf );
		}
	}
}
