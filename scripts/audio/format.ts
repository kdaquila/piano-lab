// format:audio: reformats the C++ files with clang-format.

import { format, requireLlvm } from './llvm.ts';
import { noArgs, runTask, stage } from '../lib/run.ts';

await runTask(async (args) => {
    noArgs(args);
    await requireLlvm(process.env);
    await stage('Format (clang-format)', () => format(process.env, true));
});
