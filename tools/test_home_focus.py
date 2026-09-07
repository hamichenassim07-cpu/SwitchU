#!/usr/bin/env python3
"""Actual IconGrid focus/layout methods, isolated from Switch services."""
from pathlib import Path
import subprocess,tempfile,os
from music_source_contract import function
R=Path(__file__).resolve().parents[1];src=(R/'projects/menu/src/widgets/IconGrid.cpp').read_text()
code=r'''
#include "projects/menu/src/widgets/HomeUiTween.hpp"
#include "projects/menu/src/widgets/HomeCarouselMotion.hpp"
#include <nxui/core/Types.hpp>
#include <vector>
#include <memory>
#include <cassert>
#include <cmath>
#include <iostream>
struct Icon {nxui::Rect r; void setRect(nxui::Rect rect){r=rect;} void setVisible(bool){} void forceVisible(){} };
struct IconGrid {
 bool m_carouselFocusActive=true,m_touchOwnsSelection=false;
 switchu::homeui::UiTween m_focusAmount{1.f};
 switchu::homeui::HomeCarouselMotionState m_carouselMotion;
 nxui::Rect m_rect{0,120,1280,510};
 float m_originX=0,m_originY=0;
 int m_displayCount=5,m_windowStart=0,m_pendingSettledFocusIndex=-1;
 std::vector<int>m_displayIndices{0,1,2,3,4};std::vector<std::shared_ptr<Icon>>m_allIcons;
 int visibleSlotCount()const{return 5;}
 float maxScrollPosition()const{return 4;}
 void setCarouselFocusActive(bool);void endTouchScroll(float);void layoutAtScrollPosition();
};
'''
for name in ['setCarouselFocusActive','endTouchScroll','layoutAtScrollPosition']:code+=function(src,'IconGrid::'+name)+'\n'
code+=r'''
int main(){
 IconGrid g;for(int i=0;i<5;++i)g.m_allIcons.push_back(std::make_shared<Icon>());
 switchu::homeui::jumpCarouselTo(g.m_carouselMotion,2,5);
 g.layoutAtScrollPosition();std::vector<float>centres;
 for(const auto&i:g.m_allIcons)centres.push_back(i->r.x+i->r.width*.5f);
 for(int origin=0;origin<3;++origin){ // Profile, Controllers, Settings all use this entry point.
  g.setCarouselFocusActive(false);
  for(int i=0;i<20;++i){g.m_focusAmount.update(.016f);g.layoutAtScrollPosition();}
  assert(g.m_allIcons[2]->r.width==230.f && !g.m_carouselMotion.snapActive);
  g.setCarouselFocusActive(true);g.m_focusAmount.update(.016f);g.layoutAtScrollPosition();
  assert(g.m_allIcons[2]->r.width>230.f);assert(g.m_carouselMotion.position==2.f);
  for(int i=0;i<20;++i){g.m_focusAmount.update(.016f);g.layoutAtScrollPosition();}
  assert(g.m_allIcons[2]->r.width==310.f);
 }
 for(int repeat=0;repeat<100;++repeat){
  float width=g.m_allIcons[2]->r.width;g.setCarouselFocusActive(repeat%2);
  assert(g.m_allIcons[2]->r.width==width);
  g.m_focusAmount.update(.016f);g.layoutAtScrollPosition();
  for(int i=0;i<5;++i){
   const float rest=640.f+(i-2)*242.f;
   const float expected=rest+g.m_focusAmount.value()*(centres[i]-rest);
   assert(std::abs(expected-(g.m_allIcons[i]->r.x+g.m_allIcons[i]->r.width*.5f))<.001f);
  }
 }
 g.setCarouselFocusActive(false);for(int i=0;i<20;++i)g.m_focusAmount.update(.016f);g.layoutAtScrollPosition();
 assert(g.m_allIcons[2]->r.width==230.f && g.m_carouselMotion.position==2.f);
 std::cout<<"Actual HOME focus entry/exit: immediate centre resize without index change or snap, 100 reversals from current value, both original horizontal layouts preserved: OK\n";
}
'''
with tempfile.TemporaryDirectory(prefix='switchu-home-focus-') as d:
 p=Path(d)/'test.cpp';p.write_text(code);exe=Path(d)/'test'
 subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-O2','-I'+str(R),'-idirafter',str(R/'lib/nxui/include'),str(p),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
