from pathlib import Path
from playwright.sync_api import sync_playwright
root=Path(__file__).resolve().parents[1]/'ui'
def resolver(route):
 from urllib.parse import urlparse
 path=urlparse(route.request.url).path
 if path=='/':path='/index.html'
 if path=='/synth':path='/synth.html'
 if path=='/organ':path='/organ.html'
 if path=='/api/state':return route.fulfill(json={'paramRev':1,'instrumentId':1,'activeVoices':0,'pianoLayers':[], 'params':None},status=200)
 if path.startswith('/api/'):return route.fulfill(json={'ok':True},status=200)
 p=root/path.lstrip('/')
 if p.is_file():
  types={'.html':'text/html','.js':'application/javascript','.css':'text/css','.svg':'image/svg+xml'}
  return route.fulfill(body=p.read_bytes(),status=200,content_type=types.get(p.suffix,'text/plain'))
 return route.fulfill(status=404,body='not found')
with sync_playwright() as p:
 browser=p.chromium.launch(executable_path='/usr/bin/chromium',headless=True,args=['--no-sandbox','--no-proxy-server'])
 page=browser.new_page(viewport={'width':1440,'height':900})
 page.route('https://live-stage.test/**',resolver)
 errors=[]
 page.on('pageerror',lambda e:errors.append(str(e)))
 for path in ['/','/synth','/organ','/']:
  page.goto('https://live-stage.test'+path)
  page.wait_for_timeout(250)
  print(path,'knob',page.locator('svg.knob-svg').count(),'keys',page.locator('#pianoRoll .white-key').count(),'drawbar',page.locator('.drawbar-deck').count(), 'errors',errors[-5:])
 browser.close()
