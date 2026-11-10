from http.server import HTTPServer, SimpleHTTPRequestHandler
from threading import Thread
from playwright.sync_api import sync_playwright
import json, os
root=os.path.join(os.path.dirname(os.path.dirname(__file__)),'ui')
S={'synthEnabled':False,'synthPresetId':1,'synthLayers':[{} for _ in range(3)]}
requests=[]
class H(SimpleHTTPRequestHandler):
 def __init__(self,*a,**kw): super().__init__(*a,directory=root,**kw)
 def log_message(self,*a):pass
 def do_GET(self):
  if self.path=='/synth':self.path='/synth.html'
  if self.path.startswith('/api/'):
   body={'synthEnabled':S['synthEnabled'],'synthPresetId':S['synthPresetId'],'params':S,'paramRev':len(requests),'activeVoices':0}
   if self.path=='/api/synth/presets': body=[]
   self.send_response(200);self.send_header('Content-Type','application/json');self.end_headers();self.wfile.write(json.dumps(body).encode());return
  return super().do_GET()
 def do_POST(self):
  data=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
  requests.append(data)
  if self.path=='/api/param':S[data['id']]=data['value']
  self.send_response(200);self.send_header('Content-Type','application/json');self.end_headers()
  self.wfile.write(json.dumps({'ok':True,'params':S} if data.get('id')=='synthPresetId' else {'ok':True}).encode())
server=HTTPServer(('127.0.0.1',0),H)
Thread(target=server.serve_forever,daemon=True).start()
url=f'http://127.0.0.1:{server.server_port}/synth'
with sync_playwright() as p:
 browser=p.chromium.launch(headless=True,executable_path='/usr/bin/chromium',args=['--no-sandbox'])
 page=browser.new_page(viewport={'width':1480,'height':900})
 errors=[]
 page.on('pageerror',lambda ex:errors.append(str(ex)))
 page.goto(url,wait_until='domcontentloaded',timeout=15000)
 page.wait_for_timeout(1100)
 print('URL',url)
 print('Loaded buttons:',page.locator('#synthPresetButtons button').count())
 print('Errors:',errors)
 print('First buttons',page.locator('#synthPresetButtons button').all_text_contents()[:4])
 print('Filter available:',page.locator('#synthFactoryCategory option').all_text_contents())
 print('target button count:',page.locator('#synthPreBtn_25').count())
 try:
  page.locator('#synthPreBtn_25').click(timeout=5000)
  page.wait_for_timeout(350)
  print('S after:',S)
  print('Requests:',requests)
  print('Screen:',page.locator('#synthScreenPatch').inner_text())
  print('Errors after:',errors)
 except Exception as ex: print('CLICK FAIL:',str(ex)[:900])
 finally: browser.close();server.shutdown()
