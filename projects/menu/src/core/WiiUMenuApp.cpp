#include "WiiUMenuApp.hpp"
#include "widgets/GlossyIcon.hpp"
#include "themeshop/ThemeHttp.hpp"
#include <nxui/core/Animation.hpp>
#include <nxui/core/I18n.hpp>
#include "DebugLog.hpp"
#include "bluetooth/BluetoothManager.hpp"
#include <switch.h>
#ifdef SWITCHU_MENU
#include "smi_commands.hpp"
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <chrono>
#include <ctime>
#include <array>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <system_error>

namespace {

static constexpr const char* kLayoutPath = "sdmc:/config/SwitchU/layout.json";
static constexpr int kMinHomePages = 8;
static constexpr const char* kBuiltInSoundPreset = "wiiu";

static constexpr float kGridRectX = 0.f;
static constexpr float kGridRectY = 90.f;
static constexpr float kGridRectW = 1280.f;
static constexpr float kGridRectH = 540.f;

static constexpr float kGridBaseCellW = 150.f;
static constexpr float kGridBaseCellH = 150.f;
static constexpr float kGridBasePadX  = 20.f;
static constexpr float kGridBasePadY  = 16.f;

bool isPackageSoundPreset(const std::string& preset) {
    return preset.rfind("package:", 0) == 0;
}

bool pathExists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

bool directoryExists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::is_directory(path, ec);
}

std::string resolveAudioOverridePath(const std::string& preferredBase,
                                    const std::string& fallbackBase,
                                    const char* relativePath) {
    if (!preferredBase.empty()) {
        const std::string preferredPath = preferredBase + "/" + relativePath;
        if (pathExists(preferredPath))
            return preferredPath;
    }
    return fallbackBase + "/" + relativePath;
}

std::string installedThemePathFromPackagePreset(const std::string& preset) {
    if (!isPackageSoundPreset(preset))
        return {};

    const std::string slug = preset.substr(std::strlen("package:"));
    if (slug.empty())
        return {};

    const std::string installPath = std::string("sdmc:/config/SwitchU/themes/") + slug;
    return directoryExists(installPath) ? installPath : std::string();
}

std::string resolveThemeSoundBase(const std::string& installPath) {
    if (installPath.empty())
        return {};

    const std::string directSfx = installPath + "/sfx";
    const std::string directMusic = installPath + "/music";
    const std::string soundsRoot = installPath + "/sounds";

    const bool hasDirect = directoryExists(directSfx) || directoryExists(directMusic);
    if (hasDirect)
        return installPath;
    if (directoryExists(soundsRoot))
        return soundsRoot;
    return {};
}

// Keep enough side/top clearance so large grids do not overlap HUD/side buttons.
static constexpr float kGridSafeSideMargin = 220.f;
static constexpr float kGridSafeTopBottomMargin = 20.f;

std::string titleIdToHex(uint64_t v) {
    char buf[17] = {};
    std::snprintf(buf, sizeof(buf), "%016llX", (unsigned long long)v);
    return std::string(buf);
}

bool hexToTitleId(const std::string& s, uint64_t& out) {
    if (s.empty()) {
        out = 0;
        return false;
    }
    char* end = nullptr;
    unsigned long long v = std::strtoull(s.c_str(), &end, 16);
    if (end == s.c_str() || *end != '\0') {
        out = 0;
        return false;
    }
    out = (uint64_t)v;
    return true;
}

