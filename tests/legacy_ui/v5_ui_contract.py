# main.cpp was split into src/app/** (StageApp + route groups): contract checks
# read the whole application layer instead of the single former file.
def _stage_app_source():
    import pathlib as _p
    root = _p.Path(__file__).resolve().parents[1]
    files = [root / 'src/main.cpp', *sorted((root / 'src/app').rglob('*.h')), *sorted((root / 'src/app').rglob('*.cpp'))]
    return chr(10).join(f.read_text(encoding='utf-8') for f in files)
from pathlib import Path
from bs4 import BeautifulSoup
import re
R=Path(__file__).resolve().parents[1]
fx=BeautifulSoup((R/'ui/partials/modal-fx-sends.html').read_text(encoding='utf8'),'html.parser')
knobs=fx.select('#fxSendsRows .fx-send-knob')
ids=[k.get('data-id') for k in knobs]
assert len(ids)==27,(len(ids),'fx send knobs')
assert len(set(ids))==27,'duplicate FX sends'
assert all(re.fullmatch(r'fxSend[0-8](Mod|Delay|Reverb)',s) for s in ids)
assert len(fx.select('.fx-send-row'))==9
sample=BeautifulSoup((R/'ui/partials/modal-piano-sample.html').read_text(encoding='utf8'),'html.parser')
sampleids=[k['data-id'] for k in sample.select('.knob-cell')]
assert len(sampleids)==35 and len(set(sampleids))==35
assert len(sample.select('.piano-sample-tabs button'))==5
organ=BeautifulSoup((R/'ui/partials/modal-organ-effects.html').read_text(encoding='utf8'),'html.parser')
assert len(organ.select('.knob-cell'))==6
cpp=(R/'src/core/WorkstationHost.cpp').read_text()
for bus in ('Mod','Delay','Reverb'): assert '"'+bus+'"' in cpp
for c in ('Atk','Dec','Sus','Rel','Hold','Pan','Tune'):assert '"'+c+'"' in cpp
for c in ('organRotaryMix','organVibratoDepth','organVibratoRate'):assert c in cpp
for p in ('index','synth','organ'):
 src=(R/f'ui/{p}.html').read_text()
 assert 'modal-fx-sends.html' in src
 assert '/js/fx-sends.js' in src
 assert 'modal-organ-effects.html' in src
 assert 'piano.css' in src
assert 'piano-sample-editor.js' in (R/'ui/index.html').read_text()
assert 'max-height:clamp' in (R/'ui/css/piano.css').read_text()
assert 'const SYNTH_PRESET_COUNT = 12' in (R/'ui/js/synth.js').read_text()
assert len(re.findall(r"P\d+: ", (R/'ui/js/core.js').read_text()))==12
assert 'status == 0xE0' in _stage_app_source()
assert 'data1 == 1' in _stage_app_source()
print('OK 27 FX knobs, 35 per-layer controls, 6 organ FX, 12 synth presets, MIDI bend & CC1, pages, responsive scroll')
