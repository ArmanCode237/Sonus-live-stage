import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

// The C++ host serves the built app from ui/app under /app/ and maps the page
// routes (/, /synth, /organ, /fx, /zones, /library, /settings) to index.html.
// In development, /api is proxied to the running NordVirtualStage executable.
// The host rejects cross-origin POSTs, so the proxy presents itself as the host.
const BACKEND = 'http://127.0.0.1:8080';

export default defineConfig({
  base: '/app/',
  plugins: [react()],
  build: {
    outDir: '../ui/app',
    emptyOutDir: true,
    target: 'es2020',
    sourcemap: false,
    chunkSizeWarningLimit: 900,
  },
  server: {
    port: 5173,
    proxy: {
      '/ws': {
        target: BACKEND.replace('http', 'ws'),
        ws: true,
        configure: (proxy) => {
          proxy.on('proxyReqWs', (req) => {
            req.setHeader('origin', BACKEND.replace('127.0.0.1', 'localhost'));
            req.setHeader('host', 'localhost:8080');
          });
        },
      },
      '/api': {
        target: BACKEND,
        changeOrigin: true,
        configure: (proxy) => {
          proxy.on('proxyReq', (req) => {
            req.setHeader('origin', BACKEND.replace('127.0.0.1', 'localhost'));
            req.setHeader('host', 'localhost:8080');
          });
        },
      },
    },
  },
  test: { environment: 'node', include: ['src/**/*.test.ts', 'scripts/**/*.test.mjs'] },
} as never);
