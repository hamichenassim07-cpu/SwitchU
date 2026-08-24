#pragma once

#include <switchu/music_protocol.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace switchu::menu::music {

struct CoverRef {
    std::string path;
    uint64_t offset = 0;
    uint64_t size = 0;
    bool embedded = false;

    bool valid() const { return !path.empty() && (!embedded || size > 0); }
    std::string key() const {
        return path + (embedded ? ("#" + std::to_string(offset) + ":" + std::to_string(size)) : "");
    }
};

struct Track {
    uint64_t id = 0;
    std::string path;
    std::string title;
    std::string artist;
    std::string albumArtist;
    bool albumArtistExplicit = false;
    std::string album;
    std::string genre;
    int discNumber = 0;
    int trackNumber = 0;
    int year = 0;
    uint64_t durationMs = 0;
    int64_t modifiedStamp = 0;
    CoverRef cover;
};

struct Album {
    std::string key;
    std::string title;
    std::string artist;
    std::string genre;
    int year = 0;
    std::vector<size_t> tracks;
    CoverRef cover;
    int64_t newestStamp = 0;
};

struct Artist {
    std::string name;
    std::vector<size_t> tracks;
    std::vector<size_t> albums;
};

struct Playlist {
    std::string id;
    std::string name;
    std::vector<uint64_t> trackIds;
};

struct LibrarySnapshot {
    std::vector<Track> tracks;
    std::vector<Album> albums;
    std::vector<Artist> artists;
    std::unordered_map<uint64_t, size_t> trackById;
    std::unordered_map<std::string, size_t> trackByPath;
    std::vector<size_t> recentAlbums;
    uint64_t scanGeneration = 0;
};

} // namespace switchu::menu::music
