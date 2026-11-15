# main.cpp was split into src/app/** (StageApp + route groups): contract checks
# read the whole application layer instead of the single former file.
def _stage_app_source():
    import pathlib as _p
    root = _p.Path(__file__).resolve().parents[1]
    files = [root / 'src/main.cpp', *sorted((root / 'src/app').rglob('*.h')), *sorted((root / 'src/app').rglob('*.cpp'))]
    return chr(10).join(f.read_text(encoding='utf-8') for f in files)
"""Static UI contract. The redesigned physical controls keep the existing DSP IDs."""
from pathlib import Path
import re
root=Path(__file__).resolve().parents[1]
read=lambda p: (root/p).read_text(encoding='utf-8')
main=read('ui/index.html'); synth=read('ui/js/synth.js')
knobs=read('ui/js/knobs.js'); organ=read('ui/js/organ.js')
server=_stage_app_source(); panels=read('ui/synth.html')
assert 'set_mount_point("/", uiRootDir)' in server
for b in range(5): assert f'data-id="eqGain{b}"' in main
for i in range(3):
    assert f'data-id="pianoLayerVol{i}"' in main
    assert f'pianoLayerRow{i}' in main
for name in ('pianoEnabled','synthEnabled','organEnabled'):
    assert f"toggleEffect('{name}')" in main
assert 'mainOrganDrawbars' in main and 'buildMainOrganDrawbars' in organ
assert 'synthPanel_${i}_osc' in synth and 'synthPanel_${i}_filter' in synth
assert 'synthPanel_${i}_env' in synth and 'synthPanel_${i}_voice' in synth
assert 'updateSynthWavePreview' in synth and 'toggleSynthLayer' in synth
for key in ('saw','pulse','sub','noise','osc2','osc2Det','pw','pwmDepth','pwmRate','drive',
            'cut','res','hpf','filtEnv','fAtk','fDec','fSus','fRel','atk','dec','sus','rel',
            'unison','uniDet','uniSprd','wtIdx','pan','det','oct','vol','filtKT','velCut'):
    assert f"['{key}'" in synth, key
assert 'keydown' in knobs and 'aria-valuenow' in knobs
for f in ['ui/css/nord-hardware.css','ui/js/icons.js','ui/js/dashboard.js']:
    assert (root/f).is_file(),f
    assert f.split('ui/')[1] in main,f
assert 'lcdSampleName' in main and 'mainPianoMixValue' in main
assert 'id="programsGrid"' in main
assert 'id="synthPresetButtons"' in panels
print('Nord UI controls and parameter identity: OK')
