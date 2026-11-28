// The web UI's one screen: a bar with the status and the settings button; the controls played
// with (volume and release); the keyboard along the bottom, where the hands rest on a touch
// screen; and the log's status line under it. Settings and the log open as panels.

import {
    AppBar,
    Box,
    Chip,
    IconButton,
    LinearProgress,
    Paper,
    Stack,
    Toolbar,
    Typography,
} from '@mui/material';
import { useState, type ReactNode } from 'react';
import { Keyboard } from './keyboard/Keyboard.tsx';
import { LogPanel, LogStatus } from './LogPanel.tsx';
import { usePiano } from './piano.tsx';
import { SettingSlider } from './SettingSlider.tsx';
import { SettingsPanel } from './SettingsPanel.tsx';

export function App(): ReactNode {
    const [settingsOpen, setSettingsOpen] = useState(false);
    const [logOpen, setLogOpen] = useState(false);
    return (
        <Box sx={{ height: '100dvh', display: 'flex', flexDirection: 'column' }}>
            <AppBar position="static" color="default" elevation={0}>
                <Toolbar>
                    <Typography variant="h6" component="h1" sx={{ flex: 1 }}>
                        piano-lab
                    </Typography>
                    <Status />
                    <IconButton
                        aria-label="Open settings"
                        onClick={() => setSettingsOpen(true)}
                        size="large"
                        sx={{ ml: 1 }}
                    >
                        <GearIcon />
                    </IconButton>
                </Toolbar>
            </AppBar>
            <Box component="main" sx={{ flex: 1, overflowY: 'auto', p: 2 }}>
                <Paper variant="outlined" sx={{ p: 2, maxWidth: 720, mx: 'auto' }}>
                    <Stack spacing={1}>
                        <SettingSlider
                            name="sine.gain"
                            label="Volume"
                            format={(gain) => `${Math.round(gain * 100)}%`}
                            extra={<LevelMeter />}
                        />
                        <SettingSlider name="sine.releaseT60" label="Release" />
                    </Stack>
                </Paper>
            </Box>
            <Box sx={{ px: 2, pb: 1 }}>
                <Keyboard />
            </Box>
            <Box sx={{ borderTop: 1, borderColor: 'divider' }}>
                <LogStatus onOpen={() => setLogOpen(true)} />
            </Box>
            <SettingsPanel open={settingsOpen} onClose={() => setSettingsOpen(false)} />
            <LogPanel open={logOpen} onClose={() => setLogOpen(false)} />
        </Box>
    );
}

/** What piano-lab is doing, as a chip: reconnecting, starting, running, ... */
function Status(): ReactNode {
    const connection = usePiano((state) => state.connection);
    const service = usePiano((state) => state.service);
    const [label, color] =
        connection !== 'open'
            ? (['Connecting to piano-lab…', 'warning'] as const)
            : service === 'running'
              ? (['Ready', 'success'] as const)
              : service === 'restarting'
                ? (['Audio restarting…', 'warning'] as const)
                : service === 'stopping'
                  ? (['Stopping', 'default'] as const)
                  : (['Audio starting…', 'info'] as const);
    // An <output>: a status that screen readers announce when it changes.
    return <Chip component="output" label={label} color={color} />;
}

/** The output level, as a thin bar beside the volume. */
function LevelMeter(): ReactNode {
    const level = usePiano((state) => state.level);
    return (
        <LinearProgress
            variant="determinate"
            aria-label="Output level"
            value={Math.min(level, 1) * 100}
            color={level > 1 ? 'error' : 'primary'}
            sx={{
                height: 6,
                borderRadius: 3,
                mt: 1,
                '& .MuiLinearProgress-bar': { transition: 'none' },
            }}
        />
    );
}

function GearIcon(): ReactNode {
    return (
        <svg width="24" height="24" viewBox="0 0 24 24" aria-hidden="true" fill="currentColor">
            <path d="M19.14 12.94a7.07 7.07 0 0 0 0-1.88l2.03-1.58a.5.5 0 0 0 .12-.64l-1.92-3.32a.5.5 0 0 0-.6-.22l-2.39.96a7 7 0 0 0-1.63-.94l-.36-2.54A.5.5 0 0 0 13.9 2h-3.8a.5.5 0 0 0-.49.42l-.36 2.54a7 7 0 0 0-1.63.94l-2.39-.96a.5.5 0 0 0-.6.22L2.71 8.48a.5.5 0 0 0 .12.64l2.03 1.58a7.07 7.07 0 0 0 0 1.88l-2.03 1.58a.5.5 0 0 0-.12.64l1.92 3.32a.5.5 0 0 0 .6.22l2.39-.96c.5.39 1.05.7 1.63.94l.36 2.54a.5.5 0 0 0 .49.42h3.8a.5.5 0 0 0 .49-.42l.36-2.54a7 7 0 0 0 1.63-.94l2.39.96a.5.5 0 0 0 .6-.22l1.92-3.32a.5.5 0 0 0-.12-.64zM12 15.5A3.5 3.5 0 1 1 12 8.5a3.5 3.5 0 0 1 0 7z" />
        </svg>
    );
}
