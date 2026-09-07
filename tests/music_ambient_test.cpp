#include "projects/menu/src/music/MusicAmbientPalette.hpp"
#include "projects/menu/src/music/MusicAlbumInformation.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>
using namespace switchu::menu::music;

int main() {
    std::vector<uint8_t> pixels(24 * 24 * 4, 255);
    auto sample = [&] { return ambient::samplePalette(pixels.data(), pixels.size(), 24, 24); };
    const auto white = sample();
    assert(white.sampled);
    for (auto c : white.colours) assert(c.r == c.g && c.g == c.b && c.r <= 0.83f);
    for (size_t i=0; i<pixels.size(); i+=4) pixels[i]=pixels[i+1]=pixels[i+2]=0;
    const auto black=sample();
    assert(black.sampled && black.colours[0].r > 0.25f);
    assert(black.colours[0].r == black.colours[0].b);
    for (size_t i=0; i<pixels.size(); i+=4) {
        pixels[i]=i < pixels.size()*2/3 ? 230 : 20;
        pixels[i+1]=24; pixels[i+2]=i < pixels.size()*2/3 ? 20 : 230;
    }
    const auto redBlue=sample();
    assert(redBlue.colours[0].r > redBlue.colours[0].b*4);
    assert(redBlue.colours[1].b > redBlue.colours[1].r*4);
    for (size_t i=3;i<pixels.size();i+=4) pixels[i]=0;
    assert(!sample().sampled);
    assert(!ambient::samplePalette(nullptr, 0, 24,24).sampled);
    assert(!ambient::samplePalette(pixels.data(),pixels.size()-1,24,24).sampled);
    assert(!ambient::samplePalette(pixels.data(),pixels.size(),-1,24).sampled);

    ambient::Motion motion;
    std::array<ambient::RibbonColumn,ambient::kRibbonSegments+1> start{},later{};
    motion.ribbon(1,start);
    const auto initial=motion.palette();
    motion.setTarget(redBlue);
    assert(motion.phase()==0.0 && motion.palette().colours[0].r==initial.colours[0].r);
    for(int i=0;i<52;++i) { motion.setTarget(redBlue); motion.update(1.f/60.f); }
    assert(std::abs(motion.palette().colours[0].r-redBlue.colours[0].r)<0.000001f);
    const auto displayed=motion.palette();
    const double phase=motion.phase();
    motion.setTarget(black);
    assert(motion.phase()==phase && motion.palette().colours[0].r==displayed.colours[0].r);
    motion.update(1.f/60.f);
    assert(std::abs(motion.palette().colours[0].r-displayed.colours[0].r)<0.003f);
    // Continue for twenty seconds without changing album or any input.
    for(int i=0;i<1200;++i) motion.update(1.f/60.f);
    motion.ribbon(1,later);
    float travel=0.f;
    for(size_t i=0;i<start.size();++i)travel+=std::abs(start[i].centre-later[i].centre);
    assert(travel/start.size()>8.f);
    for(int i=0;i<1200;++i) {
        const auto shown=motion.palette();
        motion.setTarget(i%2 ? black : white);
        assert(motion.palette().colours[0].r==shown.colours[0].r);
        motion.update(1.f/60.f);
        for(size_t layer=0;layer<ambient::kRibbonLayers;++layer) {
            motion.ribbon(layer,later);
            for(size_t j=0;j<later.size();++j) {
                const auto& c=later[j];
                assert(std::isfinite(c.centre) && c.halfWidth>=14.f && c.halfWidth<=78.f);
                if(j) assert(later[j-1].x<c.x);
            }
        }
    }
    const double prior=motion.phase();motion.update(120.f);
    assert(motion.phase()-prior<=0.05001);
    const double after=motion.phase();motion.update(-1.f);motion.update(0.f);
    motion.update(std::numeric_limits<float>::quiet_NaN());
    motion.update(std::numeric_limits<float>::infinity());
    assert(motion.phase()==after);

    assert(albuminfo::duration(48000)=="< 1 min");
    assert(albuminfo::duration(48*60000)=="48 min");
    assert(albuminfo::duration(63*60000)=="1 h 03");
    assert(albuminfo::duration(120*60000)=="2 h 00");
    LibrarySnapshot library; library.tracks.resize(3);
    library.tracks[0].durationMs=20*60000;
    library.tracks[1].durationMs=28*60000;
    Album album; album.tracks={0,1,999};
    auto info=albuminfo::summarise(album,library);
    assert(info.duration=="48 min" && info.count=="2 pistes");
    album.tracks={0}; assert(albuminfo::summarise(album,library).count=="1 piste");
    album.tracks={0,1,2}; assert(albuminfo::summarise(album,library).duration=="≥ 48 min");
    album.tracks={2}; assert(albuminfo::summarise(album,library).duration=="Durée inconnue");
    album.tracks.clear(); assert(albuminfo::summarise(album,library).count=="0 pistes");
    assert(albuminfo::singleLine(" \n\t", "Inconnu")=="Inconnu");
    assert(albuminfo::singleLine(" Album\nartiste ", "")=="Album artiste");
    assert(albuminfo::singleLine("Été — 日本語", "")=="Été — 日本語");
    assert(albuminfo::singleLine("A\xe2\x80\xa8" "B", "")=="A B");
    assert(albuminfo::singleLine("A\xff" "B", "")=="A�B");
    assert(albuminfo::singleLine(std::string(100000,'x'), "").size()<=4096);
    std::cout << "Ambient palette/motion + real album metadata: OK\n";
}
