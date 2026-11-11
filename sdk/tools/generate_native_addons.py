#!/usr/bin/env python3
"""Generate repetitive service clients and original monochrome PGM icons.

No vendor firmware, fonts, graphics or cryptographic material is read here.
The JSON catalog is the source of truth; generated clients compile using only SDK.
"""
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
CATALOG=json.loads((ROOT/'native-mods.json').read_text())

def icon(kind):
    pixels=bytearray(48*48)
    def line(x,y,a,b):
        steps=max(abs(a-x),abs(b-y),1)
        for s in range(steps+1):
            u=round((x+(a-x)*s/steps)*2);v=round((y+(b-y)*s/steps)*2)
            for j in (v,v+1):
                for i in (u,u+1):
                    if 0<=i<48 and 0<=j<48:pixels[j*48+i]=255
    def box(x,y,a,b):
        for p in ((x,y,a,y),(a,y,a,b),(a,b,x,b),(x,b,x,y)):line(*p)
    if kind=='wave':
        for x,h in ((3,2),(6,5),(9,8),(12,10),(15,6),(18,4),(21,2)):line(x,12-h,x,12+h)
    elif kind=='list':
        box(2,3,22,21)
        for y in (7,12,17):line(6,y,18,y)
    elif kind=='usb':
        for p in ((12,22,12,2),(9,5,12,2),(12,2,15,5),(12,15,5,11),(5,11,5,7),(12,11,20,7),(20,7,20,3)):line(*p)
    elif kind=='sun':
        box(8,8,16,16)
        for p in ((12,1,12,4),(12,20,12,23),(1,12,4,12),(20,12,23,12),(3,3,5,5),(19,19,21,21),(3,21,5,19),(19,5,21,3)):line(*p)
    elif kind in ('clock','knob'):
        box(3,3,21,21);line(12,5,12,12);line(12,12,18,15)
    elif kind=='sliders':
        for x,y in ((5,7),(12,15),(19,9)):
            line(x,2,x,22);line(x-3,y,x+3,y)
    elif kind=='slip':
        for p in ((20,5,6,5),(6,5,2,9),(2,9,6,13),(6,13,6,5),(8,18,20,18),(20,18,22,14)):line(*p)
    elif kind=='eye':
        for p in ((2,12,7,6),(7,6,17,6),(17,6,22,12),(22,12,17,18),(17,18,7,18),(7,18,2,12)):line(*p)
        box(10,9,14,15)
    elif kind=='repeat':
        for p in ((3,7,21,7),(21,7,17,3),(21,7,17,11),(21,17,3,17),(3,17,7,13),(3,17,7,21)):line(*p)
    else:
        box(2,3,22,21);line(2,8,22,8)
    return b'P5\n48 48\n255\n'+pixels

