// The one entry point behind the npm scripts: `node scripts/task.ts <task> [args...]`.
//
// Tasks for one side are named `<task>:<side>`, e.g. `build:server`. The plain names (`build`,
// `test`, ...) run that task for every side. Today the only side is the server (the C++ engine and
// backend); the UI joins in step 0.7.

import { join } from 'node:path';
import { styleText } from 'node:util';
import { exec, repoRoot, stage, TaskError } from './lib/run.ts';
import {
    buildServer,
    checkServer,
    formatServer,
    lintServer,
    render,
    startServer,
    testServer,
} from './server/tasks.ts';

/** A task gets the arguments after `--` and may return an exit code, e.g. the program it ran. */
type Task = (args: readonly string[]) => Promise<number | void>;

/** Type-checks the task scripts themselves; Node only strips the types when running them. */
async function typecheckScripts(): Promise<void> {
    const tsc = join(repoRoot, 'node_modules', 'typescript', 'bin', 'tsc');
    await stage('Type check (scripts)', () =>
        exec(process.execPath, [tsc, '-p', 'scripts'], { env: process.env }).catch(() => {
            throw new TaskError('tsc failed. If it is missing, run `npm install` first.');
        }),
    );
}

const tasks: Record<string, Task> = {
    'build:server': buildServer,
    'test:server': testServer,
    'format:server': formatServer,
    'lint:server': lintServer,
    'check:server': checkServer,
    'start:server': startServer,
    render,

    build: buildServer,
    test: testServer,
    format: formatServer,
    lint: lintServer,
    check: async (args) => {
        if (args.length > 0) {
            throw new TaskError(`check takes no arguments, got: ${args.join(' ')}`);
        }
        await typecheckScripts();
        await checkServer(args);
        console.log(styleText('green', '\nAll checks passed.'));
    },
    start: startServer,
};

const [name = '', ...args] = process.argv.slice(2);
const task = tasks[name];
if (task === undefined) {
    console.error(`Unknown task "${name}". The tasks are: ${Object.keys(tasks).join(', ')}.`);
    process.exit(1);
}

// Ctrl+C reaches every program in the terminal. Let the program we're running stop on its own
// (piano-lab finishes its recording, a build reports its failure) instead of exiting under it.
process.on('SIGINT', () => {});

try {
    process.exitCode = (await task(args)) ?? 0;
} catch (error) {
    if (!(error instanceof TaskError)) throw error;
    console.error(styleText('red', `\n${name} FAILED: ${error.message}`));
    process.exitCode = 1;
}
