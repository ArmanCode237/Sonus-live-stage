"""V14 browser smoke tests with in-page mocked backend API (no live device).
Requires Playwright and local Chromium; runs without loopback network requests.
"""
from pathlib import Path
import os,re
from playwright.sync_api import sync_playwright
UI=Path(__file__).resolve().parents[1]/'ui'
CATALOG=['organRotaryFast','organRotaryFastRate','organRotaryRamp','organRotaryStereoWidth',
        'organRotaryMix','organPercEnabled','organVibratoMode','organChorusMix',
        'studioSend2_0_0','studioSend2_0_1','studioSend2_0_2','studioSend2_0_3','studioSend2_0_4',
        'pianoSample0Attack','synthVolume','masterVolume']

def build_page(filename):
 html=(UI/filename).read_text(encoding='utf8')
 for _ in range(6):
  def insert(m):
   path=m.group(1).split('?')[0].lstrip('/')
   src=UI/path
   return src.read_text(encoding='utf8') if src.exists() else ''
  prev=html
  html=re.sub(r'<div\s+data-include="([^"]+)"\s*></div>',insert,html)
  if prev==html:break
 scripts=re.findall(r'<script\s+[^>]*src="/js/([^"?]+)[^"]*"\s*></script>',html)
 html=re.sub(r'<script\s+[^>]*src="[^"]+"\s*></script>','',html)
 def embed_css(m):
  href=m.group(1).split('?')[0].lstrip('/')
  css=UI/href
  return '<style>\n'+css.read_text(encoding='utf8')+'\n</style>' if css.exists() else ''
 html=re.sub(r'<link\s+[^>]*href="(/css/[^"]+)"[^>]*>',embed_css,html)
 html=re.sub(r'<link\s+[^>]*>','',html)
 return html,scripts
MOCK_BACKEND=r'''(catalog)=>{
 Object.defineProperty(window,'localStorage',{configurable:true,value:{getItem:()=>null,setItem:()=>{},removeItem:()=>{}}});
 window.__calls=[];
 window.__racks=[5,4,1,3].map(n=>({
   eqEnabled:false,eqPosition:0,eqDb:[0,0,0,0,0],eqFreq:[60,250,1000,4000,12000],eqQ:[1,1,1,1,1],
   eqHighPass:20,eqLowPass:20000,eqInputDb:0,eqOutputDb:0,
   slots:Array.from({length:5},()=>({type:0,enabled:false,params:Array(16).fill(.5)})),
   sends:Array.from({length:n},()=>[.2,.1,.3,0,0])
 }));
 window.__routes=[];window.__learnParam='';window.__learn=false;
 window.fetch=async(url,opts={})=>{
  url=String(url);let path=url.split('?')[0];let body={};
  try{body=JSON.parse(opts.body||'{}')}catch{}
  window.__calls.push({path,body});let result={ok:true};
  if(path==='/api/control'){
   if(opts.method==='POST'){
    if(body.op==='learn'){window.__learnParam=body.paramId;window.__learn=true;}
    if(body.op==='cancel')window.__learn=false;
    if(body.op==='oscAdd')window.__routes.push({address:body.address,paramId:body.paramId});
    if(body.op==='oscRemove')window.__routes=window.__routes.filter(r=>r.address!==body.address);
   }else result={ok:true,parameters:catalog.map(id=>({id,min:0,max:1})),midi:[],osc:window.__routes,
      learning:window.__learn,learnTarget:window.__learnParam,lastReceivedCC:-1,lastReceivedChannel:-1,
      lastLearnedCC:-1,oscRunning:true,oscPort:9000};
  }else if(path==='/api/studio-racks'){
    if(opts.method==='POST' && body.op==='send')window.__racks[body.rack].sends[body.source][body.slot]=Number(body.value);
    else result={ok:true,mode:true,version:1,racks:window.__racks};
  }else if(path==='/api/hardware') result={ok:true,activeApi:'WASAPI',activeAudioUid:1,activeAudioDeviceName:'Virtual Test',
     sampleRate:48000,bufferSize:128,availableApis:['WASAPI'],
     audioDevices:[{api:'WASAPI',uid:1,name:'Test',channels:2,preferredSr:48000,sampleRates:[48000]}],midiPorts:[]};
  else if(path==='/api/state')result={ok:true,params:{},paramRev:0,activeVoices:0,pianoLayers:[]};
  else if(path==='/api/synth/factory/status')result={ok:true,apiVersion:4,maxFactoryPresets:256,presetId:1,enabledLayers:1,synthEnabled:true};
  else if(path==='/api/library')result={ok:true,items:[]};
  else if(path==='/api/programs'||path.includes('/presets'))result=[];
  return {ok:true,json:async()=>result,text:async()=>JSON.stringify(result)};
 };
}'''

def mount(browser,filename,errors):
 page=browser.new_page(viewport={'width':1460,'height':940},device_scale_factor=1)
 page.on('pageerror',lambda exc:errors.append(str(exc)))
 html,scripts=build_page(filename)
 page.set_content(html,wait_until='domcontentloaded')
 page.evaluate(MOCK_BACKEND,CATALOG)
 for name in scripts:page.add_script_tag(content=(UI/'js'/name).read_text(encoding='utf8'))
 page.evaluate("document.dispatchEvent(new Event('DOMContentLoaded'))")
 return page

