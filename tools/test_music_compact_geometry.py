#!/usr/bin/env python3
"""Compare real current geometry/projection with the delivered V8.8 fixture."""
from pathlib import Path
import subprocess,tempfile,os
R=Path(__file__).resolve().parents[1]
s=(R/'projects/menu/src/music/MusicScreen.cpp').read_text()
s=s[s.index('constexpr float kScreenW'):s.index('// HOME main-screen typography')]
code='''#include "tests/fixtures/music_geometry_v8_8.hpp"
#include "MusicPhysicalMediaRenderer.hpp"
#include "MusicAlbumLayout.hpp"
#include "widgets/HomeCarouselStyle.hpp"
#include <nxui/core/Renderer.hpp>
#include <cassert>
#include <iostream>
namespace current {\n'''+s+'''\n}
using namespace switchu::menu::music;
nxui::Renderer ren;
PhysicalMediaGeometry geometry(bool old,float delta,float signal,float bounce) {
 const float size=(old ? v88::musicCarouselSizeForDistance(delta) : current::musicCarouselSizeForDistance(delta))*bounce;
 const float x=640.f+(old ? v88::musicCarouselCenterOffset(delta) : current::musicCarouselCenterOffset(delta));
 float focus=std::clamp(1.f-std::abs(delta)/.72f,0.f,1.f);focus=focus*focus*(3-2*focus);
 PhysicalMediaPose p;
 p.rect={x-size*.5f,515-size-current::musicCarouselSideLift(delta)+1.8f*focus+.252f,size,size};
 p.yawDeg=current::musicCarouselYaw(delta)+std::clamp(-signal*.86f,-4.6f,4.6f)+.34f*focus;
 p.pitchDeg=-.55f;p.rollDeg=std::clamp(-signal*.12f,-.85f,.85f);
 p.zLiftPx=12*focus;p.depthPx=std::clamp(size*.021f,4.5f,8.f);
 return drawAlbumPhysicalMedia(ren,nullptr,nullptr,p);
}
float gap(bool old,float fraction,float signal) {
 auto a=geometry(old,-fraction,signal,1.f),b=geometry(old,1-fraction,signal,1.f);
 float right=-1e9,left=1e9;
 for(auto p:a.front)right=std::max(right,p.x);
 for(auto p:b.front)left=std::min(left,p.x);
 return left-right;
}
int main() {
 ren.record=false;
 for(int n=-4000;n<=4000;++n) {
  float d=n*.001f;
  assert(std::abs(current::musicCarouselSizeForDistance(d)-v88::musicCarouselSizeForDistance(d)*.92f)<.0001f);
  assert(std::abs(current::musicCarouselCenterOffset(d)-v88::musicCarouselCenterOffset(d)*.92f)<.0002f);
  assert(current::musicCarouselYaw(d)==v88::musicCarouselYaw(d));
  assert(current::musicCarouselSideLift(d)==v88::musicCarouselSideLift(d));
 }
 float maxReflection=0,maxExtraOverlap=0;
 for(int frame=0;frame<=1000;++frame)for(float signal:{-7.f,0.f,7.f}) {
  float fraction=frame*.001f;
  const float oldGap=gap(true,fraction,signal),newGap=gap(false,fraction,signal);
  if(oldGap>=0.f)assert(newGap>=-0.1f);
  maxExtraOverlap=std::max(maxExtraOverlap,std::max(0.f,-newGap)-std::max(0.f,-oldGap)*.92f);
  for(float d:{-fraction,1-fraction}) {
   const float bounce=std::abs(d)<.035f ? switchu::homeui::carouselSelectionBounceScale(fraction*.42f) : 1.f;
   auto g=geometry(false,d,signal,bounce);
   const float y=std::max(496.f,(g.front[2].y+g.front[3].y)*.5f+5.f)+std::clamp(std::abs(g.front[3].y-g.front[0].y)*.235f,34.f,82.f);
   maxReflection=std::max(maxReflection,y);
  }
 }
 // Sweep the true entry bounce independently of carousel position.
 for(int n=0;n<=420;++n)for(float d:{-.034f,0.f,.034f})for(float signal:{-7.f,0.f,7.f}) {
  auto g=geometry(false,d,signal,switchu::homeui::carouselSelectionBounceScale(n*.001f));
  float y=std::max(496.f,(g.front[2].y+g.front[3].y)*.5f+5.f)+std::clamp(std::abs(g.front[3].y-g.front[0].y)*.235f,34.f,82.f);
  maxReflection=std::max(maxReflection,y);
 }
 assert(maxReflection<albumui::kTitleY);
 assert(maxExtraOverlap<1.f);
 std::cout<<"Actual V8.8 vs V8.9: 8001 size/spacing/angle samples; no new central intersection; extra overlap "<<maxExtraOverlap<<" px; reflection bound "<<maxReflection<<" < title "<<albumui::kTitleY<<": OK\\n";
}
'''
with tempfile.TemporaryDirectory(prefix='switchu_geometry_') as temp:
 p=Path(temp)/'geometry.cpp';p.write_text(code);out=Path(temp)/'geometry'
 subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-O2','-I'+str(R),'-I'+str(R/'tests/renderer_stub'),'-idirafter',str(R/'lib/nxui/include'),'-I'+str(R/'projects/menu/src/music'),'-I'+str(R/'projects/menu/src'),str(p),str(R/'projects/menu/src/music/MusicPhysicalMediaRenderer.cpp'),'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)
