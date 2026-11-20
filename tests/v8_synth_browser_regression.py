"""Actual CSS hit-testing + complete UI scripts with in-memory fake API.
No claim of Windows hardware integration: use production markup and JS/CSS,
mock only the back-end. Run: python tests/v7_synth_browser_regression.py
"""
from pathlib import Path
from playwright.sync_api import sync_playwright
import json, re

root=Path(__file__).resolve().parents[1]
ui=root/'ui'
html=(ui/'synth.html').read_text(encoding='utf-8')
for _ in range(4):
    html=re.sub(r'<div data-include="([^"]+)"></div>',
        lambda m:(ui/m[1].lstrip('/')).read_text(encoding='utf-8')
                 if (ui/m[1].lstrip('/')).is_file() else '',html)
html=re.sub(r'<script\b[^>]*src="[^"]+"[^>]*></script>','',html)
html=re.sub(r'<link\b[^>]*>','',html)
html=html.replace('</head>',''.join('<style>'+x.read_text(encoding='utf-8')+'</style>'
    for x in (ui/'css').glob('*.css'))+'</head>')
params={'synthPresetId':1,'synthEnabled':False,'synthMasterVol':0.8,
        'synthLayers':[{'enabled':True,'waveType':0,'volume':0.7,'filterType':0,'osc2Wave':0} for _ in range(3)],
        'pianoLayers':[{'enabled':i==0,'volume':1.0} for i in range(5)],
        'eqBands':[{'freqHz':f,'q':1.0,'gainDb':0.0} for f in (60,250,1000,4000,12000)],
        'maxVoices':128}
scripts=['core','include','knobs','eq','fx','fx-sends','programs','library',
         'synth','organ','keyboard','sync','polling','icons','dashboard','app']
with sync_playwright() as p:
    browser=p.chromium.launch(headless=True,executable_path='/usr/bin/chromium',args=['--no-sandbox'])
    for width in (1600,1280,950,800,600):
        page=browser.new_page(viewport={'width':width,'height':900})
        errors=[]
        page.on('pageerror',lambda e:errors.append(str(e)))
        page.set_content(html,wait_until='domcontentloaded')
        page.evaluate('''() => {
          Object.defineProperty(window,'localStorage',{configurable:true,value:{getItem:()=> '1',setItem:()=>{}}});
          window.__requests=[];window.__rev=1;
          window.fetch=async(url,opts)=>{
            if(url.startsWith('/partials/'))return {ok:true,text:async()=>''};
            if(url==='/api/synth/presets'||url==='/api/organ/presets')return {ok:true,json:async()=>[]};
            if(url==='/api/synth/factory/status')return {ok:true,json:async()=>({
              ok:true,apiVersion:3,build:'Live Stage V8 / Factory API 3',maxFactoryPresets:120,
              presetId:window.__params.synthPresetId,synthEnabled:window.__params.synthEnabled,
              synthMasterVol:window.__params.synthMasterVol,enabledLayers:2})};
            if(url==='/api/state')return {ok:true,json:async()=>({paramRev:window.__rev,
              instrumentId:0,activeVoices:0,params:JSON.parse(JSON.stringify(window.__params))})};
            if(url==='/api/synth/factory/load'){
              const request=JSON.parse(opts.body);
              window.__requests.push(request);
              window.__params.synthPresetId=request.id;
              window.__params.synthEnabled=true;window.__rev++;
              return {ok:true,json:async()=>({ok:true,presetId:request.id,
                params:JSON.parse(JSON.stringify(window.__params))})};
            }
            if(url==='/api/param')return {ok:true,json:async()=>({ok:true})};
            return {ok:true,json:async()=>({})};
          };
        }''')
        page.evaluate('p=>{window.__params=p}',params)
        for stem in scripts:page.add_script_tag(content=(ui/'js'/f'{stem}.js').read_text(encoding='utf-8'))
        page.evaluate("document.dispatchEvent(new Event('DOMContentLoaded'))")
        page.wait_for_timeout(250)
        buttons=page.locator('#synthPresetButtons button')
        assert buttons.count()==120,(width,'not all factory sounds loaded')
        rect=page.locator('#synthPresetButtons').bounding_box()
        assert rect['width']>width*0.50,(width,'grid collapsed',rect)
        for id in (49,85,120):
            page.locator(f'#synthPreBtn_{id}').click(timeout=3000)
            page.wait_for_function('(id)=>window.__params.synthPresetId===id',arg=id,timeout=2000)
            assert page.locator('#synthScreenNumber').inner_text()==str(id).zfill(3)
            assert page.locator('#synthPatchStatus').inner_text().startswith('Motor confirmado')
        page.locator('#synthFactoryDirectSelect').select_option('97')
        page.wait_for_function('window.__params.synthPresetId===97')
        page.locator('#synthFactoryCategory').select_option('BRASS')
        assert page.locator('#synthPresetButtons button').count()>=12
        page.locator('#synthFactoryCategory').select_option('ALL')
        assert page.locator('#synthVoiceMode_2').count()==1
        page.locator('#synthVoiceMode_2').click()
        assert page.locator('#synthVoiceMode_2').get_attribute('class').find('active')>=0
        if width==1600:
            page.screenshot(path='/mnt/data/Live_Stage_V8_Synth_Interface.png',full_page=True)
        assert not errors,(width,errors)
        print(f'PASS {width}px: 120 factory patches, click 49/85/120, native selector 97, BRASS filter')
        page.close()
    browser.close()
