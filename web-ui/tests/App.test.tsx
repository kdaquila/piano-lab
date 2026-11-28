// The whole screen against a stand-in link: the status, the controls drawn from the settings'
// descriptions, and the log's status line.

import { act, fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';
import type { SettingInfo } from '@piano-lab/web-api';
import { App } from '../src/App.tsx';
import { PianoProvider, type Sender } from '../src/piano.tsx';
import { confirmWithinMs, formatValue } from '../src/SettingSlider.tsx';
import { createPianoStore } from '../src/store.ts';

const number = (name: string, min: number, max: number, unit = ''): SettingInfo => ({
    name,
    kind: 'number',
    min,
    max,
    unit,
    description: name,
    defaultValue: min,
    integer: false,
    restartsDevices: false,
});

function setUp() {
    const store = createPianoStore();
    const sender: Sender = {
        send: vi.fn<Sender['send']>(() => true),
        setSettings: vi.fn<Sender['setSettings']>(() => true),
    };
    render(
        <PianoProvider store={store} sender={sender}>
            <App />
        </PianoProvider>,
    );
    return { store, sender };
}

/** The store as piano-lab fills it once connected. */
function connect(store: ReturnType<typeof createPianoStore>): void {
    act(() => {
        const state = store.getState();
        state.setConnection('open');
        state.setService('running');
        state.setSchema([number('sine.gain', 0, 1), number('sine.releaseT60', 0.01, 30, 's')]);
        state.setSettings({ 'sine.gain': 0.25, 'sine.releaseT60': 0.25 });
    });
}

describe('the web UI', () => {
    it('says it’s connecting until piano-lab answers, then that it’s ready', () => {
        const { store } = setUp();
        expect(screen.getByRole('status').textContent).toBe('Connecting to piano-lab…');
        connect(store);
        expect(screen.getByRole('status').textContent).toBe('Ready');
        act(() => store.getState().setService('restarting'));
        expect(screen.getByRole('status').textContent).toBe('Audio restarting…');
    });

    it('draws volume and release from the settings, and changes them live', () => {
        const { store, sender } = setUp();
        connect(store);
        const volume = screen.getByRole('slider', { name: 'Volume' });
        expect(volume.getAttribute('aria-valuetext')).toBe('25%');
        const release = screen.getByRole('slider', { name: 'Release' });
        expect(release.getAttribute('aria-valuetext')).toBe('0.25 s');

        fireEvent.keyDown(volume, { key: 'PageUp' });
        expect(sender.setSettings).toHaveBeenLastCalledWith({ 'sine.gain': 0.3 });
    });

    it('keeps a released slider’s value while piano-lab confirms it, and goes back if it doesn’t', () => {
        vi.useFakeTimers();
        try {
            const { store } = setUp();
            connect(store);
            const volume = screen.getByRole('slider', { name: 'Volume' });
            fireEvent.keyDown(volume, { key: 'PageUp' });
            // Not confirmed yet: the slider keeps the new value rather than snapping back.
            expect(volume.getAttribute('aria-valuetext')).toBe('30%');
            act(() => store.getState().setSettings({ 'sine.gain': 0.3, 'sine.releaseT60': 0.25 }));
            expect(volume.getAttribute('aria-valuetext')).toBe('30%');

            // A change piano-lab never confirms gives way to the value it has.
            fireEvent.keyDown(volume, { key: 'PageUp' });
            expect(volume.getAttribute('aria-valuetext')).toBe('35%');
            act(() => vi.advanceTimersByTime(confirmWithinMs));
            expect(volume.getAttribute('aria-valuetext')).toBe('30%');
        } finally {
            vi.useRealTimers();
        }
    });

    it('shows the latest problem on the status line, and the full log in its panel', () => {
        const { store } = setUp();
        expect(screen.getByText('No problems reported')).toBeTruthy();
        act(() => {
            store.getState().addLog('info', 'the piano is ready');
            store.getState().addLog('warning', 'no MIDI inputs found');
        });
        expect(screen.getByText('no MIDI inputs found')).toBeTruthy();

        fireEvent.click(screen.getByRole('button', { name: 'Open the log' }));
        expect(screen.getByRole('log').textContent).toContain('the piano is ready');
    });

    it('formats values for people', () => {
        expect(formatValue(0.012345, 's')).toBe('0.0123 s');
        expect(formatValue(440, 'Hz')).toBe('440 Hz');
        expect(formatValue(0.25, '')).toBe('0.25');
    });
});
