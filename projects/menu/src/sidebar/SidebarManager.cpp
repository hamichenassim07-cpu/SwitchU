#include "SidebarManager.hpp"
#include "core/DebugLog.hpp"
#include <nxui/core/I18n.hpp>
#include <filesystem>
#include <system_error>
#include <algorithm>

namespace {

constexpr int kSidebarIconCount = 6;

bool pathExists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

std::string joinPath(const std::string& base, const std::string& name) {
    if (base.empty())
        return name;
    if (base.back() == '/')
        return base + name;
    return base + "/" + name;
}
} // namespace

void SidebarManager::build(nxui::GpuDevice& gpu, nxui::Renderer& ren,
                           const std::string& assetsBase,
                           const Actions& actions) {
    // V9: the six functions still exist, but the HOME no longer shows a
    // permanent toolbar. Settings and Controllers are the two corner anchors;
    // focusing either one unfolds the complete system shelf.
    constexpr float btnSize = 58.f;
    constexpr float gap = 14.f;
    constexpr float startY = 624.f;

    const float groupW = 6.f * btnSize + 5.f * gap;
    const float startX = (1280.f - groupW) * 0.5f;

    m_leftButtons.clear();
    m_rightButtons.clear();
    m_settingsButton = nullptr;
    m_themeShopButton = nullptr;
    m_albumButton = nullptr;
    m_anims.clear();
    m_icons.clear();
    m_icons.resize(kSidebarIconCount);
    invalidateAssetsCache();

    auto makeBtn = [](nxui::Texture* tex,
                      const std::string& labelKey,
                      const std::string& fallback,
                      std::function<void()> action) {
        auto btn = std::make_shared<AppletButton>();
        btn->setIcon(tex);
        btn->setLabelKey(labelKey, fallback);
        btn->setAccessibilityLabel(fallback);
        btn->setAccessibilityRole(
            nxui::I18n::instance().tr("accessibility.roles.button", "button")
        );
        btn->setAccessibilityHint(
            nxui::I18n::instance().tr("accessibility.hints.open", "A to open.")
        );
        btn->setOnActivate(std::move(action));
        btn->setFocusable(true);
        return btn;
    };

    auto place = [&](const std::shared_ptr<AppletButton>& btn, int index) {
        btn->setRect({
            startX + static_cast<float>(index) * (btnSize + gap),
            startY,
            btnSize,
            btnSize
        });
    };

    {
        auto album = makeBtn(&m_icons[0], "sidebar.album", "Album", actions.onAlbum);
        m_albumButton = album.get();
        place(album, 0);
        m_leftButtons.push_back(std::move(album));

        auto miiEditor = makeBtn(
            &m_icons[1], "sidebar.mii_editor", "Mii Editor", actions.onMiiEditor
        );
        place(miiEditor, 1);
        m_leftButtons.push_back(std::move(miiEditor));

        auto settings = makeBtn(
            &m_icons[5], "sidebar.settings", "Settings", actions.onSettings
        );
        m_settingsButton = settings.get();
        place(settings, 2);
        m_leftButtons.push_back(std::move(settings));
    }

    {
        auto ctrl = makeBtn(
            &m_icons[2], "sidebar.controllers", "Controllers", actions.onControllers
        );
        place(ctrl, 3);
        m_rightButtons.push_back(std::move(ctrl));

        auto themeShop = makeBtn(
            &m_icons[4], "sidebar.theme_shop", "Theme Shop", actions.onMiiverse
        );
        m_themeShopButton = themeShop.get();
        place(themeShop, 4);
        m_rightButtons.push_back(std::move(themeShop));

        auto sleep = makeBtn(
            &m_icons[3], "sidebar.sleep", "Power", actions.onSleep
        );
        place(sleep, 5);
        m_rightButtons.push_back(std::move(sleep));
    }

    // Explicit left/right links across the two legacy vectors.
    // This makes the six buttons behave as one continuous row.
    std::vector<AppletButton*> ordered;
    ordered.reserve(6);

    for (auto& btn : m_leftButtons)
        ordered.push_back(btn.get());

    for (auto& btn : m_rightButtons)
        ordered.push_back(btn.get());

    for (size_t i = 0; i < ordered.size(); ++i) {
        AppletButton* left =
            (i > 0) ? ordered[i - 1] : ordered.front();

        AppletButton* right =
            (i + 1 < ordered.size()) ? ordered[i + 1] : ordered.back();

        ordered[i]->setCustomNavigation(nxui::FocusDirection::LEFT, left);
        ordered[i]->setCustomNavigation(nxui::FocusDirection::RIGHT, right);
    }

    m_expandProgress = 0.f;
    m_navigationExpanded = false;
    applyV9Layout(0.f);
    updateV9Navigation(false);

    (void)gpu;
    (void)ren;
    (void)assetsBase;
}

bool SidebarManager::isSidebarButton(const nxui::Widget* widget) const {
    if (!widget)
        return false;
    for (const auto& btn : m_leftButtons)
        if (btn.get() == widget)
            return true;
    for (const auto& btn : m_rightButtons)
        if (btn.get() == widget)
            return true;
    return false;
}

