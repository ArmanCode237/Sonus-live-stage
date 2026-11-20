from playwright.sync_api import sync_playwright
from pathlib import Path
base=Path(__file__).resolve().parents[1]
html=(base/'ui/partials/modal-studio-rack.html').read_text()
css=(base/'ui/css/studio-rack.css').read_text()
js=(base/'ui/js/studio-rack.js').read_text()
state={'ok':True,'mode':True,'racks':[{'eqEnabled':True,'eqDb':[0,0,0,0,0],'slots':[{'type':0,'enabled':False,'params':[.45,.55,.25,.45,.7,.25]} for _ in range(5)],'sends':[[0]*5 for _ in range(5)]} for _ in range(4)],'version':2}
state['racks'][3]['slots'][0]['type']=6;state['racks'][3]['slots'][0]['enabled']=True
state['racks'][3]['slots'][1]['type']=5;state['racks'][3]['slots'][1]['enabled']=True
state['racks'][3]['slots'][2]['type']=1;state['racks'][3]['slots'][2]['enabled']=True
state['racks'][3]['slots'][3]['type']=7;state['racks'][3]['slots'][3]['enabled']=True
for src in range(3):
 for slot in range(4):state['racks'][3]['sends'][src][slot]=.2+.03*src
with sync_playwright() as p:
 browser=p.chromium.launch(headless=True,executable_path='/usr/bin/chromium',args=['--no-sandbox'])
 for width in (1600,900,600):
  page=browser.new_page(viewport={'width':width,'height':1100}, device_scale_factor=1)
  page.set_content(f'<html><head><style>body{{background:#06080a;font-family:Arial}} .modal-backdrop{{display:block;padding:10px}}.modal-window{{position:relative}}</style><style>{css}</style></head><body>{html}</body></html>')
  page.add_script_tag(content=js)
  page.evaluate('window.openModal = id => document.getElementById(id).classList.add("open"); true')
  page.evaluate('(s)=>{window.fetch=async (url,opts)=>{if(opts?.method === "POST") return {ok:true,json:async()=>({ok:true})};return {ok:true,json:async()=>(s)}}}',state)
  page.evaluate("openStudioRack('master')")
  page.wait_for_selector('.studio-slot-unit')
  assert page.locator('.studio-slot-unit').count()==5
  assert page.locator('.studio-mixer-channel').count()==3
  page.locator('.studio-slot-unit').nth(1).locator('select').last.select_option('6') # type selection -> re-render
  page.wait_for_timeout(200)
  assert page.locator('.studio-slot-unit').nth(1).locator('select').first.input_value()=='6'
  page.locator('.studio-slot-unit').nth(0).locator('select').last.select_option('3')
  page.wait_for_timeout(125)
  assert page.locator('.studio-slot-unit').nth(0).locator('.studio-knob').count()==5, 'Shimmer should expose five parameters'
  page.locator('.studio-slot-unit').nth(0).locator('select').last.select_option('0')
  page.wait_for_timeout(125)
  assert page.locator('.studio-slot-unit').nth(0).locator('.studio-knob').count()==4, 'Room should expose four parameters'
  page.evaluate('studioRackSelect(0)')
  assert page.locator('.studio-mixer-channel').count()==5
  page.evaluate('studioRackSelect(3)')
  if width==1600:page.screenshot(path='/mnt/data/Live_Stage_720_Studio_Rack.png')
  print('PASS studio UI',width,'5 effect slots, faders, EQ')
  page.close()
 browser.close()
