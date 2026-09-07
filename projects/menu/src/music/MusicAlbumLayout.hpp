#pragma once
namespace switchu::menu::music::albumui {
// Adapted from HOME TitlePillWidget: fixed 640 px centre, independent of the
// selected object's motion, 9 px icon gap and 42 px between metadata groups.
// HOME's y=536 title would intersect Music's reflections: their measured
// lower bound is 606.7, so the whole block can move up 3 px without entering even the reflection tail.
inline constexpr float kCentreX = 640.f;
inline constexpr float kBlockY = 607.f;
inline constexpr float kTitleY = kBlockY, kArtistY = kBlockY + 38.f;
inline constexpr float kSeparatorY = kBlockY + 68.f, kMetaY = kBlockY + 70.f;
inline constexpr float kTitleWidth = 820.f, kArtistWidth = 680.f, kMetaWidth = 640.f;
inline constexpr float kTitlePixels = 30.f, kArtistPixels = 24.f, kMetaPixels = 24.f;
inline constexpr float kClockSize = 30.f, kIconGap = 9.f, kGroupGap = 42.f;
// The last row ends at 707; the right-hand footer starts at x=990. Even long
// duration/count strings stay inside x=[320,960], with no shared text area.
}
