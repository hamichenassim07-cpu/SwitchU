#!/usr/bin/env python3
"""V8.9 targeted changes anchored to the actual delivered V8.8 archive."""
import hashlib
import json
from pathlib import Path
import re
from music_source_contract import function, region
ROOT=Path(__file__).resolve().parents[1]
manifest=json.loads((ROOT/'tests/protected_v8_8.json').read_text())
failures=[]
def require(ok,message):
    if not ok: failures.append(message)
def read(path): return (ROOT/path).read_text()
for path,sha in manifest['files'].items():
    require((ROOT/path).is_file() and hashlib.sha256((ROOT/path).read_bytes()).hexdigest()==sha,
            'V8.8 protected file changed: '+path)
for spec in manifest['regions']:
    try: actual=hashlib.sha256(region(read(spec['path']),spec).encode()).hexdigest()
    except (ValueError,KeyError): actual='missing'
    require(actual==spec['sha256'],'V8.8 protected region changed: '+spec['path']+' '+spec.get('function',spec.get('after','')))
screen=read('projects/menu/src/music/MusicScreen.cpp')
physical=read('projects/menu/src/music/MusicPhysicalMediaRenderer.cpp')
pose=read('projects/menu/src/music/MusicPhysicalMediaRenderer.hpp')
cache=read('projects/menu/src/music/MusicCoverCache.cpp')
background=read('projects/menu/src/music/MusicAmbientBackground.cpp')
palette=read('projects/menu/src/music/MusicAmbientPalette.cpp')
integration=read('projects/menu/src/music/MusicIntegration.cpp')
for forbidden in ['drawVinyl','projectDiscPoint','CircleLut','vinylOutline','vinylLabelFont','vinylRadius']:
    require(forbidden not in physical+pose,'Removed disc resource/render returned: '+forbidden)
require('m_vinylSpinBoost' not in screen,'Removed disc animation state returned')
require('setWifiVisible(false)' not in integration and 'setWifiVisible(true)' in integration,'Native HOME Wi-Fi is not enabled')
require('romfs:/icons/playtime_clock_v1030.png' in screen,'Native HOME clock asset missing')
info=function(screen,'MusicScreen::drawAlbumInformation')
layout=read('projects/menu/src/music/MusicAlbumLayout.hpp')
require('kClockSize = 30.f' in layout,'Informational HOME clock must remain legible')
require('albuminfo::summarise(album, m_library)' in info,'Metadata must use actual library values')
require('kTitleY = 610.f' in layout and 'kArtistY = 648.f' in layout and 'kMetaY = 680.f' in layout,'Album information must clear the 606.7 px reflection bound')
require(info.count('pushClipRect')==3 and info.count('popClipRect')==3,'Metadata clip scopes changed')
require('rowCentreY-info.durationHeight*.5f' in info and 'rowCentreY-info.countHeight*.5f' in info,'HOME measured vertical alignment missing')
require('ren.drawCircle' in info and 'ren.drawTriangle' in info and '"♪"' not in info,'Independent informational note missing')
clock_bytes=bytes(int(h,16) for h in re.findall(r'0x([0-9a-f]{2})',read('projects/menu/src/music/MusicHomeClockPng.hpp')))
require(clock_bytes==(ROOT/'romfs/icons/playtime_clock_v1030.png').read_bytes(),'Clock fallback differs from the HOME asset')
require('loadFromMemory' in info and 'sizeof(kHomeClockPng)' in info,'Embedded exact HOME clock fallback missing')
require(screen.count(' * kMusicCarouselScale')==6 and 'kMusicCarouselScale = 0.92f' in screen,'Only the six authorised geometric factors may change')
update=function(screen,'MusicScreen::updateAmbientBackground')
require('m_ambientBackground.update(dt)' in update and 'updateAmbientBackground(dt);' in function(screen,'MusicScreen::onUpdate'),'Ambient animation is not continuously updated')
require('m_ambientBackground.draw(ren, a, kFloorY)' in screen,'Ambient effect is not rendered')
for forbidden in ['m_rootCarouselMotion','moveSelection(','m_client.','m_coverCache.get(','loadFromFile','std::vector']:
    require(forbidden not in update,'Background touches protected state or reloads data: '+forbidden)
require('ren.pushClipRect({0.f, 0.f, 1280.f, floorY-12.f})' in background,'Ribbon scissor must stop before the floor')
require('kRibbonSegments = 48' in read('projects/menu/src/music/MusicAmbientPalette.hpp') and 'kRibbonLayers = 3' in read('projects/menu/src/music/MusicAmbientPalette.hpp'),'Fixed mesh budget changed')
for forbidden in ['captureToOffscreen','applyBlur','std::thread','std::async','std::vector','loadFrom','drawTexture']:
    require(forbidden not in background+palette,'Ambient allocation/resource/blur path returned: '+forbidden)
require('ambient::samplePalette(decoded.rgba.data()' in cache,'Palette must reuse the existing decoded image')
require('prior->second.backgroundPalette' in cache,'Palette reuse on artwork eviction is missing')
require('samplePalette' not in update,'Artwork palette is analysed every frame')
require('std::min(width, 24)' in palette and 'std::min(height, 24)' in palette,'Bounded artwork sampling changed')
require('m_phase += double(step)' in palette and 'm_from = m_current' in palette and '0.85f' in read('projects/menu/src/music/MusicAmbientPalette.hpp'),'Continuous phase/smooth interpolation is missing')
entry=function(screen,'MusicScreen::openNowPlaying')
require(entry.index('m_view == View::NowPlaying')<entry.index('m_nowPlayingReturnView = m_view'),'Minus overwrites the return view before checking reentry')
require('m_view == View::Queue && m_queueReturnView == View::NowPlaying' in entry,'Queue/player return-cycle guard is missing')
# Complete local source closure (devkitPro packages remain build prerequisites).
include_dirs=[ROOT/'projects/menu/src',ROOT/'projects/common/include',ROOT/'lib/nxui/include',ROOT/'lib/nxui/include/nxui/third_party/stb',ROOT/'lib/espeak-ng/src/include']
for top in [ROOT/'projects',ROOT/'lib/nxui']:
    for p in top.rglob('*'):
        if p.suffix not in ['.cpp','.hpp','.h']: continue
        for token in re.findall(r'^\s*#\s*include\s*"([^"]+)"',p.read_text(errors='replace'),re.M):
            require(any((base/token).is_file() for base in [p.parent]+include_dirs),'Missing local include '+token+' in '+str(p.relative_to(ROOT)))
for token in re.findall(r'add_files\("([^"]+)"',read('xmake.lua')):
    require(bool(list(ROOT.glob(token.replace('**.','**/*.')))),'Empty build source pattern: '+token)
require((ROOT/'lib/Atmosphere-libs/libstratosphere/Makefile').exists(),'Atmosphere source dependency is missing')
require((ROOT/'lib/espeak-ng/CMakeLists.txt').exists(),'eSpeak source dependency is missing')
if failures:
    raise SystemExit('SwitchU V8.9 contract FAILED:\n - '+'\n - '.join(failures))
print(f"Protected V8.8: {len(manifest['files'])} complete files + {len(manifest['regions'])} source regions: OK")
print('V8.9 satin mesh, exact HOME assets, compact geometry, information layout and local build dependencies: OK')
