# main.cpp was split into src/app/** (StageApp + route groups): contract checks
# read the whole application layer instead of the single former file.
def _stage_app_source():
    import pathlib as _p
    root = _p.Path(__file__).resolve().parents[1]
    files = [root / 'src/main.cpp', *sorted((root / 'src/app').rglob('*.h')), *sorted((root / 'src/app').rglob('*.cpp'))]
    return chr(10).join(f.read_text(encoding='utf-8') for f in files)
"""Validate v9 note zones, rack topology and cross-layer bindings without extra deps."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
read=lambda p:(root/p).read_text(encoding='utf8')
cpp=read('src/core/WorkstationHost.cpp');types=read('src/core/StageTypes.h')
synth=read('ui/js/synth.js');v=read('ui/js/v701.js')
fx=read('ui/js/fx-sends.js');engine=read('src/modules/StagePianoEngine.cpp')
for s in ['synthFx','organFx']:
    assert s in engine
    assert '"'+s+'"' in _stage_app_source()
for suffix in ('PhaserOn','PhaserRate','PhaserMix','FlangerOn','FlangerRate','FlangerDepth','FlangerFeedback','FlangerMix','TremoloOn','TremoloRate','TremoloDepth'):
    assert '"'+suffix+'"' in cpp,suffix
    assert suffix in v,suffix
assert 'fxFields[30]' in cpp and 'moduleFxParams_' in engine
assert 'v701NoteName=m=>' in v and 'data-zone-param' in v and 'data-zone-parent' in v
assert 'synthZone' in v and 'pianoZone' in v and 'organZone' in v
assert 'v701GetZone(parent)' in v and 'v701ApplyZone' in v
assert 'C4 (scientific pitch)' in v
assert 'SYNTHESIS ARCHITECTURE' in synth and 'synthPanel_${i}_osc' in synth
assert 'GLIDE MODE' in synth and 'synthPanel_${i}_voice' in synth
assert 'v701MasterVoice' in synth and 'Master' in synth
assert 'studio-fx-grid' in v and 'data-fx-power' in v
assert 'studio-fader' in v and 'Math.log10' in v and 'v701FaderRequests' in v
assert 'chooseFxSendGroup' in fx and 'state.fxSends' in fx
assert 'v701MainZones' in read('ui/index.html') and 'studio-racks.css' in read('ui/index.html')
assert 'organFxSends' in cpp and 'organFxSends' in _stage_app_source() and 'organFxSends' in types
assert 'setOrganFxSend' in engine or 'setOrganFxSend' in read('src/modules/StagePianoEngine.h')
assert 'v701LocalOrganMixer' in v and 'organSend' in v
assert 'v701BuildPianoLayerMixers' in v
assert 'none_of(item.voices.begin()' in read('src/modules/StageSynthModule.cpp')
assert 'pianoSignal' in engine and 'synthSignal' in engine and 'organSignal' in engine
print('PASS v9 static: split notes, local/master independent FX, engine persistence, faders, idle bypass')
