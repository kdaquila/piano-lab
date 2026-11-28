// lint:audio: builds the dev preset (for its compile database), then runs clang-tidy.

import { build, buildEnv } from './cmake.ts';
import { lint, requireLlvm } from './llvm.ts';
import { noArgs, runTask, stage } from '../lib/run.ts';

await runTask(async (args) => {
    noArgs(args);
    const env = await buildEnv();
    await requireLlvm(env);
    await build(env, 'dev');
    await stage('Lint (clang-tidy)', () => lint(env));
});
