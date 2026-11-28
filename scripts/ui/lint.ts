// lint:ui: lints the web UI with oxlint.

import { lint } from './tools.ts';
import { noArgs, runTask } from '../lib/run.ts';

await runTask(async (args) => {
    noArgs(args);
    await lint();
});
