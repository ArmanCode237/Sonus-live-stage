#!/usr/bin/env python3
# main.cpp was split into src/app/** (StageApp + route groups): contract checks
# read the whole application layer instead of the single former file.
def _stage_app_source():
    import pathlib as _p
    root = _p.Path(__file__).resolve().parents[1]
    files = [root / 'src/main.cpp', *sorted((root / 'src/app').rglob('*.h')), *sorted((root / 'src/app').rglob('*.cpp'))]
    return chr(10).join(f.read_text(encoding='utf-8') for f in files)
"""v28.2 static compatibility contract; does not replace a running HTTP integration test."""
from pathlib import Path
from html.parser import HTMLParser
root = Path(__file__).resolve().parents[1]
main = _stage_app_source()
page = (root/'ui/library-manager.html').read_text(encoding='utf-8')
js = (root/'ui/js/library-manager.js').read_text(encoding='utf-8')
class IdParser(HTMLParser):
    def __init__(self): super().__init__();self.ids=[]
    def handle_starttag(self,tag,attrs):
        for k,v in attrs:
            if k=='id': self.ids.append(v)
parsed=IdParser();parsed.feed(page)
assert len(parsed.ids)==len(set(parsed.ids)), 'Duplicate DOM id'
for element in ('soundModule','soundName','saveSound','saveSoundAs','soundList','migratePiano',
                'oldRoot','relocateToRoot','relinkLibrary','relinkResult'):
    assert element in parsed.ids,element
for uri in ('/api/sounds','/api/sounds/save','/api/sounds/load','/api/sounds/delete',
            '/api/sounds/duplicate','/api/sounds/migrate','/api/sample-library/relocate'):
    assert uri in main and uri in js,uri
for field in ('pianoSources','studioRack','pianoSampleConfig','synthPortamento',
              'organFx','pianoLayerZones'):
    assert field in main,field
assert 'selectedSoundRefs' in main and 'soundRefs' in main
assert 'legacyModule' in main and 'soundLibrary' in main
assert 'saveJsonAtomic(soundsFilePath' in main
assert 'std::vector<float> audioL' not in (root/'src/core/NordTypes.h').read_text()
print('v28.2 UI routes, DOM, persistence, module patch and backwards compatibility: STATIC PASS')
