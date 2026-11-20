"""Live Stage V30 React-only source & 8-program integration contract.
This replaces legacy checks that referenced removed ui/js/*.js files.
Does not substitute for running the C++ realtime audio engine on target hardware.
"""
from pathlib import Path
import json
import re
root = Path(__file__).resolve().parents[1]

def read(path):
    return (root / path).read_text(encoding='utf-8')

# Global bank and legacy migration are explicitly 8-slot only.
programs = json.loads(read('programs.json'))
assert len(programs) == 8 and isinstance(programs, list)
assert [p['slot'] for p in programs] == list(range(1, 9))
assert all(isinstance(p.get('params'), dict) and p.get('name') for p in programs)
assert 'kProgramSlots = 8' in read('src/app/StageApp.h')
assert 'programs_16slots_backup.json' in read('src/app/StageApp.cpp')
assert 'std::clamp(body.value("slot", 0), 0, kProgramSlots - 1)' in read('src/app/routes/ProgramRoutes.cpp')
assert 'PROGRAM_SLOTS = 8' in read('webui/src/data/constants.ts')
program_view = read('webui/src/views/shared/Programs.tsx')
assert 'Array.from({ length: PROGRAM_SLOTS }' in program_view
assert 'aria-pressed' in program_view and 'api.recallProgram' in program_view and 'api.storeProgram' in program_view
skin = read('webui/src/styles/pro-console.css')
assert re.search(r'\.pads\s*\{[^}]*grid-template-columns:repeat\(4', skin)
assert re.search(r'\.pads\s*\{[^}]*grid-template-rows:repeat\(2', skin)

# Single React surface, plus React router deep links served by C++.
assert (root/'ui/app/index.html').is_file()
assert (root/'ui/app/assets/app.js').is_file()
assert (root/'ui/app/assets/app.css').is_file()
assert not (root/'ui/index.html').exists()
assert not (root/'ui/js').exists()
assert not (root/'ui/css').exists()
assert 'set_mount_point("/app", reactAppDir)' in read('src/app/routes/PageRoutes.cpp')
assert 'install(DIRECTORY ui/app/ DESTINATION ui/app)' in read('CMakeLists.txt')
assert 'RealtimeLink' in read('webui/src/api/realtime.ts')
assert 'applyParamById' in read('src/app/routes/RealtimeRoutes.cpp')

# All pages share shell, controls and theme tokens.
for view in ['main/MainView', 'piano/PianoView', 'synth/SynthView', 'organ/OrganView',
             'fx/FxView', 'zones/ZonesView', 'library/LibraryView', 'settings/SettingsView']:
    assert (root/'webui/src/views'/f'{view}.tsx').is_file(), view
assert 'ParamKnob' in read('webui/src/ui/Knob.tsx')
assert 'ParamFader' in read('webui/src/ui/controls.tsx')
assert 'useParam' in read('webui/src/store/hooks.ts')
assert 'pro-console.css' in read('webui/src/main.tsx')

# Existing synth and racks are not replaced by frontend simulations.
synth = read('src/modules/StageSynthModule.cpp')
registry = read('src/core/WorkstationHost.cpp')
for name in ['sawLevel','pulseLevel','highpassHz','drive']:
    assert name in synth and name in registry, name
assert 'studioSet' in read('webui/src/views/fx/FxView.tsx')
print('PASS: React-only GUI, eight programs (2×4), DSP parameter bridge and all eight views')
