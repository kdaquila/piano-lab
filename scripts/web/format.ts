// format:web: reformats the TypeScript and JavaScript with oxfmt, the task scripts included.

import { format } from './tools.ts';
import { noArgs, runTask } from '../lib/run.ts';

await runTask(async (args) => {
    noArgs(args);
    await format(true);
});
