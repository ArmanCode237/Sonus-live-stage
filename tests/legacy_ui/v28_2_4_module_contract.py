#!/usr/bin/env python3
# main.cpp was split into src/app/** (StageApp + route groups): contract checks
# read the whole application layer instead of the single former file.
def _stage_app_source():
    import pathlib as _p
    root = _p.Path(__file__).resolve().parents[1]
    files = [root / 'src/main.cpp', *sorted((root / 'src/app').rglob('*.h')), *sorted((root / 'src/app').rglob('*.cpp'))]
    return chr(10).join(f.read_text(encoding='utf-8') for f in files)
"""Static compatibility contract independent of RtAudio, RtMidi and JSON headers."""
from pathlib import Path
from html.parser import HTMLParser
ROOT=Path(__file__).resolve().parents[1]
main=_stage_app_source()
js=(ROOT/'ui/js/piano-performances-v2824.js').read_text()
prog=(ROOT/'ui/js/programs.js').read_text()
fx=(ROOT/'ui/js/studio-rack.js').read_text()
model=(ROOT/'src/core/StageTypes.h').read_text()
class Ids(HTMLParser):
 def __init__(self):super().__init__();self.ids=[]
 def handle_starttag(self,tag,attrs):self.ids += [x for k,x in attrs if k=='id']
for name in ['ui/index.html','ui/partials/modal-piano-sample.html','ui/library-manager.html']:
 p=Ids();p.feed((ROOT/name).read_text());assert len(p.ids)==len(set(p.ids)),name
for route in ['/api/piano/performances','/api/piano/performances/store','/api/piano/performances/load','/api/piano/performances/delete']:
 assert route in main,route
assert '"piano_performances.json"' in main
assert '"programs.json"' in main
assert 'restoreModulePatch(0,patch)' in main
assert 'restoreModulePatch(1,patch)' not in main[main.index('svr.Post("/api/piano/performances/load"'):main.index('svr.Post("/api/piano/performances/delete"')]
assert 'restoreModulePatch(2,patch)' not in main[main.index('svr.Post("/api/piano/performances/load"'):main.index('svr.Post("/api/piano/performances/delete"')]
assert '"studioRack"' in main and '"midiBindings"' in main and '"oscRoutes"' in main
assert 'visitPaths(changedPianoPerformances)' in main
assert 'result.ok===false' in js
assert 'stagePianoPerformanceOnGlobalRecall' in prog
assert 'PARAMETRIC EQ' in fx and 'STUDIO_CONTROLS_ENHANCED' in fx
assert 'int version=1' in model
assert 'stagePianoPerformanceStore' in (ROOT/'ui/index.html').read_text()
assert 'performanceBuilderStatus' not in (ROOT/'ui/index.html').read_text()
print('PASS v28.2.4 scoped Piano Performances, racks, relocation, UI contracts')
