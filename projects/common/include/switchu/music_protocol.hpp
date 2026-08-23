#pragma once

#include <cstdint>
#include <cstddef>

namespace switchu::music {

static constexpr uint32_t kProtocolVersion = 1;
static constexpr uint32_t kQueueMagic = 0x514D5553; // SUMQ
static constexpr uint32_t kStateMagic = 0x534D5553; // SUMS
static constexpr const char* kRootDirectory = "sdmc:/Music";
static constexpr const char* kConfigDirectory = "sdmc:/config/SwitchU/music";
static constexpr const char* kQueuePath = "sdmc:/config/SwitchU/music/queue.bin";
static constexpr const char* kStatePath = "sdmc:/config/SwitchU/music/service_state.bin";
static constexpr const char* kSessionFlagPath = "sdmc:/config/SwitchU/music/session_active.flag";
static constexpr const char* kBlacklistPath = "sdmc:/config/SwitchU/music/blacklist.txt";
static constexpr const char* kPlaylistsPath = "sdmc:/config/SwitchU/music/playlists.json";

static constexpr uint64_t kVirtualMusicTitleId = 0xFFFFFFFFFFFFF104ULL;

enum class RepeatMode : uint8_t {
    Off = 0,
    Queue = 1,
    Track = 2,
};

enum MusicStatusFlags : uint32_t {
    MusicStatus_SessionActive = 1u << 0,
    MusicStatus_Playing       = 1u << 1,
    MusicStatus_Paused        = 1u << 2,
    MusicStatus_Shuffle       = 1u << 3,
    MusicStatus_NextSoon      = 1u << 4,
    MusicStatus_AutoPaused    = 1u << 5,
    MusicStatus_AudioReady    = 1u << 6,
};

struct QueueFileHeader {
    uint32_t magic = kQueueMagic;
    uint32_t version = kProtocolVersion;
    uint32_t count = 0;
    int32_t current_index = -1;
    float volume = 0.70f;
    uint8_t shuffle = 0;
    uint8_t repeat_mode = static_cast<uint8_t>(RepeatMode::Off);
    uint8_t reserved[2] = {};
};
static_assert(sizeof(QueueFileHeader) == 24);

struct QueueEntryHeader {
    uint64_t track_id = 0;
    uint64_t duration_ms = 0;
    uint32_t path_len = 0;
    uint32_t title_len = 0;
    uint32_t artist_len = 0;
    uint32_t reserved = 0;
};
static_assert(sizeof(QueueEntryHeader) == 32);

struct IndexArgs {
    int32_t index = -1;
};

struct SeekArgs {
    uint64_t position_ms = 0;
};

struct VolumeArgs {
    float volume = 0.70f;
};

struct ToggleArgs {
    uint8_t enabled = 0;
    uint8_t reserved[3] = {};
};

struct RepeatArgs {
    uint8_t mode = static_cast<uint8_t>(RepeatMode::Off);
    uint8_t reserved[3] = {};
};

struct Status {
    uint32_t version = kProtocolVersion;
    int32_t current_index = -1;
    uint32_t queue_count = 0;
    uint32_t flags = 0;
    uint64_t position_ms = 0;
    uint64_t duration_ms = 0;
    uint64_t track_id = 0;
    float volume = 0.70f;
    uint8_t repeat_mode = static_cast<uint8_t>(RepeatMode::Off);
    uint8_t reserved[3] = {};
    uint32_t last_error = 0;
};
static_assert(sizeof(Status) == 56);

struct PersistedState {
    uint32_t magic = kStateMagic;
    uint32_t version = kProtocolVersion;
    int32_t current_index = -1;
    uint32_t flags = 0;
    uint64_t position_ms = 0;
    uint64_t track_id = 0;
    float volume = 0.70f;
    uint8_t repeat_mode = static_cast<uint8_t>(RepeatMode::Off);
    uint8_t reserved[3] = {};
};
static_assert(sizeof(PersistedState) == 40);

inline uint64_t fnv1a64(const char* data, size_t len) {
    uint64_t hash = 1469598103934665603ULL;
    for (size_t i = 0; i < len; ++i) {
        hash ^= static_cast<uint8_t>(data[i]);
        hash *= 1099511628211ULL;
    }
    return hash;
}

} // namespace switchu::music
