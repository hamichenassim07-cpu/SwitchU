#include "HomeOrderStore.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <vector>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <system_error>

namespace {
constexpr const char* kHomeOrderPath = "sdmc:/config/SwitchU/home_order_v1025.json";

std::string titleIdToHex(std::uint64_t v) {
    char buf[17] = {};
    std::snprintf(buf, sizeof(buf), "%016llX", static_cast<unsigned long long>(v));
    return buf;
}

bool hexToTitleId(const std::string& s, std::uint64_t& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    const unsigned long long value = std::strtoull(s.c_str(), &end, 16);
    if (end == s.c_str() || *end != '\0') return false;
    out = static_cast<std::uint64_t>(value);
    return out != 0;
}

std::uint64_t unixSecondsNow() {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<seconds>(system_clock::now().time_since_epoch()).count());
}
}

HomeOrderStore& HomeOrderStore::instance() {
    static HomeOrderStore store;
    store.ensureLoaded();
    return store;
}

void HomeOrderStore::ensureLoaded() {
    if (m_loaded) return;
    m_loaded = true;

    std::ifstream f(kHomeOrderPath);
    if (!f.is_open()) return;

    nlohmann::json root;
    try { f >> root; } catch (...) { return; }
    const auto it = root.find("titles");
    if (it == root.end() || !it->is_object()) return;

    for (auto jt = it->begin(); jt != it->end(); ++jt) {
        std::uint64_t tid = 0;
        if (!hexToTitleId(jt.key(), tid) || !jt.value().is_object())
            continue;
        Metadata md;
        md.pinned = jt.value().value("pinned", false);
        md.manualRank = jt.value().value("manualRank", 0);
        md.lastLaunchTime = jt.value().value("lastLaunchTime", std::uint64_t{0});
        m_entries[tid] = md;
    }

    std::vector<std::uint64_t> pinned;
    for (const auto& [tid, entry] : m_entries) {
        if (entry.pinned)
            pinned.push_back(tid);
    }
    std::stable_sort(pinned.begin(), pinned.end(), [&](std::uint64_t a, std::uint64_t b) {
        const auto& ma = m_entries.at(a);
        const auto& mb = m_entries.at(b);
        if (ma.manualRank != mb.manualRank)
            return ma.manualRank < mb.manualRank;
        if (ma.lastLaunchTime != mb.lastLaunchTime)
            return ma.lastLaunchTime > mb.lastLaunchTime;
        return a < b;
    });
    for (int i = 0; i < static_cast<int>(pinned.size()); ++i)
        m_entries[pinned[static_cast<std::size_t>(i)]].manualRank = i;
}

const HomeOrderStore::Metadata& HomeOrderStore::metadata(std::uint64_t titleId) {
    ensureLoaded();
    static const Metadata empty{};
    if (titleId == 0) return empty;
    auto it = m_entries.find(titleId);
    return it == m_entries.end() ? empty : it->second;
}

bool HomeOrderStore::isPinned(std::uint64_t titleId) {
    return metadata(titleId).pinned;
}

int HomeOrderStore::manualRank(std::uint64_t titleId) {
    return metadata(titleId).manualRank;
}

std::uint64_t HomeOrderStore::lastLaunchTime(std::uint64_t titleId) {
    return metadata(titleId).lastLaunchTime;
}

void HomeOrderStore::pinAtRank(std::uint64_t titleId, int rank) {
    if (titleId == 0) return;
    ensureLoaded();

    // V10.26: one authoritative pinned sequence. V10.25 only changed the
    // moved title's rank, allowing duplicate manualRank values and causing
    // stable_sort to fall back to the old physical layout seemingly at random.
    std::vector<std::uint64_t> pinned;
    for (const auto& [tid, entry] : m_entries) {
        if (tid != titleId && entry.pinned)
            pinned.push_back(tid);
    }
    std::stable_sort(pinned.begin(), pinned.end(), [&](std::uint64_t a, std::uint64_t b) {
        const auto& ma = m_entries.at(a);
        const auto& mb = m_entries.at(b);
        if (ma.manualRank != mb.manualRank)
            return ma.manualRank < mb.manualRank;
        if (ma.lastLaunchTime != mb.lastLaunchTime)
            return ma.lastLaunchTime > mb.lastLaunchTime;
        return a < b;
    });

    const int desired = std::clamp(rank, 0, static_cast<int>(pinned.size()));
    pinned.insert(pinned.begin() + desired, titleId);
    for (int i = 0; i < static_cast<int>(pinned.size()); ++i) {
        auto& entry = m_entries[pinned[static_cast<std::size_t>(i)]];
        entry.pinned = true;
        entry.manualRank = i;
    }
    save();
}

void HomeOrderStore::unpin(std::uint64_t titleId) {
    if (titleId == 0) return;
    ensureLoaded();
    auto& md = m_entries[titleId];
    md.pinned = false;

    std::vector<std::uint64_t> pinned;
    for (const auto& [tid, entry] : m_entries) {
        if (entry.pinned)
            pinned.push_back(tid);
    }
    std::stable_sort(pinned.begin(), pinned.end(), [&](std::uint64_t a, std::uint64_t b) {
        const auto& ma = m_entries.at(a);
        const auto& mb = m_entries.at(b);
        if (ma.manualRank != mb.manualRank)
            return ma.manualRank < mb.manualRank;
        return a < b;
    });
    for (int i = 0; i < static_cast<int>(pinned.size()); ++i)
        m_entries[pinned[static_cast<std::size_t>(i)]].manualRank = i;

    save();
}

void HomeOrderStore::markLaunched(std::uint64_t titleId) {
    if (titleId == 0) return;
    ensureLoaded();
    auto& md = m_entries[titleId];

    // Two launches can occur inside the same system-clock second. Guarantee a
    // strict timestamp order so the recent list never intermittently falls
    // back to legacy/model order on equal timestamps.
    std::uint64_t stamp = unixSecondsNow();
    for (const auto& [tid, entry] : m_entries) {
        if (tid != titleId && entry.lastLaunchTime >= stamp)
            stamp = entry.lastLaunchTime + 1;
    }
    md.lastLaunchTime = stamp;
    save();
}

void HomeOrderStore::save() {
    std::error_code ec;
    std::filesystem::create_directory("sdmc:/config", ec);
    ec.clear();
    std::filesystem::create_directory("sdmc:/config/SwitchU", ec);

    nlohmann::json root;
    root["version"] = 1;
    root["titles"] = nlohmann::json::object();
    for (const auto& [tid, md] : m_entries) {
        nlohmann::json item;
        item["pinned"] = md.pinned;
        item["manualRank"] = md.manualRank;
        item["lastLaunchTime"] = md.lastLaunchTime;
        root["titles"][titleIdToHex(tid)] = std::move(item);
    }

    std::ofstream f(kHomeOrderPath, std::ios::trunc);
    if (f.is_open())
        f << root.dump(2);
}
