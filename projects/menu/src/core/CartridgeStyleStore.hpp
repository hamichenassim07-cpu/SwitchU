#pragma once

#include <nxui/core/Types.hpp>
#include <cstdint>
#include <string>
#include <unordered_map>

// V10.26 TEST: persistent per-title cartridge appearance.
class CartridgeStyleStore {
public:
    static constexpr int kPresetCount = 8;

    static CartridgeStyleStore& instance();

    int presetFor(std::uint64_t titleId);
    void setPreset(std::uint64_t titleId, int preset);
    void reset(std::uint64_t titleId);

    nxui::Color colorFor(std::uint64_t titleId);
    static nxui::Color colorForPreset(int preset);
    static std::string nameForPreset(int preset);

private:
    CartridgeStyleStore() = default;
    void ensureLoaded();
    void save();
    static int sanitizePreset(int preset);

    bool m_loaded = false;
    std::unordered_map<std::uint64_t, int> m_presets;
};
