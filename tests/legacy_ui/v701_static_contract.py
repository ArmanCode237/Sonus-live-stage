# main.cpp was split into src/app/** (StageApp + route groups): contract checks
# read the whole application layer instead of the single former file.
def _stage_app_source():
    import pathlib as _p
    root = _p.Path(__file__).resolve().parents[1]
    files = [root / 'src/main.cpp', *sorted((root / 'src/app').rglob('*.h')), *sorted((root / 'src/app').rglob('*.cpp'))]
    return chr(10).join(f.read_text(encoding='utf-8') for f in files)
"""Contract for v7.0.1 browser/backend and program persistence.
Run python tests/v701_static_contract.py (no external dependencies)."""
from pathlib import Path
import re
root=Path(__file__).resolve().parents[1]
def read(path):return (root/path).read_text(encoding='utf8')
js=read('ui/js/core.js')
synth=read('ui/js/synth.js')
v=read('ui/js/v701.js')
ui=read('ui/synth.html')
main=_stage_app_source()
work=read('src/core/WorkstationHost.cpp')
piano=read('src/modules/StagePianoEngine.cpp')
dsp=read('src/dsp/StageDspEffects.h')
assert 'const SYNTH_PRESET_COUNT = 200' in synth
assert 'maxFactoryPresets!==200' in synth
assert 'std::clamp(presetId, 1, 200)' in read('src/modules/StageSynthModule.cpp')
assert 'std::lround(v)), 0, 20)' in work
assert 'apiVersion", 4' in main
assert 'V701_FAMILIES' in js and 'SYNTH_PRESET_NAMES.push' in js
assert 'v701MasterVoice' in v and 'L\'+i+\'_synthType' in v
for n in range(1,4):
 for f in ('synthType','fmIndex','wtMorph','voiceMode','portamento','glideMode','slide','keyLow','keyHigh'):
  assert f'"{f}"' in main or f"'{f}'" in v or f in work
for name in ('synthVoiceMaster','pianoLayerZones','synthFxSends','synthFx','organFx','synthZone','organZone','masterDriveEnabled','masterAutoPanEnabled'):
 assert main.count(name)>1, name
assert 'StageModuleFxRack' in dsp and 'synthModuleFx_.process' in piano and 'organModuleFx_.process' in piano
assert '<div id="v701SynthPerformance"' in ui and '<div id="v701SynthFx"' in ui
assert "key.endsWith('On')" in v and "key==='DelayTime'?'delayMs'" in v
# Guard and restore zones in the correct functions, never within JSON initializer.
ser=main[main.index('json serializeParams('):main.index('SynthPresetParams deserializeParams(')]
deser=main[main.index('SynthPresetParams deserializeParams('):main.index('}',main.index('SynthPresetParams deserializeParams('))+1]
assert 'const auto loadZone=' not in ser
assert 'const auto loadZone=' in deser
for i in range(3):
 assert f'"synthSend"+std::to_string(l)' in work # runtime-generated send IDs
for f in ('ChorusOn','DelayOn','ReverbOn','DriveOn','AutoPanOn'):
 assert f in v and f in work
print('PASS V7.0.1 static contract: 200 patches, parameter bounds, synthesis/voice modes, racks, keyzones, serialization and IDs')
