// The web server's and the web API's tools: tsc, Vitest, oxlint and oxfmt, all installed by npm
// and run here with the Node that runs the task.

import { join } from 'node:path';
import { exec, repoRoot, stage } from '../lib/run.ts';

/** Runs an npm-installed tool's command-line script, from `node_modules/<path>`. */
export function tool(path: string, args: readonly string[]): Promise<void> {
    return exec(process.execPath, [join(repoRoot, 'node_modules', path), ...args], {
        env: process.env,
    });
}

/** Builds the web server, and the web API package it uses, to their `dist/` folders. */
export function build(): Promise<void> {
    return stage('Build (tsc)', () => tool('typescript/bin/tsc', ['-b', 'web-server']));
}

/** Type-checks the tests, which Vitest runs from their .ts files without checking types. */
export function typecheckTests(): Promise<void> {
    return stage('Type check the tests (tsc)', async () => {
        await tool('typescript/bin/tsc', ['-p', 'web-api/tests']);
        await tool('typescript/bin/tsc', ['-p', 'web-server/tests']);
        await tool('typescript/bin/tsc', ['-p', 'web-server/acceptance']);
    });
}

/** Runs the tests, once. */
export function test(): Promise<void> {
    return stage('Tests (Vitest)', () => tool('vitest/vitest.mjs', ['run']));
}

/** Lints all the TypeScript, failing on warnings too. */
export function lint(): Promise<void> {
    return stage('Lint (oxlint)', () => tool('oxlint/bin/oxlint', ['--deny-warnings']));
}

/**
 * Formats all the TypeScript and JavaScript (the web server, the web API and the task scripts):
 * rewrites the files when `write`, otherwise only checks them.
 */
export function format(write: boolean): Promise<void> {
    const name = write ? 'Format (oxfmt)' : 'Format check (oxfmt)';
    return stage(name, () => tool('oxfmt/bin/oxfmt', [write ? '--write' : '--check']));
}
