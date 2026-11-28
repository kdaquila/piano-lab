// start: builds the C++ (dev) and the TypeScript, then runs `piano-lab start`: the web server,
// which runs the audio service, until Ctrl+C or `npx piano-lab stop`. (`npm run start:audio`
// runs the audio service alone, to type service pipe commands by hand.)

import { join } from 'node:path';
import { build as buildAudio, buildEnv } from '../audio/cmake.ts';
import { noArgs, repoRoot, run, runTask, userCwd } from '../lib/run.ts';
import { build as buildWeb } from './tools.ts';

await runTask(async (args) => {
    noArgs(args);
    await buildAudio(await buildEnv(), 'dev');
    await buildWeb();
    const command = join(repoRoot, 'web-server', 'bin', 'piano-lab.js');
    return run(process.execPath, [command, 'start'], { env: process.env, cwd: userCwd });
});
