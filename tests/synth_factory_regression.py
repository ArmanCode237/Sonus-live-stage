"""Browser-side regression for factory synth selection, runs without a server.
Uses Chromium + playwright with mocked /api requests (the physical engine still
needs to be compiled and tested on the user's Windows installation).
"""
from pathlib import Path
import re
from playwright.sync_api import sync_playwright
base=Path(__file__).resolve().parents[1]
html=(base/'ui/synth.html').read_text(encoding='utf-8')
html=re.sub(r'<script\b[^>]*src="[^"]+"[^>]*></script>','',html)
html=re.sub(r'<link\b[^>]*>','',html)
with sync_playwright() as p:
 browser=p.chromium.launch(headless=True,executable_path='/usr/bin/chromium',args=['--no-sandbox'])
 page=browser.new_page(viewport={'width':1300,'height':900})
 page.set_content(html)
 page.evaluate('''() => Object.defineProperty(window,'localStorage', {
    configurable:true, value:{ getItem:()=> '1', setItem:()=>{} }
  })''')
 page.add_script_tag(content=(base/'ui/js/core.js').read_text(encoding='utf-8'))
 page.add_script_tag(content=(base/'ui/js/synth.js').read_text(encoding='utf-8'))
 page.evaluate('''() => {
    window.__mockCalls=[];
    window.__mockVersion='v6';
    window.__server={synthEnabled:false,synthPresetId:1};
    window.updateModLeds=()=>{};
    window.syncFromServerParams=(p)=>{
      state.synthEnabled=!!p.synthEnabled;
      showSynthPreset(p.synthPresetId);
    };
    window.fetch=async(url,opts)=>{
      const body=JSON.parse(opts?.body||'{}');
      window.__mockCalls.push({url,body});
      if(url==='/api/synth/factory/load') {
        if(window.__mockVersion==='legacy') return {status:404,ok:false};
        if(window.__mockVersion==='error') return {status:500,ok:false,json:async()=>({error:'Internal error'})};
        if(window.__mockVersion==='oldengine'){
          window.__server.synthPresetId=12;
        } else {
          window.__server.synthPresetId=body.id;
        }
        window.__server.synthEnabled=true;
        return {status:200,ok:true,json:async()=>({ok:true,presetId:window.__server.synthPresetId,params:{...window.__server}})};
      }
      if(url==='/api/param'){
        window.__server[body.id]=body.value;
        return {status:200,ok:true,json:async()=>({ok:true,params:{...window.__server}})};
      }
      throw Error('Bad mock URL '+url);
    };
    buildSynthPresetButtons();
  }''')
 assert page.locator('#synthPresetButtons button').count()==48
 assert page.locator('#synthPreBtn_48').inner_text().find('Stadium')>=0
 # This used to delete all preset buttons every time a state poll arrived.
 is_same=page.evaluate('''() => {
   const button=document.querySelector('#synthPreBtn_25');
   showSynthPreset(2);
   return document.querySelector('#synthPreBtn_25')===button;
 }''')
 assert is_same, 'A polling refresh must not recreate factory buttons'
 page.locator('#synthPreBtn_25').click()
 page.wait_for_function('document.querySelector("#synthPatchStatus").textContent.includes("Cargado")')
 assert page.locator('#synthScreenNumber').inner_text()=='25'
 assert page.evaluate('window.__server.synthPresetId')==25
 assert page.evaluate('window.__server.synthEnabled') is True
 # Category filter must preserve the selected patch, and work across 48 IDs.
 page.locator('#synthFactoryCategory').select_option('LEADS')
 assert page.locator('#synthPresetButtons button').count()==7
 page.locator('#synthPreBtn_41').click()
 page.wait_for_function('document.querySelector("#synthScreenNumber").textContent==="41"')
 # Legacy executable fallback still uses API/param and confirms returned ID.
 page.evaluate('window.__mockVersion="legacy"')
 page.locator('#synthFactoryCategory').select_option('SAWS')
 page.locator('#synthPreBtn_47').click()
 page.wait_for_function('document.querySelector("#synthScreenNumber").textContent==="47"')
 assert page.evaluate('window.__mockCalls.some(x=>x.url==="/api/param"&&x.body.id==="synthPresetId"&&x.body.value===47)')
 # Explicit error instead of pretending that a factory patch was loaded.
 page.evaluate('window.__mockVersion="oldengine"')
 page.locator('#synthPreBtn_46').click()
 page.wait_for_function('document.querySelector("#synthPatchStatus").textContent.includes("No se cargó")')
 assert 'recompilaste' in page.locator('#synthPatchStatus').inner_text()
 assert page.locator('#synthPatchStatus').evaluate('(el)=>el.classList.contains("error")')
 print('PASS: 48 factory patches, no DOM replacement on polling, load+ack, category filtering, legacy fallback and stale backend detection')
 browser.close()
