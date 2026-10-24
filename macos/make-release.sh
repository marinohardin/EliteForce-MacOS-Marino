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

# dmg: the same plus an Applications shortcut to drag the app onto
ln -s /Applications "$STAGE/Applications"
rm -f "$DIST/$BASE.dmg"
hdiutil create -quiet -volname "Elite Force $VERSION" -srcfolder "$STAGE" -fs HFS+ -format UDZO -ov "$DIST/$BASE.dmg"
hdiutil verify -quiet "$DIST/$BASE.dmg"

echo "Built:"
for f in "$DIST/$BASE.dmg" "$DIST/$BASE.zip"; do
	echo "  $f  ($(du -h "$f" | cut -f1 | tr -d ' '), sha256 $(shasum -a 256 "$f" | cut -c1-16)…)"
done
