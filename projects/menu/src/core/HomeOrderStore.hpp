#pragma once
#include <cstdint>
#include <unordered_map>
#include <string>

// V10.27 compatibility holder. The former V10.25 recent/pinned ordering
// system is disabled; only the current suspended title is still shared with
// the resume animation.
class HomeOrderStore {
public:
    struct Metadata {
        bool pinned = false;
        int manualRank = 0;
        std::uint64_t lastLaunchTime = 0;
    };

    static HomeOrderStore& instance();

    // Legacy API kept so older call sites remain source-compatible. All order
    // metadata methods return neutral values / are no-ops in V10.27.
    const Metadata& metadata(std::uint64_t titleId);
    bool isPinned(std::uint64_t titleId);
    int manualRank(std::uint64_t titleId);
    std::uint64_t lastLaunchTime(std::uint64_t titleId);
    void pinAtRank(std::uint64_t titleId, int rank);
    void unpin(std::uint64_t titleId);
    void markLaunched(std::uint64_t titleId);

    void setSuspendedTitle(std::uint64_t titleId) { m_suspendedTitleId = titleId; }
    std::uint64_t suspendedTitleId() const { return m_suspendedTitleId; }

private:
    HomeOrderStore() = default;
    void ensureLoaded();
    void save();

    bool m_loaded = false;
    std::uint64_t m_suspendedTitleId = 0;
    std::unordered_map<std::uint64_t, Metadata> m_entries;
};