void SidebarManager::updateV9Navigation(bool expanded) {
    if (m_leftButtons.size() < 3 || m_rightButtons.size() < 3)
        return;

    AppletButton* ordered[6] = {
        m_leftButtons[0].get(),   // Album
        m_leftButtons[1].get(),   // Mii
        m_leftButtons[2].get(),   // Settings
        m_rightButtons[0].get(),  // Controllers
        m_rightButtons[1].get(),  // Themes
        m_rightButtons[2].get(),  // Power
    };

    if (!expanded) {
        AppletButton* settings = ordered[2];
        AppletButton* controllers = ordered[3];
        settings->setCustomNavigation(nxui::FocusDirection::LEFT, settings);
        settings->setCustomNavigation(nxui::FocusDirection::RIGHT, controllers);
        controllers->setCustomNavigation(nxui::FocusDirection::LEFT, settings);
        controllers->setCustomNavigation(nxui::FocusDirection::RIGHT, controllers);
        m_navigationExpanded = false;
        return;
    }

    for (int i = 0; i < 6; ++i) {
        ordered[i]->setCustomNavigation(
            nxui::FocusDirection::LEFT,
            ordered[std::max(0, i - 1)]
        );
        ordered[i]->setCustomNavigation(
            nxui::FocusDirection::RIGHT,
            ordered[std::min(5, i + 1)]
        );
    }
    m_navigationExpanded = true;
}

void SidebarManager::applyV9Layout(float progress) {
    if (m_leftButtons.size() < 3 || m_rightButtons.size() < 3)
        return;

    const float t = std::clamp(progress, 0.f, 1.f);
    const float eased = t * t * (3.f - 2.f * t);

    constexpr float btnSize = 58.f;
    constexpr float expandedGap = 14.f;
    constexpr float expandedY = 624.f;
    constexpr float collapsedY = 638.f;
    constexpr float leftAnchorX = 34.f;
    constexpr float rightAnchorX = 1280.f - 34.f - btnSize;

    const float groupW = 6.f * btnSize + 5.f * expandedGap;
    const float expandedStartX = (1280.f - groupW) * 0.5f;

    AppletButton* ordered[6] = {
        m_leftButtons[0].get(),
        m_leftButtons[1].get(),
        m_leftButtons[2].get(),
        m_rightButtons[0].get(),
        m_rightButtons[1].get(),
        m_rightButtons[2].get(),
    };

    for (int i = 0; i < 6; ++i) {
        const bool leftSide = i <= 2;
        const bool anchor = (i == 2 || i == 3);
        const float collapsedX = leftSide ? leftAnchorX : rightAnchorX;
        const float expandedX =
            expandedStartX + static_cast<float>(i) * (btnSize + expandedGap);

        const float x = collapsedX + (expandedX - collapsedX) * eased;
        const float y = collapsedY + (expandedY - collapsedY) * eased;

        ordered[i]->setRect({x, y, btnSize, btnSize});

        if (anchor) {
            ordered[i]->setVisible(true);
            ordered[i]->setOpacity(1.f);
        } else {
            ordered[i]->setVisible(t > 0.015f);
            ordered[i]->setOpacity(eased);
        }
    }
}

void SidebarManager::reloadAssets(nxui::GpuDevice& gpu, nxui::Renderer& ren,
                                  const std::string& assetsBase,
                                  const std::string& customIconsBase) {
    if (m_leftButtons.empty() || m_rightButtons.empty())
        return;

    if (m_assetsLoaded &&
        m_loadedAssetsBase == assetsBase &&
        m_loadedCustomIconsBase == customIconsBase) {
        DebugLog::log(
            "[sidebar-anim] reload skipped: assets unchanged (custom=%s)",
            customIconsBase.empty() ? "<empty>" : customIconsBase.c_str()
        );
        return;
    }

    loadAssets(gpu, ren, assetsBase, customIconsBase);
}

void SidebarManager::invalidateAssetsCache() {
    m_loadedAssetsBase.clear();
    m_loadedCustomIconsBase.clear();
    m_assetsLoaded = false;
}

