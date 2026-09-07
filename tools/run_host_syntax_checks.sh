#!/usr/bin/env bash
# Compile the actual changed C++ against the actual nxui interfaces. The two
# host-only headers supply declarations for Switch input types and SDL_ttf.
# They are never included by xmake or copied into runtime assets.
set -euo pipefail
SWITCHU_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$SWITCHU_ROOT"
"${CXX:-c++}" -std=c++20 -DNXUI_BACKEND_SDL2 -Wall -Wextra -Wpedantic -fsyntax-only \
    -Itests/host_include -idirafter lib/nxui/include \
    -Iprojects/menu/src -Iprojects/common/include \
    projects/menu/src/music/MusicScreen.cpp \
    projects/menu/src/music/MusicAmbientBackground.cpp \
    projects/menu/src/music/MusicAmbientPalette.cpp \
    projects/menu/src/music/MusicAlbumDetails.cpp \
    projects/menu/src/music/MusicPhysicalMediaRenderer.cpp \
    projects/menu/src/music/MusicCoverCache.cpp
echo "V8.9 changed C++ syntax / actual nxui interfaces: OK (host declarations; no Switch link)"
