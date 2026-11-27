// clang-format and clang-tidy over our C++ files.

import { readFileSync } from 'node:fs';
import { availableParallelism } from 'node:os';
import { join, resolve } from 'node:path';
import { buildDir } from './cmake.ts';
import {
    capture,
    type Env,
    exec,
    mapLimited,
    repoRoot,
    requirePrograms,
    TaskError,
} from '../lib/run.ts';

// clang-format and clang-tidy output can change between LLVM major versions, so everyone (people,
// agents, CI) uses the same one. Bump together with the README when upgrading.
const requiredLlvmMajor = 23;

// Keeps each clang-format command line well under Windows' 32,767-character limit.
const filesPerCommand = 100;

/** One entry of a compile database (compile_commands.json). */
interface CompileCommand {
    directory: string;
    file: string;
}

/** Throws unless clang-format and clang-tidy are on PATH, from the required LLVM version. */
export async function requireLlvm(env: Env): Promise<void> {
    requirePrograms(['clang-format', 'clang-tidy'], env);
    const version = (await capture('clang-format', ['--version'], { env })).stdout.trim();
    const major = /version (\d+)\./.exec(version)?.[1];
    if (major === undefined || Number(major) !== requiredLlvmMajor) {
        throw new TaskError(
            `LLVM ${requiredLlvmMajor} is required for clang-format and clang-tidy, found: ${version}`,
        );
    }
}

/** Our C++ files under engine/ and backend/: tracked or new, never ignored or dependencies. */
async function cppFiles(env: Env): Promise<string[]> {
    const listed = await capture(
        'git',
        ['ls-files', '--cached', '--others', '--exclude-standard', '--', 'engine', 'backend'],
        { env },
    );
    if (listed.code !== 0) {
        throw new TaskError(`git ls-files failed; is this a git checkout?\n${listed.stderr}`);
    }
    const files = listed.stdout
        .split('\n')
        .map((line) => line.trim())
        .filter((file) => /\.(cpp|hpp|h)$/.test(file));
    if (!files.some((file) => file.endsWith('.cpp'))) {
        throw new TaskError('No C++ source files found under engine/ or backend/.');
    }
    return files;
}

/** Reformats the C++ files with clang-format (`fix`), or only fails on badly formatted ones. */
export async function format(env: Env, fix: boolean): Promise<void> {
    const files = await cppFiles(env);
    const flags = fix ? ['-i'] : ['--dry-run', '--Werror'];
    for (let i = 0; i < files.length; i += filesPerCommand) {
        await exec('clang-format', [...flags, ...files.slice(i, i + filesPerCommand)], { env });
    }
}

/** Runs clang-tidy on every .cpp file, in parallel, with the exact flags of the dev build. */
export async function lint(env: Env): Promise<void> {
    const sources = (await cppFiles(env)).filter((file) => file.endsWith('.cpp'));

    // clang-tidy needs each file's build flags from the dev preset's compile database. A .cpp
    // that isn't in it isn't part of any target, so it is never built: report that instead of
    // letting clang-tidy guess the flags.
    const key = (path: string) => (process.platform === 'win32' ? path.toLowerCase() : path);
    const databaseFile = join(buildDir('dev'), 'compile_commands.json');
    const database = JSON.parse(readFileSync(databaseFile, 'utf8')) as CompileCommand[];
    const built = new Set(database.map((entry) => key(resolve(entry.directory, entry.file))));
    const notBuilt = sources.filter((file) => !built.has(key(resolve(repoRoot, file))));
    if (notBuilt.length > 0) {
        throw new TaskError(
            `Not part of any CMake target (add them to a CMakeLists.txt): ${notBuilt.join(', ')}`,
        );
    }

    const results = await mapLimited(sources, availableParallelism(), async (file) => ({
        file,
        result: await capture('clang-tidy', ['-p', 'build/dev', '--quiet', file], { env }),
    }));
    const failed = results.filter(({ result }) => result.code !== 0);
    for (const { result } of failed) {
        console.log((result.stdout + result.stderr).trimEnd());
    }
    if (failed.length > 0) {
        const files = failed.map(({ file }) => file);
        throw new TaskError(`clang-tidy found problems in: ${files.join(', ')}`);
    }
}
