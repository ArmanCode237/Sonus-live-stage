import re,json,pathlib,time
from playwright.sync_api import sync_playwright
root=pathlib.Path(__file__).resolve().parents[1]/'ui'
partials={str('/'+str(p.relative_to(root))).split('?')[0]:p.read_text() for p in (root/'partials').rglob('*.html')}
with sync_playwright() as p:
 b=p.chromium.launch(headless=True,executable_path='/usr/bin/chromium',args=['--no-sandbox','--disable-dev-shm-usage'])
 for html in ('index.html','synth.html','organ.html'):
  page=b.new_page(viewport={'width':1440,'height':1000});errors=[]
  page.on('pageerror',lambda e:errors.append(str(e)))
  markup=(root/html).read_text()
  scripts=re.findall(r'<script\s+src="([^"]+)"\s*>\s*</script>',markup)
  markup=re.sub(r'<script\s+src="[^"]+"\s*>\s*</script>','',markup)
  markup=re.sub(r'<link[^>]*rel="stylesheet"[^>]*>','',markup)
  page.set_content(markup)
  page.evaluate("""() => {const memory=new Map();const store={getItem:k=>memory.get(k)||null,setItem:(k,v)=>memory.set(k,String(v)),removeItem:k=>memory.delete(k),clear:()=>memory.clear()};Object.defineProperty(window,'localStorage',{configurable:true,value:store});Object.defineProperty(window,'sessionStorage',{configurable:true,value:store});}""")
  page.evaluate('''parts=>{
  const json=(data)=>new Response(JSON.stringify(data),{status:200,headers:{'Content-Type':'application/json'}});
  window.fetch=async(url,opts={})=>{
   const path=String(url).split('?')[0];
   if(parts[path])return new Response(parts[path],{status:200,headers:{'Content-Type':'text/html'}});
   if(path==='/api/state')return json({params:{pianoEnabled:true,pianoVolume:1,synthMasterVol:.7,organVolume:.7,pianoPan:0,synthPan:0,organPan:0,pianoTune:0,synthTune:0,organTune:0,synthLayers:[],pianoLayers:[],drawbars:[]},paramRev:0,pianoLayers:[],instrumentId:0,activeVoices:0,maxVoices:128,peakL:0,peakR:0,activeKeys:[],memoryMB:0,zoneCount:0,midiSeq:0,modWheel:0,pitchBend:0});
   if(path==='/api/synth/factory/edits')return json({overrides:{},favorites:[],hidden:[]});
   if(path.includes('/presets'))return json([]);
   return json({ok:true});
  }
 }''',partials)
  for source in scripts:
   src=root/source.split('?')[0].lstrip('/')
   assert src.exists(),src
   page.add_script_tag(content=src.read_text())
  page.evaluate("document.dispatchEvent(new Event('DOMContentLoaded'))")
  page.wait_for_timeout(600)
  page.evaluate("window.dispatchEvent(new Event('load'))")
  page.wait_for_timeout(900)
  obj=page.evaluate("""() => ({master:document.querySelectorAll('.stage-module-masters').length,strips:document.querySelectorAll('.stage-preset-strip').length,organBrowser:!!document.querySelector('#stageMainOrganPresetSelect'),pianoBrowser:!!document.querySelector('#stagePianoBrowserSelect'),cards:document.querySelectorAll('.factory-program-tile').length})""")
  print(html,obj,'errors',errors[:3])
  if html=='index.html':assert obj['master']>=3 and obj['organBrowser'] and obj['pianoBrowser']
  if html=='synth.html':assert obj['master']>=1 and obj['strips']>=1 and obj['cards']>=100
  if html=='organ.html':assert obj['master']>=1 and obj['strips']>=1
  assert not errors,errors[:4]
  if html in ('synth.html','organ.html'):
   page.locator('.stage-preset-strip .red-rect-btn').first.dispatch_event('click')
   assert page.locator('.stage-preset-overlay').count()==1
   page.locator('.stage-preset-overlay .expand-btn').last.dispatch_event('click')
   assert page.locator('.stage-preset-overlay').count()==0
  page.close()
 b.close()
