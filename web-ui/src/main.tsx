// The web UI's entry point: makes the store, opens the link to the piano-lab that served this
// page, and draws the app, light or dark as the system is set.

import { CssBaseline, ThemeProvider, createTheme } from '@mui/material';
import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import { App } from './App.tsx';
import { Live } from './live.ts';
import { PianoProvider } from './piano.tsx';
import { createPianoStore } from './store.ts';

const theme = createTheme({ colorSchemes: { light: true, dark: true } });
const store = createPianoStore();
const live = new Live({ store, origin: window.location.origin });
live.start();

const root = document.getElementById('root');
if (root === null) throw new Error('the page has no #root');
createRoot(root).render(
    <StrictMode>
        <ThemeProvider theme={theme}>
            <CssBaseline />
            <PianoProvider store={store} sender={live}>
                <App />
            </PianoProvider>
        </ThemeProvider>
    </StrictMode>,
);
