// Vitest runs step 0.6's acceptance checks with this (`npm run acceptance`): against the real
// audio service, so they need a sound device. One file at a time, since each plays sound.

import { defineConfig } from 'vitest/config';

export default defineConfig({
    test: {
        include: ['web-server/acceptance/**/*.test.ts'],
        fileParallelism: false,
    },
});
