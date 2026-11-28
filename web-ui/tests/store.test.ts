// The store: held keys and the log, kept flat and bounded.

import { describe, expect, it } from 'vitest';
import { createPianoStore, maxLogEntries } from '../src/store.ts';

describe('the store', () => {
    it('keeps held keys by note number, and forgets them when the link drops', () => {
        const store = createPianoStore();
        store.getState().setConnection('open');
        store.getState().setHeldKeys([60, 64, 67]);
        expect(store.getState().heldKeys).toEqual({ 60: true, 64: true, 67: true });
        store.getState().setConnection('connecting');
        expect(store.getState().heldKeys).toEqual({});
    });

    it('keeps the log by id, dropping the oldest lines past its limit', () => {
        const store = createPianoStore();
        for (let i = 0; i < maxLogEntries + 5; i++) {
            store.getState().addLog('info', `line ${i}`, i);
        }
        const { logIds, logById } = store.getState();
        expect(logIds).toHaveLength(maxLogEntries);
        expect(Object.keys(logById)).toHaveLength(maxLogEntries);
        expect(logById[logIds[0] ?? 0]?.message).toBe('line 5');
        expect(logById[logIds.at(-1) ?? 0]).toMatchObject({ level: 'info', at: maxLogEntries + 4 });
    });

    it('describes settings by name', () => {
        const store = createPianoStore();
        store.getState().setSchema([
            {
                name: 'sine.gain',
                kind: 'number',
                min: 0,
                max: 1,
                unit: '',
                description: 'Peak amplitude',
                defaultValue: 0.25,
                integer: false,
                restartsDevices: false,
            },
        ]);
        expect(store.getState().schema['sine.gain']?.max).toBe(1);
    });
});
