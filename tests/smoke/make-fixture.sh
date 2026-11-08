#!/bin/sh
# Build the smoke-test fixture: <outdir>/smoke/zz_smoke.pk3 holding only a
# default.cfg with a few binds. No retail data. With com_basegame "smoke" (not
# baseEF) the engine starts in standalone mode and does not demand pak0.pk3.
#
#   tests/smoke/make-fixture.sh <outdir>
set -eu
out="${1:?usage: make-fixture.sh <outdir>}"
mkdir -p "$out/smoke"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
cat > "$tmp/default.cfg" <<'CFG'
// smoke-test default.cfg (no retail data)
unbindall
bind w "+forward"
bind s "+back"
bind a "+moveleft"
bind d "+moveright"
bind SPACE "+moveup"
bind MOUSE1 "+attack"
bind ESCAPE "togglemenu"
seta smoke_marker "1"
CFG
python3 -I - "$tmp/default.cfg" "$out/smoke/zz_smoke.pk3" <<'PY'
import sys, zipfile
src, dst = sys.argv[1:3]
with zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED) as z:
    z.write(src, "default.cfg")
PY
echo "$out/smoke/zz_smoke.pk3"
