#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
python3 tools/verify_music_ui_contract.py
CXX="${CXX:-c++}"
OUT="${TMPDIR:-/tmp}/switchu_home_carousel_contract_test"
"$CXX" -std=c++20 -Wall -Wextra -Wpedantic -I. tests/home_carousel_motion_test.cpp -o "$OUT"
"$OUT"
rm -f "$OUT"
OUT2="${TMPDIR:-/tmp}/switchu_music_transition_math_test"
"$CXX" -std=c++20 -Wall -Wextra -Wpedantic -I. tests/music_ui_transition_math_test.cpp -o "$OUT2"
"$OUT2"
rm -f "$OUT2"
OUT3="${TMPDIR:-/tmp}/switchu_music_physical_dynamics_test"
"$CXX" -std=c++20 -Wall -Wextra -Wpedantic -I. tests/music_physical_dynamics_test.cpp -o "$OUT3"
"$OUT3"
rm -f "$OUT3"
echo "SwitchU host contract checks: OK"
