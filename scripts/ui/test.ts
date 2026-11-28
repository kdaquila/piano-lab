// test:ui: runs the web UI's unit and component tests.

import { test } from './tools.ts';
import { noArgs, runTask } from '../lib/run.ts';

await runTask(async (args) => {
    noArgs(args);
    await test();
});
