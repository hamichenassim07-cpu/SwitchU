#include "MusicAlbumDetails.hpp"
#include "MusicAlbumInformation.hpp"
#include <nxui/core/Renderer.hpp>
#include <nxui/core/Font.hpp>
#include <algorithm>

namespace switchu::menu::music {
void MusicAlbumDetails::open(const Album& album, const LibrarySnapshot& library, nxui::Font* font) {
    const auto summary = albuminfo::summarise(album, library);
    m_fields = {{{"Album", albuminfo::singleLine(album.title,"Album sans titre")},
                 {"Artiste", albuminfo::singleLine(album.artist,"Artiste inconnu")},
                 {"Année", album.year > 0 ? std::to_string(album.year) : "Non renseignée"},
                 {"Genre", albuminfo::singleLine(album.genre,"Non renseigné")},
                 {"Durée totale", summary.duration}, {"Pistes", summary.count}}};
    m_first = 0;
    layout(font);
}

void MusicAlbumDetails::layout(nxui::Font* font) {
    m_font = font;
    m_revision = font ? font->revision() : 0;
    m_lines.clear();
    if (!font) return;
    m_valueScale = 26.f / std::max(1,font->ptSize());
    m_labelScale = 22.f / std::max(1,font->ptSize());
    for (const auto& field : m_fields) {
        const auto& text = field.value;
        size_t start = 0;
        while (start < text.size()) {
            // Layout happens on opening or a font change only. Code-point
            // boundaries and word breaks preserve accents and long tag names.
            size_t end = start, lastSpace = start;
            while (end < text.size()) {
                size_t next = end + 1;
                while (next < text.size() && (static_cast<unsigned char>(text[next])&0xc0u)==0x80u) ++next;
                if (font->measure(text.substr(start,next-start)).x*m_valueScale > 650.f && end > start) break;
                if (text[end]==' ') lastSpace=end;
                end=next;
            }
            if (end < text.size() && lastSpace > start) end=lastSpace;
            m_lines.push_back({start==0 ? field.label : std::string{}, text.substr(start,end-start)});
            start=end;
            while (start<text.size() && text[start]==' ') ++start;
        }
    }
    move(0);
}

void MusicAlbumDetails::updatePage() {
    m_page = m_lines.size() > kVisibleLines
        ? std::to_string(m_first+1) + "–" +
          std::to_string(std::min(m_first+kVisibleLines,lineCount())) + " / " +
          std::to_string(lineCount()) : std::string{};
}
void MusicAlbumDetails::move(int lines) {
    m_first = std::clamp(m_first+lines, 0, std::max(0,lineCount()-kVisibleLines));
    updatePage();
}
void MusicAlbumDetails::clear() {
    m_lines.clear(); m_fields={}; m_first=0; m_page.clear(); m_font=nullptr;
}
void MusicAlbumDetails::draw(nxui::Renderer& ren, nxui::Font* font, nxui::Font* icons, float alpha) {
    if (!font || alpha<=0.f) return;
    if (font!=m_font || font->revision()!=m_revision) layout(font);
    const float scale = 1.f/std::max(1,font->ptSize());
    const nxui::Color primary{.95f,.96f,.98f,alpha};
    const nxui::Color secondary{.66f,.70f,.76f,alpha};
    // Opaque calm panel: no blur/capture, no additional GPU textures or effect.
    ren.drawRect({0.f,0.f,1280.f,720.f},{0.f,0.f,0.f,.70f*alpha});
    ren.drawRoundedRect({170.f,104.f,940.f,520.f},{.041f,.046f,.056f,.99f*alpha},24.f);
    ren.drawText("Fiche de l'album",{214.f,133.f},font,primary,30.f*scale);
    ren.drawRect({214.f,184.f,852.f,1.f},{.72f,.76f,.83f,.15f*alpha});
    ren.pushClipRect({214.f,208.f,852.f,352.f});
    for (int row=0; row<kVisibleLines && m_first+row<lineCount(); ++row) {
        const auto& line=m_lines[size_t(m_first+row)];
        const float y=211.f+row*42.f;
        ren.drawText(line.label,{214.f,y+3.f},font,secondary,m_labelScale);
        ren.drawText(line.value,{398.f,y},font,primary,m_valueScale);
    }
    ren.popClipRect();
    if (lineCount()>kVisibleLines) {
        const float height=336.f*kVisibleLines/lineCount();
        const float travel=336.f-height;
        ren.drawRect({1081.f,211.f,3.f,336.f},{.8f,.83f,.88f,.12f*alpha});
        ren.drawRect({1081.f,211.f+travel*m_first/(lineCount()-kVisibleLines),3.f,height},secondary);
        ren.drawText("↑ ↓ Défiler",{214.f,583.f},font,secondary,22.f*scale);
        ren.drawText(m_page,{710.f,583.f},font,secondary,22.f*scale);
    }
    ren.drawText(icons ? "\xee\x83\xa1" : "B",{955.f,581.f},icons ? icons : font,primary,
                 icons ? 1.04f : 24.f*scale);
    ren.drawText("Fermer",{991.f,583.f},font,primary,22.f*scale);
}
}
