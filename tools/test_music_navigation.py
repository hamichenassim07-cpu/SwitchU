#!/usr/bin/env python3
"""Execute the actual navigation methods and update block with a tiny host fixture.

No navigation algorithm is copied into the test. Method bodies are extracted
from MusicScreen.cpp on every run; only unrelated platform/audio services are
stubbed. This is not a Nintendo Switch build or input-device test.
"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from music_source_contract import block, function
ROOT=Path(__file__).resolve().parents[1]
source=Path(sys.argv[1]).read_text() if len(sys.argv)>1 else (ROOT/'projects/menu/src/music/MusicScreen.cpp').read_text()
methods=['rootView','contentTransitionBusy','clampSelectionForView','trackForId','currentTrack','openNowPlaying','goBack','contextualY','modalCancel']
code=r'''
#include "projects/menu/src/music/MusicTypes.hpp"
#include "projects/menu/src/music/MusicUiTiming.hpp"
#include <algorithm>
#include <cassert>
#include <iostream>
namespace DebugLog { template<class... T> void log(const char*,T...) {} }
using namespace switchu::menu::music;
struct MusicScreen {
 enum class View {Albums,Playlists,AlbumDetail,PlaylistDetail,NowPlaying,Queue};
 enum class Modal {None,PlaylistNameKeyboard,PlaylistChooser};
 bool m_active=true,m_closing=false,m_detailClosing=false,m_nowPlayingClosing=false,m_scanRunning=false;
 float m_rootCategoryTransition=1,m_detailTransition=1,m_nowPlayingEnter=1,m_idleTime=0,m_listVisualSelection=0;
 View m_view=View::Albums,m_nowPlayingReturnView=View::Albums,m_queueReturnView=View::NowPlaying;
 Modal m_modal=Modal::None;
 void* m_font=nullptr;
 struct Preferences {bool liked=false; bool toggle(const std::string&){liked=!liked;return true;}} m_preferences;
 std::string m_uiNotice;float m_uiNoticeTimer=0.f;
 uint64_t m_pendingPlaylistTrackId=0;
 int m_selection=0,m_nowPlayingReturnSelection=0,m_queueReturnSelection=0,m_nowControl=2;
 size_t m_detailAlbum=0,m_detailPlaylist=0;
 LibrarySnapshot m_library; switchu::music::Status m_status;
 struct Client {std::vector<uint64_t> ids{1,2,3}; const auto& queueTrackIds() const {return ids;}} m_client;
 struct Store {std::vector<Playlist> items; const auto& playlists() const {return items;}} m_playlistStore;
 void setFocusable(bool) {}
 size_t selectedTrackIndex() const {return size_t(-1);}
 void openPlaylistChooser(uint64_t) {assert(false);}
 void appendToQueue(const std::vector<uint64_t>&) {assert(false);}
 std::vector<uint64_t> playlistTrackIds(size_t) const {return {};}
 bool rootView() const; bool contentTransitionBusy() const; void clampSelectionForView();
 const Track* trackForId(uint64_t) const; const Track* currentTrack() const;
 void openNowPlaying(); void goBack(); void contextualY(); void modalCancel(); void tick(float dt);
};
'''
code+='\n'.join(function(source,'MusicScreen::'+name) for name in methods)
update=function(source,'MusicScreen::onUpdate')
start=update.index('    if (m_view == View::NowPlaying) {')
code+='\nvoid MusicScreen::tick(float dt) {\n'+block(update,start)+'\n}\n'
code+=r'''
using V=MusicScreen::View;
MusicScreen fixture(V origin) {
 MusicScreen s; s.m_view=origin; s.m_selection=4;
 s.m_library.tracks.resize(10); s.m_library.tracks[0].id=1;
 s.m_library.trackById[1]=0; s.m_status.track_id=1;
 s.m_library.albums.resize(10); for(auto& a:s.m_library.albums) a.tracks={0,1,2,3,4,5,6};
 s.m_playlistStore.items.resize(10); for(auto& p:s.m_playlistStore.items) p.trackIds={1,2,3,4,5,6};
 return s;
}
void finish(MusicScreen& s) {for(int i=0;i<80;++i) s.tick(1.f/60.f);}
int main() {
 for(V origin:{V::Albums,V::AlbumDetail,V::Playlists,V::PlaylistDetail}) {
  auto s=fixture(origin); s.openNowPlaying();
  assert(s.m_view==V::NowPlaying && s.m_nowPlayingReturnView==origin);
  assert(s.m_nowPlayingReturnSelection==4 && s.m_nowPlayingEnter==0.f);
  for(int frame=0;frame<100;++frame) {
   const float t=s.m_nowPlayingEnter; s.openNowPlaying();
   assert(s.m_nowPlayingEnter==t && s.m_nowPlayingReturnView==origin);
   s.tick(1.f/60.f);
  }
  s.m_nowControl=4; s.openNowPlaying(); assert(s.m_nowControl==4);
  s.goBack(); assert(s.m_nowPlayingClosing);
  for(int frame=0;frame<80;++frame) {
   // Repeated Minus while B is closing must not restart/cancel that exit.
   if(s.m_view==V::NowPlaying) s.openNowPlaying();
   s.tick(1.f/60.f);
  }
  assert(s.m_view==origin && s.m_selection==4);

  // B during the very first opening frames must still return correctly.
  s.openNowPlaying(); s.tick(1.f/60.f); s.goBack(); s.openNowPlaying(); finish(s);
  assert(s.m_view==origin && s.m_selection==4);

  // Queue is a child of the same player. Minus must not make a return cycle.
  for(int cycle=0;cycle<30;++cycle) {
   s.openNowPlaying(); finish(s); s.contextualY(); assert(s.m_view==V::Queue);
   if(cycle%2) s.goBack(); else s.openNowPlaying();
   assert(s.m_view==V::NowPlaying && s.m_nowPlayingReturnView==origin);
   s.openNowPlaying(); s.goBack(); finish(s);
   assert(s.m_view==origin && s.m_selection==4);
  }
 }
 auto s=fixture(V::Albums); s.m_rootCategoryTransition=.5f; s.openNowPlaying(); assert(s.m_view==V::Albums);
 s=fixture(V::AlbumDetail); s.m_detailTransition=.5f; s.openNowPlaying(); assert(s.m_view==V::AlbumDetail);
 s=fixture(V::AlbumDetail); s.m_detailClosing=true; s.openNowPlaying(); assert(s.m_view==V::AlbumDetail);
 s=fixture(V::Albums); s.m_status.track_id=0; s.openNowPlaying(); assert(s.m_view==V::Albums);
 s=fixture(V::Albums); s.m_active=false; s.openNowPlaying(); assert(s.m_view==V::Albums);
 s=fixture(V::Albums); s.m_modal=MusicScreen::Modal::PlaylistChooser; s.openNowPlaying(); assert(s.m_view==V::Albums);
 s=fixture(V::Albums); s.openNowPlaying(); finish(s); s.m_status.track_id=0; s.goBack(); finish(s); assert(s.m_view==V::Albums);
 s=fixture(V::Albums);s.contextualY();
 assert(s.m_modal==MusicScreen::Modal::None && s.m_preferences.liked);
 assert(s.m_view==V::Albums && s.m_selection==4);
 s.contextualY();assert(!s.m_preferences.liked);
 s.openNowPlaying();finish(s);s.goBack();finish(s);assert(s.m_view==V::Albums);
 std::cout << "Production navigation: Y toggles favourite without modal or navigation; repeated Minus, early B, 120 queue cycles, 4 origins, missing track and transition/modal guards: OK\n";
}
'''
with tempfile.TemporaryDirectory(prefix='switchu_navigation_') as temp:
    cpp=Path(temp)/'navigation.cpp'; exe=Path(temp)/'navigation'; cpp.write_text(code)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-Wall','-Wextra','-Wpedantic','-I'+str(ROOT),'-I'+str(ROOT/'projects/common/include'),str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