int hexNibble(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

bool hexToAccountUid(const std::string& s, AccountUid& out) {
    if (s.size() != 32)
        return false;

    AccountUid uid{};
    for (int part = 0; part < 2; ++part) {
        uint64_t value = 0;
        for (int i = 0; i < 16; ++i) {
            int nibble = hexNibble(s[(size_t)(part * 16 + i)]);
            if (nibble < 0)
                return false;
            value = (value << 4) | (uint64_t)nibble;
        }
        uid.uid[part] = value;
    }

    if (!accountUidIsValid(&uid))
        return false;
    out = uid;
    return true;
}

const char* safeTag(const nxui::Widget* widget) {
    if (!widget || widget->tag().empty())
        return "<none>";
    return widget->tag().c_str();
}

std::string utf8Codepoint(uint32_t cp) {
    std::string out;
    if (cp <= 0x7F) {
        out.push_back((char)cp);
    } else if (cp <= 0x7FF) {
        out.push_back((char)(0xC0 | (cp >> 6)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back((char)(0xE0 | (cp >> 12)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else {
        out.push_back((char)(0xF0 | (cp >> 18)));
        out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
    return out;
}

std::string buttonGlyph(nxui::Button button) {
    switch (button) {
        case nxui::Button::A: return utf8Codepoint(0xE0E0);
        case nxui::Button::B: return utf8Codepoint(0xE0E1);
        case nxui::Button::X: return utf8Codepoint(0xE0E2);
        case nxui::Button::Y: return utf8Codepoint(0xE0E3);
        case nxui::Button::L: return utf8Codepoint(0xE0E4);
        case nxui::Button::R: return utf8Codepoint(0xE0E5);
        case nxui::Button::ZL: return utf8Codepoint(0xE0E6);
        case nxui::Button::ZR: return utf8Codepoint(0xE0E7);
        case nxui::Button::Plus: return utf8Codepoint(0xE0F1);
        case nxui::Button::Minus: return utf8Codepoint(0xE0F2);
        default: return {};
    }
}

std::string dpadGlyph() {
    return utf8Codepoint(0xE0EA);
}

float lockClamp01(float value) {
    return std::clamp(value, 0.f, 1.f);
}

float lockSmoothStep(float value) {
    value = lockClamp01(value);
    return value * value * (3.f - 2.f * value);
}

float lockEaseOutCubic(float value) {
    value = lockClamp01(value);
    const float inv = 1.f - value;
    return 1.f - inv * inv * inv;
}

void drawLockArc(nxui::Renderer& ren,
                 const nxui::Vec2& center,
                 float radius,
                 float startAngle,
                 float endAngle,
                 const nxui::Color& color,
                 float thickness,
                 int segments = 72) {
    if (segments < 2 || radius <= 0.f)
        return;

    nxui::Vec2 previous = {
        center.x + std::cos(startAngle) * radius,
        center.y + std::sin(startAngle) * radius
    };

    for (int i = 1; i <= segments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segments);
        const float angle = startAngle + (endAngle - startAngle) * t;
        nxui::Vec2 current = {
            center.x + std::cos(angle) * radius,
            center.y + std::sin(angle) * radius
        };
        ren.drawLine(previous, current, color, thickness);
        previous = current;
    }
}

void drawLockGlow(nxui::Renderer& ren,
                  const nxui::Vec2& center,
                  float radius,
                  const nxui::Color& color,
                  float opacity) {
    const float alpha = lockClamp01(opacity);
    ren.drawCircle(center, radius * 2.10f, color.withAlpha(color.a * 0.035f * alpha), 72);
    ren.drawCircle(center, radius * 1.58f, color.withAlpha(color.a * 0.055f * alpha), 64);
    ren.drawCircle(center, radius * 1.22f, color.withAlpha(color.a * 0.085f * alpha), 56);
    ren.drawCircle(center, radius, color.withAlpha(color.a * 0.96f * alpha), 48);
}

void buildLockClockStrings(bool use12Hour,
                           std::string& timeText,
                           std::string& dateText) {
    std::time_t now = std::time(nullptr);
    std::tm* local = std::localtime(&now);
    if (!local) {
        timeText = "--:--";
        dateText = "DATE INDISPONIBLE";
        return;
    }

    char buffer[96] = {};
    if (use12Hour) {
        int hour = local->tm_hour % 12;
        if (hour == 0)
            hour = 12;
        std::snprintf(buffer, sizeof(buffer), "%d:%02d %s",
                      hour, local->tm_min, local->tm_hour >= 12 ? "PM" : "AM");
    } else {
        std::snprintf(buffer, sizeof(buffer), "%02d:%02d", local->tm_hour, local->tm_min);
    }
    timeText = buffer;

    static constexpr const char* kFrenchDays[] = {
        "DIMANCHE", "LUNDI", "MARDI", "MERCREDI",
        "JEUDI", "VENDREDI", "SAMEDI"
    };
    static constexpr const char* kFrenchMonths[] = {
        "JANVIER", "FÉVRIER", "MARS", "AVRIL", "MAI", "JUIN",
        "JUILLET", "AOÛT", "SEPTEMBRE", "OCTOBRE", "NOVEMBRE", "DÉCEMBRE"
    };

    const int weekday = std::clamp(local->tm_wday, 0, 6);
    const int month = std::clamp(local->tm_mon, 0, 11);
    std::snprintf(buffer, sizeof(buffer), "%s  %02d  %s  %04d",
                  kFrenchDays[weekday], local->tm_mday,
                  kFrenchMonths[month], local->tm_year + 1900);
    dateText = buffer;
}

bool appEntriesRefreshEquivalent(const AppEntry& a, const AppEntry& b) {
    return a.id == b.id &&
           a.title == b.title &&
           a.titleId == b.titleId &&
           a.viewFlags == b.viewFlags &&
           a.userRequired == b.userRequired &&
           a.startupUserKnown == b.startupUserKnown &&
           a.startupUserAccount == b.startupUserAccount &&
           a.startupUserAccountOption == b.startupUserAccountOption;
}

bool gridModelsRefreshEquivalent(const GridModel& a, const GridModel& b) {
    if (a.count() != b.count())
        return false;
    for (int i = 0; i < a.count(); ++i) {
        if (!appEntriesRefreshEquivalent(a.at(i), b.at(i)))
            return false;
    }
    return true;
}

}


WiiUMenuApp::WiiUMenuApp() {}
WiiUMenuApp::~WiiUMenuApp() {
    rootBox().clearChildren();
}

void WiiUMenuApp::setTutorialStartupFade(bool enabled) {
    m_tutorialStartupFade = enabled;
}

#ifdef SWITCHU_MENU
void WiiUMenuApp::setStartupStatus(uint64_t suspendedTitleId, bool appRunning) {
    m_launcher.setStartupStatus(suspendedTitleId, appRunning);
    m_skipStartupLock = appRunning && suspendedTitleId != 0;
}
#endif

bool WiiUMenuApp::onCreate() {
    DebugLog::log("[init] onCreate enter");
    m_config.load();
    loadMenuLayout();
    m_appLoader.setPendingTransform([this](std::vector<PendingApp>& apps) {
        applyMenuLayoutToPending(apps);
    });

    nxui::I18n::instance().initialize(std::string(SD_ASSETS) + "/i18n", "en-US");
    applyUiLanguage();
    m_accessibility.initialize(m_config.accessibilityEnabled,
                               nxui::I18n::instance().activeLanguageTag(),
                               SD_ASSETS);
    m_accessibility.setSpeakHints(m_config.accessibilitySpeakHints);
    m_accessibility.setSpeakContextEveryFocus(m_config.accessibilitySpeakContextEveryFocus);
    m_accessibility.setSpeechRate(m_config.accessibilitySpeechRate);

    m_audioFuture = m_threadPool.submit([this]() {
        m_audio.initialize();
        m_availablePresets = scanAvailablePresets();
        if (!isPackageSoundPreset(m_config.soundPreset) && m_config.soundPreset != kBuiltInSoundPreset) {
            DebugLog::log("[audio] preset '%s' is no longer shipped, falling back to '%s'",
                          m_config.soundPreset.c_str(),
                          kBuiltInSoundPreset);
            m_config.soundPreset = kBuiltInSoundPreset;
        }
        loadSoundPreset(resolveSoundPresetId(m_config.soundPreset));
    });
    DebugLog::log("[init] Audio loading started on background thread");

    if (m_launcher.suspendedTitleId() != 0) {
        m_deferredBluetoothInitFrames = 120;
        DebugLog::log("[init] Bluetooth manager initialization deferred for fast return");
    } else {
        bluetooth::Initialize();
        DebugLog::log("[init] Bluetooth manager initialized");
    }
    DebugLog::log("[init] Theme Shop HTTP runtime deferred until first request");

    DebugLog::log("[init] Config loaded (theme=%s, musicVol=%.2f, sfxVol=%.2f)",
                  m_config.themePreset.c_str(), m_config.musicVolume, m_config.sfxVolume);

    m_launcher.init({
        .playSfxModalHide = [this]() { m_audio.playSfx(Sfx::ModalHide); },
        .requestExit = [this]() { app().requestExit(); },
    });


    DebugLog::log("[init] loadResources...");
    loadResources();
    DebugLog::log("[init] buildGrid...");
    buildGrid();

    if (m_skipStartupLock) {
        m_lockScreenActive = false;
        m_lockScreenUnlocking = false;
        m_lockScreenOpacity = 0.f;
        DebugLog::log("[lockscreen] skipped: suspended application return");
    } else {
        showLockScreen();
        DebugLog::log("[lockscreen] shown at startup");
    }

#ifdef SWITCHU_DEBUG_UI
    m_debugOverlay = std::make_unique<DebugImGuiOverlay>();
    if (!m_debugOverlay->initialize(app().gpu(), app().renderer())) {
        DebugLog::log("[debug] ImGui overlay init failed");
        m_debugOverlay.reset();
    } else {
        DebugLog::log("[debug] ImGui overlay ready");
    }
#endif

#ifdef SWITCHU_MENU
    m_sysMsg.setCallback([this](SysAction a) { handleSystemAction(a); });
    DebugLog::log("[init] async notifications via AppletStorage only");
    switchu::menu::smi_cmd::menuReady();
#endif

    DebugLog::log("[init] DONE");
    return true;
}

void WiiUMenuApp::onDestroy() {
    if (m_audioFuture.valid()) m_audioFuture.get();

#ifdef SWITCHU_DEBUG_UI
    if (m_debugOverlay) {
        m_debugOverlay->shutdown(app().gpu());
        m_debugOverlay.reset();
    }
#endif

    stopEditGhost();

    themeshop::http::shutdown();

    bluetooth::Finalize();

#ifdef SWITCHU_MENU
    switchu::menu::smi_cmd::menuClosing();
#endif
    if (m_layoutDirty)
        saveMenuLayout();
    m_accessibility.shutdown();
    m_audio.shutdown();
}

void WiiUMenuApp::loadResources() {
    std::string fontPath = std::string(SD_ASSETS) + "/fonts/DejaVuSans.ttf";
    if (m_fontNormal.load(app().gpu(), app().renderer(), fontPath, 24))
        m_loadedRegularFontPath = fontPath;
    if (m_fontSmall.load(app().gpu(), app().renderer(), fontPath, 18))
        m_loadedSmallFontPath = fontPath;
    m_fontIcons.load(app().gpu(), app().renderer(), std::string(SD_ASSETS) + "/fonts/switch_icons.ttf", 24);

    std::string gameCardPath = std::string(SD_ASSETS) + "/icons/gamecard.png";
    if (m_gameCardTex.loadFromFile(app().gpu(), app().renderer(), gameCardPath))
        m_loadedGameCardPath = gameCardPath;

    m_appLoader.load(m_model, m_iconStreamer);
}

void WiiUMenuApp::buildUserAvatarBar() {
    m_userAvatarButtons.clear();

    m_userAvatarBar = std::make_shared<nxui::Box>(nxui::Axis::ROW);
    m_userAvatarBar->setMarginTop(0.f);
    m_userAvatarBar->setGap(10.f);
    m_userAvatarBar->setShrink(0.f);
    m_userAvatarBar->setSize(0.f, 64.f);
    m_userAvatarBar->setTag("userAvatarBar");
    m_userAvatarBar->setWireframeEnabled(false);

    AccountUid uids[8] = {};
    s32 count = 0;
    Result rc = accountListAllUsers(uids, 8, &count);
    DebugLog::log("[profiles] accountListAllUsers rc=0x%X count=%d", rc, count);
    if (R_FAILED(rc) || count <= 0)
        return;

    for (int i = 0; i < count; ++i) {
        AccountProfile profile{};
        rc = accountGetProfile(&profile, uids[i]);
        if (R_FAILED(rc))
            continue;

        auto avatar = std::make_shared<UserAvatarButton>();
        avatar->setSize(64.f, 64.f);
        avatar->setMinWidth(64.f);
        avatar->setMinHeight(64.f);
        avatar->setShrink(0.f);
        avatar->setCornerRadius(32.f);
        avatar->setUid(uids[i]);
        avatar->setFocusable(true);

        AccountProfileBase base{};
        AccountUserData userData{};
        if (R_SUCCEEDED(accountProfileGet(&profile, &userData, &base)))
            avatar->setNickname(base.nickname);

        u32 imgSize = 0;
        if (R_SUCCEEDED(accountProfileGetImageSize(&profile, &imgSize)) && imgSize > 0) {
            std::vector<uint8_t> imgBuf(imgSize);
            u32 realSize = 0;
            if (R_SUCCEEDED(accountProfileLoadImage(&profile, imgBuf.data(), imgSize, &realSize))
                    && realSize > 0) {
                avatar->loadAvatar(app().gpu(), app().renderer(), imgBuf.data(), realSize);
            }
        }

        AccountUid uid = uids[i];
        avatar->setOnActivate([this, uid]() {
            m_audio.playSfx(Sfx::Activate);
#ifdef SWITCHU_MENU
            m_launcher.launchUserPage(uid);
#endif
        });

        accountProfileClose(&profile);
        m_userAvatarButtons.push_back(avatar);
        m_userAvatarBar->addChild(avatar);
    }

    if (!m_userAvatarButtons.empty()) {
        const float countF = static_cast<float>(m_userAvatarButtons.size());
        m_userAvatarBar->setSize(countF * 64.f + (countF - 1.f) * 10.f, 64.f);
        m_userAvatarButtons.front()->setCustomNavigation(nxui::FocusDirection::LEFT,
                                                         m_userAvatarButtons.front().get());
        m_userAvatarButtons.back()->setCustomNavigation(nxui::FocusDirection::RIGHT,
                                                        m_userAvatarButtons.back().get());
    }
}

WiiUMenuApp::GridLayoutMetrics WiiUMenuApp::computeGridLayoutMetrics() const {
    const int cols = std::clamp(m_config.gridColumns, 3, 8);
    const int rows = std::clamp(m_config.gridRows, 2, 5);

    const float baseGridW = cols * kGridBaseCellW + (cols - 1) * kGridBasePadX;
    const float baseGridH = rows * kGridBaseCellH + (rows - 1) * kGridBasePadY;

    const float safeW = std::max(1.f, kGridRectW - (kGridSafeSideMargin * 2.f));
    const float safeH = std::max(1.f, kGridRectH - (kGridSafeTopBottomMargin * 2.f));

    const float scaleW = safeW / baseGridW;
    const float scaleH = safeH / baseGridH;
    const float scale = std::min(1.f, std::min(scaleW, scaleH));

    GridLayoutMetrics m;
    m.cellW = std::max(88.f, kGridBaseCellW * scale);
    m.cellH = std::max(88.f, kGridBaseCellH * scale);
    m.padX = std::max(8.f, kGridBasePadX * scale);
    m.padY = std::max(8.f, kGridBasePadY * scale);
    return m;
}

void WiiUMenuApp::reflowHomeGrid() {
    if (!m_grid)
        return;

    const int oldFocusedIndex = m_grid->focusedGlobalIndex();
    const int oldPage = m_grid->currentPage();
    uint64_t focusedTitleId = 0;
    if (oldFocusedIndex >= 0 && oldFocusedIndex < m_model.count())
        focusedTitleId = m_model.at(oldFocusedIndex).titleId;

    std::unordered_map<uint64_t, AppEntry> byId;
    std::vector<uint64_t> appOrder;
    byId.reserve((size_t)std::max(0, m_model.count()));
    appOrder.reserve((size_t)std::max(0, m_model.count()));
    for (const auto& entry : m_model.entries()) {
        if (entry.titleId == 0 || byId.count(entry.titleId))
            continue;
        appOrder.push_back(entry.titleId);
        byId.emplace(entry.titleId, entry);
    }

    std::vector<uint64_t> slots;
    slots.reserve(appOrder.size());

    std::unordered_set<uint64_t> placed;
    placed.reserve(byId.size());

    for (uint64_t tid : m_layoutSlots) {
        if (tid == 0 || !byId.count(tid) || placed.count(tid))
            continue;

        slots.push_back(tid);
        placed.insert(tid);
    }

    for (uint64_t tid : appOrder) {
        if (tid == 0 || placed.count(tid))
            continue;

        slots.push_back(tid);
        placed.insert(tid);
    }

    if (slots != m_layoutSlots) {
        m_layoutSlots = slots;
        m_layoutDirty = true;
    }

    GridModel rebuiltModel;

    for (uint64_t tid : slots) {
        auto it = byId.find(tid);
        if (it != byId.end())
            rebuiltModel.addEntry(it->second);
    }

    const int cols = std::clamp(m_config.gridColumns, 3, 8);
    const int rows = std::clamp(m_config.gridRows, 2, 5);

    std::vector<std::shared_ptr<GlossyIcon>> icons;
    icons.reserve((size_t)std::max(0, rebuiltModel.count()));
    const auto& oldIcons = m_grid->allIcons();
    for (int i = 0; i < rebuiltModel.count(); ++i) {
        if (i < (int)oldIcons.size() && oldIcons[i] &&
            i < m_model.count() &&
            m_model.at(i).titleId == rebuiltModel.at(i).titleId) {
            icons.push_back(oldIcons[i]);
        } else {
            auto icon = makeIcon(rebuiltModel.at(i));
            icon->setBaseColor(m_theme.iconDefault);
            icons.push_back(std::move(icon));
        }
    }

    m_model = std::move(rebuiltModel);
    m_iconStreamer.resize(m_model.count());
    m_iconStreamer.setIconDataLoader(AppListLoader::loadIconData);
    for (int i = 0; i < m_model.count(); ++i)
        m_iconStreamer.setTitleId(i, m_model.at(i).titleId);

    GridLayoutMetrics gridMetrics = computeGridLayoutMetrics();
    m_grid->setup(std::move(icons), cols, rows,
                  gridMetrics.cellW, gridMetrics.cellH,
                  gridMetrics.padX, gridMetrics.padY);

    int targetIndex = -1;
    if (focusedTitleId != 0)
        targetIndex = findTitleIndex(focusedTitleId);
    if (targetIndex < 0 && oldFocusedIndex >= 0 && m_model.count() > 0)
        targetIndex = std::clamp(oldFocusedIndex, 0, m_model.count() - 1);

    if (targetIndex >= 0)
        m_grid->focusGlobalIndex(targetIndex);
    else
        m_grid->setPage(oldPage);

    for (auto* icon : m_grid->pageIcons()) {
        if (icon)
            icon->forceVisible();
    }

    m_iconStreamer.onPageChanged(m_grid->currentPage(), m_grid->iconsPerPage(),
                                 app().gpu(), app().renderer(),
                                 m_grid->allIcons());

    const bool overlayActive =
        (m_dialog && m_dialog->isActive()) ||
        (m_themeShop && m_themeShop->isActive()) ||
        (m_settings && m_settings->isActive()) ||
        (m_userSelect && m_userSelect->isActive());
    if (!overlayActive) {
        if (auto* cur = m_grid->focusManager().current())
            focusManager().setFocus(cur);
        updateCursor();
    }

    DebugLog::log("[grid] reflowed layout cols=%d rows=%d apps=%d page=%d",
                  cols, rows, m_model.count(), m_grid->currentPage());
}

void WiiUMenuApp::loadMenuLayout() {
    m_layoutSlots.clear();

    std::ifstream f(kLayoutPath);
    if (!f.is_open())
        return;

    nlohmann::json j;
    try {
        f >> j;
    } catch (...) {
        return;
    }

    auto it = j.find("slots");
    if (it == j.end() || !it->is_array())
        return;

    for (const auto& v : *it) {
        uint64_t tid = 0;
        if (v.is_string()) {
            std::string s = v.get<std::string>();
            if (!hexToTitleId(s, tid))
                tid = 0;
        } else if (v.is_number_unsigned()) {
            tid = v.get<uint64_t>();
        } else if (v.is_number_integer()) {
            auto raw = v.get<int64_t>();
            tid = raw > 0 ? (uint64_t)raw : 0;
        }
        m_layoutSlots.push_back(tid);
    }
}

void WiiUMenuApp::saveMenuLayout() {
    std::error_code ec;
    std::filesystem::create_directory("sdmc:/config", ec);
    ec.clear();
    std::filesystem::create_directory("sdmc:/config/SwitchU", ec);

    nlohmann::json j;
    j["version"] = 1;
    j["slots"] = nlohmann::json::array();
    for (uint64_t tid : m_layoutSlots) {
        if (tid == 0)
            j["slots"].push_back("0");
        else
            j["slots"].push_back(titleIdToHex(tid));
    }

    std::ofstream f(kLayoutPath, std::ios::trunc);
    if (!f.is_open())
        return;
    f << j.dump(2);
    m_layoutDirty = false;
}

void WiiUMenuApp::applyMenuLayoutToPending(std::vector<PendingApp>& apps) {
    std::unordered_map<uint64_t, PendingApp> byId;
    std::vector<uint64_t> detectedOrder;

    byId.reserve(apps.size());
    detectedOrder.reserve(apps.size());

    for (auto& app : apps) {
        if (app.titleId == 0)
            continue;

        detectedOrder.push_back(app.titleId);
        byId.emplace(app.titleId, std::move(app));
    }

    std::vector<uint64_t> orderedIds;
    orderedIds.reserve(byId.size());

    std::unordered_set<uint64_t> placed;
    placed.reserve(byId.size());

    // Conserver l'ordre déjà enregistré, mais retirer les cases vides,
    // les applications désinstallées et les doublons.
    for (uint64_t tid : m_layoutSlots) {
        if (tid == 0 || !byId.count(tid) || placed.count(tid))
            continue;

        orderedIds.push_back(tid);
        placed.insert(tid);
    }

    // Ajouter automatiquement chaque nouveau jeu à la fin de la ligne.
    for (uint64_t tid : detectedOrder) {
        if (tid == 0 || placed.count(tid))
            continue;

        orderedIds.push_back(tid);
        placed.insert(tid);
    }

    std::vector<PendingApp> ordered;
    ordered.reserve(orderedIds.size());

    for (uint64_t tid : orderedIds) {
        auto it = byId.find(tid);
        if (it != byId.end())
            ordered.push_back(std::move(it->second));
    }

    if (orderedIds != m_layoutSlots) {
        m_layoutSlots = orderedIds;
        m_layoutDirty = true;
    }

    apps = std::move(ordered);
}

std::shared_ptr<GlossyIcon> WiiUMenuApp::makeIcon(const AppEntry& entry) {
    auto icon = std::make_shared<GlossyIcon>();
    if (entry.titleId == 0) {
        icon->setTag("glossy_icon");
        icon->setTitle("");
        icon->setTitleId(0);
        icon->setFocusable(true);
        auto& i18n = nxui::I18n::instance();
        icon->setAccessibilityLabel(i18n.tr("accessibility.grid.empty_slot", "Empty slot"));
        icon->setAccessibilityRole(i18n.tr("accessibility.roles.slot", "slot"));
        icon->setAccessibilityHint(i18n.tr("accessibility.hints.grid_empty", "Use the directional pad to move to another slot."));
        icon->setNotLaunchable(false);
        icon->setCornerRadius(m_theme.iconCornerRadius);
        return icon;
    }

    icon->setTag("glossy_icon");
    icon->setTitle(entry.title);
    icon->setTitleId(entry.titleId);
    icon->setAccessibilityLabel(entry.title);
    auto& i18n = nxui::I18n::instance();
    icon->setAccessibilityRole(entry.isGameCard()
        ? i18n.tr("accessibility.roles.game_card", "game card")
        : i18n.tr("accessibility.roles.game", "game"));
    icon->setAccessibilityHint(entry.isLaunchable()
        ? i18n.tr("accessibility.hints.game_launchable", "A to launch. X for options. Y to move. ZL or ZR to change page.")
        : i18n.tr("accessibility.hints.game_blocked", "A to show why this item is blocked."));
    // Texture is set by IconStreamer::onPageChanged() — not here.
    icon->setCornerRadius(m_theme.iconCornerRadius);
    icon->setIsGameCard(entry.isGameCard());
    icon->setGameCardTexture(&m_gameCardTex);
    icon->setNotLaunchable(!entry.isLaunchable());

#ifdef SWITCHU_MENU
    if (m_launcher.suspendedTitleId() != 0 &&
        entry.titleId == m_launcher.suspendedTitleId())
        icon->setSuspended(true);

    GlossyIcon* raw = icon.get();
    icon->setOnActivate([this, raw]() {
        uint64_t tid = raw->titleId();
        if (m_launcher.isAppSuspended(tid)) {
            m_audio.playSfx(Sfx::LaunchGame);
            nxui::Rect   fr   = raw->focusRect();
            const nxui::Texture* tex = raw->texture();
            float  cr   = raw->cornerRadius();
            nxui::Color  base = m_theme.panelBase;
            nxui::Color  bord = m_theme.panelBorder;
            m_launchAnim->start(fr, tex, cr, base, bord, 0, {},
                nullptr,
                [this]() { m_launcher.resumeApplication(); });
        } else {
            AppEntry* entry = nullptr;
            int entryIndex = findTitleIndex(tid);
            if (entryIndex >= 0)
                entry = &m_model.at(entryIndex);
            if (entry && !entry->isLaunchable()) {
                m_audio.playSfx(Sfx::ModalShow);
                m_dialogReturnFocus = raw;
                std::string reason;
                auto& i18n = nxui::I18n::instance();
                if (entry->isGameCardNotInserted())
                    reason = i18n.tr("error.gamecard_not_inserted", "Game card is not inserted.");
                else if (entry->needsVerify())
                    reason = i18n.tr("error.needs_verify", "Game data needs verification.");
                else if (entry->needsUpdate())
                    reason = i18n.tr("error.needs_update", "A required update is available.");
                else if (!entry->hasContents())
                    reason = i18n.tr("error.no_contents", "Game data is missing.");
                else
                    reason = i18n.tr("error.cannot_launch", "This game cannot be launched.");
                m_dialog->show(
                    i18n.tr("error.title", "Cannot Launch"),
                    reason,
                    {{i18n.tr("button.ok", "OK"), [this]() {}, true}},
                    0, {}
                );
                focusManager().setFocus(m_dialog.get());
                return;
            }

            nxui::Rect   fr   = raw->focusRect();
            const nxui::Texture* tex = raw->texture();
            float  cr   = raw->cornerRadius();
            nxui::Color  base = m_theme.panelBase;
            nxui::Color  bord = m_theme.panelBorder;
            auto startLaunch = [this, fr, tex, cr, base, bord, tid](AccountUid uid) {
                m_audio.playSfx(Sfx::LaunchGame);
                m_launchAnim->start(fr, tex, cr, base, bord, tid, uid,
                    [this](uint64_t id, AccountUid u) { m_launcher.launchApplication(id, u); });
            };
            if (entry) {
                if (!entry->startupUserKnown) {
                    entry->startupUserAccount = 1;
                    entry->startupUserAccountOption = 0;
                    entry->userRequired = true;
                }
                DebugLog::log("[launcher] user decision tid=%016lX startup_user=%u option=%u interactive_user=%d",
                              tid,
                              (unsigned)entry->startupUserAccount,
                              (unsigned)entry->startupUserAccountOption,
                              entry->userRequired ? 1 : 0);

                if (entry->startupUserAccount == 0) {
                    AccountUid emptyUid = {};
                    DebugLog::log("[launcher] skipping user select: NACP StartupUserAccount=None");
                    startLaunch(emptyUid);
                    return;
                }

                if (m_config.defaultProfileEnabled) {
                    AccountUid defaultUid = {};
                    if (hexToAccountUid(m_config.defaultProfileUid, defaultUid)) {
                        DebugLog::log("[launcher] skipping user select: default profile configured uid[0]=0x%016lX uid[1]=0x%016lX",
                                      defaultUid.uid[0], defaultUid.uid[1]);
                        startLaunch(defaultUid);
                        return;
                    }
                    DebugLog::log("[launcher] default profile enabled but uid is invalid");
                }

                AccountUid silentUid = {};
                const bool networkRequired = entry->startupUserAccount == 2;
                Result silentRc = accountTrySelectUserWithoutInteraction(&silentUid, networkRequired);
                DebugLog::log("[launcher] TrySelectUserWithoutInteraction network_required=%d rc=0x%X uid_valid=%d uid[0]=0x%016lX uid[1]=0x%016lX",
                              networkRequired ? 1 : 0,
                              silentRc,
                              accountUidIsValid(&silentUid) ? 1 : 0,
                              silentUid.uid[0],
                              silentUid.uid[1]);
                if (R_SUCCEEDED(silentRc) && accountUidIsValid(&silentUid)) {
                    DebugLog::log("[launcher] skipping user select: silent account selection succeeded");
                    startLaunch(silentUid);
                    return;
                }

                if (!entry->userRequired) {
                    AccountUid emptyUid = {};
                    DebugLog::log("[launcher] skipping user select fallback: startup_user=%u option=%u did not require interactive picker",
                                  (unsigned)entry->startupUserAccount,
                                  (unsigned)entry->startupUserAccountOption);
                    startLaunch(emptyUid);
                    return;
                }
            }
            if (m_userSelect) {
                bool usersLoaded = m_userSelect->loadUsers(app().gpu(), app().renderer());
                DebugLog::log("[UserSelect] lazy load result=%d", usersLoaded ? 1 : 0);
                if (usersLoaded)
                    m_audio.playSfx(Sfx::ModalShow);
            }
            m_userSelect->showUserSelect([startLaunch](AccountUid uid) { startLaunch(uid); });
            focusManager().setFocus(m_userSelect.get());
        }
    });
#else
    icon->setOnActivate([this]() {
        m_audio.playSfx(Sfx::Activate);
    });
#endif
    return icon;
}

void WiiUMenuApp::buildGrid() {
    reloadThemePresets();

    m_activePresetName = m_config.themePreset;
    ThemePreset* preset = findPresetPtr(m_activePresetName);
    if (!preset) {
        m_activePresetName = "builtin:Default Light";
        preset = findPresetPtr(m_activePresetName);
    }
    if (!preset) {
        m_activePresetName = "Default Light";
        preset = findPresetPtr(m_activePresetName);
    }

    if (preset)
        m_activePresetName = preset->id.empty() ? preset->name : preset->id;

    m_activeColors = preset->colors;
    m_activeMode = preset->mode;

    m_effectivePreset = buildEffectiveThemePreset();
    m_theme = m_effectivePreset.toTheme();

    m_background = std::make_shared<WaraWaraBackground>();
    m_background->setRect({0, 0, 1280, 720});
    applyThemeResources(m_effectivePreset);

    std::vector<std::shared_ptr<GlossyIcon>> icons;
    for (int i = 0; i < m_model.count(); ++i)
        icons.push_back(makeIcon(m_model.at(i)));

    GridLayoutMetrics gridMetrics = computeGridLayoutMetrics();

    m_grid = std::make_shared<IconGrid>();
    m_grid->setRect({kGridRectX, kGridRectY, kGridRectW, kGridRectH});
    m_grid->setup(std::move(icons),
                  std::clamp(m_config.gridColumns, 3, 8),
                  std::clamp(m_config.gridRows, 2, 5),
                  gridMetrics.cellW, gridMetrics.cellH,
                  gridMetrics.padX, gridMetrics.padY);

    m_cursor = std::make_shared<SelectionCursor>();
    m_pointerCursor = std::make_shared<SelectionCursor>();
    m_pointerCursor->setVisible(false);

    m_clock = std::make_shared<DateTimeWidget>();
    m_clock->setSize(224.f, 92.f);
    m_clock->setMarginTop(0.f);
    m_clock->setMarginLeft(0.f);
    m_clock->setFont(&m_fontNormal);
    m_clock->setSmallFont(&m_fontSmall);
    m_clock->setUse12HourClock(m_config.clockUse12Hour);
    m_clock->setCornerRadius(m_theme.cellCornerRadius);
    m_clock->setForceLiquidGlass(true);
    m_clock->setBlurEnabled(false);

    m_battery = std::make_shared<BatteryWidget>();
    m_battery->setMarginTop(12.f);
    m_battery->setMarginRight(24.f);
    m_battery->setSize(238.f, 92.f);
    m_battery->setFont(&m_fontNormal);
    m_battery->setCornerRadius(m_theme.cellCornerRadius);
    m_battery->setForceLiquidGlass(true);
    m_battery->setBlurEnabled(false);

    buildUserAvatarBar();

    m_titlePill = std::make_shared<TitlePillWidget>();
    m_titlePill->setPosition(0.f, 160.f);
    m_titlePill->setFont(&m_fontNormal);
    m_titlePill->setPadding(9.f, 22.f, 9.f, 22.f);
    m_titlePill->setForceLiquidGlass(true);
    m_titlePill->setBlurEnabled(false);

    // Conservé pour compatibilité interne, mais masqué :
    // il n'y a plus de pages dans la ligne horizontale.
    m_pageIndicator = std::make_shared<PageIndicator>();
    m_pageIndicator->setRect({0.f, 685.f, 1280.f, 28.f});
    m_pageIndicator->setTheme(&m_theme);
    m_pageIndicator->setForceLiquidGlass(true);
    m_pageIndicator->setBlurEnabled(false);
    m_pageIndicator->setVisible(false);
    m_launchAnim = std::make_shared<LaunchAnimation>();

    m_userSelect = std::make_shared<OverlayDialog>();
    m_userSelect->setFont(&m_fontNormal);
    m_userSelect->setSmallFont(&m_fontSmall);
    m_userSelect->setTheme(&m_theme);
    m_userSelect->setAccessibilitySpeechPreferences(m_config.accessibilitySpeakHints,
                                                    m_config.accessibilitySpeakPosition);
    m_userSelect->onNavigateSfx([this]() { m_audio.playSfx(Sfx::Navigate); });
    m_userSelect->onActivateSfx([this]() { m_audio.playSfx(Sfx::Activate); });
    m_userSelect->onCloseSfx([this]() { m_audio.playSfx(Sfx::ModalHide); });
    m_userSelect->onAccessibilityAnnouncement([this](const std::string& text) {
        m_accessibility.announce(text);
    });
    m_userSelect->onAccessibilityStructuredAnnouncement([this](const std::string& context,
                                                               const std::string& position,
                                                               const std::string& summary,
                                                               bool forceRepeat,
                                                               bool forceContext) {
        m_accessibility.announceStructuredFocus(context, position, summary, forceRepeat, forceContext);
    });

    m_dialog = std::make_shared<OverlayDialog>();
    m_dialog->setFont(&m_fontNormal);
    m_dialog->setSmallFont(&m_fontSmall);
    m_dialog->setTheme(&m_theme);
    m_dialog->setAccessibilitySpeechPreferences(m_config.accessibilitySpeakHints,
                                                m_config.accessibilitySpeakPosition);
    m_dialog->onNavigateSfx([this]() { m_audio.playSfx(Sfx::Navigate); });
    m_dialog->onActivateSfx([this]() { m_audio.playSfx(Sfx::Activate); });
    m_dialog->onCloseSfx([this]() { m_audio.playSfx(Sfx::ModalHide); });
    m_dialog->onAccessibilityAnnouncement([this](const std::string& text) {
        m_accessibility.announce(text);
    });
    m_dialog->onAccessibilityStructuredAnnouncement([this](const std::string& context,
                                                           const std::string& position,
                                                           const std::string& summary,
                                                           bool forceRepeat,
                                                           bool forceContext) {
        m_accessibility.announceStructuredFocus(context, position, summary, forceRepeat, forceContext);
    });

    m_progressDialog = std::make_shared<ProgressDialog>();
    m_progressDialog->setFont(&m_fontNormal);
    m_progressDialog->setSmallFont(&m_fontSmall);
    m_progressDialog->setTheme(&m_theme);

    app().renderer().setBoxWireframeEnabled(m_showWireframe);

    wireFocusCallback();
    m_grid->onPageSwitched([this]() {
        // Stream icon textures for the new page.
        m_iconStreamer.onPageChanged(m_grid->currentPage(), m_grid->iconsPerPage(),
                                     app().gpu(), app().renderer(),
                                     m_grid->allIcons());
        auto* target = m_grid->focusManager().current();
        if (target)
            focusManager().setFocus(target);
        updateCursor();
    });

    int initialPage = 0;
#ifdef SWITCHU_MENU
    if (m_launcher.suspendedTitleId() != 0) {
        int suspendedIndex = findTitleIndex(m_launcher.suspendedTitleId());
        if (suspendedIndex >= 0 && m_grid->iconsPerPage() > 0)
            initialPage = suspendedIndex / m_grid->iconsPerPage();
        if (initialPage > 0)
            m_grid->setPage(initialPage);
    }
#endif

    const bool returningFromSuspendedApp = m_launcher.suspendedTitleId() != 0;
    if (returningFromSuspendedApp) {
        m_deferredInitialAssetFrames = 1;
        DebugLog::log("[init] return path: deferring initial icon/sidebar uploads");
    } else {
        // Load textures for the initial visible page.
        m_iconStreamer.onPageChanged(m_grid->currentPage(), m_grid->iconsPerPage(),
                                     app().gpu(), app().renderer(),
                                     m_grid->allIcons());
    }

    if (returningFromSuspendedApp) {
        for (auto& icon : m_grid->allIcons())
            icon->forceVisible();
        m_returnFadeTimer = kReturnFadeInDur;
    } else {
        m_grid->startAppearAnimation();
    }
    if (m_tutorialStartupFade)
        m_tutorialStartupFadeTimer = kTutorialStartupFadeDur;

    SidebarManager::Actions sidebarActions;
#ifdef SWITCHU_MENU
    sidebarActions.onAlbum       = [this]() { m_launcher.launchAlbum(); };
    sidebarActions.onMiiEditor   = [this]() { m_launcher.launchMiiEditor(); };
    sidebarActions.onControllers = [this]() { m_launcher.launchControllerPairing(); };
#else
    sidebarActions.onAlbum       = [this]() { m_audio.playSfx(Sfx::Activate); };
    sidebarActions.onMiiEditor   = [this]() { m_audio.playSfx(Sfx::Activate); };
    sidebarActions.onControllers = [this]() { m_audio.playSfx(Sfx::Activate); };
#endif
    sidebarActions.onSettings = [this]() {
        m_audio.playSfx(Sfx::ModalShow);
        if (m_settings) {
            if (m_themeShop && m_themeShop->isActive())
                m_themeShop->hide();
            m_settings->show();
            focusManager().setFocus(m_settings.get());
        }
    };
    sidebarActions.onSleep = [this]() {
        if (!m_dialog) return;
        auto& i18n = nxui::I18n::instance();
        m_audio.playSfx(Sfx::ModalShow);
        m_dialogReturnFocus = focusManager().current();
        m_dialog->show(
            i18n.tr("power.title", "Power"),
            i18n.tr("power.choose_action", "Choose a power action."),
            {
                {i18n.tr("button.cancel", "Cancel"), [this]() {  }, true},
                {i18n.tr("power.sleep", "Sleep"), [this]() {
#ifdef SWITCHU_MENU
                    m_audio.playSfx(Sfx::ConfirmPositive);
                    m_launcher.enterSleep();
#else
                    m_audio.playSfx(Sfx::ConfirmPositive);
                    app().requestExit();
#endif
                }, true},
                {i18n.tr("power.shutdown", "Shutdown"), [this]() {
#ifdef SWITCHU_MENU
                    m_audio.playSfx(Sfx::ConfirmPositive);
                    m_launcher.shutdown();
#else
                    m_audio.playSfx(Sfx::ConfirmPositive);
                    app().requestExit();
#endif
                }, true},
                {i18n.tr("power.reboot", "Reboot"), [this]() {
#ifdef SWITCHU_MENU
                    m_audio.playSfx(Sfx::ConfirmPositive);
                    m_launcher.reboot();
#else
                    m_audio.playSfx(Sfx::ConfirmPositive);
                    app().requestExit();
#endif
                }, true}
            },
            0,
            {}
        );
        focusManager().setFocus(m_dialog.get());
    };
    sidebarActions.onMiiverse = [this]() {
        m_audio.playSfx(Sfx::ModalShow);
        if (!m_themeShop) return;
        if (m_settings && m_settings->isActive())
            m_settings->hide();
        refreshThemeShopState();
        m_themeShop->show();
        focusManager().setFocus(m_themeShop.get());
    };

    m_sidebar.build(app().gpu(), app().renderer(), SD_ASSETS, sidebarActions);
    if (!returningFromSuspendedApp) {
        m_sidebar.reloadAssets(app().gpu(), app().renderer(), SD_ASSETS,
                               resolveThemeAssetPath(m_effectivePreset, m_effectivePreset.icons.basePath));
    }

    wireGlobalActions();
    applyTheme();

    auto& root = rootBox();
    root.clearChildren();

    m_bgLayer = std::make_shared<nxui::Box>();
    m_bgLayer->setRect({0, 0, 1280, 720});
    m_bgLayer->setTag("bgLayer");
    m_bgLayer->setWireframeEnabled(false);
    m_bgLayer->addChild(m_background);

    m_contentLayer = std::make_shared<nxui::Box>();
    m_contentLayer->setRect({0, 0, 1280, 720});
    m_contentLayer->setTag("contentLayer");
    m_contentLayer->setWireframeEnabled(false);

   m_topHud = std::make_shared<nxui::Box>(nxui::Axis::ROW);
    m_topHud->setRect({0.f, 0.f, 1280.f, 116.f});
    m_topHud->setTag("topHud");
    m_topHud->setWireframeEnabled(false);
    m_topHud->setJustifyContent(nxui::JustifyContent::SPACE_BETWEEN);
    m_topHud->setAlignItems(nxui::AlignItems::FLEX_START);

    auto topLeftHud = std::make_shared<nxui::Box>(nxui::Axis::ROW);
    topLeftHud->setTag("topLeftHud");
    topLeftHud->setWireframeEnabled(false);
    topLeftHud->setGap(12.f);
    topLeftHud->setMarginTop(12.f);
    topLeftHud->setMarginLeft(24.f);
    topLeftHud->setAlignItems(nxui::AlignItems::CENTER);
    topLeftHud->setShrink(0.f);

    float topLeftWidth = 232.f;
    if (m_userAvatarBar)
        topLeftWidth += 12.f + m_userAvatarBar->rect().width;

    topLeftHud->setSize(topLeftWidth, 92.f);
    topLeftHud->addChild(m_clock);

    if (m_userAvatarBar)
        topLeftHud->addChild(m_userAvatarBar);

    m_topHud->addChild(topLeftHud);
    m_topHud->addChild(m_battery);
    m_topHud->layout();

    m_leftSidebar = std::make_shared<nxui::Box>(nxui::Axis::COLUMN);
    m_leftSidebar->setTag("leftSidebar");
    m_leftSidebar->setWireframeEnabled(false);
    for (auto& btn : m_sidebar.leftButtons())
        m_leftSidebar->addChild(btn);

    m_rightSidebar = std::make_shared<nxui::Box>(nxui::Axis::COLUMN);
    m_rightSidebar->setTag("rightSidebar");
    m_rightSidebar->setWireframeEnabled(false);
    for (auto& btn : m_sidebar.rightButtons())
        m_rightSidebar->addChild(btn);

    m_contentLayer->addChild(m_grid);
    m_contentLayer->addChild(m_leftSidebar);
    m_contentLayer->addChild(m_rightSidebar);
    m_contentLayer->addChild(m_topHud);
    m_contentLayer->addChild(m_titlePill);

    m_overlayLayer = std::make_shared<nxui::Box>();
    m_overlayLayer->setRect({0, 0, 1280, 720});
    m_overlayLayer->setTag("overlayLayer");
    m_overlayLayer->setWireframeEnabled(false);
    m_overlayLayer->addChild(m_cursor);
    m_overlayLayer->addChild(m_userSelect);

    createSettings();
    createThemeShop();

    m_overlayLayer->addChild(m_dialog);
    m_overlayLayer->addChild(m_progressDialog);
    m_overlayLayer->addChild(m_launchAnim);
    m_overlayLayer->addChild(m_pointerCursor);

    root.addChild(m_bgLayer);
    root.addChild(m_contentLayer);
    root.addChild(m_overlayLayer);

    if (!focusTitle(m_launcher.suspendedTitleId())) {
        if (auto* firstIcon = m_grid->focusManager().current())
            focusManager().setFocus(firstIcon);
    }

    if (m_layoutDirty)
        saveMenuLayout();
}

std::string WiiUMenuApp::resolveSoundPresetId(const std::string& preset) const {
    std::string effectivePreset = preset;
    if (!isPackageSoundPreset(effectivePreset) && effectivePreset != kBuiltInSoundPreset) {
        DebugLog::log("[audio] preset '%s' blocked, using '%s' instead",
                      effectivePreset.c_str(),
                      kBuiltInSoundPreset);
        return kBuiltInSoundPreset;
    }

    if (!isPackageSoundPreset(effectivePreset))
        return effectivePreset;

    if (!resolveThemeSoundBase(installedThemePathFromPackagePreset(effectivePreset)).empty()) {
        DebugLog::log("[audio] package preset '%s' resolved from install directory", effectivePreset.c_str());
        return effectivePreset;
    }

    for (const auto& themePreset : m_allPresets) {
        if (themePreset.source != ThemePresetSource::InstalledPackage || themePreset.installPath.empty())
            continue;
        if (themePreset.id != effectivePreset && themePreset.soundPreset != effectivePreset)
            continue;
        return effectivePreset;
    }

    DebugLog::log("[audio] package preset '%s' unavailable, using '%s' instead",
                  effectivePreset.c_str(),
                  kBuiltInSoundPreset);
    return kBuiltInSoundPreset;
}

void WiiUMenuApp::loadSoundPreset(const std::string& preset) {
    std::string effectivePreset = preset;
    const bool useBuiltInBase = (effectivePreset == kBuiltInSoundPreset);
    const std::string builtInBase = std::string(SD_ASSETS) + "/sounds/" + kBuiltInSoundPreset;

    std::string base;
    if (!useBuiltInBase) {
        base = resolveThemeSoundBase(installedThemePathFromPackagePreset(effectivePreset));

        for (const auto& themePreset : m_allPresets) {
            if (!base.empty())
                break;
            if (themePreset.source != ThemePresetSource::InstalledPackage || themePreset.installPath.empty())
                continue;
            if (themePreset.id != effectivePreset && themePreset.soundPreset != effectivePreset)
                continue;

            base = resolveThemeSoundBase(themePreset.installPath);
            break;
        }
    }

    if (base.empty()) {
        if (isPackageSoundPreset(effectivePreset)) {
            base = installedThemePathFromPackagePreset(effectivePreset);
        } else {
            base = std::string(SD_ASSETS) + "/sounds/" + effectivePreset;
        }
    }
    DebugLog::log("[audio] Loading preset '%s' from %s", effectivePreset.c_str(), base.c_str());

    const bool hasCustomSfx = directoryExists(base + "/sfx");
    const bool hasCustomMusic = directoryExists(base + "/music");
    const std::string musicBase = hasCustomMusic ? base : builtInBase;
    const std::string preferredSfxBase = (!useBuiltInBase && hasCustomSfx) ? base : std::string();
    auto sfxPath = [&](const char* relativePath) {
        return resolveAudioOverridePath(preferredSfxBase, builtInBase, relativePath);
    };

    if (!useBuiltInBase && !hasCustomSfx) {
        DebugLog::log("[audio] preset '%s' has no custom SFX directory, using '%s' SFX fallback",
                      effectivePreset.c_str(),
                      kBuiltInSoundPreset);
    }
    if (!useBuiltInBase && !hasCustomMusic) {
        DebugLog::log("[audio] preset '%s' has no custom music, using '%s' music fallback",
                      effectivePreset.c_str(),
                      kBuiltInSoundPreset);
    }

    m_audio.loadSfx(Sfx::Navigate,        sfxPath("sfx/navigation.wav"));
    m_audio.loadSfx(Sfx::Activate,        sfxPath("sfx/activation.wav"));
    m_audio.loadSfx(Sfx::PageChange,      sfxPath("sfx/tab_transition.wav"));
    m_audio.loadSfx(Sfx::ModalShow,       sfxPath("sfx/show_modal.wav"));
    m_audio.loadSfx(Sfx::ModalHide,       sfxPath("sfx/hide_modal.wav"));
    m_audio.loadSfx(Sfx::LaunchGame,      sfxPath("sfx/launch_game.wav"));
    m_audio.loadSfx(Sfx::ThemeToggle,     sfxPath("sfx/toggle_on.wav"));
    m_audio.loadSfx(Sfx::ToggleOff,       sfxPath("sfx/toggle_off.wav"));
    m_audio.loadSfx(Sfx::SliderUp,        sfxPath("sfx/slider_up.wav"));
    m_audio.loadSfx(Sfx::SliderDown,      sfxPath("sfx/slider_down.wav"));
    m_audio.loadSfx(Sfx::ConfirmPositive, sfxPath("sfx/confirm.wav"));
    m_audio.loadSfx(Sfx::Volume,          sfxPath("sfx/volume.wav"));

    std::string musicDir = musicBase + "/music";
    std::error_code ec;
    if (std::filesystem::is_directory(musicDir, ec)) {
        std::vector<std::string> tracks;
        ec.clear();
        for (const auto& entry : std::filesystem::directory_iterator(musicDir, ec)) {
            if (ec)
                break;

            std::string name = entry.path().filename().string();
            if (name.size() > 4 && name.substr(name.size() - 4) == ".mp3")
                tracks.push_back(name);
        }
        std::sort(tracks.begin(), tracks.end(), [](const std::string& left, const std::string& right) {
            const bool leftIsHome = (left == "home.mp3");
            const bool rightIsHome = (right == "home.mp3");
            if (leftIsHome != rightIsHome)
                return leftIsHome;
            return left < right;
        });
        for (const auto& t : tracks)
            m_audio.loadTrack(musicDir + "/" + t);
        DebugLog::log("[audio] Loaded %zu music tracks", tracks.size());
    } else {
        DebugLog::log("[audio] No music directory for preset '%s'", effectivePreset.c_str());
    }
}

void WiiUMenuApp::changeSoundPreset(const std::string& preset) {
    const std::string effectivePreset = resolveSoundPresetId(preset);
    if (m_presetChangePending && effectivePreset == m_pendingSoundPreset) {
        DebugLog::log("[audio] Preset change skipped; '%s' is already pending",
                      effectivePreset.c_str());
        return;
    }

    if (m_audioStarted && effectivePreset == m_loadedSoundPreset) {
        DebugLog::log("[audio] Preset change skipped; '%s' is already active",
                      effectivePreset.c_str());
        return;
    }

    m_audio.stop();
    m_audio.clearTracks();
    m_audio.clearSfx();

    m_presetChangePending = true;
    m_pendingSoundPreset = effectivePreset;
    m_audioFuture = m_threadPool.submit([this, effectivePreset]() {
        loadSoundPreset(effectivePreset);
    });
}

std::vector<std::string> WiiUMenuApp::scanAvailablePresets() {
    std::vector<std::string> presets;
    std::string soundsDir = std::string(SD_ASSETS) + "/sounds";
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(soundsDir, ec)) {
        if (ec)
            break;

        std::string name = entry.path().filename().string();
        if (name != kBuiltInSoundPreset) continue;

        std::string sub = entry.path().string();
        if (!entry.is_directory(ec)) {
            ec.clear();
            continue;
        }

        std::string sfxSub = sub + "/sfx";
        std::string musicSub = sub + "/music";
        bool hasSfx = directoryExists(sfxSub);
        bool hasMusic = directoryExists(musicSub);
        if (hasSfx || hasMusic)
            presets.push_back(name);
    }
    std::sort(presets.begin(), presets.end());
    return presets;
}

#ifdef SWITCHU_MENU
void WiiUMenuApp::refreshAppList() {
    DebugLog::log("[refresh] starting async app list fetch");

    if (m_editMode)
        exitEditMode();

    if (m_asyncRefreshPending) {
        DebugLog::log("[refresh] already in progress, queueing another pass");
        m_refreshQueued = true;
        return;
    }

    if (m_launchAnim && m_launchAnim->isPlaying()) m_launchAnim->stop();
    if (m_userSelect && m_userSelect->isActive()) m_userSelect->hide();

    m_refreshPrevPage = m_grid ? m_grid->currentPage() : 0;
    m_asyncRefreshPending = true;
    m_refreshQueued = false;

    m_appLoader.startAsync(m_threadPool);
}

void WiiUMenuApp::finalizeRefresh() {
    DebugLog::log("[refresh] finalizing (GPU upload)");
    m_asyncRefreshPending = false;

    GridModel refreshedModel;
    IconStreamer refreshedStreamer;
    m_appLoader.finalize(refreshedModel, refreshedStreamer);
    DebugLog::log("[refresh] found %d apps", refreshedModel.count());

    if (gridModelsRefreshEquivalent(m_model, refreshedModel)) {
        DebugLog::log("[refresh] unchanged, keeping existing grid");
        m_refreshCooldownFrames = 20;
        if (m_layoutDirty)
            saveMenuLayout();
        return;
    }

    app().gpu().waitIdle();
    m_grid->clearChildren();
    m_model = std::move(refreshedModel);
    m_iconStreamer = std::move(refreshedStreamer);

    std::vector<std::shared_ptr<GlossyIcon>> icons;
    for (int i = 0; i < m_model.count(); ++i) {
        auto icon = makeIcon(m_model.at(i));
        icon->setBaseColor(m_theme.iconDefault);
        icons.push_back(std::move(icon));
    }

    GridLayoutMetrics gridMetrics = computeGridLayoutMetrics();

    m_grid->setup(std::move(icons),
                  std::clamp(m_config.gridColumns, 3, 8),
                  std::clamp(m_config.gridRows, 2, 5),
                  gridMetrics.cellW, gridMetrics.cellH,
                  gridMetrics.padX, gridMetrics.padY);
    if (m_refreshPrevPage > 0) m_grid->setPage(m_refreshPrevPage);
    wireFocusCallback();
    m_grid->onPageSwitched([this]() {
        m_iconStreamer.onPageChanged(m_grid->currentPage(), m_grid->iconsPerPage(),
                                     app().gpu(), app().renderer(),
                                     m_grid->allIcons());
        auto* target = m_grid->focusManager().current();
        if (target) focusManager().setFocus(target);
        updateCursor();
    });

    // Load textures for the restored page.
    int page = m_refreshPrevPage > 0 ? m_refreshPrevPage : 0;
    m_iconStreamer.onPageChanged(page, m_grid->iconsPerPage(),
                                 app().gpu(), app().renderer(),
                                 m_grid->allIcons());

    m_grid->startAppearAnimation();
    if (auto* firstIcon = m_grid->focusManager().current())
        focusManager().setFocus(firstIcon);

    // Keep a short cooldown to coalesce duplicate app-record notifications.
    m_refreshCooldownFrames = 20;
    applyTheme();
    if (m_layoutDirty)
        saveMenuLayout();
    DebugLog::log("[refresh] done, %d icons on page %d", m_model.count(), m_grid->currentPage());
}

#endif

void WiiUMenuApp::onUpdate(float dt) {
#ifdef SWITCHU_DEBUG_UI
    if (m_debugOverlay) {
        m_debugOverlay->setDeltaTime(dt);
    }
#endif

    if (m_returnFadeTimer > 0.f)
        m_returnFadeTimer = std::max(0.f, m_returnFadeTimer - dt);
    if (m_tutorialStartupFadeTimer > 0.f)
        m_tutorialStartupFadeTimer = std::max(0.f, m_tutorialStartupFadeTimer - dt);

    syncThemePackageTransfer();

    if (m_deferredInitialAssetFrames > 0) {
        --m_deferredInitialAssetFrames;
        if (m_deferredInitialAssetFrames == 0) {
            DebugLog::log("[init] deferred initial icon/sidebar uploads start");
            if (m_grid) {
                m_iconStreamer.onPageChanged(m_grid->currentPage(), m_grid->iconsPerPage(),
                                             app().gpu(), app().renderer(),
                                             m_grid->allIcons());
            }
            m_sidebar.reloadAssets(app().gpu(), app().renderer(), SD_ASSETS,
                                   resolveThemeAssetPath(m_effectivePreset,
                                                         m_effectivePreset.icons.basePath));
            DebugLog::log("[init] deferred initial icon/sidebar uploads done");
        }
    }

    if (m_deferredBluetoothInitFrames > 0) {
        --m_deferredBluetoothInitFrames;
        if (m_deferredBluetoothInitFrames == 0) {
            bluetooth::Initialize();
            DebugLog::log("[init] Bluetooth manager initialized (deferred)");
        }
    }

    if (!m_audioStarted && m_audioFuture.valid() &&
        m_audioFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        m_audioFuture.get();
        m_audio.setVolume(m_config.musicVolume);
        m_audio.setSfxVolume(m_config.sfxVolume);
        if (m_config.musicEnabled) m_audio.play();
        m_loadedSoundPreset = resolveSoundPresetId(m_config.soundPreset);
        m_audioStarted = true;
        DebugLog::log("[init] Audio ready (deferred)");
    }

    if (m_presetChangePending && m_audioFuture.valid() &&
        m_audioFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        m_audioFuture.get();
        m_audio.setVolume(m_config.musicVolume);
        m_audio.setSfxVolume(m_config.sfxVolume);
        if (m_config.musicEnabled)
            m_audio.play();
        m_loadedSoundPreset = m_pendingSoundPreset.empty() ? resolveSoundPresetId(m_config.soundPreset)
                                                           : m_pendingSoundPreset;
        m_pendingSoundPreset.clear();
        m_presetChangePending = false;
        DebugLog::log("[audio] Preset change complete: %s", m_config.soundPreset.c_str());
    }

    if (m_settingsNeedRefresh && m_settings) {
        m_settingsNeedRefresh = false;
        m_settings->refreshTranslations();
    }

    if (m_pendingNetConnect) {
        m_pendingNetConnect = false;
        m_launcher.launchNetConnect();
        return;
    }

#ifdef SWITCHU_MENU
    {
        AppletStorage notifySt;
        while (R_SUCCEEDED(appletPopInteractiveInData(&notifySt))) {
            switchu::smi::DaemonNotification notif{};
            s64 sz = 0;
            appletStorageGetSize(&notifySt, &sz);
            if (sz >= (s64)sizeof(notif))
                appletStorageRead(&notifySt, 0, &notif, sizeof(notif));
            appletStorageClose(&notifySt);

            if (notif.magic != switchu::smi::kNotifyMagic) continue;
            DebugLog::log("[notify] msg=%u", (unsigned)notif.msg);

            switch (notif.msg) {
            case switchu::smi::MenuMessage::HomeRequest:
                m_sysMsg.pushAction(SysAction::HomeButton);
                break;
            case switchu::smi::MenuMessage::ApplicationExited:
                m_launcher.setAppRunning(false);
                m_launcher.setAppHasForeground(false);
                m_launcher.setSuspendedTitleId(0);
                m_sysMsg.pushAction(SysAction::HomeButton);
                break;
            case switchu::smi::MenuMessage::ApplicationSuspended:
                m_launcher.setAppRunning(true);
                m_launcher.setAppHasForeground(false);
                m_launcher.setSuspendedTitleId(notif.app_id);
                m_sysMsg.pushAction(SysAction::HomeButton);
                break;
            case switchu::smi::MenuMessage::AppRecordsChanged:
            case switchu::smi::MenuMessage::GameCardMountFailure:
                m_refreshQueued = true;
                m_deferredRefreshFrames = std::max(m_deferredRefreshFrames, 3);
                break;
            case switchu::smi::MenuMessage::AppViewFlagsUpdate: {
                uint64_t tid = notif.app_id;
                uint32_t flags = notif.payload;
                m_model.updateViewFlags(tid, flags);
                for (auto& icon : m_grid->allIcons()) {
                    if (icon->titleId() == tid) {
                        bool launchable = (flags == 0) ||
                            (flags & switchu::ns::AppViewFlag_CanLaunch);
                        icon->setNotLaunchable(!launchable);
                        icon->setIsGameCard(
                            flags & switchu::ns::AppViewFlag_IsGameCard);
                        break;
                    }
                }
                break;
            }
            case switchu::smi::MenuMessage::WakeUp:
                m_sysMsg.pushAction(SysAction::WakeUp);
                break;
            case switchu::smi::MenuMessage::BatteryStatusChanged:
                if (m_battery) {
                    const uint32_t percent = switchu::smi::batteryPayloadPercentage(notif.payload);
                    const bool charging = switchu::smi::batteryPayloadCharging(notif.payload);
                    m_battery->setBatteryStatus(percent, charging);
                    DebugLog::log("[battery] daemon status percent=%u charging=%d",
                                  (unsigned)percent,
                                  charging ? 1 : 0);
                }
                break;
            default:
                break;
            }
        }
    }
    m_sysMsg.pump();
    if (m_refreshCooldownFrames > 0)
        --m_refreshCooldownFrames;
    if (m_deferredRefreshFrames > 0)
        --m_deferredRefreshFrames;
    if (m_refreshQueued && m_deferredRefreshFrames == 0 &&
        !m_asyncRefreshPending && m_refreshCooldownFrames == 0) {
        DebugLog::log("[update] deferred refresh triggered, starting refreshAppList");
        refreshAppList();
    }
    if (m_asyncRefreshPending && m_appLoader.isReady()) {
        finalizeRefresh();
    }
#endif

    if (m_lockScreenActive) {
        handleLockScreen(dt);
        return;
    }

    bool debugTouchBlocked = false;
#ifdef SWITCHU_DEBUG_UI
    debugTouchBlocked = m_showDebugOverlay;
#endif

    if (handleAccessibilityToggleCombo()) {
        m_plusExitPending = false;
        m_plusExitPendingTimer = 0.f;
    }

    if (!app().input().isDown(nxui::Button::Plus) || !app().input().isDown(nxui::Button::Minus))
        m_accessibilityToggleComboHeld = false;

    if (m_plusExitPending) {
        m_plusExitPendingTimer -= dt;
        if (m_plusExitPendingTimer <= 0.f) {
            m_plusExitPending = false;
            m_plusExitPendingTimer = 0.f;
#ifdef SWITCHU_HOMEBREW
            m_audio.playSfx(Sfx::ModalHide);
            app().requestExit();
#endif
        }
    }

    if (!debugTouchBlocked
        && !m_launchAnim->isPlaying()
        && !(m_dialog && m_dialog->isActive())
        && !(m_themeShop && m_themeShop->isActive())
        && !(m_settings && m_settings->isActive())
        && !(m_userSelect && m_userSelect->isActive()))
    {
        handleTouch();
    }

    // Quand l'inertie se termine, synchroniser le focus sur l'icône
    // qui a pris la place correspondante. Le mode déplacement reste séparé.
    if (m_grid) {
        const int settledIndex =
            m_grid->consumeSettledFocusIndex();

        if (settledIndex >= 0) {
            auto* target =
                m_grid->focusManager().current();

            if (target) {
                m_suppressNextNavigateSfx =
                    true;

                focusManager().setFocus(
                    target
                );

                updateCursor();
            }
        }

        // Pendant le déplacement et l'inertie, le contour et le titre
        // suivent l'icône sélectionnée.
        if (m_grid->isScrollMoving()) {
            updateCursor();

            auto* current =
                focusManager().current();

            if (current &&
                current->tag() ==
                    "glossy_icon" &&
                m_titlePill) {
                constexpr float
                    kSelectedScale = 1.12f;

                nxui::Rect baseRect =
                    current->focusRect();

                const float visualExpand =
                    baseRect.width *
                    (kSelectedScale - 1.f) *
                    0.5f;

                nxui::Rect visualRect =
                    baseRect.expanded(
                        visualExpand
                    );

                m_titlePill->setAnchor(
                    visualRect.x +
                        visualRect.width * 0.5f,
                    std::max(
                        104.f,
                        visualRect.y - 64.f
                    )
                );
            }
        }
    }

    bool dialogActiveNow = (m_dialog && m_dialog->isActive());
    if (!debugTouchBlocked && dialogActiveNow)
        m_dialog->handleTouch(app().input());

    if (!debugTouchBlocked && m_themeShop && m_themeShop->isActive())
        m_themeShop->handleTouch(app().input());

    if (!debugTouchBlocked && m_settings && m_settings->isActive())
        m_settings->handleTouch(app().input());

    if (m_dialogWasActive && !dialogActiveNow) {
        if (isCurrentFocusableWidget(m_dialogReturnFocus)) {
            m_suppressNextNavigateSfx = true;
            focusManager().setFocus(m_dialogReturnFocus);
        }
        m_dialogReturnFocus = nullptr;
    }
    m_dialogWasActive = dialogActiveNow;

    if (!debugTouchBlocked && m_userSelect && m_userSelect->isActive())
        m_userSelect->handleTouch(app().input());

    if (!(m_userSelect && m_userSelect->isActive())
        && !(m_dialog && m_dialog->isActive())
        && !m_launchAnim->isPlaying())
    {
        auto* cur = focusManager().current();
        if (!cur || !cur->isFocusable()) {
            if (m_themeShop && m_themeShop->isActive()) {
                focusManager().setFocus(m_themeShop.get());
            } else if (m_settings && m_settings->isActive()) {
                focusManager().setFocus(m_settings.get());
            } else {
                auto* target = m_grid->focusManager().current();
                if (target)
                    focusManager().setFocus(target);
            }
        }
    }

    m_sidebar.update(dt, focusManager().current());

    if (m_pointerCursor) {
        bool showPointer = app().input().virtualPointerEnabled();
        m_pointerCursor->setVisible(showPointer);
        if (showPointer) {
            constexpr float kPointerSize = 30.f;
            float half = kPointerSize * 0.5f;
            nxui::Rect pointerRect {
                app().input().virtualPointerX() - half,
                app().input().virtualPointerY() - half,
                kPointerSize,
                kPointerSize,
            };
            m_pointerCursor->setOpacity(app().input().isTouching() ? 1.f : 0.92f);
            m_pointerCursor->moveTo(pointerRect, half, 0.06f);
        }
    }

    nxui::AnimationManager::instance().update(dt);

    // Sample the cursor after animation update to avoid one-frame lag.
    updateEditGhost(dt);

    if (m_editMode && m_editGhostIcon)
        m_editGhostIcon->update(dt);
}

std::vector<WiiUMenuApp::ActionHint> WiiUMenuApp::buildActionHints() {
    std::vector<ActionHint> hints;
    auto& i18n = nxui::I18n::instance();
    auto add = [&](const std::string& icon, const std::string& label) {
        if (!icon.empty() && !label.empty())
            hints.push_back({icon, label});
    };
    auto addVoiceControls = [&]() {
        if (!m_config.accessibilityEnabled)
            return;
        add(buttonGlyph(nxui::Button::L), i18n.tr("hint.repeat", "Repeat"));
        add(buttonGlyph(nxui::Button::Plus) + buttonGlyph(nxui::Button::Minus),
            i18n.tr("hint.voice", "Voice"));
    };

    if (m_launchAnim && m_launchAnim->isPlaying())
        return hints;

    if (m_dialog && m_dialog->isActive()) {
        add(buttonGlyph(nxui::Button::A), i18n.tr("hint.confirm", "Confirm"));
        add(buttonGlyph(nxui::Button::B), i18n.tr("hint.back", "Back"));
        addVoiceControls();
        return hints;
    }

    if (m_userSelect && m_userSelect->isActive()) {
        add(dpadGlyph(), i18n.tr("hint.navigate", "Navigate"));
        add(buttonGlyph(nxui::Button::A), i18n.tr("hint.select", "Select"));
        add(buttonGlyph(nxui::Button::B), i18n.tr("hint.back", "Back"));
        addVoiceControls();
        return hints;
    }

    if (m_themeShop && m_themeShop->isActive()) {
        add(dpadGlyph(), i18n.tr("hint.navigate", "Navigate"));
        add(buttonGlyph(nxui::Button::A), i18n.tr("hint.select", "Select"));
        add(buttonGlyph(nxui::Button::B), i18n.tr("hint.back", "Back"));
        add(buttonGlyph(nxui::Button::X), i18n.tr("hint.search", "Search"));
        addVoiceControls();
        return hints;
    }

    if (m_settings && m_settings->isActive()) {
        add(dpadGlyph(), i18n.tr("hint.navigate", "Navigate"));
        add(buttonGlyph(nxui::Button::A), i18n.tr("hint.select", "Select"));
        add(buttonGlyph(nxui::Button::B), i18n.tr("hint.back", "Back"));
        addVoiceControls();
        return hints;
    }

    if (m_editMode) {
        add(dpadGlyph(), i18n.tr("hint.move", "Move"));
        add(buttonGlyph(nxui::Button::Y), i18n.tr("hint.place", "Place"));
        add(buttonGlyph(nxui::Button::B), i18n.tr("hint.cancel", "Cancel"));
        addVoiceControls();
        return hints;
    }

    nxui::Widget* cur = focusManager().current();
    if (cur && cur->tag() == "glossy_icon") {
        auto* icon = static_cast<GlossyIcon*>(cur);
        if (icon->titleId() != 0) {
#ifdef SWITCHU_MENU
            add(buttonGlyph(nxui::Button::A),
                m_launcher.isAppSuspended(icon->titleId())
                    ? i18n.tr("hint.resume", "Resume")
                    : i18n.tr("hint.launch", "Launch"));
            if (m_launcher.isAppSuspended(icon->titleId()))
                add(buttonGlyph(nxui::Button::X), i18n.tr("hint.close", "Close"));
#else
            add(buttonGlyph(nxui::Button::A), i18n.tr("hint.open", "Open"));
#endif
            add(buttonGlyph(nxui::Button::Y), i18n.tr("hint.move", "Move"));
        }
    } else if (cur) {
        for (const auto& btn : m_sidebar.leftButtons()) {
            if (btn.get() == cur) {
                add(buttonGlyph(nxui::Button::A), btn->label());
                break;
            }
        }
        for (const auto& btn : m_sidebar.rightButtons()) {
            if (btn.get() == cur) {
                add(buttonGlyph(nxui::Button::A), btn->label());
                break;
            }
        }
        for (const auto& avatar : m_userAvatarButtons) {
            if (avatar.get() == cur) {
                add(buttonGlyph(nxui::Button::A), i18n.tr("hint.profile", "Profile"));
                break;
            }
        }
    }

    if (m_grid && m_grid->totalPages() > 1) {
        add(buttonGlyph(nxui::Button::ZL), i18n.tr("hint.prev_page", "Prev page"));
        add(buttonGlyph(nxui::Button::ZR), i18n.tr("hint.next_page", "Next page"));
    }
    addVoiceControls();

    return hints;
}

void WiiUMenuApp::renderActionHintBar(nxui::Renderer& ren) {
    std::vector<ActionHint> hints = buildActionHints();
    if (hints.empty())
        return;

    constexpr float kIconScale = 0.66f;
    constexpr float kTextScale = 0.54f;
    constexpr float kRowH = 22.f;
    constexpr float kRowGap = 3.f;
    constexpr float kPadX = 10.f;
    constexpr float kPadY = 8.f;
    constexpr float kIconTextGap = 6.f;
    constexpr float kScreenMargin = 18.f;
    constexpr int kMaxItems = 6;

    int count = std::min((int)hints.size(), kMaxItems);
    if (count <= 0)
        return;

    float contentW = 0.f;
    for (int i = 0; i < count; ++i) {
        nxui::Vec2 iconSize = m_fontIcons.measure(hints[(size_t)i].icon);
        nxui::Vec2 labelSize = m_fontSmall.measure(hints[(size_t)i].label);
        contentW = std::max(contentW,
                            iconSize.x * kIconScale + kIconTextGap + labelSize.x * kTextScale);
    }

    float panelW = std::clamp(contentW + kPadX * 2.f, 104.f, 210.f);
    float panelH = kPadY * 2.f + count * kRowH + (count - 1) * kRowGap;
    std::string signature;
    for (int i = 0; i < count; ++i) {
        signature += hints[(size_t)i].icon;
        signature += '\n';
        signature += hints[(size_t)i].label;
        signature += '\n';
    }

    if (!m_hintPanelInitialized) {
        m_hintPanelInitialized = true;
        m_hintPanelW.setImmediate(panelW);
        m_hintPanelH.setImmediate(panelH);
        m_hintContentReveal.setImmediate(1.f);
        m_hintSignature = signature;
    } else {
        if (std::abs(m_hintPanelW.target() - panelW) > 0.5f)
            m_hintPanelW.set(panelW, 0.20f, nxui::Easing::outCubic);
        if (std::abs(m_hintPanelH.target() - panelH) > 0.5f)
            m_hintPanelH.set(panelH, 0.20f, nxui::Easing::outCubic);
        if (m_hintSignature != signature) {
            m_hintSignature = signature;
            m_hintContentReveal.setImmediate(0.45f);
            m_hintContentReveal.set(1.f, 0.18f, nxui::Easing::outCubic);
        }
    }

    panelW = std::max(1.f, m_hintPanelW.value());
    panelH = std::max(1.f, m_hintPanelH.value());

    nxui::Rect panel = {
        1280.f - kScreenMargin - panelW,
        720.f - kScreenMargin - panelH,
        panelW,
        panelH
    };
    float radius = 16.f;

    ren.drawRoundedRect({panel.x + 0.f, panel.y + 4.f, panel.width, panel.height},
                        nxui::Color(0.f, 0.f, 0.f, 0.12f),
                        radius);

    nxui::LiquidGlassSettings savedGlass = ren.liquidGlassSettings();
    auto& glass = ren.liquidGlassSettings();
    glass.refractionIntensity = 0.018f;
    glass.blurIntensity = 0.10f;
    glass.noiseIntensity = 0.0f;
    glass.glowIntensity = 0.035f;
    glass.saturation = 0.96f;
    glass.opacityMultiplier = 1.0f;
    glass.roughness = 0.004f;
    glass.powerFactor = 18.0f;

    nxui::Color tint = m_theme.panelBase.withAlpha(m_theme.mode == nxui::ThemeMode::Dark ? 0.22f : 0.18f);
    ren.drawLiquidGlass(0, panel, radius, tint, 0.86f, m_theme.mode == nxui::ThemeMode::Dark ? 0.08f : 0.04f);
    ren.liquidGlassSettings() = savedGlass;

    ren.drawRoundedRect(panel, m_theme.panelBase.withAlpha(m_theme.mode == nxui::ThemeMode::Dark ? 0.10f : 0.08f), radius);

    ren.pushClipRect(panel.shrunk(3.f));

    float reveal = std::clamp(m_hintContentReveal.value(), 0.f, 1.f);
    float y = panel.y + kPadY + (1.f - reveal) * 4.f;
    for (int i = 0; i < count; ++i) {
        const auto& hint = hints[(size_t)i];
        nxui::Vec2 iconSize = m_fontIcons.measure(hint.icon);
        nxui::Vec2 labelSize = m_fontSmall.measure(hint.label);
        float iconX = panel.x + kPadX;
        float iconY = y + (kRowH - iconSize.y * kIconScale) * 0.5f;
        float labelX = iconX + iconSize.x * kIconScale + 7.f;
        float labelY = y + (kRowH - labelSize.y * kTextScale) * 0.5f;

        ren.drawText(hint.icon, {iconX, iconY}, &m_fontIcons,
                     m_theme.textPrimary.withAlpha(0.88f * reveal), kIconScale);
        ren.drawText(hint.label, {labelX, labelY}, &m_fontSmall,
                     m_theme.textSecondary.withAlpha(0.82f * reveal), kTextScale);
        y += kRowH + kRowGap;
    }

    ren.popClipRect();
}

void WiiUMenuApp::showLockScreen() {
    closeActiveOverlays();

    m_touchHitIndex = -1;
    m_touchOnFocused = false;
    m_touchEditDragActive = false;
    m_touchStartedInGrid = false;
    m_touchScrollActive = false;
    m_touchScrollVelocity = 0.f;

    m_lockScreenActive = true;
    m_lockScreenUnlocking = false;
    m_lockPressCount = 0;
    m_lockPressResetTimer = 0.f;
    m_lockScreenPulse = 0.f;
    m_lockScreenOpacity = 1.f;
    m_lockScreenReveal = 0.f;
    m_lockUnlockProgress = 0.f;
    m_lockPressFlash = 0.f;
    m_lockBatteryPollTimer = 0.f;

    u32 percentage = 100;
    if (R_SUCCEEDED(psmGetBatteryChargePercentage(&percentage)))
        m_lockBatteryPercent = std::min<uint32_t>(percentage, 100u);

    PsmChargerType charger = PsmChargerType_Unconnected;
    if (R_SUCCEEDED(psmGetChargerType(&charger)))
        m_lockBatteryCharging = charger != PsmChargerType_Unconnected;
}

void WiiUMenuApp::handleLockScreen(float dt) {
    m_lockScreenPulse += dt;
    m_lockScreenReveal = std::min(1.f, m_lockScreenReveal + dt / 0.48f);
    m_lockPressFlash = std::max(0.f, m_lockPressFlash - dt * 2.8f);

    m_lockBatteryPollTimer -= dt;
    if (m_lockBatteryPollTimer <= 0.f) {
        m_lockBatteryPollTimer = 2.f;

        u32 percentage = m_lockBatteryPercent;
        if (R_SUCCEEDED(psmGetBatteryChargePercentage(&percentage)))
            m_lockBatteryPercent = std::min<uint32_t>(percentage, 100u);

        PsmChargerType charger = PsmChargerType_Unconnected;
        if (R_SUCCEEDED(psmGetChargerType(&charger)))
            m_lockBatteryCharging = charger != PsmChargerType_Unconnected;
    }

    if (m_lockScreenUnlocking) {
        constexpr float kUnlockDuration = 0.72f;
        m_lockUnlockProgress = std::min(
            1.f,
            m_lockUnlockProgress + dt / kUnlockDuration
        );
        m_lockScreenOpacity = 1.f - lockSmoothStep(m_lockUnlockProgress);

        if (m_lockUnlockProgress >= 1.f) {
            m_lockScreenActive = false;
            m_lockScreenUnlocking = false;
            m_lockPressCount = 0;
            m_lockPressResetTimer = 0.f;
            m_lockScreenOpacity = 0.f;
            m_audio.playSfx(Sfx::ModalHide);
            DebugLog::log("[lockscreen] unlocked");
        }
        return;
    }

    if (m_lockPressResetTimer > 0.f) {
        m_lockPressResetTimer = std::max(0.f, m_lockPressResetTimer - dt);
        if (m_lockPressResetTimer <= 0.f && m_lockPressCount > 0) {
            m_lockPressCount = 0;
            m_lockPressFlash = 0.72f;
            m_audio.playSfx(Sfx::ToggleOff);
        }
    }

    if (!app().input().isDown(nxui::Button::A))
        return;

    ++m_lockPressCount;
    m_lockPressResetTimer = 1.55f;
    m_lockPressFlash = 1.f;

    if (m_lockPressCount < 3) {
        m_audio.playSfx(Sfx::Navigate);
        return;
    }

    m_lockPressCount = 3;
    m_lockPressResetTimer = 0.f;
    m_lockScreenUnlocking = true;
    m_lockUnlockProgress = 0.f;
    m_audio.playSfx(Sfx::ConfirmPositive);
}

void WiiUMenuApp::renderLockScreen(nxui::Renderer& ren) {
    if (!m_lockScreenActive || m_lockScreenOpacity <= 0.f)
        return;

    const float opacity = lockClamp01(m_lockScreenOpacity);
    const float reveal = lockEaseOutCubic(m_lockScreenReveal);
    const float unlock = lockSmoothStep(m_lockUnlockProgress);
    const float breathe = 0.5f + 0.5f * std::sin(m_lockScreenPulse * 1.45f);
    const float slowPulse = 0.5f + 0.5f * std::sin(m_lockScreenPulse * 0.58f);
    const float lift = -34.f * unlock;
    const float contentAlpha = opacity * reveal;

    // Fond sombre, violet et bleu : proche de la Wii U, mais plus immersif.
    ren.drawGradientRect(
        {0.f, 0.f, 1280.f, 720.f},
        nxui::Color(0.006f, 0.008f, 0.030f, 0.915f * opacity),
        nxui::Color(0.024f, 0.010f, 0.072f, 0.955f * opacity)
    );
    ren.drawGradientRect(
        {0.f, 0.f, 1280.f, 720.f},
        nxui::Color(0.055f, 0.020f, 0.125f, 0.12f * opacity),
        nxui::Color(0.005f, 0.030f, 0.085f, 0.03f * opacity)
    );

    // Grandes lumières atmosphériques placées en bord d'écran.
    ren.drawCircle(
        {-72.f, 635.f + lift},
        430.f,
        nxui::Color(0.02f, 0.52f, 0.72f, (0.080f + 0.024f * slowPulse) * opacity),
        96
    );
    ren.drawCircle(
        {1222.f, 70.f + lift},
        385.f,
        nxui::Color(0.70f, 0.07f, 0.46f, (0.074f + 0.022f * breathe) * opacity),
        96
    );
    ren.drawCircle(
        {675.f, 790.f + lift},
        355.f,
        nxui::Color(0.14f, 0.18f, 0.74f, 0.058f * opacity),
        96
    );

    // Étoiles discrètes et légèrement animées.
    static const std::array<nxui::Vec2, 30> kStars = {{
        {72.f, 110.f}, {154.f, 238.f}, {232.f, 82.f}, {318.f, 168.f},
        {406.f, 58.f}, {492.f, 128.f}, {572.f, 72.f}, {660.f, 112.f},
        {742.f, 48.f}, {826.f, 145.f}, {906.f, 86.f}, {1004.f, 174.f},
        {1102.f, 112.f}, {1190.f, 246.f}, {112.f, 430.f}, {210.f, 552.f},
        {330.f, 628.f}, {454.f, 520.f}, {562.f, 650.f}, {744.f, 588.f},
        {862.f, 656.f}, {986.f, 540.f}, {1106.f, 632.f}, {1214.f, 468.f},
        {92.f, 332.f}, {276.f, 362.f}, {1018.f, 348.f}, {1172.f, 374.f},
        {390.f, 686.f}, {902.f, 250.f}
    }};

    for (size_t i = 0; i < kStars.size(); ++i) {
        const float phase = static_cast<float>(i) * 0.71f;
        const float twinkle = 0.52f + 0.48f * std::sin(m_lockScreenPulse * 0.82f + phase);
        const float radius = (i % 6 == 0) ? 2.15f : ((i % 3 == 0) ? 1.45f : 1.0f);
        ren.drawCircle(
            {kStars[i].x, kStars[i].y + lift * 0.24f},
            radius,
            nxui::Color(0.80f, 0.90f, 1.f, (0.095f + 0.19f * twinkle) * contentAlpha),
            16
        );
    }

    // Architecture orbitale pour relier le lockscreen au monde du menu HOME.
    const nxui::Vec2 orbitCenter = {640.f, 350.f + lift};
    const float orbitRotation = m_lockScreenPulse * 0.055f;
    drawLockArc(
        ren, orbitCenter, 283.f,
        -2.82f + orbitRotation, 0.40f + orbitRotation,
        nxui::Color(0.48f, 0.72f, 1.f, 0.090f * contentAlpha), 1.35f, 96
    );
    drawLockArc(
        ren, orbitCenter, 283.f,
        0.73f + orbitRotation, 2.62f + orbitRotation,
        nxui::Color(0.78f, 0.42f, 0.98f, 0.070f * contentAlpha), 1.15f, 62
    );
    drawLockArc(
        ren, orbitCenter, 354.f,
        -0.94f - orbitRotation * 0.55f, 1.10f - orbitRotation * 0.55f,
        nxui::Color(1.f, 0.75f, 0.30f, 0.064f * contentAlpha), 1.0f, 68
    );

    const float orbiterAngleA = -0.74f + orbitRotation * 2.2f;
    const float orbiterAngleB = 2.32f - orbitRotation * 1.5f;
    const nxui::Vec2 orbiterA = {
        orbitCenter.x + std::cos(orbiterAngleA) * 283.f,
        orbitCenter.y + std::sin(orbiterAngleA) * 283.f
    };
    const nxui::Vec2 orbiterB = {
        orbitCenter.x + std::cos(orbiterAngleB) * 354.f,
        orbitCenter.y + std::sin(orbiterAngleB) * 354.f
    };
    drawLockGlow(ren, orbiterA, 3.5f, nxui::Color(0.62f, 0.88f, 1.f, 1.f), contentAlpha);
    drawLockGlow(ren, orbiterB, 2.9f, nxui::Color(1.f, 0.78f, 0.34f, 1.f), contentAlpha);

    // Identité Switch U en haut à gauche.
    const std::string brand = "SWITCH  U";
    constexpr float brandScale = 0.78f;
    const nxui::Vec2 brandSize = m_fontSmall.measure(brand);
    ren.drawText(
        brand,
        {58.f, 38.f + lift * 0.25f},
        &m_fontSmall,
        nxui::Color(0.90f, 0.95f, 1.f, 0.82f * contentAlpha),
        brandScale
    );
    ren.drawLine(
        {58.f, 68.f + lift * 0.25f},
        {58.f + std::max(62.f, brandSize.x * brandScale), 68.f + lift * 0.25f},
        nxui::Color(0.40f, 0.76f, 1.f, 0.44f * contentAlpha),
        1.4f
    );

    const std::string lockedLabel = "ÉCRAN VERROUILLÉ";
    constexpr float lockedScale = 0.56f;
    const nxui::Vec2 lockedSize = m_fontSmall.measure(lockedLabel);
    const nxui::Rect lockedPill = {
        58.f,
        80.f + lift * 0.25f,
        lockedSize.x * lockedScale + 28.f,
        28.f
    };
    ren.drawRoundedRect(
        lockedPill,
        nxui::Color(0.08f, 0.12f, 0.28f, 0.34f * contentAlpha),
        14.f
    );
    ren.drawRoundedRectOutline(
        lockedPill.shrunk(0.8f),
        nxui::Color(0.56f, 0.78f, 1.f, 0.16f * contentAlpha),
        13.2f,
        1.f
    );
    ren.drawCircle(
        {lockedPill.x + 13.f, lockedPill.y + 14.f},
        3.2f + 0.8f * breathe,
        nxui::Color(0.42f, 0.84f, 1.f, 0.82f * contentAlpha),
        20
    );
    ren.drawText(
        lockedLabel,
        {lockedPill.x + 23.f, lockedPill.y + 6.f},
        &m_fontSmall,
        nxui::Color(0.76f, 0.88f, 1.f, 0.74f * contentAlpha),
        lockedScale
    );

    // Batterie réelle en haut à droite, avec état traduit en français.
    char batteryBuffer[16] = {};
    std::snprintf(batteryBuffer, sizeof(batteryBuffer), "%u%%", m_lockBatteryPercent);
    const std::string batteryText = batteryBuffer;
    const std::string batteryStatus = m_lockBatteryCharging ? "EN CHARGE" : "BATTERIE";
    const nxui::Rect batteryPill = {1030.f, 31.f + lift * 0.25f, 192.f, 52.f};
    ren.drawRoundedRect(
        batteryPill,
        nxui::Color(0.05f, 0.08f, 0.18f, 0.38f * contentAlpha),
        26.f
    );
    ren.drawRoundedRectOutline(
        batteryPill.shrunk(1.f),
        nxui::Color(0.72f, 0.84f, 1.f, 0.18f * contentAlpha),
        25.f,
        1.1f
    );

    const nxui::Rect batteryBody = {1051.f, 49.f + lift * 0.25f, 36.f, 16.f};
    ren.drawRoundedRectOutline(
        batteryBody,
        nxui::Color(0.86f, 0.93f, 1.f, 0.66f * contentAlpha),
        5.f,
        1.4f
    );
    ren.drawRoundedRect(
        {1088.5f, 54.f + lift * 0.25f, 3.f, 6.f},
        nxui::Color(0.86f, 0.93f, 1.f, 0.56f * contentAlpha),
        1.5f
    );

    const float batteryFill = lockClamp01(static_cast<float>(m_lockBatteryPercent) / 100.f);
    const nxui::Color batteryColor = m_lockBatteryPercent <= 20
        ? nxui::Color(1.f, 0.28f, 0.30f, 0.94f * contentAlpha)
        : (m_lockBatteryCharging
            ? nxui::Color(0.98f, 0.76f, 0.28f, 0.96f * contentAlpha)
            : nxui::Color(0.44f, 0.88f, 1.f, 0.94f * contentAlpha));
    if (batteryFill > 0.f) {
        ren.drawRoundedRect(
            {1054.f, 52.f + lift * 0.25f, 30.f * batteryFill, 10.f},
            batteryColor,
            std::min(4.f, 15.f * batteryFill)
        );
    }

    ren.drawText(
        batteryText,
        {1103.f, 42.f + lift * 0.25f},
        &m_fontSmall,
        nxui::Color(0.92f, 0.96f, 1.f, 0.88f * contentAlpha),
        0.82f
    );
    ren.drawText(
        batteryStatus,
        {1155.f, 48.f + lift * 0.25f},
        &m_fontSmall,
        m_lockBatteryCharging
            ? nxui::Color(1.f, 0.80f, 0.36f, 0.78f * contentAlpha)
            : nxui::Color(0.62f, 0.75f, 0.94f, 0.58f * contentAlpha),
        0.50f
    );

    // Carte centrale en verre : elle structure l'écran sans cacher l'ambiance.
    const nxui::Rect clockPanel = {
        326.f,
        132.f + lift + (1.f - reveal) * 12.f,
        628.f,
        270.f
    };
    ren.drawRoundedRect(
        {clockPanel.x, clockPanel.y + 10.f, clockPanel.width, clockPanel.height},
        nxui::Color(0.f, 0.f, 0.f, 0.20f * contentAlpha),
        54.f
    );

    nxui::LiquidGlassSettings savedClockGlass = ren.liquidGlassSettings();
    auto& clockGlass = ren.liquidGlassSettings();
    clockGlass.refractionIntensity = 0.014f;
    clockGlass.blurIntensity = 0.105f;
    clockGlass.noiseIntensity = 0.f;
    clockGlass.glowIntensity = 0.030f + 0.014f * slowPulse;
    clockGlass.saturation = 1.02f;
    clockGlass.opacityMultiplier = 1.f;
    clockGlass.roughness = 0.004f;
    clockGlass.powerFactor = 19.f;
    ren.drawLiquidGlass(
        0,
        clockPanel,
        54.f,
        nxui::Color(0.07f, 0.09f, 0.24f, 0.22f * contentAlpha),
        0.88f * contentAlpha,
        0.055f
    );
    ren.liquidGlassSettings() = savedClockGlass;
    ren.drawRoundedRectOutline(
        clockPanel.shrunk(1.2f),
        nxui::Color(0.68f, 0.82f, 1.f, (0.13f + 0.035f * breathe) * contentAlpha),
        52.8f,
        1.3f
    );

    // Petit halo central derrière l'heure.
    ren.drawCircle(
        {640.f, 260.f + lift},
        192.f,
        nxui::Color(0.32f, 0.52f, 1.f, (0.026f + 0.012f * slowPulse) * contentAlpha),
        80
    );

    std::string timeText;
    std::string dateText;
    buildLockClockStrings(m_config.clockUse12Hour, timeText, dateText);

    const std::string welcome = m_lockScreenUnlocking ? "OUVERTURE DU MENU HOME" : "BON RETOUR";
    constexpr float welcomeScale = 0.70f;
    const nxui::Vec2 welcomeSize = m_fontSmall.measure(welcome);
    ren.drawText(
        welcome,
        {
            640.f - welcomeSize.x * welcomeScale * 0.5f,
            165.f + lift + (1.f - reveal) * 16.f
        },
        &m_fontSmall,
        m_lockScreenUnlocking
            ? nxui::Color(0.74f, 0.92f, 1.f, 0.82f * contentAlpha)
            : nxui::Color(0.62f, 0.78f, 1.f, 0.74f * contentAlpha),
        welcomeScale
    );

    constexpr float timeScale = 3.10f;
    const nxui::Vec2 timeSize = m_fontNormal.measure(timeText);
    const float timeX = 640.f - timeSize.x * timeScale * 0.5f;
    const float timeY = 205.f + lift + (1.f - reveal) * 22.f;
    ren.drawText(
        timeText,
        {timeX + 2.5f, timeY + 3.5f},
        &m_fontNormal,
        nxui::Color(0.f, 0.f, 0.f, 0.32f * contentAlpha),
        timeScale
    );
    ren.drawText(
        timeText,
        {timeX, timeY},
        &m_fontNormal,
        nxui::Color(0.95f, 0.98f, 1.f, 0.99f * contentAlpha),
        timeScale
    );
    ren.drawText(
        timeText,
        {timeX + 0.8f, timeY},
        &m_fontNormal,
        nxui::Color(0.72f, 0.88f, 1.f, 0.34f * contentAlpha),
        timeScale
    );

    constexpr float dateScale = 0.78f;
    const nxui::Vec2 dateSize = m_fontSmall.measure(dateText);
    ren.drawText(
        dateText,
        {640.f - dateSize.x * dateScale * 0.5f, 319.f + lift},
        &m_fontSmall,
        nxui::Color(0.78f, 0.86f, 0.98f, 0.76f * contentAlpha),
        dateScale
    );

    const std::string readyText = m_lockScreenUnlocking ? "DÉVERROUILLAGE EN COURS" : "SYSTÈME PRÊT";
    constexpr float readyScale = 0.52f;
    const nxui::Vec2 readySize = m_fontSmall.measure(readyText);
    ren.drawText(
        readyText,
        {640.f - readySize.x * readyScale * 0.5f, 359.f + lift},
        &m_fontSmall,
        nxui::Color(0.48f, 0.72f, 0.98f, 0.56f * contentAlpha),
        readyScale
    );

    // Les trois pressions forment une seule progression visuelle.
    constexpr float nodeYBase = 448.f;
    constexpr float nodeGap = 84.f;
    const float nodeY = nodeYBase + lift;
    const float firstNodeX = 640.f - nodeGap;
    ren.drawLine(
        {firstNodeX, nodeY},
        {firstNodeX + nodeGap * 2.f, nodeY},
        nxui::Color(0.42f, 0.58f, 0.88f, 0.20f * contentAlpha),
        2.f
    );

    if (m_lockPressCount > 1) {
        const float completedWidth = nodeGap * static_cast<float>(m_lockPressCount - 1);
        ren.drawLine(
            {firstNodeX, nodeY},
            {firstNodeX + completedWidth, nodeY},
            nxui::Color(0.45f, 0.84f, 1.f, 0.78f * contentAlpha),
            3.f
        );
    }

    for (int i = 0; i < 3; ++i) {
        const float x = firstNodeX + nodeGap * static_cast<float>(i);
        const bool completed = i < m_lockPressCount;
        const bool next = i == m_lockPressCount && !m_lockScreenUnlocking;
        const float localPulse = next ? (0.72f + 0.28f * breathe) : 1.f;
        const float flash = completed && i == m_lockPressCount - 1 ? m_lockPressFlash : 0.f;

        if (next || flash > 0.f) {
            ren.drawCircle(
                {x, nodeY},
                25.f + flash * 10.f,
                nxui::Color(0.40f, 0.74f, 1.f, (0.050f + flash * 0.082f) * contentAlpha),
                48
            );
        }

        ren.drawCircle(
            {x, nodeY},
            14.f,
            nxui::Color(0.01f, 0.02f, 0.08f, 0.82f * contentAlpha),
            40
        );
        ren.drawCircle(
            {x, nodeY},
            completed ? 9.8f + flash * 1.8f : 7.7f * localPulse,
            completed
                ? nxui::Color(0.47f, 0.86f, 1.f, 0.99f * contentAlpha)
                : nxui::Color(0.62f, 0.70f, 0.92f, (next ? 0.32f : 0.15f) * contentAlpha),
            36
        );
        if (completed) {
            ren.drawCircle(
                {x - 2.8f, nodeY - 3.4f},
                3.2f,
                nxui::Color(0.96f, 0.99f, 1.f, 0.66f * contentAlpha),
                20
            );
        }
    }

    // Barre d'action principale en verre.
    const nxui::Rect actionPanel = {
        402.f,
        516.f + lift + unlock * 18.f,
        476.f,
        86.f
    };
    ren.drawRoundedRect(
        {actionPanel.x, actionPanel.y + 8.f, actionPanel.width, actionPanel.height},
        nxui::Color(0.f, 0.f, 0.f, 0.25f * contentAlpha),
        43.f
    );

    nxui::LiquidGlassSettings savedGlass = ren.liquidGlassSettings();
    auto& glass = ren.liquidGlassSettings();
    glass.refractionIntensity = 0.020f;
    glass.blurIntensity = 0.13f;
    glass.noiseIntensity = 0.f;
    glass.glowIntensity = 0.042f + 0.020f * breathe;
    glass.saturation = 1.02f;
    glass.opacityMultiplier = 1.f;
    glass.roughness = 0.005f;
    glass.powerFactor = 18.f;
    ren.drawLiquidGlass(
        0,
        actionPanel,
        43.f,
        nxui::Color(0.10f, 0.14f, 0.32f, 0.32f * contentAlpha),
        0.92f * contentAlpha,
        0.06f
    );
    ren.liquidGlassSettings() = savedGlass;
    ren.drawRoundedRectOutline(
        actionPanel.shrunk(1.2f),
        nxui::Color(0.68f, 0.84f, 1.f, (0.16f + 0.055f * breathe) * contentAlpha),
        41.8f,
        1.4f
    );

    const nxui::Vec2 buttonCenter = {452.f, actionPanel.y + actionPanel.height * 0.5f};
    ren.drawCircle(
        buttonCenter,
        27.f + 2.f * breathe,
        nxui::Color(0.34f, 0.74f, 1.f, 0.14f * contentAlpha),
        44
    );
    ren.drawCircle(
        buttonCenter,
        22.f,
        nxui::Color(0.10f, 0.20f, 0.44f, 0.82f * contentAlpha),
        44
    );

    const std::string aGlyph = buttonGlyph(nxui::Button::A);
    constexpr float glyphScale = 1.08f;
    const nxui::Vec2 glyphSize = m_fontIcons.measure(aGlyph);
    ren.drawText(
        aGlyph,
        {
            buttonCenter.x - glyphSize.x * glyphScale * 0.5f,
            buttonCenter.y - glyphSize.y * glyphScale * 0.5f
        },
        &m_fontIcons,
        nxui::Color(0.94f, 0.98f, 1.f, 0.96f * contentAlpha),
        glyphScale
    );

    const std::string instruction = m_lockScreenUnlocking
        ? "Ouverture du menu HOME..."
        : "Appuie trois fois sur A";
    constexpr float instructionScale = 0.88f;
    ren.drawText(
        instruction,
        {496.f, actionPanel.y + 20.f},
        &m_fontSmall,
        nxui::Color(0.94f, 0.97f, 1.f, 0.94f * contentAlpha),
        instructionScale
    );

    char progressBuffer[24] = {};
    std::snprintf(progressBuffer, sizeof(progressBuffer), "%d SUR 3", m_lockPressCount);
    const std::string progressText = progressBuffer;
    constexpr float progressScale = 0.66f;
    ren.drawText(
        progressText,
        {496.f, actionPanel.y + 52.f},
        &m_fontSmall,
        nxui::Color(0.54f, 0.74f, 0.98f, 0.76f * contentAlpha),
        progressScale
    );

    const std::string footerText = "Trois pressions rapides pour accéder au menu HOME";
    constexpr float footerScale = 0.53f;
    const nxui::Vec2 footerSize = m_fontSmall.measure(footerText);
    ren.drawText(
        footerText,
        {640.f - footerSize.x * footerScale * 0.5f, 652.f + lift * 0.25f},
        &m_fontSmall,
        nxui::Color(0.58f, 0.70f, 0.90f, 0.46f * contentAlpha),
        footerScale
    );

    // L'animation finale ouvre progressivement le lockscreen vers le menu HOME.
    if (m_lockScreenUnlocking) {
        const float flashT = std::sin(std::min(1.f, m_lockUnlockProgress * 1.35f) * 3.14159265f);
        const float ringRadius = 42.f + 440.f * lockEaseOutCubic(m_lockUnlockProgress);
        drawLockArc(
            ren,
            orbitCenter,
            ringRadius,
            0.f,
            6.28318530f,
            nxui::Color(0.70f, 0.92f, 1.f, 0.34f * flashT * opacity),
            2.6f,
            108
        );
        ren.drawCircle(
            orbitCenter,
            34.f + 230.f * lockEaseOutCubic(m_lockUnlockProgress),
            nxui::Color(0.72f, 0.92f, 1.f, 0.062f * flashT * opacity),
            96
        );
        ren.drawGradientRect(
            {0.f, 0.f, 1280.f, 720.f},
            nxui::Color(0.70f, 0.90f, 1.f, 0.020f * flashT * opacity),
            nxui::Color(0.55f, 0.40f, 1.f, 0.012f * flashT * opacity)
        );
    }
}

void WiiUMenuApp::onRender(nxui::Renderer& ren) {
    if (m_returnFadeTimer > 0.f) {
        float alpha = m_returnFadeTimer / kReturnFadeInDur;
        ren.drawRect({0, 0, 1280, 720}, nxui::Color(0, 0, 0, alpha));
    }
    if (m_tutorialStartupFadeTimer > 0.f) {
        float t = std::clamp(m_tutorialStartupFadeTimer / kTutorialStartupFadeDur, 0.f, 1.f);
        float alpha = nxui::Easing::outCubic(t);
        ren.drawRect({0, 0, 1280, 720}, nxui::Color(1.f, 1.f, 1.f, alpha));
    }

    if (!m_touchScrollActive &&
        m_touchHitIndex >= 0 &&
        !m_touchOnFocused &&
        app().input().isTouching()) {
        auto icons = m_grid->pageIcons();
        if (m_touchHitIndex < (int)icons.size()) {
            nxui::Rect r = icons[m_touchHitIndex]->focusRect();
            float cr = icons[m_touchHitIndex]->cornerRadius();
            ren.drawRoundedRect(r, nxui::Color(1.f, 1.f, 1.f, 0.18f), cr);
        }
    }

    m_pageIndicator->setPageCount(m_grid->totalPages());
    m_pageIndicator->setCurrentPage(m_grid->currentPage());

    if (m_themeRenderDebugFrames > 0) {
        nxui::Widget* focus = focusManager().current();
        nxui::Widget* focusParent = focus ? focus->parent() : nullptr;
        std::vector<GlossyIcon*> pageIcons = m_grid ? m_grid->pageIcons() : std::vector<GlossyIcon*>();
        GlossyIcon* firstPageIcon = pageIcons.empty() ? nullptr : pageIcons.front();
        const nxui::Texture* firstTexture = firstPageIcon ? firstPageIcon->texture() : nullptr;

        DebugLog::log("[theme-render] preset=%s focus=%s parent=%s rootChildren=%zu contentChildren=%zu overlayChildren=%zu grid(all=%zu page=%zu vis=%d op=%.2f firstTex=%d firstIconVis=%d firstIconOp=%.2f) settings(active=%d vis=%d op=%.2f) themeshop(active=%d vis=%d op=%.2f)",
                      m_activePresetName.c_str(),
                      safeTag(focus),
                      safeTag(focusParent),
                      rootBox().children().size(),
                      m_contentLayer ? m_contentLayer->children().size() : 0,
                      m_overlayLayer ? m_overlayLayer->children().size() : 0,
                      m_grid ? m_grid->allIcons().size() : 0,
                      pageIcons.size(),
                      m_grid && m_grid->isVisible() ? 1 : 0,
                      m_grid ? m_grid->opacity() : 0.f,
                      (firstTexture && firstTexture->valid()) ? 1 : 0,
                      (firstPageIcon && firstPageIcon->isVisible()) ? 1 : 0,
                      firstPageIcon ? firstPageIcon->opacity() : 0.f,
                      (m_settings && m_settings->isActive()) ? 1 : 0,
                      (m_settings && m_settings->isVisible()) ? 1 : 0,
                      m_settings ? m_settings->opacity() : 0.f,
                      (m_themeShop && m_themeShop->isActive()) ? 1 : 0,
                      (m_themeShop && m_themeShop->isVisible()) ? 1 : 0,
                      m_themeShop ? m_themeShop->opacity() : 0.f);
        --m_themeRenderDebugFrames;
    }

    // Final topmost pass for move-mode ghost.
    if (m_editMode && m_editGhostIcon)
        m_editGhostIcon->render(ren);

    renderLockScreen(ren);


#ifdef SWITCHU_DEBUG_UI
    if (m_debugOverlay) {
        m_debugOverlay->render(ren, app().input(), m_showDebugOverlay);
    }
#endif
}
