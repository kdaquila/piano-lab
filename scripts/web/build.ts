// build:web: builds the web server and the web API package to their dist/ folders.

import { build } from './tools.ts';
import { noArgs, runTask } from '../lib/run.ts';

await runTask(async (args) => {
    noArgs(args);
    await build();
});
