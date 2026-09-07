#pragma once
namespace switchu::menu::music::albumui {
// Adapted from HOME TitlePillWidget: fixed 640 px centre, independent of the
// selected object's motion, 9 px icon gap and 42 px between metadata groups.
// HOME's y=536 title would intersect Music's reflections: their measured
// lower bound is 606.7, so the Music information starts at 610, safely below.
inline constexpr float kCentreX = 640.f;
inline constexpr float kTitleY = 610.f, kArtistY = 648.f, kMetaY = 680.f;
inline constexpr float kTitleWidth = 820.f, kArtistWidth = 680.f, kMetaWidth = 640.f;
inline constexpr float kTitlePixels = 30.f, kArtistPixels = 24.f, kMetaPixels = 24.f;
inline constexpr float kClockSize = 30.f, kIconGap = 9.f, kGroupGap = 42.f;
// The last row ends at 710; the right-hand footer starts at x=990. Even long
// duration/count strings stay inside x=[320,960], with no shared text area.
}
