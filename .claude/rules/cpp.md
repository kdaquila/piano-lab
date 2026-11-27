---
paths:
  - "sound-engine/**"
  - "audio-service/**"
  - "cmake/**"
  - "**/CMakeLists.txt"
  - "CMakePresets.json"
---

# C++ and CMake

- **Follow [docs/coding-style.md](../../docs/coding-style.md)**; read it before writing or changing C++. In short: plain structs, free functions, immutable by default and mutable only on purpose, `std::variant` instead of inheritance, errors as values (`std::expected`, `std::optional`), our code never throws, and no home-made monads or combinators (standard ones are fine).
- **Template parameters:** single letters (`C`, `V`, `K`, `F`, `T`; packs `Ts`) for up to three parameters; descriptive words only beyond three (coding-style section 10).
- **C++23 features:** only those in [cpp23.md](cpp23.md)'s "use freely" list, which are supported by every target compiler.
- **Real-time rules:** nothing called while rendering may allocate, lock, block, throw or do I/O.
- **Engine boundary:** nothing in `sound-engine/` may use JUCE, networking, JSON or `audio-service/`. A new sound-engine target must be listed in `pianolab_engine_rules()` in `sound-engine/CMakeLists.txt`.
- **Dependencies:** pinned in `cmake/Dependencies.cmake` (version + SHA-256), hidden behind our own interfaces, header-only ones included from one `.cpp` (see `docs/vision.md#dependency-rules`).
- **Build and check:** `npm run check` before handing over; `npm run format` applies clang-format. `npm run build:audio -- <preset>` and `npm run test:audio -- <preset>` take the `dev`, `release` or `asan` preset. Use the npm commands rather than raw `cmake`: they set up the Visual Studio environment.
- Modelling code cites its sources in comments.
