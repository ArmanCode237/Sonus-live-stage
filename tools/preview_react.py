"""Development-only static preview (no audio engine)."""
from http.server import HTTPServer, SimpleHTTPRequestHandler
from pathlib import Path
from urllib.parse import urlsplit
ROOT = Path(__file__).resolve().parents[1] / 'ui'
class Handler(SimpleHTTPRequestHandler):
    def __init__(self,*args,**kwargs):super().__init__(*args,directory=str(ROOT),**kwargs)
    def do_GET(self):
        p=urlsplit(self.path).path
        if p in {'/', '/piano','/synth','/organ','/fx','/zones','/library','/settings'}:
            self.path='/app/index.html'
        elif p.startswith('/api/') or p=='/ws':
            self.send_response(503)
            self.send_header('Content-Type','application/json')
            self.end_headers()
            self.wfile.write(b'{"ok":false,"error":"Preview only: C++ engine unavailable"}')
            return
        super().do_GET()
HTTPServer(('127.0.0.1',8765),Handler).serve_forever()
