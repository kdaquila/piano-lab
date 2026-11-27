// Running programs and reporting stages: the plumbing every task shares.

import { spawn } from 'node:child_process';
import { existsSync, statSync } from 'node:fs';
import { delimiter, isAbsolute, join } from 'node:path';
import { styleText } from 'node:util';

/** Environment variables for a child process. */
export type Env = NodeJS.ProcessEnv;

/** The repository root, whatever folder npm was started from. */
export const repoRoot = join(import.meta.dirname, '..', '..');

/** The folder the user ran npm from. npm itself runs scripts from the repository root. */
export const userCwd = process.env['INIT_CWD'] ?? process.cwd();

/** An expected failure, such as a missing tool or a failed build: shown without a stack trace. */
export class TaskError extends Error {}

/** A program's exit code and everything it wrote, for output that's shown only on failure. */
export interface Captured {
    code: number;
    stdout: string;
    stderr: string;
}

interface RunOptions {
    env: Env;
    cwd?: string;
    /** Pass the arguments exactly as written, for cmd, which has its own quoting rules. */
    verbatim?: boolean;
}

/**
 * Finds a program on the PATH of `env` (with PATHEXT on Windows), or returns undefined.
 * Done here rather than by the shell, so each task can use its own environment and no shell
 * ever parses arguments.
 */
export function findProgram(name: string, env: Env): string | undefined {
    const isFile = (path: string) => existsSync(path) && statSync(path).isFile();
    if (isAbsolute(name)) {
        return isFile(name) ? name : undefined;
    }
    const windows = process.platform === 'win32';
    // Windows environment names are case-insensitive, and an environment read from a child
    // process (see msvc.ts) keeps whatever case it had, e.g. `Path`.
    const lookup = (key: string) =>
        Object.entries(env).find(([k]) => (windows ? k.toUpperCase() === key : k === key))?.[1];
    const extensions = windows ? (lookup('PATHEXT') ?? '.COM;.EXE;.BAT;.CMD').split(';') : [''];
    for (const dir of (lookup('PATH') ?? '').split(delimiter)) {
        if (!dir) continue;
        for (const extension of extensions) {
            const candidate = join(dir, name + extension.toLowerCase());
            if (isFile(candidate)) return candidate;
        }
    }
    return undefined;
}

/** Throws a TaskError naming every program in `names` that isn't on the PATH of `env`. */
export function requirePrograms(names: readonly string[], env: Env): void {
    const missing = names.filter((name) => findProgram(name, env) === undefined);
    if (missing.length > 0) {
        throw new TaskError(
            `Not on PATH: ${missing.join(', ')}. See the README's setup section. ` +
                'If you installed it after opening this terminal, open a new one.',
        );
    }
}

function resolveProgram(name: string, env: Env): string {
    const path = findProgram(name, env);
    if (path === undefined) {
        throw new TaskError(`${name} is not on PATH. See the README's setup section.`);
    }
    return path;
}

/** Runs a program with the terminal as its input and output, and returns its exit code. */
export function run(
    program: string,
    args: readonly string[],
    options: RunOptions,
): Promise<number> {
    const path = resolveProgram(program, options.env);
    return new Promise((resolve, reject) => {
        const child = spawn(path, args, {
            env: options.env,
            cwd: options.cwd ?? repoRoot,
            stdio: 'inherit',
        });
        child.on('error', reject);
        child.on('close', (code) => resolve(code ?? 1));
    });
}

/** Runs a program and throws a TaskError if it fails. */
export async function exec(
    program: string,
    args: readonly string[],
    options: RunOptions,
): Promise<void> {
    const code = await run(program, args, options);
    if (code !== 0) {
        throw new TaskError(`${program} failed (exit code ${code}).`);
    }
}

/** Runs a program with its output collected instead of shown. */
export function capture(
    program: string,
    args: readonly string[],
    options: RunOptions,
): Promise<Captured> {
    const path = resolveProgram(program, options.env);
    return new Promise((resolve, reject) => {
        const child = spawn(path, args, {
            env: options.env,
            cwd: options.cwd ?? repoRoot,
            stdio: ['ignore', 'pipe', 'pipe'],
            windowsVerbatimArguments: options.verbatim ?? false,
        });
        let stdout = '';
        let stderr = '';
        child.stdout.setEncoding('utf8').on('data', (text: string) => (stdout += text));
        child.stderr.setEncoding('utf8').on('data', (text: string) => (stderr += text));
        child.on('error', reject);
        child.on('close', (code) => resolve({ code: code ?? 1, stdout, stderr }));
    });
}

/** Runs one named stage of a task, with a header and its time. Stops the task if it fails. */
export async function stage(name: string, body: () => Promise<void>): Promise<void> {
    console.log(styleText('cyan', `\n=== ${name}`));
    const start = performance.now();
    try {
        await body();
    } catch (error) {
        console.log(styleText('red', `--- ${name}: FAILED`));
        throw error;
    }
    const seconds = (performance.now() - start) / 1000;
    console.log(styleText('green', `--- ${name}: OK (${seconds.toFixed(1)} s)`));
}

/** Calls `body` on every item, with at most `limit` calls running at once. Keeps the order. */
export async function mapLimited<T, R>(
    items: readonly T[],
    limit: number,
    body: (item: T) => Promise<R>,
): Promise<R[]> {
    const results: R[] = [];
    let next = 0;
    const worker = async () => {
        while (next < items.length) {
            const index = next++;
            results[index] = await body(items[index] as T);
        }
    };
    await Promise.all(Array.from({ length: Math.min(limit, items.length) }, worker));
    return results;
}

/** Throws a TaskError if a task that takes no arguments was given some. */
export function noArgs(args: readonly string[]): void {
    if (args.length > 0) {
        throw new TaskError(`This task takes no arguments, got: ${args.join(' ')}`);
    }
}

/**
 * Runs a task script: `body` gets the arguments given after `--` on the npm command line, and may
 * return an exit code, e.g. of the program it ran. Every task file under scripts/ calls this once.
 */
export async function runTask(body: (args: string[]) => Promise<number | void>): Promise<void> {
    // Ctrl+C reaches every program in the terminal. Let the program we're running stop on its own
    // (piano-lab finishes its recording, a build reports its failure) instead of exiting under it.
    process.on('SIGINT', () => {});

    const name = process.env['npm_lifecycle_event'] ?? 'task';
    try {
        process.exitCode = (await body(process.argv.slice(2))) ?? 0;
    } catch (error) {
        if (!(error instanceof TaskError)) throw error;
        console.error(styleText('red', `\n${name} FAILED: ${error.message}`));
        process.exitCode = 1;
    }
}
