// build:ui: type-checks the web UI and builds it to web-ui/dist/, which the web server serves.

import { build } from './tools.ts';
import { noArgs, runTask } from '../lib/run.ts';

await runTask(async (args) => {
    noArgs(args);
    await build();
});
