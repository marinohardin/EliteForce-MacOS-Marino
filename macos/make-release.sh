#!/bin/sh
# Package a release from the app make-app.sh builds:
#   dist/EliteForce-MacOS-Marino-<version>-arm64.dmg  (the app, an Applications shortcut, Read Me)
#   dist/EliteForce-MacOS-Marino-<version>-arm64.zip  (the app and Read Me)
#
#   macos/make-release.sh               build (make-app.sh), then package
#   NO_BUILD=1 macos/make-release.sh    package the existing dist/Elite Force.app
#
# Upload both to a GitHub release (draft first), e.g.
#   gh release create macos-v<version> dist/EliteForce-MacOS-Marino-<version>-arm64.{dmg,zip} --draft
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIST="$ROOT/dist"
APP="$DIST/Elite Force.app"
NAME="EliteForce-MacOS-Marino"

[ -n "${NO_BUILD:-}" ] || "$ROOT/macos/make-app.sh"
[ -d "$APP" ] || { echo "no $APP: run macos/make-app.sh" >&2; exit 1; }
VERSION="$(defaults read "$APP/Contents/Info" CFBundleShortVersionString)"
BASE="$NAME-$VERSION-arm64"
STAGE="$DIST/release/$NAME-$VERSION"

codesign --verify --strict --deep "$APP"
rm -rf "$DIST/release"
mkdir -p "$STAGE"
ditto "$APP" "$STAGE/Elite Force.app"
sed "s|@VERSION@|$VERSION|g" "$ROOT/macos/release/ReadMe.txt" > "$STAGE/Read Me.txt"

# zip: ditto keeps the bundle's signature, permissions and symlinks intact
rm -f "$DIST/$BASE.zip"
ditto -c -k --keepParent "$STAGE" "$DIST/$BASE.zip"

# dmg: the same plus an Applications shortcut to drag the app onto, laid out
# over a background picture (macos/release/background.tiff, from
# make-dmg-background.py; keep the icon positions below in step with it).
# Finder arranges the window, so the first run may ask to let this terminal
# control Finder; without that the DMG still works, just without the layout.
VOL="Elite Force $VERSION"
APP_X=165; APPS_X=495; ICON_Y=190; README_X=330; README_Y=366
# Finder places icons FINDER_DY points below the position it's given (it
# reports the shifted position back), so ask that much higher
FINDER_DY=36
WIN_W=660; WIN_H=450
ln -s /Applications "$STAGE/Applications"
mkdir "$STAGE/.background"
cp "$ROOT/macos/release/background.tiff" "$STAGE/.background/background.tiff"
cp "$APP/Contents/Resources/AppIcon.icns" "$STAGE/.VolumeIcon.icns"
RW="$DIST/$BASE-rw.dmg"
rm -f "$RW" "$DIST/$BASE.dmg"
[ -d "/Volumes/$VOL" ] && hdiutil detach -quiet "/Volumes/$VOL" 2>/dev/null || true
hdiutil create -quiet -volname "$VOL" -srcfolder "$STAGE" -fs HFS+ -format UDRW -ov "$RW"
MNT="$(hdiutil attach -readwrite -noverify -noautoopen "$RW" | awk -F'\t' '/\/Volumes\//{print $NF}')"
SetFile -a C "$MNT" 2>/dev/null || true		# show .VolumeIcon.icns as the disk's icon
if ! osascript <<EOF_AS
tell application "Finder"
	tell disk "$VOL"
		open
		set current view of container window to icon view
		set toolbar visible of container window to false
		set statusbar visible of container window to false
		set the bounds of container window to {200, 120, 200 + $WIN_W, 120 + $WIN_H + 28}
		set opts to the icon view options of container window
		set arrangement of opts to not arranged
		set icon size of opts to 112
		set text size of opts to 13
		set background picture of opts to file ".background:background.tiff"
		set position of item "Elite Force.app" of container window to {$APP_X, $((ICON_Y - FINDER_DY))}
		set position of item "Applications" of container window to {$APPS_X, $((ICON_Y - FINDER_DY))}
		set position of item "Read Me.txt" of container window to {$README_X, $((README_Y - FINDER_DY))}
		close
		open
		update without registering applications
		delay 2
		close
	end tell
end tell
EOF_AS
then
	echo "WARNING: Finder didn't lay out the DMG window (allow this terminal to control Finder in System Settings > Privacy & Security > Automation); the DMG works, unstyled" >&2
fi
sync
hdiutil detach -quiet "$MNT" || { sleep 2; hdiutil detach -force -quiet "$MNT"; }
hdiutil convert -quiet "$RW" -format UDZO -imagekey zlib-level=9 -o "$DIST/$BASE.dmg"
rm -f "$RW"
hdiutil verify -quiet "$DIST/$BASE.dmg"

echo "Built:"
for f in "$DIST/$BASE.dmg" "$DIST/$BASE.zip"; do
	echo "  $f  ($(du -h "$f" | cut -f1 | tr -d ' '), sha256 $(shasum -a 256 "$f" | cut -c1-16)…)"
done
