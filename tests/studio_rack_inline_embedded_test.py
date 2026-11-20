from pathlib import Path
from playwright.sync_api import sync_playwright
import json,re
base=Path(__file__).resolve().parents[1]

def build_html(name):
 s=(base/'ui'/name).read_text(encoding='utf-8')
 def inline_includes(html):
  # Templates are only one level deep in these pages.
  def include(m):
   p=m.group(1).split('?')[0].lstrip('/')
   if not (base/'ui'/p).exists():return ''
   return (base/'ui'/p).read_text(encoding='utf-8')
  for _ in range(3):
   ns=re.sub(r'<div data-include="([^"]+)"></div>',include,html)
   if ns==html:break
   html=ns
  return html
 s=inline_includes(s)
 s=re.sub(r'<link rel="preconnect"[^>]*>', '',s)
 s=re.sub(r'<link href="https://fonts\.googleapis[^>]*>', '',s)
 def css(m):
  path=m.group(1).split('?')[0].lstrip('/')
  return '<style>'+ (base/'ui'/path).read_text(encoding='utf-8')+'</style>'
 s=re.sub(r'<link rel="stylesheet" href="(/[^"]+)">',css,s)
 def js(m):
  path=m.group(1).split('?')[0].lstrip('/')
  return '<script>\n'+(base/'ui'/path).read_text(encoding='utf-8').replace('</script>','<\\/script>')+'\n</script>'
 s=re.sub(r'<script src="(/[^"]+)"></script>',js,s)
 fake='''<script>
  window.__studioFake={ok:true,mode:true,version:2,racks:[5,3,1,3].map((n,r)=>({
    eqEnabled:true,eqDb:[0,1,0,-1,0],
    slots:Array.from({length:5},(_,i)=>({type:[6,5,1,7,2][i],enabled:true,params:[.4,.5,.3,.4,.5,.25]})),
    sends:Array.from({length:n},()=>[.2,.3,.15,.1,.05])
  }))};
  window.fetch=async function(url,options={}) {
    const path=String(url);
    if(path.startsWith('/api/studio-racks')) return {ok:true,status:200,json:async()=>options.method==='POST'?{ok:true}:window.__studioFake};
    if(path.startsWith('/api')) return {ok:true,status:200,json:async()=>({ok:true,params:{},programs:[],presets:[],data:[],samples:[]})};
    return {ok:false,status:404,json:async()=>({ok:false})};
  };
  </script>'''
 s=s.replace('</head>',fake+'</head>')
 return s

with sync_playwright() as p:
 browser=p.chromium.launch(headless=True,executable_path='/usr/bin/chromium',args=['--no-sandbox'])
 for fname in ('index.html','synth.html','organ.html'):
  for width in [1600,900,600]:
   page=browser.new_page(viewport={'width':width,'height':1000})
   errors=[];page.on('pageerror',lambda e:errors.append(str(e)))
   page.set_content(build_html(fname),wait_until='domcontentloaded')
   page.wait_for_selector('#workspaceTabs',timeout=10000)
   page.locator('#workspaceTab_fx').click()
   page.wait_for_function("document.querySelectorAll('#studioSlotUnits .studio-slot-unit').length===5",timeout=7000)
   assert page.locator('#modalStudioRack').evaluate('(el)=>el.classList.contains("studio-rack-inline")')
   assert page.locator('#studioEqKnobs .studio-knob').count()==5
   assert page.locator('#studioEngineStatus').inner_text()=='MOTOR CONECTADO · 4 RACKS'
   assert page.locator('.studio-rack-tabs [aria-selected="true"]').count()==1
   # Change selected rack and verify its channel count is reflected in the mixer.
   page.locator('[data-studio-rack="0"]').click()
   assert page.locator('#studioSendColumns .studio-mixer-channel').count()==5
   page.locator('[data-studio-rack="2"]').click()
   assert page.locator('#studioSendColumns .studio-mixer-channel').count()==1
   page.locator('[data-studio-rack="3"]').click()
   assert page.locator('#studioSendColumns .studio-mixer-channel').count()==3
   assert not [s for s in errors if any(k in s for k in ['studioRack','studioSlot','studioRender'])],errors
   if width==1600:
    page.locator('#workspacePane_fx').screenshot(path=f'/mnt/data/Live_Stage_721_{fname.replace(".html","")}_FX.png')
   print('PASS',fname,width,'UI rendered')
   page.close()
 # Missing native endpoint: the studio controls remain visible but safely disabled.
 page=browser.new_page()
 html=build_html('synth.html').replace("if(path.startsWith('/api/studio-racks')) return {ok:true,status:200,json:async()=>options.method==='POST'?{ok:true}:window.__studioFake};", "if(path.startsWith('/api/studio-racks')) return {ok:false,status:404,json:async()=>({ok:false,error:'Endpoint no disponible'})};")
 page.set_content(html,wait_until='domcontentloaded')
 page.wait_for_selector('#workspaceTabs')
 page.locator('#workspaceTab_fx').click()
 page.wait_for_function("document.getElementById('studioEngineStatus').textContent.includes('SIN CONEXIÓN')")
 assert page.locator('.studio-slot-unit').count()==5
 assert page.locator('.studio-rack-slots').evaluate('(el)=>el.inert')
 print('PASS offline fallback 404 read-only')
 browser.close()
