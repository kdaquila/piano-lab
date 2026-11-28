// The on-screen keyboard: two octaves at a size a fingertip fits, played by touch (every finger
// on its own, so chords work), pen or mouse. Sliding a finger across the keys plays each in turn.
// The overview strip below shows all 88 keys and where the view is: tap or drag it to move.
// Keys light up while held anywhere: here, on another client, or on a MIDI keyboard.

import { Box, Button, Stack, Typography } from '@mui/material';
import {
    useCallback,
    useEffect,
    useLayoutEffect,
    useRef,
    useState,
    type PointerEvent as ReactPointerEvent,
    type ReactNode,
} from 'react';
import { usePiano, useSender } from '../piano.tsx';
import {
    allKeys,
    blackLength,
    clampFirstWhite,
    isBlack,
    keyBox,
    keyName,
    startKey,
    whiteIndex,
    whiteKeyCount,
    whiteKeyPx,
    shownWhiteKeys,
} from './layout.ts';

/** How hard an on-screen key is struck, 0 to 1. */
export const touchVelocity = 0.7;

/** The key under a pointer, if any. */
function keyAt(event: ReactPointerEvent): number | undefined {
    const under =
        document.elementFromPoint?.(event.clientX, event.clientY) ?? (event.target as Element);
    const key = under?.closest?.('[data-key]')?.getAttribute('data-key');
    return key === null || key === undefined ? undefined : Number(key);
}

/** Lets a pointer keep being heard after it leaves the element it went down on. */
function capture(event: ReactPointerEvent): void {
    const element = event.currentTarget as Element;
    if (element.hasPointerCapture?.(event.pointerId) === false) {
        element.setPointerCapture?.(event.pointerId);
    }
}

/**
 * The pointers playing: each one that went down on the keys, and the key it's on now (none
 * while it's slid off them). A key sounds while at least one pointer is on it.
 */
function usePlaying(): {
    pressed: ReadonlySet<number>;
    down: (pointer: number, key: number | undefined) => void;
    move: (pointer: number, key: number | undefined) => void;
    lift: (pointer: number) => void;
} {
    const sender = useSender();
    const pointers = useRef(new Map<number, number | undefined>());
    const [pressed, setPressed] = useState<ReadonlySet<number>>(new Set());

    const keysHeld = useCallback(() => {
        const keys = new Set<number>();
        for (const key of pointers.current.values()) if (key !== undefined) keys.add(key);
        return keys;
    }, []);

    /** Puts `pointer` on `key` (or on no key), sending what changed. */
    const place = useCallback(
        (pointer: number, key: number | undefined, playing: boolean) => {
            const before = keysHeld();
            if (playing) pointers.current.set(pointer, key);
            else pointers.current.delete(pointer);
            const after = keysHeld();
            for (const old of before)
                if (!after.has(old)) sender.send({ type: 'note.off', key: old });
            for (const now of after) {
                if (!before.has(now))
                    sender.send({ type: 'note.on', key: now, velocity: touchVelocity });
            }
            setPressed(after);
        },
        [keysHeld, sender],
    );

    const down = useCallback(
        (pointer: number, key: number | undefined) => place(pointer, key, true),
        [place],
    );
    const move = useCallback(
        (pointer: number, key: number | undefined) => {
            if (pointers.current.has(pointer) && pointers.current.get(pointer) !== key) {
                place(pointer, key, true);
            }
        },
        [place],
    );
    const lift = useCallback(
        (pointer: number) => {
            if (pointers.current.has(pointer)) place(pointer, undefined, false);
        },
        [place],
    );

    // Leaving the page releases every key still held here.
    useEffect(() => {
        const all = pointers.current;
        return () => {
            for (const key of keysHeld()) sender.send({ type: 'note.off', key });
            all.clear();
        };
    }, [keysHeld, sender]);

    return { pressed, down, move, lift };
}

