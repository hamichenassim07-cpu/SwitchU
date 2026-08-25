#!/usr/bin/env python3
"""Host-side invariants for SwitchU Music V8 autonomous-finalization / physical-media contract.

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
physical_cpp = text("projects/menu/src/music/MusicPhysicalMediaRenderer.cpp")
physical_hpp = text("projects/menu/src/music/MusicPhysicalMediaRenderer.hpp")
cover_cache_cpp = text("projects/menu/src/music/MusicCoverCache.cpp")
cover_cache_hpp = text("projects/menu/src/music/MusicCoverCache.hpp")
integration = text("projects/menu/src/music/MusicIntegration.cpp")
icon_grid_cpp = text("projects/menu/src/widgets/IconGrid.cpp")
icon_grid_hpp = text("projects/menu/src/widgets/IconGrid.hpp")
title_pill = text("projects/menu/src/widgets/TitlePillWidget.cpp")
game_actions = text("projects/menu/src/widgets/GameActionsHudWidget.cpp")
typography = text("projects/menu/src/widgets/HomeTypographyStyle.hpp")
timing = text("projects/menu/src/music/MusicUiTiming.hpp")
library_cpp = text("projects/menu/src/music/MusicLibrary.cpp")
library_hpp = text("projects/menu/src/music/MusicLibrary.hpp")

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
require("GlossyIcon" not in music_hpp and "m_musicHomeCard" not in music_cpp,
        "Music content regressed to HOME application cards")
require("MusicPhysicalMediaRenderer.hpp" in music_hpp and
        "drawAlbumPhysicalMedia" in physical_cpp and "drawPlaylistPhysicalMedia" in physical_cpp,
        "physical Music media renderer is missing")
require("m_homeTitlePill" in music_hpp and "setMusicMode(true)" in integration,
        "Music no longer reuses the real HOME TitlePill")
require("setWifiVisible(false)" in integration and "setWifiVisible(true)" in integration,
        "Music Wi-Fi hide/restore contract is incomplete")

# Physical sleeves / vinyl / local artwork colour are mandatory in the new DA.
for token in ("front", "back", "depthPx", "drawVinyl", "vinylReveal", "vinylSpinRad"):
    require(token in physical_cpp + physical_hpp, f"physical-media contract missing: {token}")
require("sampleArtworkStyle" in cover_cache_cpp and "stbi_load_from_memory" in cover_cache_cpp,
        "album accent is no longer sampled from artwork")
require("gMusicAccent = artworkAccent" in music_cpp,
        "album accent is not applied to Music surfaces")

# V8: artwork analysis persists, self-prunes, and creates a real blurred/dark rear material.
for token in ("artwork_style_cache_v7.json", "artwork_back_cache", "boxBlurRgb",
              "writeTga24", "generateBackTexture", "persistStyleCache",
              "kMaxPersistentBackBytes", "kPersistentStyleMaxAgeSec",
              "lastUsedEpochSec", "prunePersistentStyleCache"):
    require(token in cover_cache_cpp, f"persistent/back-material pipeline missing: {token}")
require("getBack(" in cover_cache_hpp and "getBack(" in music_cpp,
        "generated rear material is not consumed by Music rendering")
require("backTexturePath" in cover_cache_hpp,
        "artwork style no longer tracks generated back material")
require("rearMaterialUsable" in cover_cache_cpp and "m_persistedStyles.erase(cached)" in cover_cache_cpp,
        "persistent rear-material cache no longer self-heals missing generated files")
require("playingTrack->cover" in music_cpp and "playingTrack->cover.key() != styleCover.key()" in music_cpp,
        "currently playing artwork is no longer queued as a secondary style request")

# V8 carries forward 3D lighting, level-of-detail, perspective reflection and micro-parallax.
for token in ("faceLight", "detailLevel", "vinylOutline", "vinylLagPx"):
    require(token in physical_cpp + physical_hpp, f"physical refinement missing: {token}")
require("m_sceneParallaxX" in music_cpp + music_hpp and "m_sceneParallaxY" in music_cpp + music_hpp,
        "micro-parallax state is missing")
require("m_vinylSpinBoost" in music_cpp + music_hpp and "m_vinylSpinPhase" in music_cpp + music_hpp,
        "new-track vinyl impulse/continuous spin state is missing")

# Reflection must remain strictly clipped to the floor in the physical renderer.
require("pushClipRect(floorClip)" in physical_cpp and "popClipRect()" in physical_cpp,
        "physical-media reflection lost its floor-only clip")
require("0.235f" in physical_cpp,
        "reflection is no longer constrained to roughly the lower quarter")

# Tracklist must use contextual marquee instead of shrinking selected long titles.
require("m_marqueeElapsed" in music_cpp and "drawMarqueeOrFit" in music_cpp and
        "constexpr float delay=1.0f" in music_cpp,
        "contextual marquee engine is missing")
require("titleMarquee" in music_cpp and "artistMarquee" in music_cpp and
        "View::NowPlaying && m_status.track_id" in music_cpp,
        "Now Playing long title/artist marquee was not extended")
require("drawSmokedGlassPanel" in music_cpp,
        "dark smoked/frosted glass information surfaces are missing")
require("drawPreviousRootCarousel" in music_cpp and "m_rootCategoryPreviousView" in music_hpp,
        "Albums/Playlists no longer use the outgoing+incoming vertical exchange")

# Exit is deliberately direct: 110 ms visual fade and no wait stage in the
# close path. Audio HOME fade-in may continue independently after HOME is back.
require("kExitToHomeSeconds = 0.11f" in timing and
        "timing::kExitToHomeSeconds" in music_cpp,
        "Music exit fade is no longer the 110 ms direct transition")
require("kRootCategoryTransitionSeconds = 0.22f" in timing and
        "kDetailTransformSeconds = 0.44f" in timing,
        "central Music transition timing contract is missing")
close_sources = music_cpp + integration
for forbidden_wait in ("sleep_for", "svcSleepThread", "usleep("):
    require(forbidden_wait not in close_sources,
            f"blocking wait returned to Music close path: {forbidden_wait}")
require("m_musicScreen->hide();" in integration and "setHomeApplicationsCategory(applications);" in integration,
        "Music close no longer hides UI before restoring HOME")

# V8.1 SAFE ENTRY: restore the V5/V7 joinable scan ownership after a real-Switch
# Data Abort was observed on a detached Music worker. Artwork analysis remains
# cancellable, but the library scanner must not outlive its MusicScreen owner.
require("std::future<LibrarySnapshot>" in music_hpp and "m_scanFuture" in music_cpp,
        "SAFE-ENTRY joinable library scan ownership is missing")
require("ScanTaskState" not in music_hpp and "mode=safe-async" in music_cpp,
        "detached Music library scan path returned")
require("SCAN_FILE begin" in library_cpp and "SCAN_FILE done" in library_cpp,
        "per-file library crash diagnostics are missing")
require("validId3FrameId" in library_cpp and "id3FramePayloadSupported" in library_cpp,
        "defensive ID3 frame validation is missing")
require("std::future" not in cover_cache_hpp and "m_styleFuture" not in cover_cache_cpp,
        "artwork analysis still uses a future that may block on destruction")
require("StyleWorkerState" in cover_cache_hpp and "MusicCoverCache::~MusicCoverCache" in cover_cache_cpp,
        "artwork worker lifecycle is not cancellation-safe")
require("preflightArtwork" in cover_cache_cpp and "embedded artwork preflight rejected" in cover_cache_cpp,
        "embedded artwork is not preflighted before image decode")

# No renderer-side fake blur fallback; rear material is either cached texture or
# the derived smoked back colour.
require("constexpr float taps" not in physical_cpp and "multi-tap path" not in physical_cpp,
        "legacy multi-tap rear-cover fallback returned")

# Per-frame physical cost is bounded: circle trig is precomputed and far
# neighbours use the cheapest LOD/texture tier.
require("CircleLut" in physical_cpp and "circleLut(" in physical_cpp,
        "vinyl unit-circle geometry is not precomputed")
require("pose.detailLevel >= 1" in physical_cpp and "geo.vinylOutlineCount=pose.detailLevel" in physical_cpp,
        "far-neighbour vinyl LOD is not aggressively simplified")
require("std::abs(delta) < 1.65f ? 320 : 220" in music_cpp,
        "root carousel does not use the three-tier 512/320/220 physical LOD")
require("std::vector<size_t> ends" not in music_cpp,
        "fitText still allocates a codepoint vector on the frame path")

# Fixed Music background: no known cover-palette machinery should be reintroduced.
for forbidden in ("dominantColor", "extractPalette", "coverPalette", "adaptivePalette"):
    require(forbidden not in music_cpp, f"cover-adaptive background path returned: {forbidden}")

# Real Now Playing remains present; Mario/Nintendo asset is not bundled by us.
require("View::NowPlaying" in music_cpp and "drawNowPlaying" in music_cpp,
        "Now Playing surface is missing")
runner = ROOT / "romfs/icons/music_now_playing_runner.png"
require(not runner.exists(), "Nintendo/runner image is bundled; asset choice must remain external")

if failures:
    print("SwitchU Music V8 physical/glass contract: FAILED", file=sys.stderr)
    for failure in failures:
        print(f" - {failure}", file=sys.stderr)
    sys.exit(1)

print("SwitchU Music V8 physical/glass contract: OK")
print("FIX6 audio hashes: OK")
