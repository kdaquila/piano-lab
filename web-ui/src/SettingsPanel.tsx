// The settings panel, which slides in from the side: the audio output, the MIDI inputs and the
// tuning. Set once and left alone, so they stay out of the way of playing. Changing a device
// reopens it, with a short dropout.

import {
    Box,
    Checkbox,
    Divider,
    Drawer,
    FormControl,
    FormControlLabel,
    FormGroup,
    IconButton,
    InputLabel,
    MenuItem,
    Select,
    Stack,
    Switch,
    Typography,
} from '@mui/material';
import type { ReactNode } from 'react';
import { usePiano, useSender } from './piano.tsx';
import { SettingSlider } from './SettingSlider.tsx';

/** Sample rates offered, in Hz. */
const sampleRates = [44100, 48000, 88200, 96000];
/** Buffer sizes offered, in samples (0: the device's own). */
const bufferSizes = [0, 32, 64, 128, 256, 480, 512, 1024, 2048];

interface Props {
    open: boolean;
    onClose: () => void;
}

export function SettingsPanel({ open, onClose }: Props): ReactNode {
    return (
        <Drawer anchor="right" open={open} onClose={onClose}>
            <Box
                component="section"
                sx={{ width: 'min(420px, 100vw)', p: 3 }}
                aria-label="Settings"
            >
                <Stack
                    direction="row"
                    sx={{ alignItems: 'center', justifyContent: 'space-between' }}
                >
                    <Typography variant="h6" component="h2">
                        Settings
                    </Typography>
                    <IconButton aria-label="Close settings" onClick={onClose} size="large">
                        ✕
                    </IconButton>
                </Stack>
                <Stack spacing={3} sx={{ mt: 2 }}>
                    <AudioSettings />
                    <Divider />
                    <MidiSettings />
                    <Divider />
                    <SettingSlider name="tuning.a4Hz" label="Tuning (A4)" />
                </Stack>
            </Box>
        </Drawer>
    );
}

/** A choice from a list, sent as the value of `name`. */
function Choice({
    name,
    label,
    value,
    options,
}: {
    name: string;
    label: string;
    value: string | number;
    options: readonly { value: string | number; label: string }[];
}): ReactNode {
    const sender = useSender();
    const id = `${name}-choice`;
    // A value piano-lab has that isn't offered (e.g. typed into settings.toml) stays choosable.
    const all = options.some((option) => option.value === value)
        ? options
        : [...options, { value, label: String(value) }];
    return (
        <FormControl fullWidth>
            <InputLabel id={id} shrink>
                {label}
            </InputLabel>
            <Select
                labelId={id}
                displayEmpty
                notched
                label={label}
                value={value}
                onChange={(event) => {
                    const chosen = all.find(
                        (option) => String(option.value) === String(event.target.value),
                    );
                    if (chosen !== undefined) sender.setSettings({ [name]: chosen.value });
                }}
            >
                {all.map((option) => (
                    <MenuItem key={String(option.value)} value={option.value}>
                        {option.label}
                    </MenuItem>
                ))}
            </Select>
        </FormControl>
    );
}

function AudioSettings(): ReactNode {
    const devices = usePiano((state) => state.devices);
    const type = usePiano((state) => state.settings['audio.type']);
    const output = usePiano((state) => state.settings['audio.output']);
    const sampleRate = usePiano((state) => state.settings['audio.sampleRate']);
    const bufferSize = usePiano((state) => state.settings['audio.bufferSize']);
    const sender = useSender();
    if (typeof type !== 'string' || typeof output !== 'string') return null;

    const types = devices?.audio ?? [];
    const outputs = types.find((kind) => kind.type === type)?.outputs ?? [];
    const typeId = 'audio.type-choice';
    return (
        <Stack spacing={2}>
            <Typography variant="subtitle1" component="h3">
                Audio output
            </Typography>
            <FormControl fullWidth>
                <InputLabel id={typeId} shrink>
                    Kind of device
                </InputLabel>
                <Select
                    labelId={typeId}
                    displayEmpty
                    notched
                    label="Kind of device"
                    value={type}
                    onChange={(event) =>
                        // A new kind of device starts from its default output.
                        sender.setSettings({ 'audio.type': event.target.value, 'audio.output': '' })
                    }
                >
                    <MenuItem value="">Automatic</MenuItem>
                    {types.map((kind) => (
                        <MenuItem key={kind.type} value={kind.type}>
                            {kind.type}
                        </MenuItem>
                    ))}
                    {type !== '' && !types.some((kind) => kind.type === type) && (
                        <MenuItem value={type}>{type}</MenuItem>
                    )}
                </Select>
            </FormControl>
            <Choice
                name="audio.output"
                label="Output"
                value={output}
                options={[
                    { value: '', label: 'Default' },
                    ...outputs.map((name) => ({ value: name, label: name })),
                ]}
            />
            {typeof sampleRate === 'number' && (
                <Choice
                    name="audio.sampleRate"
                    label="Sample rate"
                    value={sampleRate}
                    options={sampleRates.map((rate) => ({ value: rate, label: `${rate} Hz` }))}
                />
            )}
            {typeof bufferSize === 'number' && (
                <Choice
                    name="audio.bufferSize"
                    label="Buffer size"
                    value={bufferSize}
                    options={bufferSizes.map((size) => ({
                        value: size,
                        label: size === 0 ? "The device's own" : `${size} samples`,
                    }))}
                />
            )}
        </Stack>
    );
}

function MidiSettings(): ReactNode {
    const available = usePiano((state) => state.devices?.midi);
    const chosen = usePiano((state) => state.settings['midi.inputs']);
    const sender = useSender();
    if (!Array.isArray(chosen)) return null;

    const all = chosen.length === 0;
    // Inputs piano-lab was told to open that aren't plugged in still show, so they can be removed.
    const names = [...new Set([...(available ?? []), ...chosen])];
    return (
        <Stack spacing={1}>
            <Typography variant="subtitle1" component="h3">
                MIDI inputs
            </Typography>
            <FormControlLabel
                control={
                    <Switch
                        checked={all}
                        onChange={(event) =>
                            sender.setSettings({
                                'midi.inputs': event.target.checked ? [] : names.slice(0, 1),
                            })
                        }
                    />
                }
                label="Listen to every MIDI input"
            />
            {names.length === 0 && (
                <Typography variant="body2" color="text.secondary">
                    No MIDI inputs found.
                </Typography>
            )}
            {!all && (
                <FormGroup>
                    {names.map((name) => (
                        <FormControlLabel
                            key={name}
                            control={
                                <Checkbox
                                    checked={chosen.includes(name)}
                                    // An empty list means every input, so the last one stays.
                                    disabled={chosen.length === 1 && chosen.includes(name)}
                                    onChange={(event) =>
                                        sender.setSettings({
                                            'midi.inputs': event.target.checked
                                                ? [...chosen, name]
                                                : chosen.filter((other) => other !== name),
                                        })
                                    }
                                />
                            }
                            label={name}
                        />
                    ))}
                </FormGroup>
            )}
        </Stack>
    );
}
