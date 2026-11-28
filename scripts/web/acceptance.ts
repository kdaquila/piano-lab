// acceptance: builds the C++ (dev), the TypeScript and the web UI, then runs the acceptance checks
// against the real audio service, and the web UI's end-to-end tests in Edge. They need a sound
// device and play quiet tones, so they aren't part of `npm run check`.

import { build as buildAudio, buildEnv } from '../audio/cmake.ts';
import { noArgs, runTask, stage } from '../lib/run.ts';
import { build as buildUi, endToEnd } from '../ui/tools.ts';
import { build as buildWeb, tool } from './tools.ts';

await runTask(async (args) => {
    noArgs(args);
    await buildAudio(await buildEnv(), 'dev');
    await buildWeb();
    await buildUi();
    await stage('Acceptance checks (Vitest)', () =>
        tool('vitest/vitest.mjs', ['run', '--config', 'vitest.acceptance.config.ts']),
    );
    await endToEnd();
});
