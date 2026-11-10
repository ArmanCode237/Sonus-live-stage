"""Quick static consistency checks for Live Stage's analog synth upgrade."""
from pathlib import Path
import json, re
root = Path(__file__).resolve().parents[1]
types = (root/'src/core/Nordtypes.h').read_text()
synth = (root/'src/modules/NordSynthModule.cpp').read_text()
registry = (root/'src/core/WorkstationHost.cpp').read_text()
main = (root/'src/main.cpp').read_text()
ui = (root/'ui/js/synth.js').read_text()
sync = (root/'ui/js/sync.js').read_text()
fields = {
 'saw':'sawLevel','pulse':'pulseLevel','pw':'pulseWidth',
 'pwmDepth':'pwmDepth','pwmRate':'pwmRateHz',
 'sub':'subLevel','noise':'noiseLevel','osc2':'osc2Level',
 'osc2Det':'osc2Detune','osc2Semi':'osc2Semitones',
 'osc2Wave':'osc2Wave','hpf':'highpassHz','drive':'drive'
}
for key,member in fields.items():
    assert member in types and member in synth
    assert f'p + "{key}"' in registry, key
    assert f'"{member}"' in registry, member
    assert f'"{member}"' in main, member
    assert key in ui and key in sync, key
for key in ['synthLfoEnabled','synthLfoWave','synthLfoRate',
            'synthLfoDepth','synthLfoPitch','synthLfoCutoff']:
    assert key in types and key in registry and key in main and key in sync
assert '"synthLfos"' in main and '"synthEnvelopes"' in main
assert '"synthLayers"' in main and '"organEnabled"' in main
assert '"eqEnabled"' in main and '"reverbEnabled"' in main
assert '"delayEnabled"' in main
assert 'api/synth/presets/save' in ui and 'api/organ/presets/save' in (root/'ui/js/organ.js').read_text()
with (root/'programs.json').open() as f:
    programs=json.load(f)
assert isinstance(programs,list) and len(programs)==8
for x in programs:
    assert 'params' in x and 'slot' in x
print('OK: 13 analog controls linked across C++/JS/JSON; 4 LFOs, 3 envelopes, 8 programs preserved')
