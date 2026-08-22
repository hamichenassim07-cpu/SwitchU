#include "HomeOrderStore.hpp"

HomeOrderStore& HomeOrderStore::instance() {
    static HomeOrderStore store;
    store.ensureLoaded();
    return store;
}

void HomeOrderStore::ensureLoaded() {
    // V10.27: recency/pinning metadata is intentionally retired. Keep this
    // compatibility shell because resume code still uses suspendedTitleId().
    m_loaded = true;
}

const HomeOrderStore::Metadata& HomeOrderStore::metadata(std::uint64_t) {
    static const Metadata empty{};
    return empty;
}

bool HomeOrderStore::isPinned(std::uint64_t) {
    return false;
}

int HomeOrderStore::manualRank(std::uint64_t) {
    return 0;
}

std::uint64_t HomeOrderStore::lastLaunchTime(std::uint64_t) {
    return 0;
}

void HomeOrderStore::pinAtRank(std::uint64_t, int) {
    // Removed in V10.27. Manual Y placement is persisted by the HOME layout.
}

void HomeOrderStore::unpin(std::uint64_t) {
    // Removed in V10.27.
}

void HomeOrderStore::markLaunched(std::uint64_t) {
    // Removed in V10.27: launching a title must never change carousel order.
}

void HomeOrderStore::save() {
    // No ordering metadata is written in V10.27.
}
