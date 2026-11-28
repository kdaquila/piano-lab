// A slider for one number setting, drawn from the setting's own description (range, unit,
// whether it's whole numbers). Changes are sent while dragging, at most every 50 ms, so the sound
// follows the finger without flooding piano-lab.

import { Box, Slider, Stack, Typography } from '@mui/material';
import { useEffect, useState, type ReactNode } from 'react';
import { usePiano, useSender } from './piano.tsx';

/** The shortest time between changes sent while dragging, in milliseconds. */
export const sendEveryMs = 50;

/**
 * Runs the latest of a stream of calls at most every `everyMs`: the first at once, the rest
 * once the time is up, so the last one is never lost.
 */
export class Throttle {
    readonly #everyMs: number;
    #last = 0;
    #pending: ReturnType<typeof setTimeout> | undefined;

    constructor(everyMs: number) {
        this.#everyMs = everyMs;
    }

    run(call: () => void): void {
        clearTimeout(this.#pending);
        const wait = this.#last + this.#everyMs - Date.now();
        if (wait <= 0) {
            this.#last = Date.now();
            call();
        } else {
            this.#pending = setTimeout(() => this.run(call), wait);
        }
    }

    /** Runs `call` now, dropping any call still waiting. */
    now(call: () => void): void {
        clearTimeout(this.#pending);
        this.#last = Date.now();
        call();
    }

    cancel(): void {
        clearTimeout(this.#pending);
    }
}

/** True when a range spans enough that a logarithmic slider reads better (e.g. 0.01 s to 30 s). */
function logarithmic(min: number, max: number): boolean {
    return min > 0 && max / min >= 100;
}

/** A value for people: up to three significant figures, and its unit. */
export function formatValue(value: number, unit: string): string {
    const text = Math.abs(value) >= 100 ? value.toFixed(0) : String(Number(value.toPrecision(3)));
    return unit === '' ? text : `${text} ${unit}`;
}

interface Props {
    /** The setting, e.g. `sine.gain`. */
    name: string;
    /** What people call it, e.g. Volume. */
    label: string;
    /** Shows the value for people (e.g. as a percentage); `formatValue` by default. */
    format?: (value: number) => string;
    /** Shown beside the label, e.g. a level meter. */
    extra?: ReactNode;
}

/** The slider, or nothing until the setting's description has arrived. */
export function SettingSlider({ name, label, format, extra }: Props): ReactNode {
    const info = usePiano((state) => state.schema[name]);
    const current = usePiano((state) => state.settings[name]);
    const sender = useSender();
    // While dragging, the slider shows the finger's value, not the last one piano-lab confirmed.
    const [dragging, setDragging] = useState<number | undefined>(undefined);
    const [throttle] = useState(() => new Throttle(sendEveryMs));

    useEffect(() => () => throttle.cancel(), [throttle]);

    if (info === undefined || info.kind !== 'number' || typeof current !== 'number') return null;
    const min = info.min ?? 0;
    const max = info.max ?? 1;
    const log = logarithmic(min, max);
    // The slider runs 0 to 1000; this maps it to the setting's range and back.
    const toValue = (position: number) => {
        const share = position / 1000;
        const value = log ? min * (max / min) ** share : min + (max - min) * share;
        return info.integer ? Math.round(value) : Number(value.toPrecision(4));
    };
    const toPosition = (value: number) =>
        log
            ? (Math.log(value / min) / Math.log(max / min)) * 1000
            : ((value - min) / (max - min)) * 1000;
    const shown = dragging ?? current;
    const describe = format ?? ((value: number) => formatValue(value, info.unit));

    return (
        <Box>
            <Stack direction="row" spacing={1} sx={{ alignItems: 'baseline' }}>
                <Typography variant="subtitle1" component="span" id={`${name}-label`}>
                    {label}
                </Typography>
                <Typography variant="body2" color="text.secondary" component="span">
                    {describe(shown)}
                </Typography>
                <Box sx={{ flex: 1 }}>{extra}</Box>
            </Stack>
            <Slider
                aria-labelledby={`${name}-label`}
                aria-valuetext={describe(shown)}
                title={info.description}
                min={0}
                max={1000}
                // 200 steps across the range; Page Up and Down move a quarter of it.
                step={5}
                shiftStep={50}
                value={toPosition(shown)}
                onChange={(_event, position) => {
                    const value = toValue(position);
                    setDragging(value);
                    throttle.run(() => sender.setSettings({ [name]: value }));
                }}
                onChangeCommitted={(_event, position) => {
                    throttle.now(() => sender.setSettings({ [name]: toValue(position) }));
                    setDragging(undefined);
                }}
                sx={{
                    py: 2.5,
                    '& .MuiSlider-thumb': { width: 28, height: 28 },
                    '& .MuiSlider-rail, & .MuiSlider-track': { height: 8 },
                }}
            />
        </Box>
    );
}
