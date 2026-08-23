#pragma once

#include "MusicTypes.hpp"

#include <string>
#include <vector>

namespace switchu::menu::music {

class MusicPlaylistStore {
public:
    bool load(const LibrarySnapshot& library);
    bool save() const;

    const std::vector<Playlist>& playlists() const { return m_playlists; }
    std::vector<Playlist>& playlists() { return m_playlists; }

    size_t create(const std::string& name);
    bool erase(size_t index);
    bool addTrack(size_t playlistIndex, uint64_t trackId);
    bool removeTrack(size_t playlistIndex, size_t itemIndex);
    void pruneMissing(const LibrarySnapshot& library);

private:
    std::vector<Playlist> m_playlists;
};

} // namespace switchu::menu::music
