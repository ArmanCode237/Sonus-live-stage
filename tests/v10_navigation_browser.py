from pathlib import Path
from playwright.sync_api import sync_playwright
import re, json
root=Path(__file__).resolve().parents[1];ui=root/'ui'
html_cache={}
def page_html(name):
 s=(ui/name).read_text(encoding='utf8')
 for _ in range(4):
  s=re.sub(r'<div data-include="([^"\n]+)"></div>',lambda m:(ui/m[1].lstrip('/')).read_text(encoding='utf8') if (ui/m[1].lstrip('/')).exists() else '',s)
 files=re.findall(r'<script\b[^>]*src="/js/([^"?]+)[^"]*"[^>]*></script>',s)
 s=re.sub(r'<script\b[^>]*src="/js/[^"?]+[^"]*"[^>]*></script>','',s)
 s=re.sub(r'<link\b[^>]*>','',s)
 css='\n'.join(p.read_text(encoding='utf8') for p in (ui/'css').glob('*.css') if p.name not in ('settings.css','config-v10.css'))
 s=s.replace('</head>','<style>'+css+'</style></head>')
 return s,files
samples=[
  {'path':'/samples/grand.npno','name':'Concert Grand','group':'Acoustic Pianos','ext':'.npno','sizeMB':10,'favorite':False},
  {'path':'/samples/mellow.npno','name':'Mellow Grand','group':'Acoustic Pianos','ext':'.npno','sizeMB':10,'favorite':False},
  {'path':'/samples/ep.npno','name':'Rhodes EP','group':'Electric Pianos','ext':'.npno','sizeMB':10,'favorite':False},
  {'path':'/samples/pad.nsmp','name':'Warm Pad','group':'Pads & Atmospheres','ext':'.nsmp','sizeMB':5,'favorite':False},
]
with sync_playwright() as p:
 browser=p.chromium.launch(headless=True,executable_path='/usr/bin/chromium',args=['--no-sandbox'])
 for name,pageType,wantTabs in [('index.html','main',4),('synth.html','synth',5),('organ.html','organ',5)]:
  page=browser.new_page(viewport={'width':1440,'height':940},device_scale_factor=1)
  errors=[];page.on('pageerror',lambda exc:errors.append(str(exc)))
  html,scripts=page_html(name)
  page.set_content(html,wait_until='domcontentloaded')
  page.evaluate('''items => {
    Object.defineProperty(window,'localStorage',{configurable:true,value:{getItem:()=> '1',setItem:()=>{}}});
    window.__calls=[];
    window.fetch=async(url,opts={})=>{
      window.__calls.push({url,body:opts.body});
      if(url.startsWith('/api/library'))return {ok:true,json:async()=>({items})};
      if(url==='/api/programs'||url==='/api/synth/presets'||url==='/api/organ/presets')return {ok:true,json:async()=>[]};
      if(url==='/api/synth/factory/status')return {ok:true,json:async()=>({ok:true,apiVersion:4,maxFactoryPresets:200,presetId:1,build:'TEST',enabledLayers:2,synthEnabled:true})};
      if(url==='/api/state')return {ok:true,json:async()=>({paramRev:0,instrumentId:0,activeVoices:0,pianoLayers:[],programName:'DEMO',params:null})};
      if(url==='/api/load')return {ok:true,json:async()=>({ok:true,params:null,pianoLayers:[],sourcePath:JSON.parse(opts.body).path,instrumentName:'Test Sample'})};
      return {ok:true,json:async()=>({ok:true})};
    };
  }''',samples)
  for f in scripts:page.add_script_tag(content=(ui/'js'/f).read_text(encoding='utf8'))
  page.evaluate("document.dispatchEvent(new Event('DOMContentLoaded'))")
  page.wait_for_timeout(350)
  count=page.locator('#workspaceTabs button').count()
  print(pageType,'tabs',count, 'errors',errors[:3])
  assert count==wantTabs,(pageType,count)
  if pageType=='main':
    assert page.locator('#workspacePane_sounds').is_visible()
    page.evaluate('''() => {
      state.pianoLayerSources[0]={name:'Concert Grand',sourcePath:'/samples/grand.npno'};
      state.pianoLayerSources[1]={name:'Rhodes EP',sourcePath:'/samples/ep.npno'};
      updatePianoLayerControls();selectPianoLayer(1);
    }''')
    assert page.locator('#quickCatSelect').input_value()=='Electric Pianos'
    assert page.locator('#modelIndexName').inner_text()=='Rhodes EP'
    page.evaluate('selectPianoLayer(0)');assert page.locator('#quickCatSelect').input_value()=='Acoustic Pianos'
    page.evaluate('''() => {state.synthPresetId=19;showSynthPreset(19);}''')
    assert '019' in page.locator('#synthPresetNumberMain').inner_text()
    page.locator('#workspaceTab_fx').click();assert page.locator('#workspacePane_fx').is_visible();assert not page.locator('#workspacePane_sounds').is_visible()
    page.locator('#workspaceTab_zones').click();assert page.locator('#v701MainZones').is_visible()
    page.locator('#workspaceTab_sounds').click();page.evaluate('openMainSynthBrowser()');assert page.locator('#mainSynthBrowserList button').count()==200
    page.evaluate("closeModal('modalSynthBrowserMain')")
    page.screenshot(path='/mnt/data/Live_Stage_V10_Main_preview.png',full_page=False)
  elif pageType=='synth':
    assert page.locator('#workspacePane_presets').is_visible()
    page.locator('#workspaceTab_edit').click();assert page.locator('.synth-layer-card').count()==3
    page.locator('#workspaceTab_zones').click();assert page.locator('#workspacePane_zones .v701-zone-notes').count()==4
    page.locator('#workspaceTab_fx').click();assert page.locator('#workspacePane_fx .studio-fx-unit').count()==8
    page.locator('#workspaceTab_module').click();assert page.locator('#synthGlobalControls').is_visible()
    page.screenshot(path='/mnt/data/Live_Stage_V10_Synth_preview.png',full_page=False)
  else:
    assert page.locator('#workspacePane_presets').is_visible()
    assert page.locator('#organPresetModelGrid button').count()==8
    page.locator('#organPresetModelGrid [data-organ-model="7"]').click()
    assert page.evaluate('state.organModel')==7
    page.locator('#workspaceTab_fx').click();assert page.locator('#workspacePane_fx .studio-fx-unit').count()==8
    page.locator('#workspaceTab_edit').click();assert page.locator('#organDrawbarsDeck .drawbar-column').count()==9
    page.screenshot(path='/mnt/data/Live_Stage_V10_Organ_preview.png',full_page=False)
  assert not errors,(pageType,errors)
  print('PASS',pageType)
  page.close()
 browser.close()
