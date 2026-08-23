#include "MusicClient.hpp"

#include "smi_commands.hpp"
#include "DebugLog.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace switchu::menu::music {

namespace {

bool ensureConfigDir() {
    std::error_code ec;
    std::filesystem::create_directories(switchu::music::kConfigDirectory, ec);
    return !ec;
}

bool commandSucceeded(Result rc, const char* name) {
    if (R_SUCCEEDED(rc)) return true;
    DebugLog::log("[music-client] %s command FAIL rc=0x%X", name, rc);
    return false;
}

} // namespace

bool MusicClient::writeQueue(const LibrarySnapshot& library,
                             const std::vector<size_t>& trackIndices,
                             int currentIndex,
                             float volume,
                             bool shuffle,
                             switchu::music::RepeatMode repeat) {
    std::vector<uint64_t> ids;
    ids.reserve(trackIndices.size());
    for (size_t index : trackIndices) {
        if (index < library.tracks.size())
            ids.push_back(library.tracks[index].id);
    }
    return writeQueueIds(library, ids, currentIndex, volume, shuffle, repeat);
}

bool MusicClient::writeQueueIds(const LibrarySnapshot& library,
                                const std::vector<uint64_t>& trackIds,
                                int currentIndex,
                                float volume,
                                bool shuffle,
                                switchu::music::RepeatMode repeat) {
    ensureConfigDir();
    const std::string tmp = std::string(switchu::music::kQueuePath) + ".tmp";
    std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return false;

    std::vector<const Track*> tracks;
    tracks.reserve(trackIds.size());
    for (uint64_t id : trackIds) {
        auto found = library.trackById.find(id);
        if (found != library.trackById.end())
            tracks.push_back(&library.tracks[found->second]);
    }

    switchu::music::QueueFileHeader header{};
    header.count = static_cast<uint32_t>(tracks.size());
    header.current_index = tracks.empty() ? -1 : std::clamp(currentIndex, 0, static_cast<int>(tracks.size()) - 1);
    header.volume = std::clamp(volume, 0.f, 1.f);
    header.shuffle = shuffle ? 1 : 0;
    header.repeat_mode = static_cast<uint8_t>(repeat);
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));

    m_queueTrackIds.clear();
    m_queueTrackIds.reserve(tracks.size());
    for (const Track* track : tracks) {
        switchu::music::QueueEntryHeader eh{};
        eh.track_id = track->id;
        eh.duration_ms = track->durationMs;
        eh.path_len = static_cast<uint32_t>(track->path.size());
        eh.title_len = static_cast<uint32_t>(track->title.size());
        eh.artist_len = static_cast<uint32_t>(track->artist.size());
        file.write(reinterpret_cast<const char*>(&eh), sizeof(eh));
        file.write(track->path.data(), static_cast<std::streamsize>(track->path.size()));
        file.write(track->title.data(), static_cast<std::streamsize>(track->title.size()));
        file.write(track->artist.data(), static_cast<std::streamsize>(track->artist.size()));
        m_queueTrackIds.push_back(track->id);
    }
    file.close();
    if (!file) return false;

    std::error_code ec;
    std::filesystem::remove(switchu::music::kQueuePath, ec);
    ec.clear();
    std::filesystem::rename(tmp, switchu::music::kQueuePath, ec);
    if (ec) return false;
    return true;
}

bool MusicClient::reloadQueue() {
#ifdef SWITCHU_MENU
    return commandSucceeded(switchu::menu::smi_cmd::musicReloadQueue(), "reloadQueue");
#else
    return false;
#endif
}

bool MusicClient::playIndex(int index) {
#ifdef SWITCHU_MENU
    return commandSucceeded(switchu::menu::smi_cmd::musicPlayIndex(index), "playIndex");
#else
    (void)index; return false;
#endif
}

bool MusicClient::togglePause() {
#ifdef SWITCHU_MENU
    return commandSucceeded(switchu::menu::smi_cmd::musicTogglePause(), "togglePause");
#else
    return false;
#endif
}

bool MusicClient::pause() {
#ifdef SWITCHU_MENU
    return commandSucceeded(switchu::menu::smi_cmd::musicPause(), "pause");
#else
    return false;
#endif
}

bool MusicClient::resume() {
#ifdef SWITCHU_MENU
    return commandSucceeded(switchu::menu::smi_cmd::musicResume(), "resume");
#else
    return false;
#endif
}

bool MusicClient::next() {
#ifdef SWITCHU_MENU
    return commandSucceeded(switchu::menu::smi_cmd::musicNext(), "next");
#else
    return false;
#endif
}

bool MusicClient::previous() {
#ifdef SWITCHU_MENU
    return commandSucceeded(switchu::menu::smi_cmd::musicPrevious(), "previous");
#else
    return false;
#endif
}

bool MusicClient::seekMs(uint64_t ms) {
#ifdef SWITCHU_MENU
    return commandSucceeded(switchu::menu::smi_cmd::musicSeek(ms), "seek");
#else
    (void)ms; return false;
#endif
}

bool MusicClient::setVolume(float volume) {
#ifdef SWITCHU_MENU
    return commandSucceeded(switchu::menu::smi_cmd::musicSetVolume(volume), "setVolume");
#else
    (void)volume; return false;
#endif
}

bool MusicClient::setShuffle(bool enabled) {
#ifdef SWITCHU_MENU
    return commandSucceeded(switchu::menu::smi_cmd::musicSetShuffle(enabled), "setShuffle");
#else
    (void)enabled; return false;
#endif
}

bool MusicClient::setRepeat(switchu::music::RepeatMode mode) {
#ifdef SWITCHU_MENU
    return commandSucceeded(switchu::menu::smi_cmd::musicSetRepeat(mode), "setRepeat");
#else
    (void)mode; return false;
#endif
}

bool MusicClient::stop(bool clearSession) {
#ifdef SWITCHU_MENU
    Result rc = clearSession
        ? switchu::menu::smi_cmd::musicClearSession()
        : switchu::menu::smi_cmd::musicStop();
    return commandSucceeded(rc, clearSession ? "clearSession" : "stop");
#else
    (void)clearSession; return false;
#endif
}

bool MusicClient::getStatus(switchu::music::Status& out) {
#ifdef SWITCHU_MENU
    const Result rc = switchu::menu::smi_cmd::getMusicStatus(out);
    return commandSucceeded(rc, "getStatus");
#else
    (void)out; return false;
#endif
}

bool MusicClient::loadQueueTrackIds() {
    m_queueTrackIds.clear();
    std::ifstream file(switchu::music::kQueuePath, std::ios::binary);
    if (!file.is_open()) return false;
    switchu::music::QueueFileHeader header{};
    if (!file.read(reinterpret_cast<char*>(&header), sizeof(header)) ||
        header.magic != switchu::music::kQueueMagic ||
        header.version != switchu::music::kProtocolVersion ||
        header.count > 8192)
        return false;

    m_queueTrackIds.reserve(header.count);
    for (uint32_t i = 0; i < header.count; ++i) {
        switchu::music::QueueEntryHeader eh{};
        if (!file.read(reinterpret_cast<char*>(&eh), sizeof(eh))) return false;
        if (eh.path_len > 4096 || eh.title_len > 4096 || eh.artist_len > 4096) return false;
        m_queueTrackIds.push_back(eh.track_id);
        file.seekg(static_cast<std::streamoff>(eh.path_len + eh.title_len + eh.artist_len), std::ios::cur);
        if (!file) return false;
    }
    return true;
}

} // namespace switchu::menu::music
