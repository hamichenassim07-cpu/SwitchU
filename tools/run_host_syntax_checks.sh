#!/usr/bin/env bash
# Compile the actual changed C++ against the actual nxui interfaces. The two
# host-only headers supply declarations for platform APIs, SDL_ttf and SDL_mixer.
# They are never included by xmake or copied into runtime assets.
set -euo pipefail
SWITCHU_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$SWITCHU_ROOT"
"${CXX:-c++}" -std=c++20 -DNXUI_BACKEND_SDL2 -Wall -Wextra -Wpedantic -fsyntax-only \
    -Itests/host_include -idirafter lib/nxui/include \
    -Iprojects/menu/src -Iprojects/common/include -Ilib/espeak-ng/src/include \
    projects/menu/src/music/MusicScreen.cpp \
    projects/menu/src/music/MusicAmbientBackground.cpp \
    projects/menu/src/music/MusicAmbientPalette.cpp \
    projects/menu/src/music/MusicPreferences.cpp \
    projects/menu/src/music/MusicPhysicalMediaRenderer.cpp \
    projects/menu/src/music/MusicCoverCache.cpp \
    projects/menu/src/music/MusicIntegration.cpp \
    projects/menu/src/widgets/IconGrid.cpp \
    projects/menu/src/widgets/UserAvatarButton.cpp \
    projects/menu/src/widgets/AppletButton.cpp \
    projects/menu/src/widgets/DateTimeWidget.cpp \
    projects/menu/src/widgets/WaraWaraBackgroundPreviewV80.cpp
echo "V9.0 Music/HOME C++ syntax / actual nxui interfaces: OK (host declarations; no Switch link)"
