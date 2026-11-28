#!/usr/bin/env node
// The `piano-lab` command. Its code is compiled to dist/ (`npm run build`); this small file is
// what npm links as the command, so the link exists even before the first build.

import { existsSync } from 'node:fs';

const program = new URL('../dist/cli.js', import.meta.url);
if (existsSync(program)) {
    await import(program.href);
} else {
    console.error("error: piano-lab isn't built yet; run npm run build");
    process.exitCode = 1;
}
