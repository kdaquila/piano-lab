// check:ui: the quality gate for the web UI. Formatting (checked, never changed), the build (with
// its type check), the tests' types, the tests, then oxlint. Stops at the first failing stage.
// The end-to-end tests need a sound device, so they run with `npm run acceptance` instead.

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
