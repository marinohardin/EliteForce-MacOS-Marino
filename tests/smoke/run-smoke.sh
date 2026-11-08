#!/bin/sh
# Engine smoke tests that need no retail game data.
#
#   tests/smoke/run-smoke.sh server <efsp> [--wine]
#   tests/smoke/run-smoke.sh client <efsp> [--wine]
#
# <efsp> is the engine executable (efsp, efsp.exe or the Mac binary); the game
# modules (libefgame/libefui or efgame/efui DLLs) must sit next to it.
#
# server: dedicated (headless) Holomatch server, no GPU. Boots filesystem init
#         with the fixture, lists the search path, quits. Expects exit 0.
# client: full client boot (SDL window, Vulkan renderer, sound, efui load).
#         Run under xvfb-run + Mesa lavapipe on Linux. With the fixture the UI
#         stops at the missing retail SP_BUTTONTEXT.DAT, so the expected
#         result is exit != 0 with that error, after the renderer and efui
#         were brought up; a clean exit 0 is accepted too.
set -u

mode="${1:?usage: run-smoke.sh server|client <efsp> [--wine]}"
engine="${2:?missing engine path}"
wine=""
[ "${3:-}" = "--wine" ] && wine="wine"

here="$(cd "$(dirname "$0")" && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
fix="$work/data"; home="$work/home"; log="$work/out.log"
mkdir -p "$home"
"$here/make-fixture.sh" "$fix" >/dev/null || { echo "FAIL: fixture"; exit 1; }

# no dialogs, no launcher: keep it headless and scriptable
export VOYAGERSP_TEST_NODIALOG=1
export HOME="$home" XDG_DATA_HOME="$home/.local/share" XDG_CONFIG_HOME="$home/.config"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"

fix_arg="$fix"
if [ -n "$wine" ]; then
	export WINEDEBUG="${WINEDEBUG:--all}" WINEPREFIX="${WINEPREFIX:-$work/wine}"
	fix_arg="$(winepath -w "$fix" 2>/dev/null || echo "$fix")"
fi
common="+set fs_basepath $fix_arg +set fs_homepath $(printf %s "$home") +set com_basegame smoke"

# timeout(1) is "gtimeout" (coreutils) or absent on macOS
if command -v timeout >/dev/null 2>&1; then tmo() { timeout "$@"; }
elif command -v gtimeout >/dev/null 2>&1; then tmo() { gtimeout "$@"; }
else tmo() { shift; "$@"; }; fi

fail() { echo "FAIL: $*"; echo "---- output ----"; cat "$log"; echo "----------------"; exit 1; }
expect() { grep -q -- "$1" "$log" || fail "expected output not found: $1"; }

case "$mode" in
server)
	# shellcheck disable=SC2086
	tmo 120 $wine "$engine" +set dedicated 1 +set net_port 0 $common +path +quit </dev/null >"$log" 2>&1
	rc=$?
	[ $rc -eq 0 ] || fail "server exit status $rc (expected 0)"
	expect "----- FS_Startup -----"
	expect "zz_smoke.pk3"
	echo "PASS: server smoke (fixture pk3 mounted, clean quit)"
	;;
client)
	# shellcheck disable=SC2086
	tmo 180 $wine "$engine" $common +set r_fullscreen 0 +set s_initsound 0 +path +quit </dev/null >"$log" 2>&1
	rc=$?
	expect "----- FS_Startup -----"
	expect "zz_smoke.pk3"
	if [ $rc -eq 0 ]; then
		echo "PASS: client smoke (clean exit)"
	else
		# known stop with fixture-only data: efui loaded, retail UI text missing
		expect "SP_BUTTONTEXT.DAT"
		expect "efui"
		echo "PASS: client smoke (renderer + efui up, stopped at missing retail data, exit $rc)"
	fi
	;;
*) echo "unknown mode $mode"; exit 2 ;;
esac
