// Vite builds the web UI to dist/ (`npm run build:ui`), which the web server serves. It also runs
// the development server (`npm run start:ui`), which reloads on every change and passes the web
// API through to a running piano-lab.

import react from '@vitejs/plugin-react';
import { defineConfig } from 'vite';

/** Where `npm start` serves piano-lab. */
const pianoLab = 'http://localhost:4440';
/** Where the development server serves the web UI. */
const devPort = 5173;

export default defineConfig({
    root: import.meta.dirname,
    plugins: [react()],
    // Served from this computer, so one bundle is fine, however large.
    build: { outDir: 'dist', emptyOutDir: true, chunkSizeWarningLimit: 2048 },
    server: {
        port: devPort,
        strictPort: true,
        proxy: {
            '/api': {
                target: pianoLab,
                changeOrigin: true,
                ws: true,
                // piano-lab refuses pages from other origins. The development server's own page is
                // vouched for as piano-lab's; any other origin is passed on as it is, and refused.
                configure: (proxy) => {
                    const own = `http://localhost:${devPort}`;
                    const vouch = (
                        request: { headers: Record<string, unknown> },
                        proxied: {
                            setHeader(name: string, value: string): void;
                        },
                    ) => {
                        if (request.headers['origin'] === own)
                            proxied.setHeader('origin', pianoLab);
                    };
                    proxy.on('proxyReq', (proxied, request) => vouch(request, proxied));
                    proxy.on('proxyReqWs', (proxied, request) => vouch(request, proxied));
                },
            },
        },
    },
});
