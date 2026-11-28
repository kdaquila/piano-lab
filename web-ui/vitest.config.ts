// Vitest runs the web UI's unit and component tests (`npm run test:ui`), in jsdom.

import react from '@vitejs/plugin-react';
import { defineConfig } from 'vitest/config';

export default defineConfig({
    root: import.meta.dirname,
    plugins: [react()],
    test: {
        include: ['tests/**/*.test.{ts,tsx}'],
        environment: 'jsdom',
        setupFiles: ['tests/setup.ts'],
    },
});
