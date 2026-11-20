"""Static safety contracts; runtime HTTP and audio tests run separately."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
engine=(root/'webui/src/store/engine.ts').read_text()
studio=(root/'webui/src/store/studio.ts').read_text()
notify=(root/'webui/src/store/notify.ts').read_text()
views=list((root/'webui/src/views').rglob('*.tsx'))
all_views='\n'.join(p.read_text() for p in views)
host=(root/'src/core/WorkstationHost.cpp').read_text()
assert 'beginEditTransaction(replaceEditBuffer)' in notify
assert 'beginStudioEditTransaction(replaceEditBuffer)' in notify
assert 'await previous' in notify and 'releaseQueue()' in notify
assert 'this.activeWrites' in engine and 'this.stateEpoch' in engine
assert 'commandsInFlight' in studio and 'writesSuspended' in studio
for operation in ['api.loadSynthFactory(', 'api.loadOrganPreset(', 'api.loadSample(', 'api.loadSound(']:
    assert operation in all_views
synth=host[host.index('void WorkstationHost::applySynthPresetJson('):host.index('void WorkstationHost::applyOrganPresetJson(')]
organ=host[host.index('void WorkstationHost::applyOrganPresetJson('):host.index('bool WorkstationHost::loadOrganSample(')]
assert 'c = SynthLayerConfig{};' in synth
assert 'const SynthPresetParams presetDefaults{}' in synth
assert 'const ModuleFxConfig defaultFx{}' in organ
assert 'organModule_.drawbars = defaults.drawbars;' in organ
assert 'setModuleFxParam(true,k,value)' in synth
assert 'setModuleFxParam(false,i,next)' in organ
assert 'p.value("organEnabled", organModule_.organEnabled)' not in organ
assert 'p.value("synthMacroCutoff", multiSynth_.synthMacroCutoff)' not in synth
css=(root/'webui/src/styles/hardware-console.css').read_text()
assert '--lcd-bg' in css and '--red' in css and '.knob__cap' in css
print('PASS frontend transaction + backend preset defaults + hardware UI static contracts')
