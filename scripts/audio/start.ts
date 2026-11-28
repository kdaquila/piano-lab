// start:audio: builds, then runs the audio service until Ctrl+C. Its stdin is the terminal, so
// service pipe commands can be typed in.

import { runProgram } from './cmake.ts';
import { noArgs, runTask } from '../lib/run.ts';

await runTask(async (args) => {
    noArgs(args);
    return runProgram(['run']);
});
