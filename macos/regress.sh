#!/bin/sh
# Regression harness: proves a change leaves single player and Holomatch game logic unchanged.
#
#   macos/regress.sh record [NAME]   run every scenario with the current build, keep it as baseline NAME
#   macos/regress.sh check [NAME]    run them again, compare with baseline NAME (default: the newest)
#   macos/regress.sh list            the recorded baselines
#
# NAME defaults to the commit (git describe --always --dirty), so a baseline is per commit/build.
# Typical use for a change on your branch:
#   git switch --detach <base>; cmake --build build-macos -j10 (+ efgame/buildmac); macos/regress.sh record
#   git switch <branch>;        rebuild;                                           macos/regress.sh check
# (or record on your branch before you edit, then check after). Exit status: 0 all identical,
# 1 something differed or failed, 2 usage error.
#
# What it runs (each in a fresh scratch HOME with symlinks to the retail pak0-pak3, so nothing touches
# your game folder, config or saves): the engine with +set regress_log (qcommon/regress.c), which fixes
# the time step (one 50 ms game frame per engine frame, no waiting on the clock), the random seeds and
# the local player's input (a scripted walk/strafe/turn/jump/crouch/fire pattern), and logs every
# entity's game state each frame. The scenarios:
#   sp-borg1       SP new game on borg1 (intro cutscene, ICARUS scripts, NPCs), 2000 frames
#   sp-save        SP: load a voy1 save, scripted input, 600 frames (REGRESS_SAVE, see below)
#   sp-transition  SP new game on borg1, `maptransition borg2` at frame 40, on to frame 400
#   hm-listen      Holomatch listen server on hm_voy1, 3 bots + the local client connecting, 1200 frames
#   hm-dedicated   Holomatch dedicated server on hm_voy1 (port 27980), 3 bots, 1200 frames
# check prints, per scenario, "identical" or the first differing frame, entity and fields.
#
# Environment:
#   APP=<path.app>     test this app instead (default: assemble "EF Test Regression" from build-macos
#                      and efgame/buildmac with make-app.sh NO_BUILD=1, and delete it afterwards)
#   SCENARIOS="..."    run only these (default: all of the above)
#   REGRESS_DIR=<dir>  where baselines live (default ~/Library/Caches/VoyagerSP-regress)
#   REGRESS_SAVE=<file.sav>  the save for sp-save (default: the newest voy1 save in your saves folder;
#                      record copies it into the baseline, and check loads that copy)
#   EF_DATA=<dir>      the baseEF folder with pak0-pak3 (default ~/Library/Application Support/EliteForce-MacOS/baseEF)
#   REGRESS_ARGS="..." extra arguments for every run, e.g. "+set sp_coop 0" (default: none)
#   KEEP=1             keep the scratch folder (logs, console output) for a look
#   REGRESS_CMOD=1     Holomatch with cMod's game modules (the app's Contents/Resources/cmod) instead of
#                      the stock ones the runs use otherwise (+set sv_cmodGame 0 +set cl_cmodModules 0):
#                      different game code, so record a baseline of its own for it
#   TMPDIR=<dir>       where the scratch folder goes (default: your private temp folder; use one only
#                      this run uses: it holds the scratch HOMEs while the game runs)
#   VOYAGERSP_TEST_BACKGROUND=0  show the game window the usual way. By default the runs use the quiet
#                      background mode (sys_osx.m): the window opens behind your other windows, the app
#                      never comes forward or takes the mouse/keyboard, and system input is ignored
#
# Runtime: about 2 minutes for all five on an M-series Mac. A window opens (behind the others) for the SP
# and listen-server runs; it doesn't take keys or the mouse, so you can keep working.
#
# What it can't catch: rendering, sound, music, UI/menus, input handling, network timing and anything
# the fingerprint doesn't include (game-private state outside the logged fields, client-side cgame
# state). Holomatch fingerprints are the network-visible entity/player states, not the QVM's private
# data. It only covers what the scenarios reach in their frames. See docs/macos-port-notes.md.
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REGRESS_DIR="${REGRESS_DIR:-$HOME/Library/Caches/VoyagerSP-regress}"
EF_DATA="${EF_DATA:-$HOME/Library/Application Support/EliteForce-MacOS/baseEF}"
ALL_SCENARIOS="sp-borg1 sp-save sp-transition hm-listen hm-dedicated"
SCENARIOS="${SCENARIOS:-$ALL_SCENARIOS}"
TEST_NAME="EF Test Regression"
TEST_ID="org.voyagersp.testregress"
PORT=27980
LSREGISTER=/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister

