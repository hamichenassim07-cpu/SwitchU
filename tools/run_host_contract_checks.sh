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
echo "SwitchU host contract checks: OK"
