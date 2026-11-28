// Where the piano's 88 keys are, measured in white-key widths from the left end (A0). Pure
// arithmetic, shared by the keyboard and its overview strip.

/** The lowest key, A0, as a MIDI note number. */
export const lowestKey = 21;
/** The highest key, C8. */
export const highestKey = 108;
/** Every key, lowest first. */
export const allKeys: readonly number[] = Array.from(
    { length: highestKey - lowestKey + 1 },
    (_, i) => lowestKey + i,
);

/** Pitch classes of the black keys (C = 0). */
const blackClasses = new Set([1, 3, 6, 8, 10]);

/** True for a black key. */
export function isBlack(key: number): boolean {
    return blackClasses.has(key % 12);
}

/** How many white keys lie left of `key` (for a white key, its index among them). */
export function whiteIndex(key: number): number {
    let count = 0;
    for (let k = lowestKey; k < key; k++) {
        if (!isBlack(k)) count++;
    }
    return count;
}

/** The number of white keys: 52. */
export const whiteKeyCount = whiteIndex(highestKey) + 1;

/** A black key's width, as a share of a white key's. */
export const blackWidth = 0.6;
/** A black key's length, as a share of a white key's. */
export const blackLength = 0.62;

/** A key's left edge and width, in white-key widths. */
export function keyBox(key: number): { left: number; width: number } {
    return isBlack(key)
        ? { left: whiteIndex(key) - blackWidth / 2, width: blackWidth }
        : { left: whiteIndex(key), width: 1 };
}

/** The white keys shown across the keyboard's width: two octaves, C to C. */
export const shownWhiteKeys = 15;
/** The narrowest a white key gets, in CSS pixels, so a fingertip fits. */
export const minWhiteKeyPx = 44;

/** A white key's width in pixels, for a keyboard `widthPx` wide. */
export function whiteKeyPx(widthPx: number): number {
    return Math.max(minWhiteKeyPx, widthPx / shownWhiteKeys);
}

/** Keeps the first white key shown in range, so the view never runs off either end. */
export function clampFirstWhite(first: number, visibleWhites: number): number {
    return Math.min(Math.max(0, first), Math.max(0, whiteKeyCount - visibleWhites));
}

/** Where the keyboard starts: C3, so the two octaves around middle C show. */
export const startKey = 48;

/** A key's name, e.g. C4 (middle C) or F♯5. */
export function keyName(key: number): string {
    const names = ['C', 'C♯', 'D', 'D♯', 'E', 'F', 'F♯', 'G', 'G♯', 'A', 'A♯', 'B'];
    return `${names[key % 12]}${Math.floor(key / 12) - 1}`;
}
