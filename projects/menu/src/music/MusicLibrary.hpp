#pragma once

#include "MusicTypes.hpp"

#include <atomic>
#include <string>
#include <vector>

namespace switchu::menu::music {

class MusicLibrary {
public:
    static LibrarySnapshot scan(const std::string& root,
                                std::atomic<uint32_t>* filesVisited = nullptr,
                                std::atomic<uint32_t>* tracksFound = nullptr);

};

} // namespace switchu::menu::music
