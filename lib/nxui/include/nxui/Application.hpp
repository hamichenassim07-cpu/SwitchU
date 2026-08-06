#pragma once
#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/Input.hpp>
#include <memory>

namespace nxui {

class Activity;

/// Top-level application object.
/// Owns the GPU device, Renderer, and Input, runs the main loop,
/// and delegates lifecycle events to the attached Activity.
class Application {
public:
    Application() = default;
    ~Application();

    void setActivity(std::unique_ptr<Activity> activity);
    void requestActivity(std::unique_ptr<Activity> activity);

    bool initialize();
    void run();
    void shutdown();

    GpuDevice& gpu()       { return m_gpu; }
    Renderer&  renderer()  { return *m_renderer; }
    Input&     input()     { return m_input; }

    // V6.3: stop rendering immediately when a foreground handoff is queued.
    // This prevents one last HOME frame from being submitted after the menu
    // has already asked the daemon to resume the suspended application.
    void requestExit() {
        m_renderEnabled = false;
        m_running = false;
    }
    bool isRunning() const { return m_running; }

    void setRenderEnabled(bool e) { m_renderEnabled = e; }
    bool renderEnabled() const    { return m_renderEnabled; }

    // Used by the lockscreen's safe handoff without keeping a fragile pointer
    // to WiiUMenuApp. There is only one nxui Application in the menu process.
    static Application* current() { return s_current; }

private:
    void dispatchInput();
    bool applyPendingActivity();

    static Application* s_current;

    GpuDevice  m_gpu;
    std::unique_ptr<Renderer> m_renderer;
    Input      m_input;

    std::unique_ptr<Activity> m_activity;
    std::unique_ptr<Activity> m_pendingActivity;
    bool m_running = true;
    bool m_renderEnabled = true;
    int  m_navDebounce = 0;
};

} // namespace nxui
