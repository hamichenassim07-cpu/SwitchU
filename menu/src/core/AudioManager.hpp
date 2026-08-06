#pragma once
#include <SDL2/SDL_mixer.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <atomic>

enum class Sfx {
    Navigate,
    Activate,
    PageChange,
    ModalShow,
    ModalHide,
    LaunchGame,
    ThemeToggle,
    ToggleOff,
    SliderUp,
    SliderDown,
    ConfirmPositive,
    Volume,
};

class AudioManager {
public:
    AudioManager() = default;
    ~AudioManager();

    bool initialize();
    void shutdown();

    void loadTrack(const std::string& path);
    void loadLockscreenTrack(const std::string& path);
    void clearTracks();

    // Existing HOME music entry point.
    void play();
    void playHome(int fadeMs = 0);
    void playLockscreen(int fadeMs = 480);
    void fadeOutForGame(int fadeMs = 520);
    void update();
    void stop();
    void nextTrack();

    void setVolume(float vol);
    float volume() const { return m_volume; }
    void setLockscreenVolume(float vol);
    float lockscreenVolume() const { return m_lockscreenVolume; }
    bool hasLockscreenTrack() const { return m_lockscreenTrack != nullptr; }
    bool isPlaying() const { return m_playing; }
    static AudioManager* active() { return s_active.load(); }

    // The lockscreen can appear before the asynchronous audio loader has
    // finished. Keeping this process-wide intent prevents the later legacy
    // play() call from replacing the lockscreen scene with HOME music.
    static void setLockscreenVisible(bool visible) {
        s_lockscreenVisible.store(visible);
    }
    static bool lockscreenVisible() { return s_lockscreenVisible.load(); }

    void loadSfx(Sfx id, const std::string& path);
    void clearSfx();
    void playSfx(Sfx id);
    void loadNamedSfx(const std::string& id, const std::string& path, float volumeScale = 1.f);
    void playNamedSfx(const std::string& id);
    void setSfxVolume(float vol);
    float sfxVolume() const { return m_sfxVolume; }

private:
    enum class MusicScene {
        None,
        Home,
        Lockscreen,
    };

    static std::atomic<AudioManager*> s_instance;
    static std::atomic<AudioManager*> s_active;
    static std::atomic<bool> s_lockscreenVisible;
    static void onTrackFinished();

    void requestScene(MusicScene scene, int fadeOutMs, int fadeInMs);
    void startSceneLocked(MusicScene scene, int fadeInMs);

    std::mutex m_trackMutex;
    std::vector<Mix_Music*> m_tracks;
    Mix_Music* m_lockscreenTrack = nullptr;
    int   m_current = 0;
    float m_volume  = 0.5f;
    float m_lockscreenVolume = 0.35f;
    std::atomic<bool> m_playing{false};
    bool  m_initialized = false;

    MusicScene m_scene = MusicScene::None;
    MusicScene m_pendingScene = MusicScene::None;
    bool m_sceneTransitionPending = false;
    int m_pendingFadeInMs = 0;

    std::mutex m_sfxMutex;
    std::unordered_map<int, Mix_Chunk*> m_sfx;
    std::unordered_map<std::string, Mix_Chunk*> m_namedSfx;
    std::unordered_map<std::string, float> m_namedSfxVolumeScales;
    float m_sfxVolume = 0.7f;
};