usage() { sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }

cmd="${1:-}"; [ $# -gt 0 ] && shift
NAME="${1:-}"

case "$cmd" in
list)
	[ -d "$REGRESS_DIR" ] || { echo "no baselines in $REGRESS_DIR"; exit 0; }
	ls -t "$REGRESS_DIR" | while read -r b; do
		printf '%-28s %s\n' "$b" "$(sed -n 's/^recorded=//p' "$REGRESS_DIR/$b/meta" 2>/dev/null)"
	done
	exit 0 ;;
record|check) ;;
*) usage ;;
esac

if [ "$cmd" = record ]; then
	[ -n "$NAME" ] || NAME="$(git -C "$ROOT" describe --always --dirty --exclude '*' 2>/dev/null || echo unknown)"
else
	[ -n "$NAME" ] || NAME="$(ls -t "$REGRESS_DIR" 2>/dev/null | head -1)"
	[ -n "$NAME" ] && [ -d "$REGRESS_DIR/$NAME" ] || { echo "regress: no baseline ${NAME:-recorded yet} in $REGRESS_DIR (run record first)" >&2; exit 2; }
fi
BASE="$REGRESS_DIR/$NAME"

for p in pak0 pak1 pak2 pak3; do
	[ -f "$EF_DATA/$p.pk3" ] || { echo "regress: $EF_DATA/$p.pk3 not found (set EF_DATA)" >&2; exit 2; }
done

T="${TMPDIR:-/tmp}"
SCRATCH="$(mktemp -d "${T%/}/voyagersp-regress.XXXXXX")"
OWN_APP=""
cleanup() {
	# stop a run that's still going and wait for it to exit before its scratch HOME goes
	if [ -n "${PID:-}" ]; then
		kill "$PID" 2>/dev/null
		n=0; while kill -0 "$PID" 2>/dev/null && [ $n -lt 20 ]; do sleep 0.5; n=$((n + 1)); done
		kill -9 "$PID" 2>/dev/null; wait "$PID" 2>/dev/null
	fi
	if [ -n "$OWN_APP" ]; then
		"$LSREGISTER" -u "$OWN_APP" 2>/dev/null
		rm -rf "$OWN_APP"
		defaults delete "$TEST_ID" >/dev/null 2>&1
	fi
	if [ -n "${KEEP:-}" ]; then echo "regress: scratch kept in $SCRATCH"; else rm -rf "$SCRATCH"; fi
}
trap cleanup EXIT
trap 'exit 1' INT TERM HUP

# ---- the app under test -------------------------------------------------------
if [ -z "${APP:-}" ]; then
	OWN_APP="$ROOT/dist/$TEST_NAME.app"
	echo "regress: assembling $OWN_APP from build-macos + efgame/buildmac"
	APP_NAME="$TEST_NAME" BUNDLE_ID="$TEST_ID" NO_BUILD=1 "$ROOT/macos/make-app.sh" >"$SCRATCH/make-app.log" 2>&1 ||
		{ cat "$SCRATCH/make-app.log" >&2; exit 1; }
	APP="$OWN_APP"
fi
EXE="$(find "$APP/Contents/MacOS" -maxdepth 1 -type f -perm -u+x ! -name '*.dylib' | head -1)"
[ -x "$EXE" ] || { echo "regress: no executable in $APP" >&2; exit 2; }
BUILD_ID="efsp $(md5 -q "$EXE" | cut -c1-12), libefgame $(md5 -q "$APP/Contents/MacOS/libefgame.dylib" | cut -c1-12)"

# ---- the voy1 save --------------------------------------------------------------
# a save's COMM chunk holds its map name as one of its first strings
save_map() { head -c 256 "$1" | strings -n 3 | awk '{print $NF}' | grep -m1 -E '^_?[a-z]+[0-9]*$'; }
newest_voy1_save() {
	ls -t "$EF_DATA/saves" 2>/dev/null | grep '\.sav$' | grep -v -x -e auto.sav -e __trans.sav | while read -r f; do
		[ "$(save_map "$EF_DATA/saves/$f")" = voy1 ] && { echo "$EF_DATA/saves/$f"; break; }
	done
}
SAVE=""
case " $SCENARIOS " in *" sp-save "*)
	if [ "$cmd" = check ]; then
		SAVE="$BASE/sp-save.sav"
	elif [ -n "${REGRESS_SAVE:-}" ]; then
		SAVE="$REGRESS_SAVE"
	else
		SAVE="$(newest_voy1_save)"
	fi
	[ -f "$SAVE" ] || { echo "regress: sp-save: no voy1 save (${SAVE:-none found}); set REGRESS_SAVE or drop sp-save from SCENARIOS" >&2; exit 2; }