/** The keyboard, with its controls for moving the view. */
export function Keyboard(): ReactNode {
    const heldKeys = usePiano((state) => state.heldKeys);
    const { pressed, down, move, lift } = usePlaying();
    const frame = useRef<HTMLDivElement>(null);
    const [widthPx, setWidthPx] = useState(0);
    const [first, setFirst] = useState(whiteIndex(startKey));

    useLayoutEffect(() => {
        const element = frame.current;
        if (element === null) return undefined;
        const measure = () => setWidthPx(element.clientWidth);
        measure();
        const observer = new ResizeObserver(measure);
        observer.observe(element);
        return () => observer.disconnect();
    }, []);

    const keyPx = whiteKeyPx(widthPx > 0 ? widthPx : shownWhiteKeys * 60);
    const visible = widthPx > 0 ? widthPx / keyPx : shownWhiteKeys;
    const shown = clampFirstWhite(first, visible);
    const moveBy = (whites: number) => setFirst(clampFirstWhite(shown + whites, visible));
    const lit = (key: number) => pressed.has(key) || heldKeys[key] === true;

    // The range shown, by its outermost whole white keys.
    const firstShown = allKeys.find(
        (key) => !isBlack(key) && whiteIndex(key) >= Math.ceil(shown - 0.01),
    );
    const lastShown = allKeys.findLast(
        (key) => !isBlack(key) && whiteIndex(key) + 1 <= shown + visible + 0.01,
    );

    return (
        <Stack spacing={1}>
            <Stack direction="row" sx={{ alignItems: 'center', justifyContent: 'space-between' }}>
                <Button
                    size="large"
                    variant="outlined"
                    aria-label="Lower octave"
                    onClick={() => moveBy(-7)}
                    disabled={shown <= 0}
                >
                    ◀
                </Button>
                <Typography variant="body2" color="text.secondary" aria-live="polite">
                    {firstShown !== undefined && lastShown !== undefined
                        ? `${keyName(firstShown)} – ${keyName(lastShown)}`
                        : ''}
                </Typography>
                <Button
                    size="large"
                    variant="outlined"
                    aria-label="Higher octave"
                    onClick={() => moveBy(7)}
                    disabled={shown + visible >= whiteKeyCount - 0.01}
                >
                    ▶
                </Button>
            </Stack>
            <Box
                ref={frame}
                component="fieldset"
                aria-label="Piano keyboard"
                onPointerDown={(event) => {
                    event.preventDefault();
                    capture(event);
                    down(event.pointerId, keyAt(event));
                }}
                // Only pointers that went down on the keys play; a mouse just passing doesn't.
                onPointerMove={(event) => move(event.pointerId, keyAt(event))}
                onPointerUp={(event) => lift(event.pointerId)}
                onPointerCancel={(event) => lift(event.pointerId)}
                // Only the keyboard itself losing a pointer ends its note. Lost-capture events
                // bubble, so one from a key (a browser may capture a touch to the key under it
                // before the keyboard takes it over; Edge and Chrome don't) must not stop it.
                onLostPointerCapture={(event) => {
                    if (event.target === event.currentTarget) lift(event.pointerId);
                }}
                onContextMenu={(event) => event.preventDefault()}
                sx={{
                    position: 'relative',
                    m: 0,
                    p: 0,
                    border: 0,
                    minWidth: 0,
                    height: 'clamp(140px, 32vh, 280px)',
                    overflow: 'hidden',
                    touchAction: 'none',
                    userSelect: 'none',
                    WebkitUserSelect: 'none',
                    borderRadius: 1,
                    bgcolor: 'grey.900',
                }}
            >
                <Box
                    sx={{
                        position: 'absolute',
                        inset: 0,
                        width: whiteKeyCount * keyPx,
                        transform: `translateX(${-shown * keyPx}px)`,
                        transition: 'transform 150ms ease-out',
                    }}
                >
                    {allKeys.map((key) => (
                        <Key key={key} note={key} keyPx={keyPx} lit={lit(key)} />
                    ))}
                </Box>
            </Box>
            <Overview
                first={shown}
                visible={visible}
                lit={lit}
                onMove={(whites) => setFirst(clampFirstWhite(whites, visible))}
            />
        </Stack>
    );
}

