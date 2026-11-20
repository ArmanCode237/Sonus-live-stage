"""Offline visual audit: memory router is used only in the isolated browser harness.
The shipped browser build uses BrowserRouter and the real C++ host."""
from pathlib import Path
from playwright.sync_api import sync_playwright
root=Path(__file__).resolve().parents[1]
css=(root/'ui/app/assets/app.css').read_text()
js=(root/'ui/app/assets/app.js').read_text().replace('react_router_dom_1.BrowserRouter','react_router_dom_1.MemoryRouter')
html='<html><head><meta charset="utf-8"><style>'+css+'</style></head><body><div id="root"></div><script>'+js.replace('</script>','<\\/script>')+'</script></body></html>'
out=root/'preview';out.mkdir(exist_ok=True)
with sync_playwright() as pw:
    browser=pw.chromium.launch(headless=True,executable_path='/usr/bin/chromium',args=['--no-sandbox'])
    page=browser.new_page(viewport={'width':1600,'height':990},device_scale_factor=1)
    errors=[];page.on('pageerror',lambda e:errors.append(str(e)))
    page.set_content(html,wait_until='load',timeout=30000)
    pages=[('main','PERFORM'),('piano','PIANO'),('synth','SYNTH'),('organ','ORGAN'),('fx','FX / MIX'),('zones','ZONES'),('library','LIBRARY'),('settings','SETTINGS')]
    for slug,label in pages:
        if slug!='main':page.get_by_role('link',name=label,exact=True).first.click()
        page.wait_for_timeout(550)
        rowboxes=page.locator('.pad').evaluate_all('(els) => els.map(e => ({x:Math.round(e.getBoundingClientRect().x),y:Math.round(e.getBoundingClientRect().y),width:Math.round(e.getBoundingClientRect().width)}))')
        print(slug,'h1',page.locator('h1').all_text_contents(),'panels',page.locator('.panel').count(),'pad_boxes',rowboxes[:8],'errors',len(errors))
        page.screenshot(path=str(out/(slug+'.png')),full_page=False)
    # verify keyboard toggle accessibility and store workflow display
    page.get_by_role('link',name='PERFORM',exact=True).first.click()
    page.get_by_role('button',name='STORE',exact=True).click()
    print('Store mode',page.get_by_text('SELECT DESTINATION',exact=True).count())
    page.get_by_role('button',name='Programa 5: Program 5').click()
    print('Store modal',page.get_by_text('GUARDAR EN PROGRAM 5',exact=True).count())
    page.get_by_role('button',name='CANCELAR',exact=True).click()
    page.get_by_role('button',name='KEYBOARD',exact=False).click()
    print('Keyboard',page.locator('#stage-keyboard').count())
    page.screenshot(path=str(out/'main-with-keyboard.png'))
    print('ALL PAGE ERRORS',errors[:20]);browser.close()