esac

# ---- one scenario ------------------------------------------------------------------
# run_scenario NAME LOG: run it with a fresh HOME, write the fingerprint log to LOG
run_scenario() {
	RC=""
	sc="$1"; log="$2"
	H="$SCRATCH/home-$sc"
	D="$H/Library/Application Support/EliteForce-MacOS/baseEF"
	mkdir -p "$D/saves"
	for p in pak0 pak1 pak2 pak3; do
		ln -s "$EF_DATA/$p.pk3" "$D/$p.pk3"
		[ -f "$D/$p.pk3" ] || { echo "regress: $D/$p.pk3 isn't readable" >&2; SECS=0; return 1; }
	done
	common="+set regress_log \"$log\" +set r_fullscreen 0 +set r_swapInterval 0 +set com_maxfps 0 ${REGRESS_ARGS:-}"
	sp="+set com_holomatch 0 $common"
	# the stock game modules (pak2): the app's cMod modules (by Noah Metzger, macos/cmod) are other game code
	# and would change every Holomatch run; REGRESS_CMOD=1 keeps them (for a baseline of its own)
	stock="+set sv_cmodGame 0 +set cl_cmodModules 0"
	[ -n "${REGRESS_CMOD:-}" ] && stock=""
	hm="+set com_holomatch 1 $common $stock +set sv_hostname Regress +set sv_master1 \"\" +set sv_master2 \"\" +set sv_master3 \"\" +set g_gametype 0 +set bot_enable 1 +set bot_minplayers 4 +set sv_maxclients 8 +set net_port $PORT"
	case "$sc" in
	sp-borg1)      args="$sp +set regress_frames 2000 +map borg1" ;;
	sp-save)       cp "$SAVE" "$D/saves/regress.sav"
	               args="$sp +set regress_frames 600 +load regress" ;;
	sp-transition) args="$sp +set regress_frames 400 +set regress_cmdframe 40 +set regress_cmd \"maptransition borg2\" +map borg1" ;;
	hm-listen)     args="$hm +set dedicated 0 +set regress_frames 1200 +map hm_voy1" ;;
	hm-dedicated)  args="$hm +set dedicated 1 +set regress_frames 1200 +map hm_voy1" ;;
	*) echo "regress: unknown scenario $sc" >&2; return 1 ;;
	esac
	eval "set -- $args"
	# VOYAGERSP_TEST_NODIALOG: an error prints and exits instead of showing an alert (sys_osx.m);
	# VOYAGERSP_TEST_BACKGROUND: the quiet background mode, unless set to 0
	env HOME="$H" VOYAGERSP_TEST_NODIALOG=1 VOYAGERSP_TEST_BACKGROUND="${VOYAGERSP_TEST_BACKGROUND-1}" \
		"$EXE" "$@" >"$SCRATCH/$sc.out" 2>&1 </dev/null &
	PID=$!
	t=0
	while kill -0 "$PID" 2>/dev/null; do
		sleep 1; t=$((t + 1))
		[ $t -ge 600 ] && { kill "$PID"; echo "  (killed after ${t}s)"; break; }
	done
	wait "$PID" 2>/dev/null; RC=$?
	PID=""
	SECS=$t
	grep -q '^end ' "$log" 2>/dev/null && [ $RC -eq 0 ]
}

