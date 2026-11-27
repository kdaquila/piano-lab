// start:audio: builds, then plays the piano live until Ctrl+C.

import { runProgram } from './cmake.ts';
import { noArgs, runTask } from '../lib/run.ts';

await runTask(async (args) => {
    noArgs(args);
    return runProgram(['server', 'start']);
});
