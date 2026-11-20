from pathlib import Path
from playwright.sync_api import sync_playwright
import re
base=Path(__file__).resolve().parents[1]

def make_html(name):
    s=(base/'ui'/name).read_text()
    def inc(m):
        p=base/'ui'/m.group(1).split('?')[0].lstrip('/')
        return p.read_text() if p.exists() else ''
    for _ in range(3):s=re.sub(r'<div data-include="([^"]+)"></div>',inc,s)
    s=re.sub(r'<link rel="preconnect"[^>]+>', '',s)
    s=re.sub(r'<link href="https://fonts.googleapis[^>]+>', '',s)
    s=re.sub(r'<link rel="stylesheet" href="(/[^\"]+)">',lambda m:'<style>'+ (base/'ui'/m.group(1).split('?')[0].lstrip('/')).read_text()+'</style>',s)
    s=re.sub(r'<script src="(/[^\"]+)"></script>',lambda m:'<script>\n'+(base/'ui'/m.group(1).split('?')[0].lstrip('/')).read_text().replace('</script>','<\\/script>')+'\n</script>',s)
    fake='''<script>window.__fxChanges=[];let bank=[5,3,1,3];
     window.__rackData={ok:true,mode:true,version:3,racks:bank.map((count,r)=>({
      eqEnabled:true,eqDb:[0,1,0,-2,0],
      slots:Array.from({length:5},(_,i)=>({type:i===0?6:[5,1,7,2][i-1],enabled:true,
      params:[.6,.6,.7,.1,.9,.76,1,.65,.22,.39,.2,.1,.55,1,.99,.82]})),
      sends:Array.from({length:count},()=>[.23,.3,.1,.08,.04])
     }))};
     window.fetch=async (url,options={})=>{const path=String(url);
      if(path.includes('/api/studio-racks')){ if(options.method==='POST'){
       const b=JSON.parse(options.body);window.__fxChanges.push(b);
       if(b.op==='slot'&&b.field>=2)window.__rackData.racks[b.rack].slots[b.slot].params[b.field-2]=b.value;
       return {ok:true,status:200,json:async()=>({ok:true})};
      } return {ok:true,status:200,json:async()=>window.__rackData};}
      return {ok:true,status:200,json:async()=>({ok:true,params:{},programs:[],data:[],pianoLayers:[],samples:[],presets:[]})};
     };</script>'''
    return s.replace('</head>',fake+'</head>')

with sync_playwright() as p:
 browser=p.chromium.launch(executable_path='/usr/bin/chromium',headless=True,args=['--no-sandbox'])
 for page_name in ['index.html','synth.html','organ.html']:
  for width in (1600,950,600):
   page=browser.new_page(viewport={'width':width,'height':1000})
   errors=[];page.on('pageerror',lambda err:errors.append(str(err)))
   page.set_content(make_html(page_name),wait_until='domcontentloaded')
   page.wait_for_selector('#workspaceTabs',timeout=10000)
   page.locator('#workspaceTab_fx').click()
   page.wait_for_function('document.querySelectorAll("#studioSlotUnits .studio-slot-unit").length===5',timeout=8000)
   assert page.locator('#studioSlotUnits .studio-slot-unit').count()==5
   assert page.locator('.studio-legacy-details').count()==0
   assert page.locator('#studioEqKnobs .studio-knob').count()==5
   assert page.locator('#studioSlotUnits .studio-slot-unit').first.locator('.studio-knob').count()==12
   assert page.locator('#studioSlotUnits .studio-slot-unit').first.locator('.studio-algo-switches select').count()==3
   # Shimmer pitch control field 8: p6; choose pitch shift and confirm forwarded POST.
   p_input=page.locator('#studioSlotUnits .studio-slot-unit').first.locator('input[aria-label="PITCH SHIFT"]')
   p_input.fill('0.75');p_input.dispatch_event('change');page.wait_for_timeout(80)
   changes=page.evaluate('window.__fxChanges')
   assert any(x.get('op')=='slot' and x.get('field')==8 and x.get('value')==.75 for x in changes),changes
   if page_name=='index.html':
    assert page.locator('.stage-output-deck [data-id="volume"]').count()==1
    assert page.locator('.stage-output-deck [data-id="pan"]').count()==1
    assert page.locator('.stage-output-deck [data-id="tune"]').count()==1
   assert not [e for e in errors if 'studio' in e.lower()],errors
   if width==1600:
    page.locator('.studio-rack-window').screenshot(path=str(base/f'V730_{page_name.split(".")[0]}_STUDIO.png'))
   print('PASS UI',page_name,width)
   page.close()
 browser.close()
