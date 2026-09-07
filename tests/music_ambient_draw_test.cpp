// Actual production mesh through a recording renderer, never a console timing.
#include "MusicAmbientBackground.hpp"
#include <nxui/core/Renderer.hpp>
#include <cassert>
#include <iostream>
#include <cstdlib>
#include <new>
#include <cmath>
static size_t allocations=0;
void* operator new(std::size_t size) {++allocations;if(auto* p=std::malloc(size))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
using namespace switchu::menu::music;
int main() {
    nxui::Renderer ren;
    MusicAmbientBackground background;
    const ambient::Palette red{{{{.82f,.15f,.08f},{.70f,.23f,.11f},{.77f,.11f,.30f}}},true};
    background.setPalette(red);
    std::vector<std::vector<double>> first,atThree;
    for (int frame=0; frame<=1380; ++frame) {
        background.update(1.f/60.f);
        ren.commands.clear();
        background.draw(ren,1.f,494.f);
        assert(ren.commands.size()==2994); // 8976 vertices, one colour-only batch
        assert(ren.commands.front()==std::vector<double>({0,0,0,1280,482}));
        assert(ren.commands.back()==std::vector<double>({1}));
        if (frame==0) first=ren.commands;
        if (frame==180) atThree=ren.commands;
        for(const auto& c:ren.commands)if(c[0]==9) {
            for(auto value:c)assert(std::isfinite(value));
            for(int v=0;v<3;++v) {
                const float y=float(c[2+v*6]),a=float(c[6+v*6]);
                assert(a>=0.f && a<=1.f);
                if(y>=482.f) assert(a==0.f);
            }
        }
    }
    assert(first!=atThree && atThree!=ren.commands && nxui::Texture::uploads==0);
    // Exclude allocations owned by the test recorder, then exercise the whole
    // production component including its FIRST frame and repeated palette swaps.
    ren.record=false;
    MusicAmbientBackground fresh;
    const size_t before=allocations;
    for(int frame=0;frame<1800;++frame) {
        if(frame%19==0)fresh.setPalette(frame%2 ? red : ambient::Palette{});
        fresh.update(1.f/60.f);fresh.draw(ren,1.f,494.f);
    }
    assert(before==allocations && nxui::Texture::uploads==0);
    std::cout << "Production ribbons: 23 s continuous motion, 850 ms palette path, bounded geometry, floor scissor; 30 s animation with ZERO allocations/uploads: OK\n";
}
