"""No external deps: verify UI/DSP/server/JSON bindings are complete."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
read=lambda n:(root/n).read_text(encoding='utf8')
registry=read('src/core/WorkstationHost.cpp');src=read('src/main.cpp')
params=read('src/core/Nordtypes.h');piano=read('src/modules/NordPianoEngine.cpp')
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
assert 'programName' in read('src/main.cpp')
print('PASS: 13 new parameters persisted + organ patch + UI + 3 LCD instrument rows')
