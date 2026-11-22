---
paths:
  - "engine/**/*.{hpp,cpp,h}"
  - "backend/**/*.{hpp,cpp,h}"
---

# C++ doc comments

Full rule: [docs/coding-style.md](../../docs/coding-style.md), section 9. Checklist for every change:

- **Every function, type, concept, alias and namespace-level constant you add or touch has a `///` doc comment**, including anonymous-namespace and test helpers.
- Skip only: lambdas, `TEST_CASE`s, and `.cpp` definitions of functions documented at their declaration. A set of standard overloads may share one comment.
- First sentence: what it does or returns. Then the non-obvious parts: units, bad-input behaviour (recovered and recorded, or the `Error` returned), real-time safety, parameters it writes to, and modelling sources.
- `///` above a declaration, `///<` after a field. Prose with names in backticks; no boilerplate `@param` tags.
- Before handing work over, scan your diff for undocumented declarations. No tool checks this yet.
