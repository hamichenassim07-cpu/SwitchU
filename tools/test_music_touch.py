#!/usr/bin/env python3
"""Feed contact traces to the actual MusicScreen::handleTouch method."""
from pathlib import Path
import subprocess,tempfile,os
from music_source_contract import function
R=Path(__file__).resolve().parents[1];src=(R/'projects/menu/src/music/MusicScreen.cpp').read_text()
geometry=src[src.index('constexpr float kScreenW'):src.index('// HOME main-screen typography')]
code=r'''
#include "projects/menu/src/widgets/HomeCarouselMotion.hpp"
#include "projects/menu/src/music/MusicTypes.hpp"
#include <nxui/core/Types.hpp>
#include <vector>
#include <array>
#include <cassert>
#include <iostream>
namespace nxui {
 enum class Button{DLeft,DRight,DUp,DDown,LStickL,LStickR,A,B,L,Minus};
 struct Input {
  bool down=false,up=false,held=false,cancelled=false,physical=false;
  float x=640,y=330,sx=640,sy=330,t=0;
  bool touchDown()const{return down;}bool touchUp()const{return up;}bool isTouching()const{return held;}
  bool touchCancelled()const{return cancelled;}bool pointerConsumesButton(Button)const{return false;}
  bool isDown(Button)const{return physical;}
  float touchX()const{return x;}float touchY()const{return y;}float touchDeltaX()const{return x-sx;}float touchDeltaY()const{return y-sy;}float touchDuration()const{return t;}
 };
}
using namespace switchu::menu::music;
'''+geometry+r'''
float clamp01(float v){return std::clamp(v,0.f,1.f);}
struct MusicScreen {
 enum class View{Albums,Playlists,NowPlaying,AlbumDetail,PlaylistDetail,Queue};enum class Modal{None};
 View m_view=View::Albums;Modal m_modal=Modal::None;
 bool m_active=true,m_closing=false,m_touchTracking=false,m_touchTimelineScrub=false,m_touchStartedInCarousel=false,m_touchScrollActive=false;
 float m_touchLastX=0,m_touchLastDuration=0,m_touchScrollVelocity=0,m_touchStartX=0,m_touchStartY=0,m_idleTime=0,m_rootInfoReveal=1,m_listVisualSelection=0;
 int m_selection=4,m_nowControl=0,activations=0;size_t m_detailAlbum=0,m_detailPlaylist=0;
 switchu::homeui::HomeCarouselMotionState m_rootCarouselMotion;
 LibrarySnapshot m_library;switchu::music::Status m_status{};
 struct Client{bool seekMs(uint64_t){return false;}std::vector<int>queueTrackIds()const{return {};}}m_client;
 struct Store{std::vector<Playlist>p;const auto&playlists()const{return p;}}m_playlistStore;
 bool rootView()const{return m_view==View::Albums||m_view==View::Playlists;}
 int rootItemCount()const{return 10;}bool contentTransitionBusy()const{return false;}
 void retargetRootCarousel(bool){switchu::homeui::retargetCarouselSnap(m_rootCarouselMotion,m_selection,10);}
 void clampSelectionForView(){}float visibleListStartVisual(size_t,int)const{return 0;}
 void activateSelection(){++activations;}void activateNowPlayingControl(){++activations;}
 void handleTouch(nxui::Input&);
};
'''+function(src,'MusicScreen::handleTouch')+r'''
MusicScreen start(nxui::Input& in){
 MusicScreen s;switchu::homeui::jumpCarouselTo(s.m_rootCarouselMotion,4,10);
 in={};in.down=true;in.held=true;s.handleTouch(in);in.down=false;return s;
}
int main(){
 nxui::Input in;auto s=start(in);
 in.up=true;in.held=false;s.handleTouch(in);assert(!s.activations && s.m_selection==4);
 s=start(in);in.x=635;in.t=.016;s.handleTouch(in);assert(!s.m_touchScrollActive && s.m_rootCarouselMotion.position==4);
 in.x=600;in.t=.032;s.handleTouch(in);assert(s.m_touchScrollActive && s.m_rootCarouselMotion.position>4);
 in.x=344;in.t=.050;s.handleTouch(in);assert(s.m_selection==5);
 in.x=450;in.t=.066;s.handleTouch(in);assert(s.m_rootCarouselMotion.position<5);
 in.up=true;in.held=false;s.handleTouch(in);assert(!s.m_touchScrollActive && !s.activations);
 for(int f=0;f<600;++f)switchu::homeui::updateCarouselMotion(s.m_rootCarouselMotion,1.f/60,10);
 assert(s.m_rootCarouselMotion.position==std::round(s.m_rootCarouselMotion.position));
 s=start(in);in.x=550;in.t=.016;s.handleTouch(in);in.cancelled=true;s.handleTouch(in);
 assert(!s.m_touchTracking && !s.m_rootCarouselMotion.touchScrolling && !s.activations);
 in.cancelled=false;in.up=true;in.held=false;s.handleTouch(in);assert(!s.activations);
 s=start(in);in.y=400;in.t=.016;s.handleTouch(in);in.y=330;in.up=true;in.held=false;s.handleTouch(in);assert(!s.m_touchTracking && !s.activations);
 s=start(in);in.x=570;in.t=.016;s.handleTouch(in);in.y=650;s.handleTouch(in);assert(!s.m_rootCarouselMotion.touchScrolling && !s.m_touchTracking);
 s=start(in);in.x=570;in.t=.016;s.handleTouch(in);in.physical=true;s.handleTouch(in);assert(!s.m_rootCarouselMotion.touchScrolling && !s.activations);
 std::cout<<"Actual Music touch: tap/subthreshold/swipe/reversal/release/cancel/multitouch signal/vertical excursion/outside/controller takeover; zero accidental openings or playback calls: OK\n";
}
'''
with tempfile.TemporaryDirectory(prefix='switchu-music-touch-') as d:
 p=Path(d)/'test.cpp';p.write_text(code);exe=Path(d)/'test'
 subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-O2','-I'+str(R),'-I'+str(R/'projects/common/include'),'-idirafter',str(R/'lib/nxui/include'),str(p),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