def generate():
    for m in CATALOG:
        folder=ROOT/'mods'/m['folder'];folder.mkdir(parents=True,exist_ok=True)
        code=['/* Generated service client; see native-mods.json and NATIVE-SERVICES.md. */',
              '#include "oqmod_native_feature.h"']
        for n,(_key,_label,names,_fallback,_base) in enumerate(m['options']):
            code.append(f'static const char *const values{n}[]='+json.dumps(names).replace('[','{').replace(']','}')+';')
        code.append('static const struct oq_native_option oq_feature_options[]={')
        for n,(key,label,names,fallback,base) in enumerate(m['options']):
            code.append('    {'+f'{json.dumps(key)},{json.dumps(label)},values{n},{len(names)},{fallback},{base}'+'},')
        code+=['};','static const struct oq_native_feature oq_feature={'+','.join(json.dumps(m[k]) for k in ('service','name','description'))+f', {m["priority"]}'+'};',
               '#define OQ_NATIVE_FEATURE_IMPLEMENTATION','#include "oqmod_native_feature.h"','']
        if m.get('implementation')!='custom':(folder/'mod.c').write_text('\n'.join(code))
        (folder/'icon.pgm').write_bytes(icon(m['icon']))
        (folder/'menu.order').write_text(f'{m["priority"]}\n')
        if m['folder']=='fx-frequency':
            (folder/'README.md').write_text('# FX Frequency — USB-owned engine\n\n'
                'Requires the R113 extraction host / SDK 1.3 development interface.\n'
                'This folder owns packet hooks/policy, drawing, touch, settings and menu.\n'
                'The core supplies shared transport and a popup-aware reserved surface only.\n'
                'R112 and older built-in frequency engines are refused. See ../../ENGINE-EXTRACTION.md.\n')
        elif m['folder']=='quick-usb':
            (folder/'README.md').write_text('# Quick USB Access — USB-owned implementation\n\n'
                'Requires the R113 extraction host / SDK 1.3 development interfaces.\n'
                'This folder owns shortcut drawing, UTF-8 labels, colours, active-source\n'
                'styling, layout, hitboxes, touch requests and activation. The host supplies\n'
                'cached native source snapshots and guarded native navigation; no track is loaded.\n'
                'See ../../ENGINE-EXTRACTION.md for API and lifetime rules.\n')
        elif m['folder']=='light-theme':
            (folder/'README.md').write_text('# Light Theme — USB-owned native engine\n\n'
                'Owns native paint hooks, palette/ink algorithms and colour caches.\n'
                'Requires the R113 theme provider bridge; stock colours when unavailable/off.\n'
                'Includes GPL-derived source and license under native/. See ../../ENGINE-EXTRACTION.md.\n')
        elif m['folder']=='smooth-waveforms':
            (folder/'README.md').write_text('# Smooth Waveforms — USB-owned render policy\n\n'
                'Native Height 30 remains the default. Owns period/allowlist and paint-clock policy.\n'
                'Shared host hooks measure FPS and dispatch callbacks; they do not implement smoothing.\n'
                'Requires the R113 retained-clock/render-provider interface. See ../../ENGINE-EXTRACTION.md.\n')
        elif m['folder']=='fx-target':
            (folder/'README.md').write_text('# FX Target Knob — USB-owned engine\n\n'
                'Owns detents, circular target ordering, SHIFT and acknowledgement policy.\n'
                'Shared native encoder routing acknowledges physical counters and preserves menu/browser priority.\n'
                'Requires the R113 target-action and fail-open TIME-lease APIs. See ../../ENGINE-EXTRACTION.md.\n')
        elif m['folder']=='slip-mode':
            (folder/'README.md').write_text('# Slip Mode — USB-owned engine\n\n'
                'Owns DirectionHandler swap, per-deck state convergence, cleanup and LED patterns.\n'
                'Requires the R113 early input identity, native Slip primitives and disabled-cleanup ticks.\n'
                'Held releases stay owned across disable; USB removal restores mod-owned Slip.\n'
                'See ../../ENGINE-EXTRACTION.md for shared/native and hardware-validation boundaries.\n')
        elif m['folder']=='four-deck':
            (folder/'README.md').write_text('# Four-Deck Waveforms — USB-owned layout engine\n\n'
                'Owns lane/card/overview/mixer geometry, fast repair, both deck orders, card refresh and waveform height scaling.\n'
                'Requires the R113 shared native update/paint provider and checked component primitives.\n'
                'Cooperates with the separately external sidebar; native app hooks are not duplicated.\n'
                'Native Height 30 geometry is retained. Physical rendering/FPS still needs regression.\n'
                'See ../../ENGINE-EXTRACTION.md for lifecycle and restart rules.\n')
        elif m['folder']=='side-now-playing-legacy':
            (folder/'README.md').write_text('# Side Screen Now Playing (Legacy) — USB-owned record engine\n\n'
                'Owns live state/grid/loop presentation, waveform window, cue/metadata scheduling, privacy and overrides.\n'
                'Requires the R113 coherent cached-analysis lease and side-record provider.\n'
                'Shared host retains passive decode/cache, browser/transport arbitration and fixed carrier/CRC.\n'
                'Uses the installed controller wire protocol; a new controller primitive can still require firmware.\n'
                'See ../../ENGINE-EXTRACTION.md for hardware-validation and restart boundaries.\n')
        elif m['folder']=='fx-sidebar':
            (folder/'README.md').write_text('# Beat FX Sidebar — USB-owned engine\n\n'
                'Owns compact native geometry, chooser reparenting, format/time layout, exact restoration and frame drawing.\n'
                'Requires the R113 native-view provider and guarded GUI component adapters.\n'
                'The host retains shared popup/lifetime observation and native update dispatch.\n'
                'See ../../ENGINE-EXTRACTION.md for the resident-provider and hardware-validation boundaries.\n')
        elif m['folder']=='hide-titles':
            (folder/'README.md').write_text('# Hide Track Names — USB-owned engine\n\n'
                'Owns native title visibility hooks, per-deck hiding, eye drawing/taps and stock side-text masks.\n'
                'Requires the R113 multi-panel, waveform observer, cached deck-text and metadata-mask APIs.\n'
                'Preserves the existing fallback eyes and does not re-enable the disabled component-tree scan.\n'
                'See ../../ENGINE-EXTRACTION.md for lifecycle and hardware-validation boundaries.\n')
        elif m['folder']=='zone-continuous':
            (folder/'README.md').write_text('# ZONE Continuous Play — USB-owned engine\n\n'
                'Requires the R113 development loader registry and routing telemetry.\n'
                'This folder owns the next-track hook, continuation policy and deferred PLAY.\n'
                'Early shared observers retain loader identities but perform no playback actions.\n'
                'No physical hardware validation yet; see ../../ENGINE-EXTRACTION.md.\n')
        elif m['folder'] in ('side-browser','playlist-editor','stems'):
            (folder/'README.md').write_text(f'# {m["name"]} — USB-owned implementation\n\n{m["description"]}\n\n'
                'Requires the matching R113 extraction foundation and appended SDK services.\n'
                'This folder contains the actual feature implementation, not a settings-only wrapper.\n'
                'See ../../ENGINE-EXTRACTION.md for shared bridges, resident lifetime,\n'
                'hardware-validation limits and the required matched firmware package.\n')
        else:
            (folder/'README.md').write_text(f'# {m["name"]}\n\n{m["description"]}\n\nNative service: `{m["service"]}`. Requires R112 / SDK 1.2.1 (service protocol 2).\n'
                'This add-on owns its menu, preferences and activation policy. The host supplies\n'
                'the shared version-checked native backend; see ../../NATIVE-SERVICES.md.\n')
    # Assets for already-external mods. They do not need a binary rebuild merely
    # to get an icon and a menu position on the new host.
    for folder,priority,kind in [('tyto-sidescreen',20,'wave'),('gated-cues',110,'slip'),
                                 ('party',180,'sun'),('prodjlink',160,'usb'),('bt-wave',170,'wave')]:
        out=ROOT/'assets'/folder;out.mkdir(parents=True,exist_ok=True)
        (out/'icon.pgm').write_bytes(icon(kind));(out/'menu.order').write_text(f'{priority}\n')
    print(f'Prepared {len(CATALOG)} SDK catalog entries/icons, preserving custom engine sources; 5 legacy add-on icon packs.')

if __name__=='__main__':generate()
