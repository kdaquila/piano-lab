// Runs a real piano-lab for the end-to-end tests (Playwright starts it): the web server with the
// built web UI, on its own port (serve-port.ts), without opening a browser. The audio service is
// copied to its own folder and run from there, so its settings, logs and recordings (written next
// to the program) stay out of the developer's own.

import { copyFileSync, mkdirSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { audioServiceProgram } from '../../web-server/src/audio-service.ts';
import { runWebServer } from '../../web-server/src/main.ts';
import { endToEndPort } from './serve-port.ts';

const program = audioServiceProgram();
if ('problem' in program) {
    console.error(program.problem);
    process.exit(1);
}
// The same folder every run, emptied first: Playwright ends this process without warning.
const folder = join(tmpdir(), 'piano-lab-end-to-end');
rmSync(folder, { recursive: true, force: true });
mkdirSync(folder, { recursive: true });
const copy = join(folder, program.file.split(/[\\/]/).at(-1) ?? 'piano-lab-audio');
copyFileSync(program.file, copy);

process.exitCode = await runWebServer({
    port: endToEndPort,
    browser: false,
    command: { file: copy, args: ['run'] },
});
