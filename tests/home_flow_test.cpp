#include "projects/menu/src/widgets/HomeUiTween.hpp"
#include "projects/menu/src/widgets/HomeCarouselMotion.hpp"
#include "lib/nxui/include/nxui/core/TouchContactGuard.hpp"
#include "projects/menu/src/music/MusicPreferences.hpp"
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
int main(int argc, char** argv) {
    using namespace switchu::homeui;
    UiTween focus;
    focus.target(1.f, .21f);
    focus.update(.016f); assert(focus.value() > 0.f);
    for (int i=0;i<5;++i) focus.update(.016f);
    float v=focus.value(); focus.target(0.f, .17f); assert(focus.value()==v);
    focus.update(.016f); assert(focus.value()<v && focus.value()>0.f);
    for (int i=0;i<30;++i) {v=focus.value();focus.target(i%2 ? 1.f : 0.f,.18f);assert(focus.value()==v);focus.update(.016f);}
    focus.target(0.f,.17f); for(int i=0;i<30;++i)focus.update(.016f); assert(focus.value()==0.f);
    focus.target(1.f,.21f); focus.update(120.f); assert(focus.value()<.2f);
    v=focus.value();focus.update(std::numeric_limits<float>::quiet_NaN());assert(focus.value()==v);
    focus.update(std::numeric_limits<float>::infinity());assert(focus.value()==v);
    assert(uiDelta(-1.f)==0.f);
    nxui::TouchContactGuard guard;
    assert(guard.update(1,42)); assert(guard.update(1,42));
    assert(!guard.update(2,42) && guard.cancelled());
    assert(!guard.update(1,42) && guard.cancelled());
    assert(!guard.update(0,0) && guard.cancelled());
    assert(guard.update(1,43) && !guard.cancelled());
    assert(!guard.update(1,44) && guard.cancelled());
    guard.update(0,0);assert(guard.update(1,45));assert(!guard.update(0,0) && !guard.cancelled());
    HomeCarouselMotionState state;
    jumpCarouselTo(state,50,100);beginCarouselTouch(state,100);
    dragCarouselTouch(state,-296.24f,100,296.24f);assert(std::abs(state.position-51.f)<.001f);
    dragCarouselTouch(state,148.12f,100,296.24f);assert(std::abs(state.position-50.5f)<.001f);
    endCarouselTouch(state,-100000.f,100,296.24f);assert(state.velocity<=3.6f);
    for(int i=0;i<600;++i)updateCarouselMotion(state,1.f/60,100);
    assert(!state.snapActive && !state.inertiaActive && state.position<=52.f);
    beginCarouselTouch(state,100);dragCarouselTouch(state,40.f,100);
    retargetCarouselSnap(state,51,100);assert(!state.touchScrolling && !state.inertiaActive);
    for(int i=0;i<200;++i)updateCarouselMotion(state,1.f/60,100);
    assert(state.position==51.f);
    assert(argc==2);
    const auto path=std::string(argv[1])+"/preferences-v1.txt";
    using switchu::menu::music::MusicPreferences;
    std::filesystem::remove(path);
    MusicPreferences prefs(path);assert(prefs.load());
    const std::string key="Björk\\Homogenic\nédition \"1997\"";
    prefs.remember(key);assert(prefs.toggle(key));assert(prefs.favourite(key));
    MusicPreferences reloaded(path);assert(reloaded.load());assert(reloaded.favourite(key));assert(reloaded.lastAlbum()==key);
    assert(reloaded.toggle(key));MusicPreferences removed(path);assert(removed.load());assert(!removed.favourite(key));
    {std::ofstream bad(path);bad<<"bad preferences";}
    assert(!removed.load());assert(removed.lastAlbum()==key);
    MusicPreferences failed(path+"/cannot-create-child");assert(!failed.toggle(key));assert(!failed.favourite(key));
    std::cout<<"Interruptible focus/pill, finite dt (including fast math), multitouch cancellation, bounded swipe/inertia, controller takeover, UTF-8 persistent favourites/resume and write failure rollback: OK\n";
}