/** One key. */
function Key({ note, keyPx, lit }: { note: number; keyPx: number; lit: boolean }): ReactNode {
    const black = isBlack(note);
    const { left, width } = keyBox(note);
    return (
        <Box
            data-key={note}
            data-lit={lit}
            aria-label={keyName(note)}
            sx={{
                position: 'absolute',
                top: 0,
                left: left * keyPx,
                width: width * keyPx,
                height: black ? `${blackLength * 100}%` : '100%',
                zIndex: black ? 1 : 0,
                boxSizing: 'border-box',
                border: 1,
                borderColor: 'grey.800',
                borderRadius: '0 0 6px 6px',
                bgcolor: lit ? 'primary.main' : black ? 'grey.900' : 'grey.50',
                boxShadow: black ? 3 : 0,
                display: 'flex',
                alignItems: 'flex-end',
                justifyContent: 'center',
                pb: 1,
            }}
        >
            {note % 12 === 0 && (
                <Typography variant="caption" sx={{ color: 'grey.600', pointerEvents: 'none' }}>
                    {keyName(note)}
                </Typography>
            )}
        </Box>
    );
}

/** All 88 keys in a thin strip, with the part the keyboard shows marked. */
function Overview({
    first,
    visible,
    lit,
    onMove,
}: {
    first: number;
    visible: number;
    lit: (key: number) => boolean;
    onMove: (first: number) => void;
}): ReactNode {
    const pct = (whites: number) => `${(whites / whiteKeyCount) * 100}%`;
    const follow = (event: ReactPointerEvent<HTMLDivElement>) => {
        const box = event.currentTarget.getBoundingClientRect();
        if (box.width <= 0) return;
        const at = ((event.clientX - box.left) / box.width) * whiteKeyCount;
        onMove(at - visible / 2);
    };
    return (
        <Box
            // A native range input can't draw the keys, so this is a slider by its role.
            // oxlint-disable-next-line jsx-a11y/prefer-tag-over-role
            role="slider"
            aria-label="Keyboard position"
            aria-valuemin={0}
            aria-valuemax={whiteKeyCount}
            aria-valuenow={Math.round(first)}
            tabIndex={0}
            onKeyDown={(event) => {
                if (event.key === 'ArrowLeft') onMove(first - 7);
                if (event.key === 'ArrowRight') onMove(first + 7);
            }}
            onPointerDown={(event) => {
                capture(event);
                follow(event);
            }}
            // Only while pressed: a mouse or a pen just passing over (hovering) doesn't move it.
            onPointerMove={(event) => {
                if (event.buttons !== 0) follow(event);
            }}
            sx={{
                position: 'relative',
                height: 36,
                touchAction: 'none',
                cursor: 'pointer',
                borderRadius: 0.5,
                overflow: 'hidden',
                bgcolor: 'grey.900',
            }}
        >
            {allKeys.map((key) => {
                const { left, width } = keyBox(key);
                const black = isBlack(key);
                return (
                    <Box
                        key={key}
                        sx={{
                            position: 'absolute',
                            top: 0,
                            left: pct(left),
                            width: pct(width),
                            height: black ? '60%' : '100%',
                            zIndex: black ? 1 : 0,
                            boxSizing: 'border-box',
                            borderRight: black ? 0 : 1,
                            borderColor: 'grey.700',
                            bgcolor: lit(key) ? 'primary.main' : black ? 'grey.900' : 'grey.300',
                        }}
                    />
                );
            })}
            <Box
                sx={{
                    position: 'absolute',
                    top: 0,
                    bottom: 0,
                    zIndex: 2,
                    left: pct(first),
                    width: pct(visible),
                    border: 2,
                    borderColor: 'primary.main',
                    borderRadius: 0.5,
                    bgcolor: 'rgba(25, 118, 210, 0.15)',
                    transition: 'left 150ms ease-out',
                }}
            />
        </Box>
    );
}
