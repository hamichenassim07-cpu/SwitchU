#pragma once

namespace switchu::homeui {

// Smallest permanent text scale used on the main HOME surface.
// Music must never shrink meaningful text below this value; long content is
// ellipsised or scrolled instead. Keeping the value here prevents HOME and
// Music typography from silently diverging.
inline constexpr float kMinimumMainTextScale = 0.83f;
inline constexpr float kMainActionGlyphScale = 0.97f;

static_assert(kMinimumMainTextScale > 0.f);
static_assert(kMainActionGlyphScale >= kMinimumMainTextScale);

} // namespace switchu::homeui
