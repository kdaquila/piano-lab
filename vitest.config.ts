// Vitest runs the web server's and the web API's tests (`npm run test:web`), from their .ts
// files. A workspace's own package is used as built, so test:web builds first.

import { defineConfig } from 'vitest/config';

export default defineConfig({
    test: {
        include: ['web-*/tests/**/*.test.ts'],
    },
});
