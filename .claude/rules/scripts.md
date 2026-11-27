---
paths:
  - "scripts/**"
  - "package.json"
  - ".npmrc"
---

# Task scripts (npm + TypeScript)

- `package.json` is the list of tasks. Tasks for one part are `<task>:<part>` (`build:server`), each its own file in the part's folder (`node scripts/server/build.ts`) that calls `runTask` once. The plain name runs every part with npm-run-all2 (`run-s "build:*"`), so a new part's tasks join without edits. Shared plumbing goes in `scripts/lib/`.
- Node (24.15+) runs the `.ts` files directly by stripping the types, with no build step. So use only erasable syntax (no `enum`, `namespace` or constructor parameter properties), use `import type` for types, and import other scripts by their `.ts` name. `tsconfig.json` enforces this, and `npm run check` runs `tsc` on the scripts.
- Start programs through `scripts/lib/run.ts` (`exec`, `run`, `capture`): never through a shell, never with a command line built as one string (the Visual Studio setup in `msvc.ts` is the one exception, since `vcvars64.bat` only runs in cmd).
- C++ tools get the environment from `msvcEnv()`; never change the user's terminal.
- Report expected failures with `TaskError` (shown without a stack trace).
- Dependencies are dev tools only, at exact versions (`.npmrc` has `save-exact`).
