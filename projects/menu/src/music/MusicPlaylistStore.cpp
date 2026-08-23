#include "MusicPlaylistStore.hpp"

#include <switchu/music_protocol.hpp>

#include "core/DebugLog.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace switchu::menu::music {

namespace {
std::string makePlaylistId(size_t ordinal) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return "local-" + std::to_string(static_cast<unsigned long long>(now)) +
           "-" + std::to_string(ordinal);
}
}

bool MusicPlaylistStore::load(const LibrarySnapshot& library) {
    m_playlists.clear();
    std::ifstream file(switchu::music::kPlaylistsPath);
    if (!file.is_open())
        return true;

    try {
        nlohmann::json root;
        file >> root;
        const auto it = root.find("playlists");
        if (it == root.end() || !it->is_array())
            return false;

        for (const auto& value : *it) {
            if (!value.is_object()) continue;
            Playlist p{};
            p.id = value.value("id", "");
            p.name = value.value("name", "Playlist");
            auto tracks = value.find("tracks");
            if (tracks != value.end() && tracks->is_array()) {
                for (const auto& trackPath : *tracks) {
                    if (!trackPath.is_string()) continue;
                    auto found = library.trackByPath.find(trackPath.get<std::string>());
                    if (found != library.trackByPath.end())
                        p.trackIds.push_back(library.tracks[found->second].id);
                }
            }
            auto trackIds = value.find("track_ids");
            if (trackIds != value.end() && trackIds->is_array()) {
                for (const auto& raw : *trackIds) {
                    if (!raw.is_string()) continue;
                    try {
                        const uint64_t id = std::stoull(raw.get<std::string>());
                        if (library.trackById.find(id) != library.trackById.end() &&
                            std::find(p.trackIds.begin(), p.trackIds.end(), id) == p.trackIds.end())
                            p.trackIds.push_back(id);
                    } catch (...) {}
                }
            }
            if (p.id.empty()) p.id = makePlaylistId(m_playlists.size());
            m_playlists.push_back(std::move(p));
        }
    } catch (...) {
        DebugLog::log("[music-playlist] malformed playlists.json");
        return false;
    }
    return true;
}

bool MusicPlaylistStore::save() const {
    std::error_code ec;
    std::filesystem::create_directories(switchu::music::kConfigDirectory, ec);

    // Track paths are more portable than internal hashes. Because this store
    // does not own the library snapshot, the UI writes a path cache into each
    // playlist using a companion mapping before calling save(). For V0.01 the
    // canonical on-disk representation uses decimal IDs as a fallback too.
    nlohmann::json root;
    root["version"] = 1;
    root["playlists"] = nlohmann::json::array();
    for (const auto& p : m_playlists) {
        nlohmann::json item;
        item["id"] = p.id;
        item["name"] = p.name;
        item["track_ids"] = nlohmann::json::array();
        for (uint64_t id : p.trackIds)
            item["track_ids"].push_back(std::to_string(static_cast<unsigned long long>(id)));
        root["playlists"].push_back(std::move(item));
    }

    const std::string tmp = std::string(switchu::music::kPlaylistsPath) + ".tmp";
    std::ofstream file(tmp, std::ios::trunc);
    if (!file.is_open()) return false;
    file << root.dump(2);
    file.close();
    if (!file) return false;
    std::filesystem::remove(switchu::music::kPlaylistsPath, ec);
    ec.clear();
    std::filesystem::rename(tmp, switchu::music::kPlaylistsPath, ec);
    return !ec;
}

size_t MusicPlaylistStore::create(const std::string& name) {
    Playlist p{};
    p.id = makePlaylistId(m_playlists.size());
    p.name = name.empty() ? "Nouvelle playlist" : name;
    m_playlists.push_back(std::move(p));
    return m_playlists.size() - 1;
}

bool MusicPlaylistStore::erase(size_t index) {
    if (index >= m_playlists.size()) return false;
    m_playlists.erase(m_playlists.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

bool MusicPlaylistStore::addTrack(size_t playlistIndex, uint64_t trackId) {
    if (playlistIndex >= m_playlists.size() || trackId == 0) return false;
    auto& tracks = m_playlists[playlistIndex].trackIds;
    if (std::find(tracks.begin(), tracks.end(), trackId) != tracks.end())
        return false;
    tracks.push_back(trackId);
    return true;
}

bool MusicPlaylistStore::removeTrack(size_t playlistIndex, size_t itemIndex) {
    if (playlistIndex >= m_playlists.size()) return false;
    auto& tracks = m_playlists[playlistIndex].trackIds;
    if (itemIndex >= tracks.size()) return false;
    tracks.erase(tracks.begin() + static_cast<std::ptrdiff_t>(itemIndex));
    return true;
}

void MusicPlaylistStore::pruneMissing(const LibrarySnapshot& library) {
    for (auto& p : m_playlists) {
        p.trackIds.erase(
            std::remove_if(p.trackIds.begin(), p.trackIds.end(), [&](uint64_t id) {
                return library.trackById.find(id) == library.trackById.end();
            }),
            p.trackIds.end());
    }
}

} // namespace switchu::menu::music
