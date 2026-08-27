#pragma once

namespace switchu::menu::music::timing {

// Interaction timings from the current SwitchU Music contract. Keep them
// centralized so UI polish cannot silently drift back toward slow transitions.
inline constexpr float kRootCategoryTransitionSeconds = 0.22f; // 200-250 ms target
inline constexpr float kDetailTransformSeconds = 0.40f;        // tighter physical hand-off
inline constexpr float kExitToHomeSeconds = 0.11f;             // <= 160 ms target
inline constexpr float kNowPlayingEnterSeconds = 0.28f;        // sleeve has time to travel
inline constexpr float kNowPlayingExitSeconds = 0.23f;

static_assert(kRootCategoryTransitionSeconds >= 0.20f &&
              kRootCategoryTransitionSeconds <= 0.25f);
static_assert(kDetailTransformSeconds > 0.f && kDetailTransformSeconds <= 0.50f);
static_assert(kExitToHomeSeconds > 0.f && kExitToHomeSeconds <= 0.16f);

} // namespace switchu::menu::music::timing
