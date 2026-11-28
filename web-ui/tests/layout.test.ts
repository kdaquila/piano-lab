// The keyboard's arithmetic: which keys are black, where each key sits, and the view's limits.

import { describe, expect, it } from 'vitest';
import {
    allKeys,
    clampFirstWhite,
    isBlack,
    keyBox,
    keyName,
    minWhiteKeyPx,
    whiteIndex,
    whiteKeyCount,
    whiteKeyPx,
} from '../src/keyboard/layout.ts';

describe('the keyboard layout', () => {
    it('has the piano’s 88 keys, 52 of them white', () => {
        expect(allKeys).toHaveLength(88);
        expect(allKeys.filter((key) => !isBlack(key))).toHaveLength(52);
        expect(whiteKeyCount).toBe(52);
    });

    it('places white keys side by side and black keys across their gaps', () => {
        expect(whiteIndex(21)).toBe(0); // A0
        expect(whiteIndex(60)).toBe(23); // middle C
        expect(keyBox(60)).toEqual({ left: 23, width: 1 });
        expect(keyBox(61)).toEqual({ left: 24 - 0.3, width: 0.6 }); // C♯4, between C4 and D4
        expect(keyBox(108)).toEqual({ left: 51, width: 1 }); // C8
    });

    it('names keys as musicians do', () => {
        expect(keyName(60)).toBe('C4');
        expect(keyName(21)).toBe('A0');
        expect(keyName(66)).toBe('F♯4');
    });

    it('shows two octaves across the width, but never keys too narrow for a finger', () => {
        expect(whiteKeyPx(1500)).toBe(100);
        expect(whiteKeyPx(375)).toBe(minWhiteKeyPx);
    });

    it('keeps the view on the keyboard', () => {
        expect(clampFirstWhite(-5, 15)).toBe(0);
        expect(clampFirstWhite(50, 15)).toBe(37);
        expect(clampFirstWhite(20, 15)).toBe(20);
        expect(clampFirstWhite(10, 60)).toBe(0);
    });
});
