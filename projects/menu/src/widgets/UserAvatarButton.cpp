#include "UserAvatarButton.hpp"

#include <nxui/core/Renderer.hpp>

#include <algorithm>

UserAvatarButton::UserAvatarButton() {
    setCornerRadius(28.f);
    setPadding(0.f);
    setLiquidGlassEnabled(false);
    setBlurEnabled(false);
    setForceLiquidGlass(false);
    setPanelOpacity(0.f);
    setBorderWidth(0.f);
}

void UserAvatarButton::setChromeEnabled(bool enabled) {
    m_chromeEnabled = enabled;
    setPadding(enabled ? 4.f : 0.f);
    setLiquidGlassEnabled(enabled);
    setForceLiquidGlass(enabled);
    setPanelOpacity(enabled ? 0.86f : 0.f);
    setBorderWidth(enabled ? 1.f : 0.f);
}

void UserAvatarButton::loadAvatar(nxui::GpuDevice& gpu, nxui::Renderer& ren,
                                  const void* data, std::size_t size) {
    m_avatarTexture.loadFromMemory(gpu, ren, static_cast<const std::uint8_t*>(data), size, 96);
}

void UserAvatarButton::onContentRender(nxui::Renderer& ren) {
    const float alpha = opacity();
    nxui::Rect avatarRect = m_chromeEnabled ? glassContentRect() : rect();
    const float side = std::min(avatarRect.width, avatarRect.height);
    avatarRect.x += (avatarRect.width - side) * 0.5f;
    avatarRect.y += (avatarRect.height - side) * 0.5f;
    avatarRect.width = side;
    avatarRect.height = side;
    const float radius = m_chromeEnabled
        ? std::max(0.f, cornerRadius() - padding().top)
        : side * 0.5f;

    if (m_avatarTexture.valid()) {
        ren.drawTextureRounded(&m_avatarTexture,
                               avatarRect,
                               radius,
                               nxui::Color::white().withAlpha(alpha));
    } else {
        ren.drawRoundedRect(avatarRect,
                            nxui::Color(0.42f, 0.42f, 0.50f, 0.42f * alpha),
                            radius);
    }

    if (!m_showFocusedNickname || !m_nicknameFont || m_nickname.empty())
        return;

    constexpr float kNicknameScale = 0.72f;
    const float nicknameAlpha = m_focused ? 1.00f : 0.76f;
    const nxui::Vec2 base = m_nicknameFont->measure(m_nickname);
    const float textW = base.x * kNicknameScale;
    const float x = rect().x + (rect().width - textW) * 0.5f;
    const float y = rect().y + rect().height + 3.f;

    ren.drawText(m_nickname,
                 {x + 1.f, y + 1.2f},
                 m_nicknameFont,
                 nxui::Color(0.f, 0.f, 0.f, 0.58f * alpha * nicknameAlpha),
                 kNicknameScale);
    ren.drawText(m_nickname,
                 {x, y},
                 m_nicknameFont,
                 nxui::Color(0.98f, 0.99f, 1.00f, 0.96f * alpha * nicknameAlpha),
                 kNicknameScale);
}
