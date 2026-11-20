"""MAIN LCD / organ modal browser regression, using local HTML and mocked engine state."""
from pathlib import Path
from playwright.sync_api import sync_playwright
import re, json
root=Path(__file__).resolve().parents[1];ui=root/'ui'
scripts=['core','include','knobs','eq','fx','fx-sends','programs','library','piano-sample-editor','piano',
         'synth','organ','keyboard','sync','polling','icons','dashboard','app']
def get_html(name):
 html=(ui/name).read_text(encoding='utf8')
 for _ in range(3):
  html=re.sub(r'<div data-include="([^"]+)"></div>',lambda m:(ui/m[1].lstrip('/')).read_text(encoding='utf8') if (ui/m[1].lstrip('/')).is_file() else '',html)
 html=re.sub(r'<script\b[^>]*src="[^"]+"[^>]*></script>','',html)
 html=re.sub(r'<link\b[^>]*>','',html)
 return html.replace('</head>',''.join('<style>'+f.read_text(encoding='utf8')+'</style>' for f in (ui/'css').glob('*.css'))+'</head>')
with sync_playwright() as p:
 b=p.chromium.launch(headless=True,executable_path='/usr/bin/chromium',args=['--no-sandbox'])
 for width in [1600,900,600]:
  page=b.new_page(viewport={'width':width,'height':1000})
  errors=[];page.on('pageerror',lambda e:errors.append(str(e)))
  page.set_content(get_html('index.html'),wait_until='domcontentloaded')
  page.evaluate('''() => {
    Object.defineProperty(window,'localStorage',{configurable:true,value:{getItem:()=> '1',setItem:()=>{}}});
    window.fetch=async(url,opts)=> {
      if(url.startsWith('/partials/'))return {ok:true,text:async()=>''};
      if(url==='/api/programs'||url==='/api/synth/presets'||url==='/api/organ/presets')return {ok:true,json:async()=>[]};
      if(url==='/api/state')return {ok:true,json:async()=>({paramRev:0,instrumentId:0,activeVoices:0,pianoLayers:[],programName:'DEMO',params:null})};
      return {ok:true,json:async()=>({ok:true})};
    };
  }''')
  for stem in scripts:page.add_script_tag(content=(ui/'js'/f'{stem}.js').read_text(encoding='utf8'))
  page.evaluate("document.dispatchEvent(new Event('DOMContentLoaded'))")
  page.wait_for_timeout(180)
  page.evaluate('''() => {
    state.pianoEnabled=true;state.synthEnabled=false;state.organEnabled=false;
    state.activeProgramSlot=3;
    state.pianoLayers=Array.from({length:5},(_,i)=>({enabled:i<2,volume:1}));
    state.pianoLayerSources=[{name:'Concert Grand',sourcePath:'grand.npno'},{name:'Layered Piano',sourcePath:'layer.npno'},
      ...Array.from({length:3},()=>({name:'VACÍA',sourcePath:''}))];
    document.getElementById('lcdProgramName').textContent='Worship Stage 01';
    updateMainDashboard();
  }''')
  assert page.locator('#lcdProgramName').inner_text()=='Worship Stage 01'
  assert page.locator('#lcdPianoPreset').inner_text().startswith('Concert Grand') and '+1 LAYER' in page.locator('#lcdPianoPreset').inner_text()
  assert page.locator('#lcdVoicePiano').is_visible()
  assert not page.locator('#lcdVoiceSynth').is_visible()
  assert not page.locator('#lcdVoiceOrgan').is_visible()
  page.evaluate('''() => {state.synthEnabled=true;state.organEnabled=true;state.organModel=3;state.synthPresetId=25;updateMainDashboard()}''')
  assert page.locator('#lcdVoiceSynth').is_visible()
  assert page.locator('#lcdVoiceOrgan').is_visible()
  assert 'Classic Poly Brass' in page.locator('#lcdSynthPreset').inner_text()
  assert 'PIPE ORGAN' in page.locator('#lcdOrganPreset').inner_text()
  assert page.locator('.lcd-instrument svg').count()==3
  if width==1600:page.screenshot(path='/mnt/data/Live_Stage_V7_Preview_MAIN.png',full_page=True)
  assert not errors,errors
  page.close()
  page=b.new_page(viewport={'width':width,'height':900})
  errors=[];page.on('pageerror',lambda e:errors.append(str(e)))
  page.set_content(get_html('organ.html'),wait_until='domcontentloaded')
  page.evaluate('''() => {
    Object.defineProperty(window,'localStorage',{configurable:true,value:{getItem:()=> '1',setItem:()=>{}}});
    window.fetch=async(url,opts)=>{if(url.startsWith('/partials/'))return {ok:true,text:async()=>''};
      if(url==='/api/organ/presets'||url==='/api/synth/presets'||url==='/api/programs')return {ok:true,json:async()=>[]};
      if(url==='/api/state')return {ok:true,json:async()=>({paramRev:0,instrumentId:0,activeVoices:0,params:null})};
      return {ok:true,json:async()=>({ok:true})};};
  }''')
  for stem in scripts:page.add_script_tag(content=(ui/'js'/f'{stem}.js').read_text(encoding='utf8'))
  page.evaluate("document.dispatchEvent(new Event('DOMContentLoaded'))")
  page.wait_for_timeout(180)
  page.evaluate("openModal('modalOrganEffects')")
  assert page.locator('[data-id="organChorusWidth"] svg.knob-svg').count()==1
  assert page.locator('[data-id="organChorusVoices"] svg.knob-svg').count()==1
  assert page.locator('[data-id="organChorusTone"] svg.knob-svg').count()==1
  page.locator('#led_organChorusEnabled').click()
  assert page.locator('#led_organChorusEnabled').evaluate('(el)=>el.classList.contains("on")')
  if width==1600:page.screenshot(path='/mnt/data/Live_Stage_V7_Preview_OrganFX.png',full_page=True)
  assert not errors,errors
  page.close()
  print('PASS main/organ browser',width)
 b.close()
