// test:audio [preset]: builds, then runs the tests. The preset is dev by default.

import { build, buildEnv, parsePreset, test } from './cmake.ts';
import { runTask } from '../lib/run.ts';

await runTask(async (args) => {
    const preset = parsePreset(args);
    const env = await buildEnv();
    await build(env, preset);
    await test(env, preset);
});
