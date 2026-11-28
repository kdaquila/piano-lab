// The log: a status line along the bottom that says whether all's well, turning amber or red when
// piano-lab reports a problem, and the full log in a panel it opens.

import { Box, Button, Drawer, IconButton, Stack, Typography } from '@mui/material';
import type { ReactNode } from 'react';
import { usePiano } from './piano.tsx';
import type { LogEntry } from './store.ts';

/** The colour of each level, from the theme. */
const levelColor: Record<LogEntry['level'], string> = {
    info: 'text.secondary',
    warning: 'warning.main',
    error: 'error.main',
};

/** The time of day a line arrived, e.g. 14:03:07. */
function time(at: number): string {
    return new Date(at).toLocaleTimeString(undefined, { hour12: false });
}

/** The status line: the most recent problem since the log was last cleared, or that all's well. */
export function LogStatus({ onOpen }: { onOpen: () => void }): ReactNode {
    const worst = usePiano((state) => {
        let found: LogEntry | undefined;
        for (const id of state.logIds) {
            const entry = state.logById[id];
            if (entry === undefined || entry.level === 'info') continue;
            if (found === undefined || entry.level === 'error' || found.level === 'warning') {
                found = entry;
            }
        }
        return found;
    });
    return (
        <Button
            onClick={onOpen}
            fullWidth
            color="inherit"
            aria-label="Open the log"
            sx={{ justifyContent: 'flex-start', textTransform: 'none', minHeight: 48 }}
        >
            <Box
                component="span"
                sx={{
                    width: 10,
                    height: 10,
                    mr: 1.5,
                    flex: 'none',
                    borderRadius: '50%',
                    bgcolor: worst === undefined ? 'success.main' : levelColor[worst.level],
                }}
            />
            <Typography variant="body2" noWrap component="span">
                {worst === undefined ? 'No problems reported' : worst.message}
            </Typography>
        </Button>
    );
}

/** The full log, newest at the bottom. */
export function LogPanel({ open, onClose }: { open: boolean; onClose: () => void }): ReactNode {
    const ids = usePiano((state) => state.logIds);
    const byId = usePiano((state) => state.logById);
    const clear = usePiano((state) => state.clearLog);
    return (
        <Drawer anchor="bottom" open={open} onClose={onClose}>
            <Box
                sx={{ p: 2, height: 'min(50vh, 480px)', display: 'flex', flexDirection: 'column' }}
            >
                <Stack direction="row" spacing={1} sx={{ alignItems: 'center' }}>
                    <Typography variant="h6" component="h2" sx={{ flex: 1 }}>
                        Log
                    </Typography>
                    <Button onClick={clear} disabled={ids.length === 0}>
                        Clear
                    </Button>
                    <IconButton aria-label="Close the log" onClick={onClose} size="large">
                        ✕
                    </IconButton>
                </Stack>
                <Box
                    role="log"
                    sx={{
                        flex: 1,
                        overflowY: 'auto',
                        fontFamily: 'monospace',
                        fontSize: 14,
                        mt: 1,
                    }}
                >
                    {ids.length === 0 && (
                        <Typography variant="body2" color="text.secondary">
                            Nothing logged yet.
                        </Typography>
                    )}
                    {ids.map((id) => {
                        const entry = byId[id];
                        if (entry === undefined) return null;
                        return (
                            <Box key={id} sx={{ color: levelColor[entry.level], py: 0.25 }}>
                                {time(entry.at)} {entry.level.padEnd(7, ' ')} {entry.message}
                            </Box>
                        );
                    })}
                </Box>
            </Box>
        </Drawer>
    );
}
