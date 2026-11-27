// render <input> [options]: builds, then renders a music file to WAV (see docs/rendering.md).
// Paths are relative to the folder npm was run from.

import { runProgram } from './cmake.ts';
import { runTask } from '../lib/run.ts';

await runTask((args) => runProgram(['render', ...args]));
