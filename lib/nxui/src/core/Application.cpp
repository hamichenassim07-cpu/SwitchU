#include <nxui/Application.hpp>
#include <nxui/Activity.hpp>
#include <nxui/core/Animation.hpp>
#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/Input.hpp>
#include <nxui/focus/FocusManager.hpp>
#include <switch.h>
#include <vector>
#ifdef SWITCHU_MENU
#include <switchu/file_log.hpp>
#endif

namespace nxui {

Application* Application::s_current = nullptr;

Application::~Application() {
    shutdown();
}

void Application::setActivity(std::unique_ptr<Activity> activity) {
    m_activity = std::move(activity);
    if (m_activity) m_activity->m_app = this;
}

void Application::requestActivity(std::unique_ptr<Activity> activity) {
    m_pendingActivity = std::move(activity);
    if (m_pendingActivity)
        m_pendingActivity->m_app = this;
}

bool Application::applyPendingActivity() {
    if (!m_pendingActivity)
        return true;

    AnimationManager::instance().clear();
    if (m_activity)
        m_activity->onDestroy();

    m_activity = std::move(m_pendingActivity);
    m_navDebounce = 0;

    if (m_activity) {
        m_activity->m_rootBox->setRect({0, 0, (float)m_gpu.width(), (float)m_gpu.height()});
        return m_activity->onCreate();
    }
    return true;
}

bool Application::initialize() {
    s_current = this;
    m_running = true;
    m_renderEnabled = true;

    if (!m_gpu.initialize()) {
        s_current = nullptr;
        return false;
    }

    m_renderer = std::make_unique<Renderer>(m_gpu);
    if (!m_renderer->initialize()) {
        s_current = nullptr;
        return false;
    }

    m_input.initialize();

    // Present one clean frame immediately so stale framebuffer content from a
    // previous process is never visible on screen.
    m_gpu.beginFrame();
    m_renderer->beginFrame();
    m_renderer->endFrame();
    m_gpu.endFrame();

    if (m_activity) {
        m_activity->m_rootBox->setRect({0, 0, (float)m_gpu.width(), (float)m_gpu.height()});
        if (!m_activity->onCreate()) {
            s_current = nullptr;
            return false;
        }
    }
    return true;
}

void Application::dispatchInput() {
    if (!m_activity) return;

    Widget* root = m_activity->focusRoot();
    if (!root) return;

    auto& fm = m_activity->focusManager();

    auto isUnderRoot = [root](Widget* w) {
        for (Widget* it = w; it != nullptr; it = it->parent()) {
            if (it == root) return true;
        }
        return false;
    };
    Widget* curFocus = fm.current();
    if (!curFocus || !isUnderRoot(curFocus)) {
        if (root->isFocusable()) {
            fm.setFocus(root);
        } else {
            std::vector<Widget*> focusables;
            root->collectFocusable(focusables);
            if (!focusables.empty())
                fm.setFocus(focusables[0]);
        }
    }

    static int s_horizontalHoldFrames = 0;
    static int s_horizontalHeldDir = 0;

    const bool leftDown =
        m_input.isDown(Button::DLeft) ||
        m_input.isDown(Button::LStickL) ||
        m_input.isDown(Button::RStickL);
    const bool rightDown =
        m_input.isDown(Button::DRight) ||
        m_input.isDown(Button::LStickR) ||
        m_input.isDown(Button::RStickR);
    const bool leftHeld =
        m_input.isHeld(Button::DLeft) ||
        m_input.isHeld(Button::LStickL) ||
        m_input.isHeld(Button::RStickL);
    const bool rightHeld =
        m_input.isHeld(Button::DRight) ||
        m_input.isHeld(Button::LStickR) ||
        m_input.isHeld(Button::RStickR);

    bool repeatLeft = false;
    bool repeatRight = false;

    const int heldDir =
        (leftHeld && !rightHeld) ? -1 :
        (rightHeld && !leftHeld) ? 1 : 0;

    if (leftDown) {
        s_horizontalHeldDir = -1;
        s_horizontalHoldFrames = 20;
    } else if (rightDown) {
        s_horizontalHeldDir = 1;
        s_horizontalHoldFrames = 20;
    } else if (heldDir == 0) {
        s_horizontalHeldDir = 0;
        s_horizontalHoldFrames = 0;
    } else if (heldDir != s_horizontalHeldDir) {
        s_horizontalHeldDir = heldDir;
        s_horizontalHoldFrames = 20;
    } else if (s_horizontalHoldFrames > 0) {
        --s_horizontalHoldFrames;
    } else {
        repeatLeft = (heldDir < 0);
        repeatRight = (heldDir > 0);
        s_horizontalHoldFrames = 5;
    }

    bool anyDpad =
        m_input.isDown(Button::DLeft)   || m_input.isDown(Button::DRight)  ||
        m_input.isDown(Button::DUp)     || m_input.isDown(Button::DDown)   ||
        m_input.isDown(Button::LStickL) || m_input.isDown(Button::LStickR) ||
        m_input.isDown(Button::LStickU) || m_input.isDown(Button::LStickD) ||
        m_input.isDown(Button::RStickL) || m_input.isDown(Button::RStickR) ||
        m_input.isDown(Button::RStickU) || m_input.isDown(Button::RStickD) ||
        repeatLeft || repeatRight;

    if (m_navDebounce > 0) {
        --m_navDebounce;
    } else if (anyDpad) {
        m_navDebounce = (repeatLeft || repeatRight) ? 0 : 6;

        Widget* cur = fm.current();
        auto tryDir = [&](Button dpad, Button leftStick, Button rightStick, FocusDirection dir) {
            bool dpadDown =
                m_input.isDown(dpad) ||
                (dpad == Button::DLeft && repeatLeft) ||
                (dpad == Button::DRight && repeatRight);
            bool leftStickDown =
                m_input.isDown(leftStick) ||
                (leftStick == Button::LStickL && repeatLeft) ||
                (leftStick == Button::LStickR && repeatRight);
            bool rightStickDown =
                m_input.isDown(rightStick) ||
                (rightStick == Button::RStickL && repeatLeft) ||
                (rightStick == Button::RStickR && repeatRight);

            if (!dpadDown && !leftStickDown && !rightStickDown)
                return;
            if (cur) {
                if (dpadDown && cur->fireAction(static_cast<uint64_t>(dpad)))
                    return;
                if (leftStickDown && cur->fireAction(static_cast<uint64_t>(leftStick)))
                    return;
                if (rightStickDown && cur->fireAction(static_cast<uint64_t>(rightStick)))
                    return;
            }
            fm.navigate(dir, root);
        };

        tryDir(Button::DLeft,  Button::LStickL, Button::RStickL, FocusDirection::LEFT);
        tryDir(Button::DRight, Button::LStickR, Button::RStickR, FocusDirection::RIGHT);
        tryDir(Button::DUp,    Button::LStickU, Button::RStickU, FocusDirection::UP);
        tryDir(Button::DDown,  Button::LStickD, Button::RStickD, FocusDirection::DOWN);
    }

    constexpr uint64_t kDpadMask =
        static_cast<uint64_t>(Button::DLeft)   | static_cast<uint64_t>(Button::DRight)  |
        static_cast<uint64_t>(Button::DUp)     | static_cast<uint64_t>(Button::DDown)   |
        static_cast<uint64_t>(Button::LStickL) | static_cast<uint64_t>(Button::LStickR) |
        static_cast<uint64_t>(Button::LStickU) | static_cast<uint64_t>(Button::LStickD) |
        static_cast<uint64_t>(Button::RStickL) | static_cast<uint64_t>(Button::RStickR) |
        static_cast<uint64_t>(Button::RStickU) | static_cast<uint64_t>(Button::RStickD);

    constexpr uint64_t kA = static_cast<uint64_t>(Button::A);
    bool pointerConsumesA = m_input.pointerConsumesButton(Button::A);
    uint64_t actionExcludeMask = kDpadMask;
    if (pointerConsumesA)
        actionExcludeMask |= kA;

    uint64_t consumed = fm.dispatchActions(m_input, actionExcludeMask);

    if (!pointerConsumesA && !(consumed & kA) && m_input.isDown(Button::A)) {
        if (auto* w = fm.current())
            w->activate();
    }

    if (root->frameworkTouchEnabled())
        fm.handleTouch(m_input, root);
}

void Application::run() {
    uint64_t prevTick = armGetSystemTick();

    while (m_running) {
        uint64_t nowTick = armGetSystemTick();
        float dt = static_cast<float>(nowTick - prevTick)
                 / static_cast<float>(armGetSystemTickFreq());
        prevTick = nowTick;
        if (dt > 0.1f) dt = 0.016f;

        m_input.update();
        dispatchInput();

        if (m_activity) {
            m_activity->onUpdate(dt);
            if (!applyPendingActivity()) {
                m_running = false;
                break;
            }
            m_activity->m_rootBox->update(dt);

            if (m_renderEnabled && m_running) {
                m_gpu.beginFrame();
                m_renderer->beginFrame();
                m_activity->m_rootBox->render(*m_renderer);
                m_activity->onRender(*m_renderer);
                m_renderer->endFrame();
                m_gpu.endFrame();
            } else if (m_running) {
                svcSleepThread(100000000LL);
            }
        }
    }
}

void Application::shutdown() {
    if (s_current != this && !m_renderer && !m_activity)
        return;

#ifdef SWITCHU_MENU
    switchu::FileLog::log("[menu] shutdown begin");
#endif
    AnimationManager::instance().clear();
    m_renderEnabled = false;

    // Critical V6.3 ordering: finish every submitted command before the
    // Activity destroys textures that may still be referenced by the GPU.
    // Previously this wait happened only inside GpuDevice::shutdown(), after
    // m_activity.reset(), which could expose freed texture memory briefly.
    m_gpu.waitIdle();
#ifdef SWITCHU_MENU
    switchu::FileLog::log("[menu] gpu idle before resource destruction");
#endif

    m_input.shutdown();

    if (m_activity) {
        m_activity->onDestroy();
#ifdef SWITCHU_MENU
        switchu::FileLog::log("[menu] activity onDestroy complete");
#endif
        m_gpu.waitIdle();
        m_activity.reset();
    }
    m_pendingActivity.reset();
    m_renderer.reset();
    m_gpu.shutdown();
#ifdef SWITCHU_MENU
    switchu::FileLog::log("[menu] gpu shutdown complete");
#endif

    if (s_current == this)
        s_current = nullptr;
}

} // namespace nxui
