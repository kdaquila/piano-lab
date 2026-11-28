// start: builds the C++ (dev), the TypeScript and the web UI, then runs `piano-lab start`: the
// web server, which runs the audio service and opens the web UI in the browser, until Ctrl+C or
// `npx piano-lab stop`. (`npm run start:audio` runs the audio service alone, to type service pipe
// commands by hand; `npm run start:ui` runs the web UI's development server.)

import { join } from 'node:path';
import { build as buildAudio, buildEnv } from '../audio/cmake.ts';
import { noArgs, repoRoot, run, runTask, userCwd } from '../lib/run.ts';
import { build as buildUi } from '../ui/tools.ts';
import { build as buildWeb } from './tools.ts';

await runTask(async (args) => {
    noArgs(args);
    await buildAudio(await buildEnv(), 'dev');
    await buildWeb();
    await buildUi();
    const command = join(repoRoot, 'web-server', 'bin', 'piano-lab.js');
    return run(process.execPath, [command, 'start'], { env: process.env, cwd: userCwd });
});
