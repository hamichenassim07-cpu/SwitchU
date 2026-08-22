#pragma once

#include <cstdint>
#include <unordered_map>

// V10.27: read the real Horizon play log through pdm:qry.
// No value is fabricated: callers get false when Horizon has no usable data.
class PlayTimeProvider {
public:
    static PlayTimeProvider& instance();

    bool minutesFor(std::uint64_t titleId, std::uint64_t& minutesOut);

private:
    struct CacheEntry {
        bool available = false;
        std::uint64_t minutes = 0;
        std::uint64_t queriedAtMs = 0;
    };

    PlayTimeProvider() = default;
    ~PlayTimeProvider();

    bool ensureReady();

    bool m_initAttempted = false;
    bool m_ready = false;
    std::uint64_t m_lastInitAttemptMs = 0;
    std::unordered_map<std::uint64_t, CacheEntry> m_cache;
};
