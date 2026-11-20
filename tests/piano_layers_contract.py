# main.cpp was split into src/app/** (StageApp + route groups): contract checks
# read the whole application layer instead of the single former file.
def _stage_app_source():
    import pathlib as _p
    root = _p.Path(__file__).resolve().parents[1]
    files = [root / 'src/main.cpp', *sorted((root / 'src/app').rglob('*.h')), *sorted((root / 'src/app').rglob('*.cpp'))]
    return chr(10).join(f.read_text(encoding='utf-8') for f in files)
"""Static API/UI contract test for the piano 3-layer implementation."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
source=lambda name:(root/name).read_text(encoding='utf-8')
main=_stage_app_source(); host=source('src/core/WorkstationHost.cpp')
engine=source('src/modules/StagePianoEngine.cpp')
ui=source('ui/index.html'); organ=source('ui/js/organ.js')
piano=source('ui/js/piano.js'); lib=source('ui/js/library.js')
assert 'pianoVolume' in host and 'pianoVolume' in main and 'pianoVolume' in ui
assert 'pianoLayerVol' in host and 'pianoLayerEnabled' in host
assert 'serializePianoLayerSources(engine)' in main and 'restorePianoLayerSources' in main
assert 'svr.Post("/api/piano/layers/clear"' in main
assert 'pianoLayerSources' in piano and "body:JSON.stringify({path,layer})" in lib
assert 'sampleBank = inst' in engine and 'voiceInst.undampedFromKey' in engine
for i in range(3):
 assert f'pianoLayerRow{i}' in ui and f'data-id="pianoLayerVol{i}"' in ui
for i in range(5):
 assert f'data-id="eqGain{i}"' in ui
assert 'VOICE & LOOP CFG' not in ui
assert 'drawbar-handle' in organ and 'drawbar-led' not in organ
assert 'buildMainOrganDrawbars' in organ and 'mainOrganDrawbars' in ui
print('Piano/EQ/organ UI and API contracts (knob edition): OK')
