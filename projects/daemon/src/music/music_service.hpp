#pragma once

#include <switchu/music_protocol.hpp>

#include <switch.h>
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

namespace switchu::daemon::music {

class MusicService {
public:
    MusicService();
    ~MusicService();

    bool initialize();
    void shutdown();
    void update();

    bool reloadQueue();
    bool playIndex(int index);
    void togglePause();
    void pause();
    void resume();
    void next();
    void previous();
    void seekMs(uint64_t positionMs);
    void setVolume(float volume);
    void setShuffle(bool enabled);
    void setRepeat(switchu::music::RepeatMode mode);
    void stop(bool clearSession = false);

    switchu::music::Status status() const;

    void onGameLaunching(uint64_t titleId);
    void onGameEnded();

private:
    struct QueueEntry {
        uint64_t trackId = 0;
        uint64_t durationMs = 0;
        std::string path;
        std::string title;
        std::string artist;
    };

    static std::atomic<MusicService*> s_active;
    static void onMusicFinishedStatic();

    bool ensureAudioLocked();
    void closeAudioLocked();
    bool loadTrackLocked(int index, bool autoplay, uint64_t startMs = 0);
    void freeMusicLocked();
    void handleFinishedLocked();
    int nextIndexLocked(bool forward) const;
    uint64_t currentPositionMsLocked() const;
    uint64_t currentDurationMsLocked() const;
    bool hasNextLocked() const;
    void updateNextSoonLocked();
    void writeSessionFlagLocked(bool active) const;
    void persistLocked(bool force = false);
    void loadPersistedStateLocked();
    void loadBlacklistLocked();
    bool isBlacklistedLocked(uint64_t titleId) const;

    mutable std::recursive_mutex m_mutex;
    std::vector<QueueEntry> m_queue;
    int m_currentIndex = -1;
    Mix_Music* m_music = nullptr;

    bool m_initialized = false;
    bool m_audioReady = false;
    bool m_sessionActive = false;
    bool m_playing = false;
    bool m_paused = false;
    bool m_shuffle = false;
    bool m_nextSoon = false;
    bool m_autoPausedForGame = false;
    switchu::music::RepeatMode m_repeat = switchu::music::RepeatMode::Off;
    float m_volume = 0.70f;
    uint32_t m_lastError = 0;

    uint64_t m_positionBaseMs = 0;
    uint64_t m_playStartTick = 0;
    uint64_t m_lastPersistTick = 0;
    std::atomic<bool> m_finishedPending{false};

    std::unordered_set<uint64_t> m_blacklist;
    std::mt19937 m_rng;
};

MusicService& service();

} // namespace switchu::daemon::music
