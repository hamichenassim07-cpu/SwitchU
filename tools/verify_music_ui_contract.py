#!/usr/bin/env python3
"""Host-side invariants for SwitchU Music's HOME-native contract.

This intentionally performs source-level checks without requiring devkitPro. It
is a regression guard, not a substitute for the real Switch build/test.
"""
from __future__ import annotations

import hashlib
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

FIX6_HASHES = {
    "projects/daemon/src/music/music_service.cpp": "385a40f5de9d495b29cffb92fbbd49644af62db3aec5c5cc54be72152777a53b",
    "projects/daemon/src/music/music_service.hpp": "36cb8a777f6a1cc5e0f551ec7cbbbae1c5de98b1aeda32353ccb401d0863c816",
    "projects/daemon/src/music/stream_decoder.cpp": "666944134db3ec7609164980900d6d07f5ace2382bea385d188f94a8d1df8537",
    "projects/daemon/src/music/stream_decoder.hpp": "ae6fa6cb2f5af71cbf7cadf3b4607bc319f3816285dc201822168fcb031b2a33",
}

failures: list[str] = []


def text(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        failures.append(message)


def sha256(rel: str) -> str:
    return hashlib.sha256((ROOT / rel).read_bytes()).hexdigest()


def balanced_calls(source: str, needle: str) -> list[str]:
    calls: list[str] = []
    pos = 0
    while True:
        start = source.find(needle, pos)
        if start < 0:
            return calls
        open_paren = start + len(needle) - 1
        depth = 0
        in_string = False
        escape = False
        for i in range(open_paren, len(source)):
            ch = source[i]
            if in_string:
                if escape:
                    escape = False
                elif ch == "\\":
                    escape = True
                elif ch == '"':
                    in_string = False
                continue
            if ch == '"':
                in_string = True
            elif ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
                if depth == 0:
                    calls.append(source[start : i + 1])
                    pos = i + 1
                    break
        else:
            return calls


def top_level_args(call: str) -> list[str]:
    inside = call[call.find("(") + 1 : -1]
    args: list[str] = []
    current: list[str] = []
    depth = 0
    in_string = False
    escape = False
    for ch in inside:
        if in_string:
            current.append(ch)
            if escape:
                escape = False
            elif ch == "\\":
                escape = True
            elif ch == '"':
                in_string = False
            continue
        if ch == '"':
            in_string = True
            current.append(ch)
        elif ch in "([{":
            depth += 1
            current.append(ch)
        elif ch in ")]}":
            depth -= 1
            current.append(ch)
        elif ch == "," and depth == 0:
            args.append("".join(current).strip())
            current = []
        else:
            current.append(ch)
    args.append("".join(current).strip())
    return args


def action_block(source: str, button: str) -> str:
    needle = f"nxui::Button::{button}"
    start = source.find(needle)
    if start < 0:
        return ""
    brace = source.find("{", start)
    if brace < 0:
        return ""
    depth = 0
    for i in range(brace, len(source)):
        ch = source[i]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return source[brace : i + 1]
    return ""

music_cpp = text("projects/menu/src/music/MusicScreen.cpp")
music_hpp = text("projects/menu/src/music/MusicScreen.hpp")
integration = text("projects/menu/src/music/MusicIntegration.cpp")
icon_grid_cpp = text("projects/menu/src/widgets/IconGrid.cpp")
icon_grid_hpp = text("projects/menu/src/widgets/IconGrid.hpp")
title_pill = text("projects/menu/src/widgets/TitlePillWidget.cpp")
game_actions = text("projects/menu/src/widgets/GameActionsHudWidget.cpp")
typography = text("projects/menu/src/widgets/HomeTypographyStyle.hpp")

# FIX6 is immutable.
for rel, expected in FIX6_HASHES.items():
    actual = sha256(rel)
    require(actual == expected, f"FIX6 changed: {rel} ({actual} != {expected})")

# No legacy terminology that would reintroduce the old UI architecture.
for token in ("m_tabIndex", "setTab(", "drawCrtBackground", "kTabCount"):
    require(token not in music_cpp + music_hpp, f"legacy Music identifier returned: {token}")

# One shared HOME motion engine and geometry source are consumed by both roots.
for source, name in ((music_hpp, "MusicScreen.hpp"), (icon_grid_hpp, "IconGrid.hpp")):
    require("HomeCarouselMotion.hpp" in source, f"{name} does not consume shared HOME motion")
require("HomeCarouselStyle.hpp" in icon_grid_cpp, "IconGrid lost shared HOME carousel style")
require("HomeCarouselStyle.hpp" in music_cpp, "Music lost shared HOME carousel style")
require("HomeCarouselMotionState" in music_hpp, "Music root does not own shared HOME motion state")
require("HomeCarouselMotionState" in icon_grid_hpp, "IconGrid does not own shared HOME motion state")

# HOME typography floor is a single source of truth.
require("kMinimumMainTextScale = 0.83f" in typography,
        "HOME minimum typography scale is no longer 0.83")
for src, name in ((music_cpp, "MusicScreen"), (title_pill, "TitlePill"), (game_actions, "GameActionsHud")):
    require("0.83f" not in src, f"{name} duplicated literal 0.83 instead of shared HOME typography")
all_menu_literals = []
for candidate in (ROOT / "projects/menu/src").rglob("*"):
    if not candidate.is_file() or candidate.suffix not in {".cpp", ".hpp"}:
        continue
    if candidate.name == "HomeTypographyStyle.hpp":
        continue
    if "0.83f" in candidate.read_text(encoding="utf-8", errors="replace"):
        all_menu_literals.append(str(candidate.relative_to(ROOT)))
require(not all_menu_literals,
        "HOME typography floor duplicated outside HomeTypographyStyle.hpp: " + ", ".join(all_menu_literals))
require("HomeTypographyStyle.hpp" in music_cpp, "Music does not consume HOME typography floor")
require("HomeTypographyStyle.hpp" in title_pill, "TitlePill does not consume HOME typography floor")
require("HomeTypographyStyle.hpp" in game_actions, "GameActionsHud does not consume HOME typography floor")

# Literal drawText/fitText scales in Music may be larger than the floor, never smaller.
for needle in ("ren.drawText(", "fitText("):
    for call in balanced_calls(music_cpp, needle):
        args = top_level_args(call)
        if not args:
            continue
        match = re.fullmatch(r"([0-9]+(?:\.[0-9]+)?)f?", args[-1])
        if match:
            require(float(match.group(1)) >= 0.83,
                    f"Music literal text scale below HOME floor in: {call[:100]}")

# Albums/Playlists stay distinct but hidden; only the D-pad switches root category.
require("View::Albums" in music_cpp and "View::Playlists" in music_cpp,
        "Albums/Playlists roots are missing")
require("Albums | Playlists" not in music_cpp and "Albums|Playlists" not in music_cpp,
        "permanent Albums/Playlists bar literal found")
up = action_block(music_cpp, "DUp")
down = action_block(music_cpp, "DDown")
stick_up = action_block(music_cpp, "LStickU")
stick_down = action_block(music_cpp, "LStickD")
require("setRootCategory" in up and "rootView()" in up,
        "D-Pad Up no longer switches the hidden root category")
require("setRootCategory" in down and "rootView()" in down,
        "D-Pad Down no longer switches the hidden root category")
require("setRootCategory" not in stick_up and "if (!rootView())" in stick_up,
        "left-stick Up can change Albums/Playlists")
require("setRootCategory" not in stick_down and "if (!rootView())" in stick_down,
        "left-stick Down can change Albums/Playlists")

# Root actions / native HOME components.
require("Y File" not in music_cpp and "Y Files" not in music_cpp,
        "obsolete Y File action returned")
require("GlossyIcon m_musicHomeCard" in music_hpp,
        "Music root no longer uses HOME GlossyIcon")
require("m_homeTitlePill" in music_hpp and "setMusicMode(true)" in integration,
        "Music no longer reuses the real HOME TitlePill")
require("setWifiVisible(false)" in integration and "setWifiVisible(true)" in integration,
        "Music Wi-Fi hide/restore contract is incomplete")

# Reflection must remain clipped to the declared floor.
reflection_start = music_cpp.find("Reflection is a property of the floor")
reflection_end = music_cpp.find("void MusicScreen::drawMusicBackground", reflection_start)
reflection = music_cpp[reflection_start:reflection_end] if reflection_start >= 0 and reflection_end > reflection_start else ""
require("floorClip" in reflection and "pushClipRect(floorClip)" in reflection and "popClipRect()" in reflection,
        "carousel reflection lost its floor-only clip")
require("drawTexturedTriangle" in reflection, "reflection draw path was unexpectedly removed")

# Fixed Music background: no known cover-palette machinery should be reintroduced.
for forbidden in ("dominantColor", "extractPalette", "coverPalette", "adaptivePalette"):
    require(forbidden not in music_cpp, f"cover-adaptive background path returned: {forbidden}")

# Real Now Playing remains present; Mario/Nintendo asset is not bundled by us.
require("View::NowPlaying" in music_cpp and "drawNowPlaying" in music_cpp,
        "Now Playing surface is missing")
runner = ROOT / "romfs/icons/music_now_playing_runner.png"
require(not runner.exists(), "Nintendo/runner image is bundled; asset choice must remain external")

if failures:
    print("SwitchU Music HOME-native contract: FAILED", file=sys.stderr)
    for failure in failures:
        print(f" - {failure}", file=sys.stderr)
    sys.exit(1)

print("SwitchU Music HOME-native contract: OK")
print("FIX6 audio hashes: OK")
