---
paths:
  - "scripts/**"
  - "package.json"
  - ".npmrc"
---

# Task scripts (npm + TypeScript)

- The npm commands in `package.json` all call `node scripts/task.ts <task>`. Tasks for one part are `<task>:<part>` (`build:server`); the plain name (`build`) runs every part. A new task goes in the table in `scripts/task.ts`, its code in the part's folder (`scripts/server/`), shared plumbing in `scripts/lib/`.
- Node (24.12+) runs the `.ts` files directly by stripping the types, with no build step. So use only erasable syntax (no `enum`, `namespace` or constructor parameter properties), use `import type` for types, and import other scripts by their `.ts` name. `tsconfig.json` enforces this, and `npm run check` runs `tsc` on the scripts.
- Start programs through `scripts/lib/run.ts` (`exec`, `run`, `capture`): never through a shell, never with a command line built as one string (the Visual Studio setup in `msvc.ts` is the one exception, since `vcvars64.bat` only runs in cmd).
- C++ tools get the environment from `msvcEnv()`; never change the user's terminal.
- Report expected failures with `TaskError` (shown without a stack trace).
- Dependencies are dev tools only, at exact versions (`.npmrc` has `save-exact`).
