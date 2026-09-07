#!/usr/bin/env bash
set -euo pipefail
SWITCHU_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$SWITCHU_ROOT"
python3 tools/verify_music_ui_contract.py
python3 tools/test_music_navigation.py
python3 tools/test_music_compact_geometry.py
SWITCHU_CXX="${CXX:-c++}"
SWITCHU_TEST_DIR="$(mktemp -d "${TMPDIR:-/tmp}/switchu-v89-tests.XXXXXX")"
trap 'rm -rf "$SWITCHU_TEST_DIR"' EXIT
for SWITCHU_TEST in home_carousel_motion_test music_ui_transition_math_test; do
    "$SWITCHU_CXX" -std=c++20 -Wall -Wextra -Wpedantic -I. "tests/$SWITCHU_TEST.cpp" -o "$SWITCHU_TEST_DIR/$SWITCHU_TEST"
    "$SWITCHU_TEST_DIR/$SWITCHU_TEST"
done
"$SWITCHU_CXX" -std=c++20 -Wall -Wextra -Wpedantic -I. -Iprojects/common/include \
    tests/music_ambient_test.cpp projects/menu/src/music/MusicAmbientPalette.cpp \
    -o "$SWITCHU_TEST_DIR/music_ambient_test"
"$SWITCHU_TEST_DIR/music_ambient_test"
# The release build uses fast math: verify the resume/invalid-dt guard there too.
"$SWITCHU_CXX" -std=c++20 -O3 -ffast-math -I. -Iprojects/common/include \
    tests/music_ambient_test.cpp projects/menu/src/music/MusicAmbientPalette.cpp \
    -o "$SWITCHU_TEST_DIR/music_ambient_release_test"
"$SWITCHU_TEST_DIR/music_ambient_release_test"
"$SWITCHU_CXX" -std=c++20 -Wall -Wextra -Wpedantic \
    -Itests/renderer_stub -idirafter lib/nxui/include -Iprojects/menu/src/music \
    tests/music_ambient_draw_test.cpp projects/menu/src/music/MusicAmbientPalette.cpp \
    projects/menu/src/music/MusicAmbientBackground.cpp -o "$SWITCHU_TEST_DIR/music_ambient_draw_test"
"$SWITCHU_TEST_DIR/music_ambient_draw_test"
"$SWITCHU_CXX" -std=c++20 -Wall -Wextra -Wpedantic \
    -Itests/renderer_stub -idirafter lib/nxui/include -Iprojects/menu/src/music -Iprojects/common/include \
    tests/music_album_details_test.cpp projects/menu/src/music/MusicAlbumDetails.cpp \
    -o "$SWITCHU_TEST_DIR/music_album_details_test"
"$SWITCHU_TEST_DIR/music_album_details_test"
echo "SwitchU V8.9 host contracts: OK (not a Nintendo Switch build)"
