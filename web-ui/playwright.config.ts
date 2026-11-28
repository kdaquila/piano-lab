// Playwright runs the web UI's end-to-end tests (`npm run acceptance`) in Microsoft Edge, the
// one already on every Windows computer, so no browser is downloaded. They drive a real
// piano-lab (e2e/serve.ts), which plays quiet notes on the default sound device.

import { defineConfig } from '@playwright/test';
import { endToEndPort } from './e2e/serve-port.ts';

const origin = `http://localhost:${endToEndPort}`;

export default defineConfig({
    testDir: 'e2e',
    // One piano-lab, played by one test at a time.
    workers: 1,
    fullyParallel: false,
    forbidOnly: true,
    reporter: [['list']],
    outputDir: '../test-results/web-ui',
    use: {
        baseURL: origin,
        channel: 'msedge',
        hasTouch: true,
        viewport: { width: 1280, height: 800 },
        trace: 'retain-on-failure',
    },
    webServer: {
        command: 'node web-ui/e2e/serve.ts',
        cwd: '..',
        url: `${origin}/api/v0/status`,
        reuseExistingServer: false,
        timeout: 60_000,
        stdout: 'ignore',
        stderr: 'pipe',
    },
});
