#include "AudioManager.hpp"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>

AudioManager::~AudioManager() { shutdown(); }

bool AudioManager::initialize() {
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        std::fprintf(stderr, "[Audio] SDL_Init(AUDIO) failed: %s\n", SDL_GetError());
        return false;
    }
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 4096) < 0) {
        std::fprintf(stderr, "[Audio] Mix_OpenAudio failed: %s\n", Mix_GetError());
        return false;
    }
    Mix_AllocateChannels(8);
    m_initialized = true;
    s_active.store(this);
    setVolume(m_volume);
    return true;
}

void AudioManager::shutdown() {
    if (!m_initialized) return;

    Mix_HookMusicFinished(nullptr);
    s_instance.store(nullptr);
    Mix_HaltMusic();
    Mix_HaltChannel(-1);
    Mix_CloseAudio();
    m_initialized = false;
    s_lockscreenVisible.store(false);
    if (s_active.load() == this)
        s_active.store(nullptr);
    m_playing.store(false);
    m_scene = MusicScene::None;
    m_pendingScene = MusicScene::None;
    m_sceneTransitionPending = false;

    for (auto* m : m_tracks) Mix_FreeMusic(m);
    m_tracks.clear();
    if (m_lockscreenTrack) {
        Mix_FreeMusic(m_lockscreenTrack);
        m_lockscreenTrack = nullptr;
    }
    for (auto& [id, chunk] : m_sfx) Mix_FreeChunk(chunk);
    m_sfx.clear();
    for (auto& [id, chunk] : m_namedSfx) Mix_FreeChunk(chunk);
    m_namedSfx.clear();
    m_namedSfxVolumeScales.clear();
}

void AudioManager::loadTrack(const std::string& path) {
    // The old preset scanner loads every MP3. Route the reserved lockscreen
    // filename to its own scene here so it can never enter the HOME playlist,
    // even when WiiUMenuApp.cpp has not been patched by an installer.
    const std::string filename = std::filesystem::path(path).filename().string();
    if (filename == "lockscreen.mp3") {
        loadLockscreenTrack(path);
        return;
    }

    Mix_Music* music = Mix_LoadMUS(path.c_str());
    if (!music) {
        std::fprintf(stderr, "[Audio] Failed to load %s: %s\n", path.c_str(), Mix_GetError());
        return;
    }
    std::lock_guard<std::mutex> lk(m_trackMutex);
    m_tracks.push_back(music);
}

void AudioManager::loadLockscreenTrack(const std::string& path) {
    Mix_Music* music = Mix_LoadMUS(path.c_str());
    if (!music) {
        std::fprintf(stderr, "[Audio] Failed to load lockscreen music %s: %s\n",
                     path.c_str(), Mix_GetError());
        return;
    }

    std::lock_guard<std::mutex> lk(m_trackMutex);
    if (m_lockscreenTrack)
        Mix_FreeMusic(m_lockscreenTrack);
    m_lockscreenTrack = music;
}

void AudioManager::clearTracks() {
    std::lock_guard<std::mutex> lk(m_trackMutex);
    Mix_HookMusicFinished(nullptr);
    Mix_HaltMusic();
    m_playing.store(false);
    m_scene = MusicScene::None;
    m_pendingScene = MusicScene::None;
    m_sceneTransitionPending = false;

    for (auto* m : m_tracks) Mix_FreeMusic(m);
    m_tracks.clear();
    if (m_lockscreenTrack) {
        Mix_FreeMusic(m_lockscreenTrack);
        m_lockscreenTrack = nullptr;
    }
    m_current = 0;
}

std::atomic<AudioManager*> AudioManager::s_instance{nullptr};
std::atomic<AudioManager*> AudioManager::s_active{nullptr};
std::atomic<bool> AudioManager::s_lockscreenVisible{false};

void AudioManager::onTrackFinished() {
    AudioManager* inst = s_instance.load();
    if (inst) inst->nextTrack();
}

