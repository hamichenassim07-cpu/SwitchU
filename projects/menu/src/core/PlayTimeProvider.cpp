#include "PlayTimeProvider.hpp"

#include <switch.h>
#include <chrono>

namespace {
constexpr std::uint64_t kNanosecondsPerMinute = 60000000000ULL;
constexpr std::uint64_t kCacheLifetimeMs = 15000ULL;

std::uint64_t monotonicMs() {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}
}

PlayTimeProvider& PlayTimeProvider::instance() {
    static PlayTimeProvider provider;
    return provider;
}

PlayTimeProvider::~PlayTimeProvider() {
    if (m_ready)
        pdmqryExit();
}

bool PlayTimeProvider::ensureReady() {
    if (m_ready)
        return true;

    const std::uint64_t nowMs = monotonicMs();
    if (m_initAttempted &&
        nowMs >= m_lastInitAttemptMs &&
        nowMs - m_lastInitAttemptMs < 5000ULL) {
        return false;
    }

    m_initAttempted = true;
    m_lastInitAttemptMs = nowMs;
    m_ready = R_SUCCEEDED(pdmqryInitialize());
    return m_ready;
}

bool PlayTimeProvider::minutesFor(std::uint64_t titleId,
                                  std::uint64_t& minutesOut) {
    minutesOut = 0;
    if (titleId == 0)
        return false;

    const std::uint64_t nowMs = monotonicMs();
    if (const auto it = m_cache.find(titleId); it != m_cache.end() &&
        nowMs >= it->second.queriedAtMs &&
        nowMs - it->second.queriedAtMs < kCacheLifetimeMs) {
        if (it->second.available)
            minutesOut = it->second.minutes;
        return it->second.available;
    }

    CacheEntry entry{};
    entry.queriedAtMs = nowMs;
    if (ensureReady()) {
        PdmPlayStatistics stats{};
        // false keeps the standard retail play-log policy. libnx converts
        // older firmware minute-based records to the current nanosecond field.
        if (R_SUCCEEDED(
                pdmqryQueryPlayStatisticsByApplicationId(
                    titleId,
                    false,
                    &stats))) {
            std::uint64_t minutes = stats.playtime / kNanosecondsPerMinute;
            // A real non-zero session shorter than one minute should not look
            // identical to a title that has never been played.
            if (stats.playtime > 0 && minutes == 0)
                minutes = 1;
            entry.available = true;
            entry.minutes = minutes;
            minutesOut = minutes;
        }
    }

    m_cache[titleId] = entry;
    return entry.available;
}
