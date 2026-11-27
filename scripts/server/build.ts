// build:server [preset]: configures when needed, then builds. The preset is dev by default.

import { build, buildEnv, parsePreset } from './cmake.ts';
import { runTask } from '../lib/run.ts';

await runTask(async (args) => {
    const preset = parsePreset(args);
    await build(await buildEnv(), preset);
});
