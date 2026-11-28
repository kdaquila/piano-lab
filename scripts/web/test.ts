// test:web: builds, then runs the web server's and the web API's tests.

import { build, test } from './tools.ts';
import { noArgs, runTask } from '../lib/run.ts';

await runTask(async (args) => {
    noArgs(args);
    await build();
    await test();
});
