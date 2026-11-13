#!/bin/sh
# Build the Linux x86_64 dependencies (SDL3, sdl2-compat, liblzma, FFmpeg) inside
# the Steam Runtime 3 "sniper" SDK container, so they only need what the Steam
# Runtime (and so every supported distro / Steam Deck) provides.
#
#   linux/docker-build.sh        ->  build-deps/linux/install, build-ffmpeg/linux/install
#
# Needs Docker. On Apple Silicon the amd64 image runs under emulation (slow).
# SDK_IMAGE overrides the image (e.g. SDK_IMAGE=ubuntu:20.04, which then needs
# cmake ninja-build and the SDL3 build dependencies installed first; see EXTRA_APT).
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK_IMAGE="${SDK_IMAGE:-registry.gitlab.steamos.cloud/steamrt/sniper/sdk:latest}"

exec docker run --rm --platform linux/amd64 \
	-v "$ROOT:/src" -w /src \
	-e HOME=/tmp \
	"$SDK_IMAGE" \
	sh -eu -c 'linux/build-deps.sh && linux/build-ffmpeg.sh && chmod -R a+rX build-deps/linux build-ffmpeg/linux'
