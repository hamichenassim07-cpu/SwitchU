#!/usr/bin/env python3
"""Exercise the actual HOME composition block, with simulated decoded media.
No GPU upload or MP4 decoder is simulated as a successful console test.
"""
from pathlib import Path
import tempfile,subprocess,os
from music_source_contract import function
R=Path(__file__).resolve().parents[1]
s=function((R/'projects/menu/src/widgets/WaraWaraBackgroundPreviewV80.cpp').read_text(),'WaraWaraBackground::onRender')
s=s[s.index('    const nxui::Rect area = {'):s.rfind('}')]
code=r'''
#define SWITCHU_V81_FFMPEG 1
#include "MusicAmbientBackground.hpp"
#include <nxui/core/Renderer.hpp>
#include <algorithm>
#include <cassert>
#include <iostream>
using namespace switchu::menu::music;
bool videoPlaybackEnabled(){return true;}
float smoothStep01(float x){return x*x*(3-2*x);}
nxui::Rect coverRect(int,int,nxui::Rect r){return r;}
nxui::Rect videoCoverRect(int,int,nxui::Rect r){return r;}
struct Stream {
 bool available=false;int id=0;
 bool valid()const{return available;}
 int width()const{return 1280;}int height()const{return 720;}
 void draw(nxui::Renderer& ren,nxui::Rect,float a)const{ren.commands.push_back({10,double(id),a});}
};
struct Runtime {
 bool currentAvailable=false,nextAvailable=false,currentHasVideo=false,nextHasVideo=false;
 bool videoFallbackRequired=false,transitioning=false,videoHasCurrent=false;
 int currentIndex=0,nextIndex=1,videoCurrentIndex=0;
 float fade=0,videoOpacity=0;
 Stream textures[2]{{false,1},{false,2}},videoTextures[2]{{false,3},{false,4}};
};
struct Home {
 nxui::Rect m_rect{0,0,1280,720};float m_opacity=1;
 MusicAmbientBackground m_ambientBackground;Runtime r;
 void draw(nxui::Renderer& ren){
'''+s+r'''
 }
};
int triangles(const nxui::Renderer& r){return std::count_if(r.commands.begin(),r.commands.end(),[](const auto&c){return c[0]==9;});}
int media(const nxui::Renderer& r){return std::count_if(r.commands.begin(),r.commands.end(),[](const auto&c){return c[0]==10;});}
int main(){
 nxui::Renderer ren;Home h;
 h.draw(ren);assert(triangles(ren)==2992 && media(ren)==0);
 assert(ren.commands.front()[0]==3 && ren.commands.front()[8]==1); // opaque own base
 h.r.currentAvailable=true;h.r.textures[0].available=true;
 ren.commands.clear();h.draw(ren);assert(triangles(ren)==0 && media(ren)==1);
 h.r.transitioning=true;h.r.fade=.5f;h.r.nextAvailable=true;h.r.textures[1].available=true;
 ren.commands.clear();h.draw(ren);assert(triangles(ren)==0 && media(ren)==2);
 for(const auto&c:ren.commands)if(c[0]==10){if(c[1]==1)assert(c[2]==1);if(c[1]==2)assert(c[2]==.5);}
 h.r.nextAvailable=false;
 ren.commands.clear();h.draw(ren);assert(triangles(ren)==2992 && media(ren)==1);
 h.r.currentAvailable=false;h.r.transitioning=false;h.r.currentHasVideo=true;
 // A title that owns only a video retains its quiet pre-roll; ribbons are not overlaid.
 ren.commands.clear();h.draw(ren);assert(triangles(ren)==0 && media(ren)==0);
 h.r.videoHasCurrent=true;h.r.videoTextures[0].available=true;h.r.videoOpacity=1;
 ren.commands.clear();h.draw(ren);assert(triangles(ren)==0 && media(ren)==1);
 h.r.transitioning=true;h.r.nextHasVideo=false;h.r.fade=.5;h.r.videoOpacity=.5;
 ren.commands.clear();h.draw(ren);assert(triangles(ren)==2992 && media(ren)==1);
 h.r.transitioning=false;h.r.videoHasCurrent=false;h.r.videoFallbackRequired=true;
 ren.commands.clear();h.draw(ren);assert(triangles(ren)==2992); // unsupported/corrupt media fallback
 std::cout<<"Actual HOME composition: opaque fallback, custom still, still cross-fade without dip, video pre-roll/playback, video exit and failed-media fallback: OK (simulated decoded media, no MP4/device test)\n";
}
'''
with tempfile.TemporaryDirectory(prefix='switchu-home-background-') as d:
 p=Path(d)/'test.cpp';p.write_text(code);exe=Path(d)/'test'
 subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-O2','-I'+str(R/'tests/renderer_stub'),'-idirafter',str(R/'lib/nxui/include'),'-I'+str(R/'projects/menu/src/music'),str(p),str(R/'projects/menu/src/music/MusicAmbientBackground.cpp'),str(R/'projects/menu/src/music/MusicAmbientPalette.cpp'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
