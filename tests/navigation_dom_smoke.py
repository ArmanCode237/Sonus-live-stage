"""Offline visual initialization smoke test. Runtime network forbidden in this harness;
all app partials, JavaScript and CSS are inlined for a self-contained Chromium run."""
import re
from pathlib import Path
from playwright.sync_api import sync_playwright
root=Path(__file__).resolve().parents[1]/'ui'

def assembled(name):
    html=(root/name).read_text()
    for _ in range(4):
        html,n=re.subn(r'<div data-include="/([^"]+)"></div>', lambda m:(root/m.group(1)).read_text(),html)
        if not n:break
    html=re.sub(r'<link[^>]+(?:fonts\.googleapis|fonts\.gstatic)[^>]*>', '', html)
    def link(m):
        css=(root/m.group(1)).read_text()
        return '<style>'+css+'</style>'
    html=re.sub(r'<link rel="stylesheet" href="/([^"]+)">',link,html)
    def js(m):
        code=(root/m.group(1)).read_text()
        code=code.replace("localStorage.getItem('stage_kb_vis')", "'1'")
        return '<script>'+code+'</script>'
    html=re.sub(r'<script src="/([^"]+)"></script>',js,html)
    return html

with sync_playwright() as p:
    b=p.chromium.launch(executable_path='/usr/bin/chromium',headless=True,args=['--no-sandbox'])
    for name in ['index.html','synth.html','organ.html','index.html']:
        page=b.new_page(viewport={'width':1440,'height':900})
        errors=[]
        page.on('pageerror',lambda e: errors.append(str(e)))
        page.set_content(assembled(name),wait_until='load')
        page.wait_for_timeout(250)
        knobs=page.locator('svg.knob-svg').count()
        keys=page.locator('#pianoRoll .white-key').count()
        draws=page.locator('.drawbar-column').count()
        fx=page.locator('.fx-send-row').count()
        print(name, 'knobs',knobs,'keys',keys,'drawbars',draws,'fx sources',fx,'errors',errors[-4:])
        assert fx == 9 and keys == 52
        if name=='index.html':assert knobs>40
        if name=='synth.html':assert knobs>30
        if name=='organ.html':assert draws == 9
        assert not errors, errors
        if name=='index.html':
            page.evaluate('selectPianoLayer(4)')
            assert page.evaluate('state.activePianoLayer') == 4
            page.evaluate('state.fxSends[4].reverb=0.3; refreshFxSends()')
            assert page.evaluate('state.fxSend4Reverb') == 0.3
        if name=='synth.html':
            page.evaluate("setSynthActivePanel(1,'filter')")
            assert page.locator('#synthPanel_1_filter').is_visible()
            assert not page.locator('#synthPanel_1_osc').is_visible()
        page.close()
    b.close()
