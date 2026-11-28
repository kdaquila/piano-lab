// check:web: the quality gate for the TypeScript (the web server, the web API and the task
// scripts' formatting). Formatting (checked, never changed), the build, the tests' types, the
// tests, then oxlint. Stops at the first failing stage.

import { build, format, lint, test, typecheckTests } from './tools.ts';
import { noArgs, runTask } from '../lib/run.ts';

await runTask(async (args) => {
    noArgs(args);
    await format(false);
    await build();
    await typecheckTests();
    await test();
    await lint();
});
