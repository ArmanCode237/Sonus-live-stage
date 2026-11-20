"""UI regression: python tests/v111_browser_ui.py (requires Playwright+Chromium).
Inlines frontend partials and fakes only the HTTP transport; loads real frontend JS.
"""
from pathlib import Path
from playwright.sync_api import sync_playwright
import re

ROOT=Path(__file__).resolve().parents[1]
UI=ROOT/'ui'
def build_html(name):
    html=(UI/name).read_text(encoding='utf8')
    for _ in range(5):
        html=re.sub(r'<div data-include="([^"\n]+)"></div>',
            lambda m: (UI/m.group(1).split('?',1)[0].lstrip('/')).read_text(encoding='utf8'),html)
    scripts=re.findall(r'<script\b[^>]*src="/js/([^"?]+)[^"]*"[^>]*></script>',html)
    html=re.sub(r'<script\b[^>]*src="/js/[^"?]+[^"]*"[^>]*></script>','',html)
    html=re.sub(r'<link\b[^>]*>','',html)
    css='\n'.join(f.read_text(encoding='utf8') for f in (UI/'css').glob('*.css'))
    html=html.replace('</head>','<style>'+css+'</style></head>')
    return html,scripts

with sync_playwright() as p:
    browser=p.chromium.launch(headless=True,executable_path='/usr/bin/chromium',args=['--no-sandbox'])
    for name in ['index.html','synth.html','organ.html']:
        page=browser.new_page(viewport={'width':1440,'height':960},device_scale_factor=1)
        page.set_default_timeout(4000)
        print('START',name,flush=True)
        errs=[]
        page.on('pageerror',lambda e:errs.append(str(e)))
        html,scripts=build_html(name)
        page.set_content(html,wait_until='domcontentloaded')
        page.evaluate('''() => {
          window.__calls=[];
          Object.defineProperty(window,'localStorage',{configurable:true,value:{getItem:()=> '1',setItem:()=>{}}});
          const racks=[5,4,1,3].map(count=>({eqEnabled:true,eqDb:[0,0,0,0,0],eqFreq:[60,250,1000,4000,12000],eqQ:[.9,1,1,1,.9],
            eqHighPass:20,eqLowPass:20000,eqInputDb:0,eqOutputDb:0,eqPosition:0,
            slots:Array.from({length:5},()=>({type:0,enabled:false,params:Array(16).fill(.5)})),
            sends:Array.from({length:count},()=>[0,0,0,0,0])}));
          window.fetch=async(url,opts={})=>{
            window.__calls.push({url,body:opts.body});
            let json={ok:true};
            if(url==='/api/studio-racks')json={ok:true,racks,mode:true};
            else if(url==='/api/synth/factory/status')json={ok:true,apiVersion:5,maxFactoryPresets:256,presetId:1,build:'TEST',enabledLayers:4,synthEnabled:true};
            else if(url==='/api/library')json={ok:true,items:[]};
            else if(['/api/programs','/api/synth/presets','/api/organ/presets'].includes(url))json=[];
            else if(url==='/api/state')json={paramRev:0,instrumentId:0,activeVoices:0,pianoLayers:[],programName:'DEMO',params:null};
            return {ok:true,status:200,json:async()=>json};
          };
        }''')
        for f in scripts:page.add_script_tag(content=(UI/'js'/f).read_text(encoding='utf8'))
        page.evaluate("document.dispatchEvent(new Event('DOMContentLoaded'))")
        page.wait_for_timeout(350)
        assert page.locator('#modalStudioRack.studio-rack-inline').count()==1,('not mounted',name)
        assert page.locator('.studio-subtabs button').count()==2
        print('FX switch',name,flush=True)
        page.locator('#workspaceTab_fx').click()
        page.wait_for_timeout(100)
        assert page.locator('#studioEffectsPane').is_visible()
        assert page.locator('#studioSlotUnits .studio-eq-unit').count()==1
        assert page.locator('#studioSlotUnits .studio-eq-band').count()==5
        assert page.locator('#studioSlotUnits .studio-slot-unit').count()==5
        print('SENDS switch',name,flush=True)
        if name=='synth.html':page.screenshot(path='/tmp/stage_fx_preview.png')
        page.locator('#studioSendsTab').click()
        if name=='synth.html':page.screenshot(path='/tmp/stage_sends_preview.png')
        assert page.locator('#studioSendsPane').is_visible()
        assert not page.locator('#studioEffectsPane').is_visible()
        page.evaluate('studioRackSelect(1)')
        assert page.locator('#studioSendsPane .studio-mixer-channel').count()==4
        assert page.locator('#studioSendsPane input[type=range]').count()==20
        assert page.locator('#studioSendsPane input[type=range]').first.is_enabled()
        vis=page.locator('#studioSendsPane input[type=range]').first.evaluate('''el=>{
          const r=el.getBoundingClientRect(),style=getComputedStyle(el);
          const target=document.elementFromPoint(r.left+r.width/2,r.top+r.height/2);
          return {w:r.width,h:r.height,writing:style.writingMode,cover:!(target===el||el.contains(target))};
        }''')
        assert vis['w']>60 and vis['h']>=20 and not vis['cover'],(name,vis)
        # Exercise user edit (real callbacks against fake API transport).
        print('SLIDER test',name,flush=True)
        page.locator('#studioSendsPane input[type=range]').first.fill('0.4')
        page.locator('#studioEffectsTab').click()
        page.wait_for_timeout(100)
        assert page.locator('#studioSlotUnits .studio-eq-unit').is_visible()
        page.evaluate('studioEqMove(1,1)')
        page.wait_for_timeout(100)
        assert 'POSITION 2 / 6' in page.locator('.studio-eq-brand').inner_text()
        if name=='synth.html':
            page.locator('#workspaceTab_presets').click()
            assert page.locator('.synth-layer-card').count()==4
            assert page.locator('.synth-seq-panel').count()==4
            assert page.locator('#synthPresetButtons button').count()==256
            page.locator('#synthFactorySearch').fill('Prophet')
            assert page.locator('#synthPresetButtons button').count()>0
            assert page.locator('#synthPresetButtons button').count()<256
            page.locator('#synthFactorySearch').fill('')
            page.locator('.synth-category-pill[data-family="BASS"]').click()
            assert page.locator('#synthPresetButtons button').count()>0
            assert page.locator('#synthPresetButtons button').count()<256
            page.locator('.synth-category-pill[data-family="ALL"]').click()
            assert page.locator('#synthPresetButtons button').count()==256
        assert not errs,(name,errs)
        print(f'PASS {name}: studio subtabs, 5-band EQ, 4 synth sends, unblocked sliders, presets, {len(page.evaluate("window.__calls"))} HTTP calls')
        page.close()
    browser.close()
