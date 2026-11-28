// The web UI's tools: tsc (types only), Vite, Vitest, Playwright, oxlint and oxfmt, all installed
// by npm and run here with the Node that runs the task.

import { stage } from '../lib/run.ts';
import { tool } from '../web/tools.ts';

/** The web UI's own folder, for the tools that take paths. */
const folder = 'web-ui';

/** Type-checks the web UI, then builds it to `web-ui/dist/`, which the web server serves. */
export async function build(): Promise<void> {
    // The web UI imports the web API package as built.
    await stage('Build the web API (tsc)', () => tool('typescript/bin/tsc', ['-b', 'web-api']));
    await stage('Type check (tsc)', () => tool('typescript/bin/tsc', ['-p', folder]));
    await stage('Build (Vite)', () =>
        tool('vite/bin/vite.js', ['build', folder, '--logLevel', 'warn']),
    );
}

/** Type-checks the tests and the tool configs, which run without their types being checked. */
export function typecheckTests(): Promise<void> {
    return stage('Type check the tests (tsc)', async () => {
        await tool('typescript/bin/tsc', ['-p', `${folder}/tests`]);
        await tool('typescript/bin/tsc', ['-p', `${folder}/e2e`]);
    });
}

/** Runs the unit and component tests, once. */
export function test(): Promise<void> {
    return stage('Tests (Vitest)', () =>
        tool('vitest/vitest.mjs', ['run', '--config', `${folder}/vitest.config.ts`]),
    );
}

/** Runs the end-to-end tests in Edge, against a real piano-lab (they need a sound device). */
export function endToEnd(): Promise<void> {
    return stage('End-to-end tests (Playwright)', () =>
        tool('@playwright/test/cli.js', ['test', '--config', `${folder}/playwright.config.ts`]),
    );
}

/** Lints the web UI, failing on warnings too. */
export function lint(): Promise<void> {
    return stage('Lint (oxlint)', () => tool('oxlint/bin/oxlint', ['--deny-warnings', folder]));
}

/** Formats the web UI: rewrites the files when `write`, otherwise only checks them. */
export function format(write: boolean): Promise<void> {
    const name = write ? 'Format (oxfmt)' : 'Format check (oxfmt)';
    return stage(name, () => tool('oxfmt/bin/oxfmt', [write ? '--write' : '--check', folder]));
}
