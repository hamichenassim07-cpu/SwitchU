#pragma once
#include "MusicTypes.hpp"
#include <array>
#include <string>
#include <vector>
namespace nxui { class Renderer; class Font; }
namespace switchu::menu::music {
// Read-only, removable information sheet. Owns display strings, never Track,
// Album, player or carousel state. Long metadata is wrapped and scrollable.
class MusicAlbumDetails {
public:
    void open(const Album& album, const LibrarySnapshot& library, nxui::Font* font);
    void move(int lines);
    void clear();
    void draw(nxui::Renderer& ren, nxui::Font* font, nxui::Font* icons, float alpha);
    int firstLine() const { return m_first; }
    int lineCount() const { return int(m_lines.size()); }
    static constexpr int kVisibleLines = 8;
private:
    struct Line { std::string label, value; };
    void layout(nxui::Font* font);
    void updatePage();
    std::array<Line,6> m_fields;
    std::vector<Line> m_lines;
    nxui::Font* m_font = nullptr;
    uint64_t m_revision = 0;
    float m_valueScale = 1.f, m_labelScale = 1.f;
    int m_first = 0;
    std::string m_page;
};
}
