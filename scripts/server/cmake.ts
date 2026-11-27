// Configuring, building and testing the C++ side with the CMake presets (CMakePresets.json).

import { existsSync, statSync } from 'node:fs';
import { join } from 'node:path';
import { type Env, exec, repoRoot, stage, TaskError } from '../lib/run.ts';

/** The presets: everyday debug, optimised, and the engine under AddressSanitizer. */
export const presets = ['dev', 'release', 'asan'] as const;
export type Preset = (typeof presets)[number];

/** Reads the optional preset argument of build:server and test:server. `dev` when there's none. */
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
    return join(buildDir(preset), 'backend', 'app', 'piano-lab.exe');
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