def run():
 with sync_playwright() as p:
  browser=p.chromium.launch(headless=True,executable_path=os.getenv('CHROMIUM','/usr/bin/chromium'),args=['--no-sandbox','--disable-dev-shm-usage'])
  errors=[]
  try:
   page=mount(browser,'organ.html',errors)
   page.wait_for_function("document.getElementById('workspacePane_presets')?.contains(document.getElementById('organToneRotarySpeed'))",timeout=12000)
   assert page.locator('#organToneRotarySpeed').is_visible(), 'rotary switch must be visible inside presets pane'
   page.locator('#organToneRotarySpeed').click()
   assert page.evaluate('state.organRotaryFast') is True,'rotary button should toggle actual JS state'
   page.locator('#organToneRotarySpeed').click(button='right')
   assert page.locator('.stage-learn-pop').is_visible(), 'right-click should present MIDI learn popup'
   assert 'organRotaryFast' in page.locator('.stage-learn-pop').inner_text()
   page.locator('.stage-learn-pop [data-stage-learn]').click()
   page.wait_for_timeout(300)
   assert page.evaluate("window.__calls.some(c=>c.path==='/api/control'&&c.body.op==='learn'&&c.body.paramId==='organRotaryFast')"), 'context learn must use backend'
   page.keyboard.press('Escape')
   page.evaluate("openModal('modalOrganEffects')")
   page.wait_for_selector('#modalOrganEffects [data-midi-param="studioSend2_0_0"]',timeout=12000)
   modal=page.locator('#modalOrganEffects')
   assert modal.locator('.studio-mixer-faders input[type=range]').count()==5,'organ should have 5 studio sends'
   assert modal.locator('#organInternalInserts').count()==1,'independent organ insert rack absent'
   for param_id in ['organRotaryFastRate','organRotaryRamp','organRotaryStereoWidth','organChorusMix','organPercDecaySec']:
    assert modal.locator(f'.knob-cell[data-id="{param_id}"]').count()==1,param_id
   page.evaluate('''()=>{
    const el=document.querySelector('#modalOrganEffects [data-midi-param="studioSend2_0_0"]');el.value='0.77';el.dispatchEvent(new Event('input',{bubbles:true}));el.dispatchEvent(new Event('change',{bubbles:true}));
   }''')
   page.wait_for_timeout(350)
   assert abs(page.evaluate('window.__racks[2].sends[0][0]')-.77)<.001,'organ fader should write canonical studio send API'
   assert page.locator('#modalOrganEffects [data-midi-param="studioSend2_0_0"]').input_value()=='0.77'
   assert page.locator('#studioSendColumns [data-midi-param="studioSend2_0_0"]').input_value()=='0.77','studio mixer send should mirror modal fader'
   if os.environ.get('V14_PREVIEW'):
    path=Path(os.environ['V14_PREVIEW']);path.mkdir(exist_ok=True,parents=True)
    page.screenshot(path=str(path/'organ-fx.png'))
   page.close()
   page=mount(browser,'settings.html',errors)
   page.evaluate("activateConfigTab('routing')")
   page.wait_for_function("document.querySelector('#stageParamCatalog')?.options.length >= 10",timeout=12000)
   assert page.locator('#stageParamCatalog option').count()>=10,'missing catalog on MIDI/OSC settings'
   page.locator('#stageParamSearch').fill('organRotary')
   assert 0<page.locator('#stageParamCatalog option').count()<10,'catalog search not filtering'
   page.select_option('#stageParamCatalog','organRotaryFastRate')
   assert page.locator('#stageCtrlParam').input_value()=='organRotaryFastRate'
   page.locator('#stageOscAddress').fill('/stage/organ/fast')
   page.evaluate("stageAddOscRoute()")
   page.wait_for_timeout(350)
   assert page.evaluate("window.__calls.some(c=>c.path==='/api/control' && c.body.op==='oscAdd' && c.body.paramId==='organRotaryFastRate')"),'OSC route not submitted'
   assert '/stage/organ/fast' in page.locator('#stageOscRoutes').inner_text(),'OSC route not shown'
   if os.environ.get('V14_PREVIEW'):
    page.screenshot(path=str(Path(os.environ['V14_PREVIEW'])/'midi-osc.png'))
   page.close()
   page=mount(browser,'index.html',errors)
   page.wait_for_selector('#modalPianoSample',state='attached',timeout=12000)
   assert page.locator('#modalPianoSample .sample-sends-grid.studio-sends-horizontal').count()==5
   page.evaluate("openModal('modalPianoSample')")
   page.wait_for_selector('#modalPianoSample .studio-mixer-faders input',timeout=12000)
   styles=page.locator('#modalPianoSample .studio-sends-horizontal .studio-fader input[type=range]').first.evaluate('el=>({writingMode:getComputedStyle(el).writingMode,height:getComputedStyle(el).height})')
   assert styles['writingMode'] not in ('vertical-lr','vertical-rl'), 'studio sends should be horizontal'
   fader_wm=page.locator('#modalPianoSample .studio-sends-horizontal .studio-fader').first.evaluate('el=>getComputedStyle(el).writingMode')
   assert fader_wm=='horizontal-tb', 'label/output must not inherit legacy vertical writing-mode'
   programs=page.locator('#progPad_0');assert programs.count()==1, 'PROGRAMS pad missing'
   height=programs.evaluate('el=>Math.round(el.getBoundingClientRect().height)')
   assert height>=60, f'program pad too short: {height}px'
   if os.environ.get('V14_PREVIEW'):
    page.screenshot(path=str(Path(os.environ['V14_PREVIEW'])/'piano-config.png'))
   page.close()
   print('PASS V14: organ preset rotary, right-click MIDI CC, independent organ DSP, shared Studio sends, MIDI catalog, OSC route, piano sample send CSS, PROGRAMS size')
   if errors:print('Browser page errors (mocked API):',errors[:8])
  finally:browser.close()

if __name__=='__main__':run()