void AudioManager::startSceneLocked(MusicScene scene, int fadeInMs) {
    m_sceneTransitionPending = false;
    m_pendingScene = MusicScene::None;
    m_pendingFadeInMs = 0;

    if (scene == MusicScene::Home) {
        if (m_tracks.empty()) {
            m_scene = MusicScene::None;
            m_playing.store(false);
            return;
        }

        m_current = std::clamp(m_current, 0, static_cast<int>(m_tracks.size()) - 1);
        Mix_VolumeMusic(static_cast<int>(m_volume * MIX_MAX_VOLUME));
        s_instance.store(this);
        Mix_HookMusicFinished(onTrackFinished);
        if (fadeInMs > 0)
            Mix_FadeInMusic(m_tracks[m_current], 1, fadeInMs);
        else
            Mix_PlayMusic(m_tracks[m_current], 1);
        m_scene = MusicScene::Home;
        m_playing.store(true);
        return;
    }

    if (scene == MusicScene::Lockscreen) {
        if (!m_lockscreenTrack) {
            // Missing custom track: retain the current HOME music rather than
            // turning a missing optional file into an audio failure.
            m_sceneTransitionPending = false;
            if (!m_tracks.empty())
                startSceneLocked(MusicScene::Home, fadeInMs);
            return;
        }

        Mix_VolumeMusic(static_cast<int>(m_lockscreenVolume * MIX_MAX_VOLUME));
        s_instance.store(nullptr);
        Mix_HookMusicFinished(nullptr);
        if (fadeInMs > 0)
            Mix_FadeInMusic(m_lockscreenTrack, -1, fadeInMs);
        else
            Mix_PlayMusic(m_lockscreenTrack, -1);
        m_scene = MusicScene::Lockscreen;
        m_playing.store(true);
        return;
    }

    Mix_HookMusicFinished(nullptr);
    s_instance.store(nullptr);
    Mix_HaltMusic();
    m_scene = MusicScene::None;
    m_playing.store(false);
}

void AudioManager::requestScene(MusicScene scene, int fadeOutMs, int fadeInMs) {
    std::lock_guard<std::mutex> lk(m_trackMutex);
    if (!m_initialized)
        return;

    if (scene == MusicScene::Lockscreen && !m_lockscreenTrack)
        return;
    if (scene == MusicScene::Home && m_tracks.empty())
        return;

    if (m_scene == scene && Mix_PlayingMusic())
        return;

    m_pendingScene = scene;
    m_pendingFadeInMs = std::max(0, fadeInMs);
    Mix_HookMusicFinished(nullptr);
    s_instance.store(nullptr);

    if (Mix_PlayingMusic() && fadeOutMs > 0) {
        const int started = Mix_FadeOutMusic(fadeOutMs);
        if (started != 0) {
            m_sceneTransitionPending = true;
            return;
        }
    }

    Mix_HaltMusic();
    startSceneLocked(scene, m_pendingFadeInMs);
}

void AudioManager::play() {
    // WiiUMenuApp starts audio asynchronously. If the lockscreen is already
    // visible by the time loading completes, enter its dedicated scene rather
    // than briefly starting HOME music over it.
    if (s_lockscreenVisible.load() && hasLockscreenTrack())
        playLockscreen(0);
    else
        playHome(0);
}

void AudioManager::playHome(int fadeMs) {
    requestScene(MusicScene::Home, fadeMs, fadeMs);
}

void AudioManager::playLockscreen(int fadeMs) {
    requestScene(MusicScene::Lockscreen, fadeMs, fadeMs);
}

void AudioManager::fadeOutForGame(int fadeMs) {
    std::lock_guard<std::mutex> lk(m_trackMutex);
    if (!m_initialized)
        return;

    Mix_HookMusicFinished(nullptr);
    s_instance.store(nullptr);
    m_pendingScene = MusicScene::None;
    m_pendingFadeInMs = 0;

    if (Mix_PlayingMusic() && fadeMs > 0) {
        const int started = Mix_FadeOutMusic(fadeMs);
        m_sceneTransitionPending = started != 0;
        if (m_sceneTransitionPending)
            return;
    }

    Mix_HaltMusic();
    m_sceneTransitionPending = false;
    m_scene = MusicScene::None;
    m_playing.store(false);
}

void AudioManager::update() {
    std::lock_guard<std::mutex> lk(m_trackMutex);
    if (!m_initialized || !m_sceneTransitionPending)
        return;

    if (Mix_PlayingMusic())
        return;

    const MusicScene target = m_pendingScene;
    const int fadeIn = m_pendingFadeInMs;
    if (target == MusicScene::None) {
        m_sceneTransitionPending = false;
        m_pendingScene = MusicScene::None;
        m_pendingFadeInMs = 0;
        m_scene = MusicScene::None;
        m_playing.store(false);
        return;
    }

    startSceneLocked(target, fadeIn);
}

void AudioManager::stop() {
    std::lock_guard<std::mutex> lk(m_trackMutex);
    Mix_HookMusicFinished(nullptr);
    s_instance.store(nullptr);
    Mix_HaltMusic();
    m_scene = MusicScene::None;
    m_pendingScene = MusicScene::None;
    m_sceneTransitionPending = false;
    m_pendingFadeInMs = 0;
    m_playing.store(false);
}

