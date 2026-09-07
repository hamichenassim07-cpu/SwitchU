#!/usr/bin/env python3
"""Actual HOME touch handler; input and focus services are isolated host fixtures."""
from pathlib import Path
import subprocess,tempfile,os
from music_source_contract import function
R=Path(__file__).resolve().parents[1];src=(R/'projects/menu/src/core/WiiUMenuAppInteraction.cpp').read_text()
code=r'''
#include "projects/menu/src/widgets/HomeCarouselMotion.hpp"
#include <nxui/core/Types.hpp>
#include <memory>
#include <vector>
#include <cassert>
#include <iostream>
namespace nxui {
 enum class Button{DLeft,DRight,DUp,DDown,LStickL,LStickR,A,B,L,R};
 struct Input {
  bool down=false,up=false,held=false,cancelled=false,physical=false;
  float x=640,y=330,sx=640,sy=330,t=0;
  bool touchDown()const{return down;}bool touchUp()const{return up;}bool isTouching()const{return held;}
  bool touchCancelled()const{return cancelled;}bool pointerConsumesButton(Button)const{return false;}
  bool isDown(Button)const{return physical;}
  float touchX()const{return x;}float touchY()const{return y;}float touchDeltaX()const{return x-sx;}float touchDeltaY()const{return y-sy;}float touchDuration()const{return t;}
 };
 struct Widget{virtual ~Widget()=default;};
}
struct GlossyIcon:nxui::Widget{};
struct UserAvatarButton:nxui::Widget{int activations=0;bool isVisible()const{return true;}bool hitTest(float x,float y)const{return x>170&&x<230&&y>17&&y<73;}void activate(){++activations;}};
struct Focus {nxui::Widget*cur=nullptr;nxui::Widget*current(){return cur;}void setFocus(nxui::Widget*p){cur=p;}};
struct Grid {
 switchu::homeui::HomeCarouselMotionState state;GlossyIcon icon;Focus f;int selected=4;bool focused=false;
 Grid(){f.cur=&icon;switchu::homeui::jumpCarouselTo(state,4,10);}
 Focus&focusManager(){return f;}bool focusGlobalIndex(int i){selected=i;return true;}int focusedGlobalIndex(){return selected;}
 int hitTest(float,float y)const{return y>=170&&y<=540?4:-1;}
 bool isTouchScrolling()const{return state.touchScrolling;}bool canTouchScroll()const{return true;}
 void setCarouselFocusActive(bool v){focused=v;}
 void beginTouchScroll(){switchu::homeui::beginCarouselTouch(state,10);}
 void dragTouchScroll(float d){switchu::homeui::dragCarouselTouch(state,d,10);}
 void endTouchScroll(float v){switchu::homeui::endCarouselTouch(state,v,10);}
};
struct App{nxui::Input in;nxui::Input&input(){return in;}};
struct Music{bool isActive()const{return false;}void handleTouch(nxui::Input&) {}};
struct WiiUMenuApp {
 App a;Focus f;App&app(){return a;}Focus&focusManager(){return f;}
 std::shared_ptr<Grid>m_grid=std::make_shared<Grid>();Music*m_musicScreen=nullptr;
 std::vector<std::shared_ptr<UserAvatarButton>>m_userAvatarButtons;
 UserAvatarButton*m_touchAvatarTarget=nullptr;
 int m_touchHitIndex=-1;bool m_editMode=false,m_touchOnFocused=false,m_touchStartedInGrid=false,m_touchScrollActive=false,m_touchAvatarWasFocused=false;
 float m_touchLastX=0,m_touchLastDuration=0,m_touchScrollVelocity=0;
 void updateCursor(){}void handleTouch();
};
'''+function(src,'WiiUMenuApp::handleTouch')+r'''
WiiUMenuApp start(float x=640,float y=330){WiiUMenuApp h;auto&i=h.a.in;i.x=i.sx=x;i.y=i.sy=y;i.down=i.held=true;h.handleTouch();i.down=false;return h;}
int main(){
 auto h=start();auto&i=h.a.in;i.up=true;i.held=false;h.handleTouch();assert(!h.m_grid->isTouchScrolling());
 h=start();h.a.in.x=570;h.a.in.t=.016;h.handleTouch();assert(h.m_grid->isTouchScrolling()&&h.m_grid->focused&&h.f.cur==&h.m_grid->icon);
 h.a.in.x=398;h.a.in.t=.032;h.handleTouch();assert(h.m_grid->state.position==5.f);
 h.a.in.x=519;h.a.in.t=.048;h.handleTouch();assert(h.m_grid->state.position==4.5f);
 h.a.in.up=true;h.a.in.held=false;h.handleTouch();assert(!h.m_touchScrollActive&&!h.m_grid->isTouchScrolling());
 for(int k=0;k<600;++k)switchu::homeui::updateCarouselMotion(h.m_grid->state,1.f/60,10);
 assert(h.m_grid->state.position==std::round(h.m_grid->state.position));
 h=start();h.a.in.x=570;h.a.in.t=.016;h.handleTouch();h.a.in.cancelled=true;h.handleTouch();assert(!h.m_touchStartedInGrid&&!h.m_grid->isTouchScrolling()&&h.m_touchHitIndex==-1);
 h=start();h.a.in.x=570;h.a.in.t=.016;h.handleTouch();h.a.in.y=650;h.handleTouch();assert(!h.m_grid->isTouchScrolling());
 h=start();h.a.in.x=570;h.a.in.t=.016;h.handleTouch();h.a.in.physical=true;h.handleTouch();assert(!h.m_grid->isTouchScrolling());
 h=start();h.a.in.y=400;h.handleTouch();h.a.in.y=330;h.a.in.up=true;h.a.in.held=false;h.handleTouch();assert(h.m_touchHitIndex==-1);
 std::cout<<"Actual HOME touch: tap, swipe, return to carousel focus, finger reversal, release, cancellation, outside zone and physical control takeover: OK; no launch action exists in this handler\n";
}
'''
with tempfile.TemporaryDirectory(prefix='switchu-home-touch-') as d:
 p=Path(d)/'test.cpp';p.write_text(code);exe=Path(d)/'test'
 subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-O2','-I'+str(R),'-idirafter',str(R/'lib/nxui/include'),str(p),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
