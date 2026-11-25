#!/usr/bin/env python3
# main.cpp was split into src/app/** (StageApp + route groups): contract checks
# read the whole application layer instead of the single former file.
def _stage_app_source():
    import pathlib as _p
    root = _p.Path(__file__).resolve().parents[1]
    files = [root / 'src/main.cpp', *sorted((root / 'src/app').rglob('*.h')), *sorted((root / 'src/app').rglob('*.cpp'))]
    return chr(10).join(f.read_text(encoding='utf-8') for f in files)
"""V28.2.1 static contract; no assumption of integrated host compilation."""
from pathlib import Path
from html.parser import HTMLParser
root=Path(__file__).resolve().parents[1]
main=_stage_app_source()
libjs=(root/'ui/js/library-manager.js').read_text()
programs=(root/'ui/js/programs.js').read_text()
presets=(root/'ui/js/presets-v17.js').read_text()
class Ids(HTMLParser):
 def __init__(self):super().__init__();self.ids=[]
 def handle_starttag(self,tag,attrs):
  self.ids.extend(value for key,value in attrs if key=='id')
ids=Ids();ids.feed((root/'ui/library-manager.html').read_text())
assert len(ids.ids)==len(set(ids.ids)),'duplicate ids in library'
assert 'soundLayer' in ids.ids
assert 'scope' in main and 'savedLayer' in main and 'piano-layer-v1' in main
assert 'capturePianoLayerSound' in main and 'restorePianoLayerSound' in main
assert '"sampleConfig",all["pianoSampleConfig"][layer]' in main
assert '"fxSends",all["fxSends"][layer]' in main
assert '"studioAux",all["studioRacks"][0]["sends"][layer]' in main
assert 'cfg.sends[layer][i]=std::clamp' in main
assert '"source",sources[layer]' in main
assert 'merged["pianoSampleConfig"][layer]=patch["sampleConfig"]' in main
assert 'merged["fxSends"][layer]=patch["fxSends"]' in main
assert 'engine.setPianoLayerSampleParam(layer,k,v[k])' in main
assert 'engine.setPianoWheelRoute(layer,0,c.wheelCutoffOctaves)' in main
assert 'engine.setSamplePortamento(layer,1,c.portamentoSec)' in main
assert 'engine.setFxSend(layer,0,p.fxSends[layer].modulation)' in main
assert 'json::array({nullptr,nullptr,nullptr,nullptr,nullptr})' in main
assert 'getLayerInstrument(soundLayer)' in main
assert 'scope' in libjs and 'targetLayer()' in libjs and 'soundLayer' in libjs
assert 'sound.value("scope",std::string{})=="layer"' in main
assert 'selectedSoundRefs["pianoLayers"][layer]=id' in main
assert 'selectedSoundRefs["pianoLayers"][soundLayer]=id' in main
assert 'stageProgramMarkDirty' in programs and 'stageProgramMarkClean' in programs
assert 'window.stageOnProgramTransition?.()' in programs
assert "dialog('CAMBIOS SIN GUARDAR'" not in presets
assert 'async function confirmChanges(module){return !busy;}' in presets
assert '* MODIFICADO' in programs and '* MODIFICADO' in presets
for i in range(5):
 assert f'id="pianoLayerName{i}"' in (root/'ui/index.html').read_text()
 assert f'layer={i}' in (root/'ui/index.html').read_text()
for file in ('StagePianoEngine.cpp','BankLoader.cpp','NsmpLoader.cpp'):
 assert (root/'src/modules'/file if file=='StagePianoEngine.cpp' else root/'src/loaders'/file).is_file()
print('PASS: v28.2.1 per-layer Sound snapshots, DSP isolation, no modal, UI navigation, program refs')
