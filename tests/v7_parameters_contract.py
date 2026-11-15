# main.cpp was split into src/app/** (StageApp + route groups): contract checks
# read the whole application layer instead of the single former file.
def _stage_app_source():
    import pathlib as _p
    root = _p.Path(__file__).resolve().parents[1]
    files = [root / 'src/main.cpp', *sorted((root / 'src/app').rglob('*.h')), *sorted((root / 'src/app').rglob('*.cpp'))]
    return chr(10).join(f.read_text(encoding='utf-8') for f in files)
"""No external deps: verify UI/DSP/server/JSON bindings are complete."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
read=lambda n:(root/n).read_text(encoding='utf8')
registry=read('src/core/WorkstationHost.cpp');src=_stage_app_source()
params=read('src/core/NordTypes.h');piano=read('src/modules/NordPianoEngine.cpp')
organ=read('src/modules/NordOrganModule.cpp')
ui=read('ui/partials/modal-fxrack.html')+read('ui/partials/modal-organ-effects.html')
sync=read('ui/js/sync.js')
common=['chorusVoices','chorusWidth','chorusFeedback','chorusTone','chorusDelay']
private=['organChorusEnabled','organChorusRate','organChorusDepth','organChorusMix','organChorusWidth',
         'organChorusFeedback','organChorusTone','organChorusVoices']
for n in common+private:
 for filename,content in [('struct',params),('registry',registry),('serialize',src),('engine',piano),('UI',ui),('sync',sync)]:
  assert n in content,(n,filename)
for n in ['chorusEnabled','chorusRate','chorusDepth','chorusMix','chorusWidth','chorusFeedback','chorusTone','chorusVoices']:
 assert f'"{n}"' in registry
 assert n in organ
assert read('ui/index.html').count('lcdVoicePiano')==1
assert read('ui/index.html').count('lcdVoiceSynth')==1
assert read('ui/index.html').count('lcdVoiceOrgan')==1
assert 'programName' in _stage_app_source()
print('PASS: 13 new parameters persisted + organ patch + UI + 3 LCD instrument rows')
