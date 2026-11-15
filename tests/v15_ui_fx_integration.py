"""Browser UI regression for four layers, wavetable selection and internal FX modal.
Backend calls are intercepted; real audio is verified in v15_synth_factory_fx.cpp.
"""
from pathlib import Path
from playwright.sync_api import sync_playwright
from v14_browser_integration import mount
import os

def run():
 with sync_playwright() as p:
  browser=p.chromium.launch(headless=True,executable_path=os.getenv('CHROMIUM','/usr/bin/chromium'),args=['--no-sandbox','--disable-dev-shm-usage'])
  errors=[]
  try:
   main=mount(browser,'index.html',errors)
   main.wait_for_selector('#mainPill_L4')
   assert '4-LAYER' in main.locator('.main-synth-module').inner_text()
   assert '256' in main.locator('.main-synth-module').inner_text()
   main.locator('#mainPill_L4').click()
   assert main.evaluate('window.__calls.some(x=>x.path==="/api/param"&&x.body.id==="L4_en")')
   main.close()
   page=mount(browser,'synth.html',errors)
   page.evaluate("activateWorkspaceTab('edit')")
   page.wait_for_selector('#synth3LayersContainer .synth-layer-card')
   assert page.locator('.synth-layer-card').count()==4
   assert page.locator('[id^="synthWavetableSelect_"]').count()==4
   page.locator('#synthWavetableSelect_2').select_option('18')
   assert page.evaluate('window.__calls.some(x=>x.path==="/api/param"&&x.body.id==="L2_wtIdx"&&x.body.value===18)')
   page.locator('.synth-layer-card').nth(2).locator('.synth-layer-fx-button').click()
   modal=page.locator('#modalSynthLayerFx')
   assert modal.is_visible()
   assert 'LAYER C' in modal.locator('h2').inner_text()
   assert modal.locator('.synth-insert-unit').count()==5
   assert modal.locator('[data-synth-insert-on]').count()==5
   assert modal.locator('.knob-cell').count()==21
   modal.locator('[data-synth-insert-on="L3_fx1_on"]').click()
   assert page.evaluate('window.__calls.some(x=>x.path==="/api/param"&&x.body.id==="L3_fx1_on"&&x.body.value===1)')
   assert modal.locator('[data-synth-insert-on="L3_fx1_on"]').get_attribute('aria-pressed')=='true'
   modal.locator('.knob-cell[data-id="L3_fx4_time"]').click(button='right')
   assert page.locator('.stage-learn-pop').is_visible()
   assert 'L3_fx4_time' in page.locator('.stage-learn-pop').inner_text()
   page.keyboard.press('Escape')
   for width in (1440,820,540):
    page.set_viewport_size({'width':width,'height':900})
    page.evaluate('openSynthLayerFx(4)')
    assert modal.is_visible()
    assert modal.locator('.synth-insert-unit').count()==5
    assert modal.locator('.knob-cell').count()==21
    assert 'LAYER D' in modal.locator('h2').inner_text()
    if os.getenv('V15_PREVIEW'):
     path=Path(os.getenv('V15_PREVIEW'));path.mkdir(parents=True,exist_ok=True)
     modal.screenshot(path=str(path/f'synth_fx_{width}.png'))
    page.evaluate("closeModal('modalSynthLayerFx')")
   print('PASS V15: MAIN 4 layers, 256 names, WT selector, five layer inserts, MIDI learn, modal 1440/820/540')
   assert not errors,errors[:10]
  finally:browser.close()
if __name__=='__main__':run()
