#pragma once

#include "MusicTypes.hpp"
#include <switchu/music_protocol.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace switchu::menu::music {

class MusicClient {
public:
    bool writeQueue(const LibrarySnapshot& library,
                    const std::vector<size_t>& trackIndices,
                    int currentIndex,
                    float volume,
                    bool shuffle,
                    switchu::music::RepeatMode repeat);
    bool writeQueueIds(const LibrarySnapshot& library,
                       const std::vector<uint64_t>& trackIds,
                       int currentIndex,
                       float volume,
                       bool shuffle,
                       switchu::music::RepeatMode repeat);

    bool reloadQueue();
    bool playIndex(int index);
    bool togglePause();
    bool pause();
    bool resume();
    bool next();
    bool previous();
    bool seekMs(uint64_t ms);
    bool setVolume(float volume);
    bool setShuffle(bool enabled);
    bool setRepeat(switchu::music::RepeatMode mode);
    bool stop(bool clearSession = false);
    bool getStatus(switchu::music::Status& out);

    const std::vector<uint64_t>& queueTrackIds() const { return m_queueTrackIds; }
    bool loadQueueTrackIds();

private:
    std::vector<uint64_t> m_queueTrackIds;
};

} // namespace switchu::menu::music