void SidebarManager::loadAssets(nxui::GpuDevice& gpu, nxui::Renderer& ren,
                                const std::string& assetsBase,
                                const std::string& customIconsBase) {
    gpu.waitIdle();

    std::string defaultIconsBase = joinPath(assetsBase, "icons");
    const bool useCustomStaticIcons = !customIconsBase.empty();

    auto defaultAssetPath = [&](const char* fileName) {
        return joinPath(defaultIconsBase, fileName);
    };

    auto resolveAsset = [&](const char* fileName) {
        if (!customIconsBase.empty()) {
            std::string customPath = joinPath(customIconsBase, fileName);
            if (pathExists(customPath))
                return customPath;
        }
        return defaultAssetPath(fileName);
    };

    auto loadIconTexture = [&](int iconIdx, const char* fileName) {
        if (useCustomStaticIcons) {
            std::string customPath = joinPath(customIconsBase, fileName);
            if (pathExists(customPath)) {
                if (m_icons[iconIdx].loadFromFile(gpu, ren, customPath))
                    return;

                DebugLog::log(
                    "[sidebar-assets] custom icon load failed, falling back: %s",
                    customPath.c_str()
                );
            }
        }

        const std::string fallbackPath = defaultAssetPath(fileName);
        if (!m_icons[iconIdx].loadFromFile(gpu, ren, fallbackPath)) {
            DebugLog::log(
                "[sidebar-assets] fallback icon load failed: %s",
                fallbackPath.c_str()
            );
        }
    };

    static const char* iconFiles[] = {
        "album.png",
        "mii_editor.png",
        "controller.png",
        "power.png",
        "themes.png",
        "settings.png",
    };

    if ((int)m_icons.size() != kSidebarIconCount)
        m_icons.resize(kSidebarIconCount);

    for (int i = 0; i < kSidebarIconCount; ++i)
        loadIconTexture(i, iconFiles[i]);

    static const struct {
        int iconIdx;
        const char* webpFile;
        bool useFirstFrame;
    } animDefs[] = {
        {0, "album.webp", false},
        {1, "mii_editor.webp", false},
        {2, "controller.webp", true},
        {3, "power.webp", false},
        {4, "themes.webp", false},
        {5, "settings.webp", false},
    };

    m_anims.clear();

    if (useCustomStaticIcons) {
        DebugLog::log(
            "[sidebar-anim] custom theme icons use PNG only; skipping WebP animations (%s)",
            customIconsBase.c_str()
        );
    }

    for (const auto& def : animDefs) {
        AppletButton* btn = nullptr;

        if (def.iconIdx == 0) btn = m_leftButtons[0].get();
        else if (def.iconIdx == 1) btn = m_leftButtons[1].get();
        else if (def.iconIdx == 2) btn = m_rightButtons[0].get();
        else if (def.iconIdx == 3) btn = m_rightButtons[2].get();
        else if (def.iconIdx == 4) btn = m_rightButtons[1].get();
        else if (def.iconIdx == 5) btn = m_leftButtons[2].get();

        if (!btn)
            continue;

        nxui::Texture* staticTex = &m_icons[def.iconIdx];

        if (useCustomStaticIcons) {
            btn->setIcon(staticTex);
            continue;
        }

        nxui::Texture* idleTex = def.useFirstFrame ? nullptr : staticTex;
        tryLoadAnimation(gpu, ren, resolveAsset(def.webpFile), btn, idleTex);
        btn->setIcon(idleTex ? idleTex : staticTex);
    }

    m_loadedAssetsBase = assetsBase;
    m_loadedCustomIconsBase = customIconsBase;
    m_assetsLoaded = true;
}

void SidebarManager::tryLoadAnimation(nxui::GpuDevice& gpu,
                                      nxui::Renderer& ren,
                                      const std::string& webpPath,
                                      AppletButton* button,
                                      nxui::Texture* staticIcon) {
    AnimEntry entry;
    entry.button = button;
    entry.staticTex = staticIcon;

    if (entry.anim.load(gpu, ren, webpPath))
        m_anims.push_back(std::move(entry));
}

void SidebarManager::update(float dt, nxui::Widget* focusedWidget) {
    const bool wantsExpanded = isSidebarButton(focusedWidget);
    const float speed = std::max(0.f, dt) / 0.18f;

    if (wantsExpanded)
        m_expandProgress = std::min(1.f, m_expandProgress + speed);
    else
        m_expandProgress = std::max(0.f, m_expandProgress - speed);

    applyV9Layout(m_expandProgress);

    if (wantsExpanded != m_navigationExpanded)
        updateV9Navigation(wantsExpanded);

    for (auto& e : m_anims) {
        if (!e.button || !e.button->isVisible())
            continue;

        bool focused = (focusedWidget == e.button);
        e.anim.update(dt, focused);

        if (focused && e.anim.hasFrames()) {
            e.button->setIcon(e.anim.currentFrame());
        } else {
            nxui::Texture* idle =
                e.staticTex
                    ? e.staticTex
                    : (e.anim.hasFrames() ? e.anim.currentFrame() : nullptr);

            e.button->setIcon(idle);
        }
    }
}

void SidebarManager::applyTheme(const nxui::Theme& theme) {
    auto apply = [&](std::shared_ptr<AppletButton>& btn) {
        btn->setBaseColor(theme.iconDefault);
        btn->setBorderColor(theme.panelBorder);
        btn->setHighlightColor(theme.panelHighlight);
        btn->setLiquidGlassEnabled(true);
        btn->setForceLiquidGlass(true);
        btn->setBlurEnabled(false);
        btn->setBorderWidth(2.2f);
    };

    for (auto& btn : m_leftButtons)
        apply(btn);

    for (auto& btn : m_rightButtons)
        apply(btn);
}
