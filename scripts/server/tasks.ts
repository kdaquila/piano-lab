// The server tasks: the C++ engine and backend, built with CMake, Ninja and MSVC.
// Each takes the arguments given after `--` on the npm command line.

import { build, type Preset, parsePreset, programPath, test } from './cmake.ts';
import { format, lint, requireLlvm } from './llvm.ts';
import { msvcEnv } from '../lib/msvc.ts';
import { type Env, requirePrograms, run, stage, TaskError, userCwd } from '../lib/run.ts';

function noArgs(task: string, args: readonly string[]): void {
    if (args.length > 0) {
        throw new TaskError(`${task} takes no arguments, got: ${args.join(' ')}`);
    }
}

/** The build environment, with the build tools checked up front. */
async function buildEnv(): Promise<Env> {
    const env = await msvcEnv();
    requirePrograms(['cl', 'cmake', 'ninja', 'ctest'], env);
    return env;
}

/** build:server [preset]: configures when needed and builds. */
export async function buildServer(args: readonly string[]): Promise<void> {
    const preset = parsePreset(args);
    await build(await buildEnv(), preset);
}

/** test:server [preset]: builds, then runs the tests. */
export async function testServer(args: readonly string[]): Promise<void> {
    const preset = parsePreset(args);
    const env = await buildEnv();
    await build(env, preset);
    await test(env, preset);
}

/** format:server: reformats the C++ files with clang-format. */
export async function formatServer(args: readonly string[]): Promise<void> {
    noArgs('format:server', args);
    await requireLlvm(process.env);
    await stage('Format (clang-format)', () => format(process.env, true));
}

/** lint:server: builds the dev preset (for its compile database), then runs clang-tidy. */
export async function lintServer(args: readonly string[]): Promise<void> {
    noArgs('lint:server', args);
    const env = await buildEnv();
    await requireLlvm(env);
    await build(env, 'dev');
    await stage('Lint (clang-tidy)', () => lint(env));
}

/**
 * check:server: the server's quality gate. Formatting (checked, never changed), the dev build and
 * tests, clang-tidy, then the engine's build and tests under AddressSanitizer. Stops at the first
 * failing stage.
 */
export async function checkServer(args: readonly string[]): Promise<void> {
    noArgs('check:server', args);
    const env = await buildEnv();
    await requireLlvm(env);
    await stage('Format check (clang-format)', () => format(env, false));
    await build(env, 'dev');
    await test(env, 'dev');
    await stage('Lint (clang-tidy)', () => lint(env));
    await build(env, 'asan');
    await test(env, 'asan');
}

/** Builds the dev preset, then runs piano-lab from the folder npm was started in. */
async function runProgram(args: readonly string[]): Promise<number> {
    const preset: Preset = 'dev';
    const env = await buildEnv();
    await build(env, preset);
    console.log('');
    return run(programPath(preset), args, { env: process.env, cwd: userCwd });
}

/** start:server: builds, then plays the piano live until Ctrl+C. */
export async function startServer(args: readonly string[]): Promise<number> {
    noArgs('start:server', args);
    return runProgram(['server', 'start']);
}

/** render <input> [options]: builds, then renders a music file to WAV (see docs/rendering.md). */
export async function render(args: readonly string[]): Promise<number> {
    return runProgram(['render', ...args]);
}
