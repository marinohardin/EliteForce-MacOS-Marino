#!/bin/sh
# Launch the macOS build against a retail Elite Force install.
#   ./run-macos.sh                      (uses ./Star Trek Voyager Elite Force)
#   EF_DIR=/path/to/install ./run-macos.sh +map borg1
# Extra arguments are passed to the engine (e.g. +set r_fullscreen 1).
# This runs the dev build (Homebrew libs). For a self-contained, double-clickable
# app use macos/make-app.sh, which writes dist/EliteForceSP.app.
cd "$(dirname "$0")"
EF_DIR="${EF_DIR:-$PWD/Star Trek Voyager Elite Force}"
cp efgame/buildmac/libefgame.dylib efgame/buildmac/libefui.dylib build-macos/ || exit 1
export SDL_VULKAN_LIBRARY="${SDL_VULKAN_LIBRARY:-/opt/homebrew/lib/libvulkan.1.dylib}"
exec build-macos/efsp +set fs_basepath "$EF_DIR" \
	+set r_fullscreen 0 +set r_mode -1 +set r_customwidth 1280 +set r_customheight 720 "$@"
