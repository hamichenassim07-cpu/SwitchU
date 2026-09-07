#include "MusicAlbumDetails.hpp"
#include <nxui/core/Renderer.hpp>
#include <cassert>
#include <iostream>
using namespace switchu::menu::music;
int main() {
 nxui::Font font;nxui::Renderer ren;LibrarySnapshot lib;lib.tracks.resize(14);
 for(auto& t:lib.tracks)t.durationMs=180000;
 Album a;a.title="IGOR";a.artist="Tyler, The Creator";a.year=2019;a.genre="Hip-hop";
 for(size_t i=0;i<14;++i)a.tracks.push_back(i);
 MusicAlbumDetails sheet;sheet.open(a,lib,&font);assert(sheet.lineCount()==6);
 sheet.move(20);assert(sheet.firstLine()==0);sheet.draw(ren,&font,nullptr,1.f);
 a.title="Music to Be Murdered By – Side B (Deluxe Edition)";
 a.artist=std::string(2000,'a')+" — Édition longue, accents : été, Noël";
 sheet.open(a,lib,&font);assert(sheet.lineCount()>8);
 sheet.move(10000);assert(sheet.firstLine()==sheet.lineCount()-8);
 sheet.move(-10000);assert(sheet.firstLine()==0);
 sheet.draw(ren,&font,nullptr,1.f);
 sheet.clear();assert(sheet.lineCount()==0 && sheet.firstLine()==0);
 a.title="";a.artist="";a.genre="";a.year=0;a.tracks.clear();
 sheet.open(a,lib,&font);assert(sheet.lineCount()==6);
 std::cout<<"Album sheet: real metadata, long UTF-8 wrapping, bounded scrolling, missing fields and clear: OK\n";
}
