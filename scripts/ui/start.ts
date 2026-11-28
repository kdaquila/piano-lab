// start:ui: runs the web UI's development server on http://localhost:5173, which reloads the page
// on every change. It talks to the piano-lab that `npm start` runs, so start that first.

import { noArgs, runTask } from '../lib/run.ts';
import { tool } from '../web/tools.ts';

await runTask(async (args) => {
    noArgs(args);
    await tool('vite/bin/vite.js', ['web-ui']);
});
