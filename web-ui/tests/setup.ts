// Fills in what jsdom lacks that the web UI uses, before each test file.

import { afterEach } from 'vitest';
import { cleanup } from '@testing-library/react';

// The keyboard measures its width with a ResizeObserver; jsdom has none (and lays nothing out).
globalThis.ResizeObserver ??= class {
    observe(): void {}
    unobserve(): void {}
    disconnect(): void {}
};

afterEach(() => cleanup());
