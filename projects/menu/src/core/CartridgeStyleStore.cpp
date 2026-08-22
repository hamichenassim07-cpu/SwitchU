#include "CartridgeStyleStore.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace {
constexpr const char* kStylePath =
    "sdmc:/config/SwitchU/cartridge_styles.json";
constexpr const char* kStyleTempPath =
    "sdmc:/config/SwitchU/cartridge_styles.json.tmp";

std::string titleIdToHex(std::uint64_t value) {
    char buffer[17] = {};
    std::snprintf(buffer, sizeof(buffer), "%016llX",
                  static_cast<unsigned long long>(value));
    return buffer;
}

bool hexToTitleId(const std::string& text, std::uint64_t& out) {
    if (text.empty())
        return false;
    char* end = nullptr;
    const unsigned long long value = std::strtoull(text.c_str(), &end, 16);
    if (end == text.c_str() || *end != '\0')
        return false;
    out = static_cast<std::uint64_t>(value);
    return out != 0;
}
} // namespace

CartridgeStyleStore& CartridgeStyleStore::instance() {
    static CartridgeStyleStore store;
    store.ensureLoaded();
    return store;
}

int CartridgeStyleStore::sanitizePreset(int preset) {
    return std::clamp(preset, 0, kPresetCount - 1);
}

void CartridgeStyleStore::ensureLoaded() {
    if (m_loaded)
        return;
    m_loaded = true;

    std::ifstream file(kStylePath);
    if (!file.is_open())
        return;

    try {
        nlohmann::json root;
        file >> root;
        const auto it = root.find("titles");
        if (it == root.end() || !it->is_object())
            return;

        for (auto jt = it->begin(); jt != it->end(); ++jt) {
            std::uint64_t titleId = 0;
            if (!hexToTitleId(jt.key(), titleId) ||
                !jt.value().is_number_integer())
                continue;
            m_presets[titleId] = sanitizePreset(jt.value().get<int>());
        }
    } catch (...) {
        // A damaged optional style file must never prevent HOME from booting.
        m_presets.clear();
    }
}

int CartridgeStyleStore::presetFor(std::uint64_t titleId) {
    ensureLoaded();
    if (titleId == 0)
        return 0;
    const auto it = m_presets.find(titleId);
    return it == m_presets.end() ? 0 : sanitizePreset(it->second);
}

void CartridgeStyleStore::setPreset(std::uint64_t titleId, int preset) {
    if (titleId == 0)
        return;
    ensureLoaded();
    m_presets[titleId] = sanitizePreset(preset);
    save();
}

void CartridgeStyleStore::reset(std::uint64_t titleId) {
    if (titleId == 0)
        return;
    ensureLoaded();
    m_presets.erase(titleId);
    save();
}

nxui::Color CartridgeStyleStore::colorFor(std::uint64_t titleId) {
    return colorForPreset(presetFor(titleId));
}

nxui::Color CartridgeStyleStore::colorForPreset(int preset) {
    switch (sanitizePreset(preset)) {
        default:
        case 0: return nxui::Color(0.10f, 0.11f, 0.13f, 1.f); // Anthracite
        case 1: return nxui::Color(0.08f, 0.22f, 0.42f, 1.f); // Bleu
        case 2: return nxui::Color(0.42f, 0.075f, 0.085f, 1.f); // Rouge
        case 3: return nxui::Color(0.29f, 0.095f, 0.43f, 1.f); // Violet
        case 4: return nxui::Color(0.56f, 0.58f, 0.63f, 1.f); // Blanc perle
        case 5: return nxui::Color(0.075f, 0.31f, 0.19f, 1.f); // Vert
        case 6: return nxui::Color(0.46f, 0.19f, 0.045f, 1.f); // Orange
        case 7: return nxui::Color(0.43f, 0.095f, 0.29f, 1.f); // Rose
    }
}

std::string CartridgeStyleStore::nameForPreset(int preset) {
    switch (sanitizePreset(preset)) {
        default:
        case 0: return "Anthracite";
        case 1: return "Bleu";
        case 2: return "Rouge";
        case 3: return "Violet";
        case 4: return "Blanc perle";
        case 5: return "Vert";
        case 6: return "Orange";
        case 7: return "Rose";
    }
}

void CartridgeStyleStore::save() {
    std::error_code ec;
    std::filesystem::create_directory("sdmc:/config", ec);
    ec.clear();
    std::filesystem::create_directory("sdmc:/config/SwitchU", ec);

    nlohmann::json root;
    root["version"] = 1;
    root["titles"] = nlohmann::json::object();
    for (const auto& [titleId, preset] : m_presets)
        root["titles"][titleIdToHex(titleId)] = sanitizePreset(preset);

    {
        std::ofstream file(kStyleTempPath, std::ios::trunc);
        if (!file.is_open())
            return;
        file << root.dump(2);
        file.flush();
        if (!file.good())
            return;
    }

    ec.clear();
    std::filesystem::rename(kStyleTempPath, kStylePath, ec);
    if (ec) {
        ec.clear();
        std::filesystem::remove(kStylePath, ec);
        ec.clear();
        std::filesystem::rename(kStyleTempPath, kStylePath, ec);
    }
}
