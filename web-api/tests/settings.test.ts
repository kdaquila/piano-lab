// The settings schemas: the shape checks the web server makes before the audio service sees a
// change.

import { describe, expect, it } from 'vitest';
import { Compile } from 'typebox/compile';
import { SettingValues, maxSettingNameLength, maxSettings } from '../src/index.ts';

const values = Compile(SettingValues);

describe('SettingValues', () => {
    it('accepts settings by name', () => {
        expect(values.Check({})).toBe(true);
        expect(
            values.Check({ 'sine.gain': 0.5, 'audio.type': 'ASIO', 'midi.inputs': ['A', 'B'] }),
        ).toBe(true);
    });

    it('refuses names that aren’t dotted words, or are too long', () => {
        for (const name of [
            '',
            'bad name',
            'sine.',
            '.gain',
            '1st.gain',
            '__proto__.x',
            'sine..gain',
        ]) {
            expect(values.Check({ [name]: 1 }), name).toBe(false);
        }
        expect(values.Check({ ['a'.repeat(maxSettingNameLength)]: 1 })).toBe(true);
        expect(values.Check({ ['a'.repeat(maxSettingNameLength + 1)]: 1 })).toBe(false);
    });

    it('refuses values that aren’t plain or are too big', () => {
        for (const value of [
            true,
            null,
            { nested: 1 },
            [1, 2],
            'x'.repeat(257),
            Array(65).fill('x'),
        ]) {
            expect(values.Check({ 'sine.gain': value }), JSON.stringify(value).slice(0, 40)).toBe(
                false,
            );
        }
        const many = Object.fromEntries(
            Array.from({ length: maxSettings + 1 }, (_, i) => [`s.n${i}`, 1]),
        );
        expect(values.Check(many)).toBe(false);
    });
});
