#!/usr/bin/env python3
# main.cpp was split into src/app/** (StageApp + route groups): contract checks
# read the whole application layer instead of the single former file.
def _stage_app_source():
    import pathlib as _p
    root = _p.Path(__file__).resolve().parents[1]
    files = [root / 'src/main.cpp', *sorted((root / 'src/app').rglob('*.h')), *sorted((root / 'src/app').rglob('*.cpp'))]
    return chr(10).join(f.read_text(encoding='utf-8') for f in files)
from pathlib import Path
from html.parser import HTMLParser
root=Path(__file__).resolve().parents[1]
main=_stage_app_source()
model=(root/'src/core/StageTypes.h').read_text()
engine=(root/'src/modules/StagePianoEngine.cpp').read_text()
registry=(root/'src/core/WorkstationHost.cpp').read_text()
editor=(root/'ui/js/piano-sample-editor.js').read_text()
program=(root/'ui/js/programs.js').read_text()
class Parser(HTMLParser):
 def __init__(self):super().__init__();self.ids=[]
 def handle_starttag(self,tag,attrs):
  self.ids.extend(x for k,x in attrs if k=='id')
for rel in ['ui/index.html','ui/partials/modal-piano-sample.html','ui/library-manager.html']:
 parser=Parser();parser.feed((root/rel).read_text())
 assert len(parser.ids)==len(set(parser.ids)),f'Duplicate IDs in {rel}'
assert 'saveCurrentPianoSound()' in (root/'ui/partials/modal-piano-sample.html').read_text()
assert 'location.href=\'/library-manager' not in (root/'ui/partials/modal-piano-sample.html').read_text()
for name in ['wheelCutoffEnabled','wheelCutoffInvert','wheelAmpEnabled','wheelVibratoEnabled',
             'wheelVibratoRateHz','wheelVibratoCents','monoMode','legatoEnabled']:
 assert name in model and name in main and name in engine, name
for name in ['WheelCutoffOn','WheelCutoffInvert','WheelAmpOn','WheelVibratoOn','WheelVibratoDepth',
             'WheelVibratoRate','Mono','Legato']:
 assert 'prefix+"'+name+'"' in registry, name
assert 'findDefaultSoundForSource' in main
assert 'restorePianoLayerSound(pianoLayer,(*saved)["patch"])' in main
assert 'defaultForSource' in main
assert 'svr.Post("/api/performances/store"' in main
assert '"two-sounds-v1"' in main and '"three-sounds-v1"' in main
assert 'rollback();' in main
assert 'stageSoundMarkClean' in program and 'stageProgramMarkDirty' in editor
assert 'window.stageSoundMarkClean' not in main
print('PASS: unique IDs, sound binding, performance composer, modulation and rollback contract')
