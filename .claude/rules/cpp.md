---
paths:
  - "engine/**"
  - "backend/**"
  - "cmake/**"
  - "**/CMakeLists.txt"
  - "CMakePresets.json"
---

# C++ and CMake

- **Follow [docs/coding-style.md](../../docs/coding-style.md)**; read it before writing or changing C++. In short: plain structs, free functions, immutable by default and mutable only on purpose, `std::variant` instead of inheritance, and no purist FP (no currying, monads or combinator chains).
- **Real-time rules:** nothing called while rendering may allocate, lock, block, throw or do I/O.
- **Engine boundary:** nothing in `engine/` may use JUCE, networking, JSON or `backend/`. A new engine target must be listed in `pianolab_engine_rules()` in `engine/CMakeLists.txt`.
- **Dependencies:** pinned in `cmake/Dependencies.cmake` (version + SHA-256), hidden behind our own interfaces, header-only ones included from one `.cpp` (see `docs/vision.md#dependency-rules`).
- **Build and check:** `.\tools\check.ps1` (`-Fix` applies clang-format). For single commands, run `. .\tools\dev-shell.ps1` first, then use the `dev`, `release` or `asan` presets.
- Modelling code cites its sources in comments.
