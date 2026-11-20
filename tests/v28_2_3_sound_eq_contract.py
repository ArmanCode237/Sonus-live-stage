#!/usr/bin/env python3
# main.cpp was split into src/app/** (StageApp + route groups): contract checks
# read the whole application layer instead of the single former file.
def _stage_app_source():
    import pathlib as _p
    root = _p.Path(__file__).resolve().parents[1]
    files = [root / 'src/main.cpp', *sorted((root / 'src/app').rglob('*.h')), *sorted((root / 'src/app').rglob('*.cpp'))]
    return chr(10).join(f.read_text(encoding='utf-8') for f in files)
from html.parser import HTMLParser
from pathlib import Path
root=Path(__file__).resolve().parents[1]
read=lambda rel:(root/rel).read_text(encoding='utf-8')
engine=read('src/modules/StagePianoEngine.cpp')
types=read('src/core/StageTypes.h')
main=_stage_app_source()
registry=read('src/core/WorkstationHost.cpp')
ui=read('ui/partials/modal-piano-sample.html')
js=read('ui/js/piano-sample-editor.js')
studio=read('ui/js/studio-rack.js')
for marker in ['soundEqEnabled','soundEqFreq','soundEqQ','soundEqGain','soundEqHighPassSlope','soundEqLowPassSlope']:
 assert marker in types and marker in main and marker in engine,marker
for layer in range(5):
 for suffix in ['EqHP','EqLP','EqF0','EqQ0','EqG0','EqF1','EqQ1','EqG1','EqF2','EqQ2','EqG2']:
  assert f'pianoSample{layer}{suffix}' in ui,(layer,suffix)
 assert f'stageSoundEqCurve{layer}' in ui
for marker in ['eqHighPassSlope','eqLowPassSlope']:
 assert marker in main and marker in engine and marker in studio
for marker in ['prefix+"EqHP"','prefix+"EqLP"','prefix+"EqF"','prefix+"EqQ"','prefix+"EqG"']:
 assert marker in registry,marker
assert 'stageSoundEqDraw' in js
assert 'stageDrawEqResponse' in studio and 'studioEqSlopeChange' in studio
assert 'srcRate' in engine
assert 'previous.targetVelocityGain=newGain;' in engine
assert 'three-sounds-v1' in main
assert 'performanceSoundC' in read('ui/js/programs.js') and 'performanceSoundC' in read('ui/index.html')
class IDs(HTMLParser):
 def __init__(self):super().__init__();self.ids=[]
 def handle_starttag(self,tag,attrs):self.ids.extend(v for k,v in attrs if k=='id')
for html in ['ui/index.html','ui/partials/modal-piano-sample.html']:
 p=IDs();p.feed(read(html));assert len(p.ids)==len(set(p.ids)),html
print('PASS Sound EQ persistence, per-layer controls, slope/curve linkage, optional 3rd Sound, unique IDs')
