// The on-screen keyboard: every pointer plays on its own, so chords work; sliding moves a note;
// and keys held anywhere light up.

import { act, fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';
import { Keyboard, touchVelocity } from '../src/keyboard/Keyboard.tsx';
import { PianoProvider, type Sender } from '../src/piano.tsx';
import { createPianoStore } from '../src/store.ts';

function setUp() {
    const store = createPianoStore();
    const sent: unknown[] = [];
    const sender: Sender = {
        send: vi.fn<Sender['send']>((command) => sent.push(command) > 0),
        setSettings: vi.fn<Sender['setSettings']>(() => true),
    };
    const view = render(
        <PianoProvider store={store} sender={sender}>
            <Keyboard />
        </PianoProvider>,
    );
    const key = (note: number) => {
        const element = view.container.querySelector(`[data-key="${note}"]`);
        if (element === null) throw new Error(`no key ${note}`);
        return element;
    };
    return { store, sent, key };
}

const touch = (pointerId: number) => ({
    pointerId,
    pointerType: 'touch',
    isPrimary: pointerId === 1,
});

describe('the on-screen keyboard', () => {
    it('plays a chord: every finger is its own note', () => {
        const { sent, key } = setUp();
        fireEvent.pointerDown(key(60), touch(1));
        fireEvent.pointerDown(key(64), touch(2));
        fireEvent.pointerDown(key(67), touch(3));
        expect(sent).toEqual([
            { type: 'note.on', key: 60, velocity: touchVelocity },
            { type: 'note.on', key: 64, velocity: touchVelocity },
            { type: 'note.on', key: 67, velocity: touchVelocity },
        ]);
        for (const note of [60, 64, 67]) expect(key(note).getAttribute('data-lit')).toBe('true');

        fireEvent.pointerUp(key(64), touch(2));
        expect(sent.at(-1)).toEqual({ type: 'note.off', key: 64 });
        expect(key(64).getAttribute('data-lit')).toBe('false');
        expect(key(60).getAttribute('data-lit')).toBe('true');
    });

    it('moves a note when a finger slides onto another key', () => {
        const { sent, key } = setUp();
        fireEvent.pointerDown(key(60), touch(1));
        fireEvent.pointerMove(key(62), touch(1));
        fireEvent.pointerUp(key(62), touch(1));
        expect(sent).toEqual([
            { type: 'note.on', key: 60, velocity: touchVelocity },
            { type: 'note.off', key: 60 },
            { type: 'note.on', key: 62, velocity: touchVelocity },
            { type: 'note.off', key: 62 },
        ]);
    });

    it('keeps a key sounding while any finger is still on it', () => {
        const { sent, key } = setUp();
        fireEvent.pointerDown(key(60), touch(1));
        fireEvent.pointerDown(key(60), touch(2));
        fireEvent.pointerUp(key(60), touch(1));
        expect(sent).toEqual([{ type: 'note.on', key: 60, velocity: touchVelocity }]);
        fireEvent.pointerCancel(key(60), touch(2));
        expect(sent.at(-1)).toEqual({ type: 'note.off', key: 60 });
    });

    it('ignores a mouse that passes over without pressing', () => {
        const { sent, key } = setUp();
        fireEvent.pointerMove(key(60), { pointerId: 1, pointerType: 'mouse', buttons: 0 });
        expect(sent).toEqual([]);
    });

    it('lights keys held anywhere, e.g. on a MIDI keyboard', () => {
        const { store, key } = setUp();
        act(() => store.getState().setHeldKeys([65]));
        expect(key(65).getAttribute('data-lit')).toBe('true');
    });

    it('moves the view an octave at a time', () => {
        setUp();
        expect(screen.getByText('C3 – C5')).toBeTruthy();
        fireEvent.click(screen.getByRole('button', { name: 'Higher octave' }));
        expect(screen.getByText('C4 – C6')).toBeTruthy();
        fireEvent.click(screen.getByRole('button', { name: 'Lower octave' }));
        fireEvent.click(screen.getByRole('button', { name: 'Lower octave' }));
        expect(screen.getByText('C2 – C4')).toBeTruthy();
    });
});
