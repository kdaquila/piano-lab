// Configuring, building and testing the C++ side with the CMake presets (CMakePresets.json).

import { existsSync, statSync } from 'node:fs';
import { join } from 'node:path';
import { msvcEnv } from '../lib/msvc.ts';
import {
    type Env,
    exec,
    repoRoot,
    requirePrograms,
    run,
    stage,
    TaskError,
    userCwd,
} from '../lib/run.ts';

/** The presets: everyday debug, optimised, and the sound engine under AddressSanitizer. */
export const presets = ['dev', 'release', 'asan'] as const;
export type Preset = (typeof presets)[number];

/** Reads the optional preset argument of build:audio and test:audio: `dev` when there's none. */
export function parsePreset(args: readonly string[]): Preset {
    if (args.length > 1) {
        throw new TaskError(`Expected at most one preset, got: ${args.join(' ')}`);
    }
    const name = args[0] ?? 'dev';
    const preset = presets.find((p) => p === name);
    if (preset === undefined) {
        throw new TaskError(`Unknown preset "${name}". The presets are: ${presets.join(', ')}.`);
    }
    return preset;
}

/** The preset's build folder. */
export function buildDir(preset: Preset): string {
    return join(repoRoot, 'build', preset);
}

/** The piano-lab program built by a preset. */
export function programPath(preset: Preset): string {
    return join(buildDir(preset), 'audio-service', 'app', 'piano-lab.exe');
}

/**
 * Configures when the build folder is new or the presets changed since. A build re-runs CMake by
 * itself when a CMakeLists.txt changes, but not when a preset's cache variables do.
 */
function needsConfigure(preset: Preset): boolean {
    const cache = join(buildDir(preset), 'CMakeCache.txt');
    if (!existsSync(cache)) return true;
    const configured = statSync(cache).mtimeMs;
    return ['CMakePresets.json', 'CMakeUserPresets.json']
        .map((file) => join(repoRoot, file))
        .some((file) => existsSync(file) && statSync(file).mtimeMs > configured);
}

/** The Visual Studio build environment, with the build tools checked up front. */
export async function buildEnv(): Promise<Env> {
    const env = await msvcEnv();
    requirePrograms(['cl', 'cmake', 'ninja', 'ctest'], env);
    return env;
}

/** Configures (when needed) and builds a preset. */
export async function build(env: Env, preset: Preset): Promise<void> {
    if (needsConfigure(preset)) {
        await stage(`Configure (${preset})`, () => exec('cmake', ['--preset', preset], { env }));
    }
    await stage(`Build (${preset})`, () => exec('cmake', ['--build', '--preset', preset], { env }));
}

/** Runs a built preset's tests. */
export async function test(env: Env, preset: Preset): Promise<void> {
    await stage(`Test (${preset})`, () => exec('ctest', ['--preset', preset], { env }));
}

/** Builds the dev preset, then runs piano-lab from the folder npm was started in. */
export async function runProgram(args: readonly string[]): Promise<number> {
    const env = await buildEnv();
    await build(env, 'dev');
    console.log('');
    return run(programPath('dev'), args, { env: process.env, cwd: userCwd });
}