# ---- compare -------------------------------------------------------------------------
# compare BASELINE NEW: print where they first differ (frame, entity, fields)
compare() {
	a="$1"; b="$2"
	line="$(cmp "$a" "$b" 2>/dev/null | sed -n 's/.* line \([0-9]*\).*/\1/p')"
	[ -n "$line" ] || line="$(( $(wc -l <"$a") < $(wc -l <"$b") ? $(wc -l <"$a") + 1 : $(wc -l <"$b") + 1 ))"
	awk -v L="$line" '
	# pass 1 (baseline) and 2 (new): read up to the end of the frame that holds line L
	FNR == 1 { file++; frame = -1; done = 0 }
	done { next }
	$1 == "f" { if (FNR > L) { done = 1; next } frame = $2; hdr[file] = $0; next }
	$1 == "e" || $1 == "p" || $1 == "l" { k = $1 " " $2; v = $0; sub(/^[^ ]+ [^ ]+ /, "", v); val[file, k] = v; keys[k] = 1
		if (FNR >= L) { inframe[k] = 1 } ; next }
	$1 == "x" { k = $2 " " $3; val[file, k] = "(gone)"; keys[k] = 1; if (FNR >= L) inframe[k] = 1; next }
	$1 == "end" { endl[file] = $0 }
	END {
		split(hdr[1], h, " ")
		if (hdr[1] != hdr[2]) {
			printf "    differs at line %d: baseline [%s] vs new [%s]\n", L, hdr[1], hdr[2]
		} else {
			printf "    first difference: frame %s (map %s, game time %s)\n", h[2], h[3], h[4]
		}
		shown = 0
		for (k in inframe) {
			if (val[1, k] == val[2, k]) continue
			if (++shown > 8) { print "    ..."; break }
			split(k, kk, " "); kind = kk[1] == "e" ? "entity" : (kk[1] == "p" ? "player state" : "level")
			na = split(val[1, k], fa, " "); nb = split(val[2, k], fb, " ")
			cls = ""; for (i = 1; i <= na; i++) if (fa[i] ~ /^class=/) cls = " (" substr(fa[i], 7) ")"
			printf "    %s %s%s:\n", kind, kk[2], cls
			if (val[1, k] == "" || val[2, k] == "" || val[1, k] == "(gone)" || val[2, k] == "(gone)") {
				va = val[1, k] == "" ? "(absent)" : substr(val[1, k], 1, 200)
				vb = val[2, k] == "" ? "(absent)" : substr(val[2, k], 1, 200)
				printf "      baseline: %s\n      new:      %s\n", va, vb
				continue
			}
			for (i = 1; i <= na; i++) { split(fa[i], p, "="); A[p[1]] = substr(fa[i], length(p[1]) + 2) }
			for (i = 1; i <= nb; i++) { split(fb[i], p, "="); f = p[1]; v = substr(fb[i], length(f) + 2)
				if (!(f in A)) printf "      %s: (none) -> %s\n", f, v
				else if (A[f] != v) printf "      %s: %s -> %s\n", f, A[f], v }
			delete A
		}
		if (!shown) printf "    (line %d differs)\n", L
	}' "$a" "$b"
}

# ---- run -------------------------------------------------------------------------------
echo "regress: $cmd baseline '$NAME' ($REGRESS_DIR)"
echo "regress: app $APP ($BUILD_ID)"
if [ "$cmd" = record ]; then
	rm -rf "$BASE"; mkdir -p "$BASE"
	{ echo "recorded=$(date '+%Y-%m-%d %H:%M') $(git -C "$ROOT" describe --always --dirty --exclude '*' 2>/dev/null)"
	  echo "build=$BUILD_ID"; echo "app=$APP"; echo "scenarios=$SCENARIOS"; } >"$BASE/meta"
	[ -n "$SAVE" ] && cp "$SAVE" "$BASE/sp-save.sav"
else
	echo "regress: baseline $(sed -n 's/^recorded=//p' "$BASE/meta"), $(sed -n 's/^build=//p' "$BASE/meta")"
fi

status=0
START=$(date +%s)
for sc in $SCENARIOS; do
	log="$SCRATCH/$sc.log"
	printf '%-14s ' "$sc"
	if [ "$cmd" = check ] && [ ! -f "$BASE/$sc.log.gz" ]; then
		echo "SKIPPED (not in the baseline)"; continue
	fi
	if ! run_scenario "$sc" "$log"; then
		last="$(grep '^f ' "$log" 2>/dev/null | tail -1)"
		echo "FAILED after ${SECS}s (exit code ${RC:-?}): the run ended before its last frame (${last:-no frames logged}); see $sc.out:"
		grep -iE 'error|fatal|signal|crash' "$SCRATCH/$sc.out" | head -5 | sed 's/^/    /'
		KEEP=1; status=1; continue
	fi
	end="$(tail -1 "$log")"
	if [ "$cmd" = record ]; then
		gzip -c "$log" >"$BASE/$sc.log.gz"
		echo "recorded ${end#end } in ${SECS}s"
	else
		gunzip -c "$BASE/$sc.log.gz" >"$SCRATCH/$sc.base.log"
		if cmp -s "$SCRATCH/$sc.base.log" "$log"; then
			echo "identical (${end#end }, ${SECS}s)"
		else
			echo "DIFFERS (${SECS}s)"
			compare "$SCRATCH/$sc.base.log" "$log"
			status=1
		fi
	fi
done
echo "regress: done in $(( $(date +%s) - START ))s"
[ "$cmd" = check ] && { [ $status -eq 0 ] && echo "regress: ALL IDENTICAL" || echo "regress: DIFFERENCES FOUND"; }
exit $status
