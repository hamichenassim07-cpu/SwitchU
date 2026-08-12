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
    // V10: keep all six system actions wired internally, but expose only the
    // two quiet corner controls requested by the HOME design: Settings on the
    // left and Controllers on the right. No expanding shelf remains.
    constexpr float btnSize = 66.f;
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
        settings->setChromeEnabled(false);
        settings->setIconCircular(true);
        settings->setSelectionHaloEnabled(true);
        m_settingsButton = settings.get();
        place(settings, 2);
        m_leftButtons.push_back(std::move(settings));
    }

    {
        auto ctrl = makeBtn(
            &m_icons[2], "sidebar.controllers", "Controllers", actions.onControllers
        );
        ctrl->setChromeEnabled(false);
        ctrl->setIconCircular(true);
        ctrl->setSelectionHaloEnabled(true);
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

    // V10 fixed corner layout. The four legacy actions stay alive in the
    // manager, but are deliberately hidden from the HOME until a later design.
    constexpr float cornerY = 632.f;
    constexpr float leftAnchorX = 28.f;
    constexpr float rightAnchorX = 1280.f - 28.f - btnSize;

    AppletButton* album = m_leftButtons[0].get();
    AppletButton* mii = m_leftButtons[1].get();
    AppletButton* settings = m_leftButtons[2].get();
    AppletButton* controllers = m_rightButtons[0].get();
    AppletButton* themes = m_rightButtons[1].get();
    AppletButton* power = m_rightButtons[2].get();

    settings->setRect({leftAnchorX, cornerY, btnSize, btnSize});
    controllers->setRect({rightAnchorX, cornerY, btnSize, btnSize});

    settings->setVisible(true);
    settings->setOpacity(1.f);
    controllers->setVisible(true);
    controllers->setOpacity(1.f);

    for (AppletButton* hidden : {album, mii, themes, power}) {
        hidden->setVisible(false);
        hidden->setOpacity(0.f);
    }

    settings->setCustomNavigation(nxui::FocusDirection::LEFT, settings);
    settings->setCustomNavigation(nxui::FocusDirection::RIGHT, controllers);
    controllers->setCustomNavigation(nxui::FocusDirection::LEFT, settings);
    controllers->setCustomNavigation(nxui::FocusDirection::RIGHT, controllers);


    (void)gpu;
    (void)ren;
    (void)assetsBase;
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
    // V10: no shelf animation. Only animate the two visible corner icons.
    (void)dt;

    for (auto& e : m_anims) {
        if (!e.button || !e.button->isVisible())
            continue;

        const bool focused = (focusedWidget == e.button);
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