void AudioManager::nextTrack() {
    std::lock_guard<std::mutex> lk(m_trackMutex);
    if (m_tracks.empty() || m_scene != MusicScene::Home)
        return;
    m_current = (m_current + 1) % static_cast<int>(m_tracks.size());
    Mix_VolumeMusic(static_cast<int>(m_volume * MIX_MAX_VOLUME));
    Mix_PlayMusic(m_tracks[m_current], 1);
    m_playing.store(true);
}

void AudioManager::setVolume(float vol) {
    m_volume = std::clamp(vol, 0.f, 1.f);
    if (m_scene != MusicScene::Lockscreen)
        Mix_VolumeMusic(static_cast<int>(m_volume * MIX_MAX_VOLUME));
}

void AudioManager::setLockscreenVolume(float vol) {
    m_lockscreenVolume = std::clamp(vol, 0.f, 1.f);
    if (m_scene == MusicScene::Lockscreen)
        Mix_VolumeMusic(static_cast<int>(m_lockscreenVolume * MIX_MAX_VOLUME));
}

void AudioManager::loadSfx(Sfx id, const std::string& path) {
    Mix_Chunk* chunk = Mix_LoadWAV(path.c_str());
    if (!chunk) {
        std::fprintf(stderr, "[Audio] Failed to load SFX %s: %s\n", path.c_str(), Mix_GetError());
        return;
    }
    Mix_VolumeChunk(chunk, static_cast<int>(m_sfxVolume * MIX_MAX_VOLUME));
    std::lock_guard<std::mutex> lk(m_sfxMutex);
    auto it = m_sfx.find(static_cast<int>(id));
    if (it != m_sfx.end()) {
        Mix_FreeChunk(it->second);
        it->second = chunk;
    } else {
        m_sfx[static_cast<int>(id)] = chunk;
    }
}

void AudioManager::clearSfx() {
    std::lock_guard<std::mutex> lk(m_sfxMutex);
    Mix_HaltChannel(-1);
    for (auto& [id, chunk] : m_sfx) Mix_FreeChunk(chunk);
    m_sfx.clear();
    for (auto& [id, chunk] : m_namedSfx) Mix_FreeChunk(chunk);
    m_namedSfx.clear();
    m_namedSfxVolumeScales.clear();
}

void AudioManager::playSfx(Sfx id) {
    std::lock_guard<std::mutex> lk(m_sfxMutex);
    auto it = m_sfx.find(static_cast<int>(id));
    if (it != m_sfx.end())
        Mix_PlayChannel(-1, it->second, 0);
}

void AudioManager::loadNamedSfx(const std::string& id, const std::string& path,
                                float volumeScale) {
    Mix_Chunk* chunk = Mix_LoadWAV(path.c_str());
    if (!chunk) {
        std::fprintf(stderr, "[Audio] Failed to load named SFX %s: %s\n",
                     path.c_str(), Mix_GetError());
        return;
    }
    Mix_VolumeChunk(chunk,
                    static_cast<int>(m_sfxVolume * volumeScale * MIX_MAX_VOLUME));
    std::lock_guard<std::mutex> lk(m_sfxMutex);
    auto it = m_namedSfx.find(id);
    if (it != m_namedSfx.end()) {
        Mix_FreeChunk(it->second);
        it->second = chunk;
    } else {
        m_namedSfx[id] = chunk;
    }
    m_namedSfxVolumeScales[id] = volumeScale;
}

void AudioManager::playNamedSfx(const std::string& id) {
    std::lock_guard<std::mutex> lk(m_sfxMutex);
    auto it = m_namedSfx.find(id);
    if (it != m_namedSfx.end())
        Mix_PlayChannel(-1, it->second, 0);
}

void AudioManager::setSfxVolume(float vol) {
    std::lock_guard<std::mutex> lk(m_sfxMutex);
    m_sfxVolume = std::clamp(vol, 0.f, 1.f);
    for (auto& [id, chunk] : m_sfx)
        Mix_VolumeChunk(chunk, static_cast<int>(m_sfxVolume * MIX_MAX_VOLUME));
    for (auto& [id, chunk] : m_namedSfx) {
        float scale = 1.f;
        auto it = m_namedSfxVolumeScales.find(id);
        if (it != m_namedSfxVolumeScales.end())
            scale = it->second;
        Mix_VolumeChunk(chunk,
                        static_cast<int>(m_sfxVolume * scale * MIX_MAX_VOLUME));
    }
}
