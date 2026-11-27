// check:server: the server's quality gate. Formatting (checked, never changed), the dev build and
// tests, clang-tidy, then the engine's build and tests under AddressSanitizer. Stops at the first
// failing stage.

import { build, buildEnv, test } from './cmake.ts';
import { format, lint, requireLlvm } from './llvm.ts';
import { noArgs, runTask, stage } from '../lib/run.ts';

await runTask(async (args) => {
    noArgs(args);
    const env = await buildEnv();
    await requireLlvm(env);
    await stage('Format check (clang-format)', () => format(env, false));
    await build(env, 'dev');
    await test(env, 'dev');
    await stage('Lint (clang-tidy)', () => lint(env));
    await build(env, 'asan');
    await test(env, 'asan');
});
