// format:ui: reformats the web UI with oxfmt.

import { format } from './tools.ts';
import { noArgs, runTask } from '../lib/run.ts';

await runTask(async (args) => {
    noArgs(args);
    await format(true);
});
