# Coding style (C++)

piano-lab's C++ is written in a **pragmatic functional style**: plain data, free functions, and immutable values by default. Mutation is allowed, but only on purpose. Formatting and naming are enforced by `.clang-format` and `.clang-tidy`; this document covers what the tools can't.

## The rules

### 1. Data is plain structs
- Use aggregates: public fields, no getters or setters, default member values, and designated initializers to build them (`SineConfig{.gain = 0.25}`).
- A `class` with private state is the exception. Use one only when it genuinely protects an invariant or owns a resource (RAII). It should be rare.

### 2. Behaviour is free functions
- Put functions in the library's namespace (`pianolab::models`), not inside types.
- Make them **pure** wherever possible: inputs by value or `const&`, the result returned, no hidden state, no side effects.
- Mark results `[[nodiscard]]`. Mark functions that run on the audio thread `noexcept`.
- Maths (tuning, coefficients, modal frequencies, hammer curves) is always pure functions. They're the easiest code to test.

### 3. Immutable by default, mutable on purpose
- Prefer `const` locals and returning new values over modifying existing ones. Small state structs are cheap to copy: `SineState next = strike(state, coeffs, velocity);`
- **Mutation is allowed** when it's needed for performance or the problem is inherently stateful. Typical cases are audio buffers, per-block state on the audio thread, and storage allocated up front.
- When something is mutated, keep it local and visible: a non-`const` reference or `std::span<float>` parameter means "this function writes here". Don't mutate casually, and don't mutate through hidden or shared state.

### 4. No purist functional programming
- Don't write our own functional abstractions: no home-made monads, currying, point-free style, combinator libraries, or chains of `std::function`.
- **The standard library's own are fine:** `std::optional` and `std::expected` with `and_then`, `transform` and `or_else`, and the standard ranges views, when they read clearly.
- Ordinary `for` loops are fine and often clearest. Use `<algorithm>`/`<ranges>` when it actually reads better.

### 5. Polymorphism with `std::variant`, not inheritance
- A closed set of alternatives is a `std::variant`, dispatched with `std::visit` (like a TypeScript discriminated union with a `switch`). Key models are the main example.
- **No inheritance hierarchies.** The one exception is implementing a third-party interface that requires it, such as JUCE's audio callback. That lives in a thin adapter in `backend/`, and the rest of the code never sees it.

### 6. Real-time rules (the audio thread)
- Never allocate, lock, block, throw, or do I/O while rendering. Setup functions (`prepare…`) may allocate; rendering may not. The no-allocation test enforces this.
- Use fixed-size data on the audio path. Audio buffers are `std::span<float>`, and model state is `double` where precision matters.

### 7. Threads share immutable snapshots or queues
- Never share mutable fields between threads.
- Settings changes are new immutable snapshots handed to the audio thread. Events travel through single-producer/single-consumer queues (`rt::SpscQueue`). Single numbers can be atomics.

### 8. Errors are recovered and reported, never fatal
- **The music never stops for an error**, in any build. There are no aborting `assert`s, and nothing on the audio thread throws.
- On the audio path, every problem gets a defined fallback (clamp, skip, split, silence) and is recorded in the engine's fixed-size `Diagnostics` (a counter per problem kind). The backend reads them on another thread, logs everything, and forwards them to the UI.
- **Errors are values, Rust-style.** An operation that can fail returns `std::expected<T, Error>` (our `Result`), and "maybe a value" is `std::optional<T>` (our `Option`). Callers handle them in ordinary code (`if (!result) { … }`), or with the standard monadic methods.
- **One error type**, `Error{message}`, with a clear human-readable message. No error enums or hierarchies until a real need shows up.
- **Our code doesn't throw**, and nothing catches by exception type. The standard library can still throw in setup code (e.g. out of memory), so the backend has one catch-all at its outer boundary that logs the message and keeps going.
- **Tests are the strict part:** normal-use tests check that the diagnostics stay at zero, and dedicated tests trigger each problem and check both the recovery and the recorded diagnostic.

### 9. Every declaration has a doc comment
- **Every function, type, concept, type alias and namespace-level constant gets a `///` doc comment**: public and internal alike, including helpers in anonymous namespaces and in tests.
- Exceptions: lambdas, `TEST_CASE`s (the test name is the description), and a `.cpp` definition of a function already documented at its declaration (document once, where it's declared). A set of standard overloads, such as the `operator new`/`delete` family, may share one comment.
- **What to write:** start with one sentence on what it does or returns. Then add whatever isn't obvious from the signature: units (seconds, samples, Hz, 0-1), what happens with bad input (recovered and recorded, or the `Error` it returns), real-time safety, which parameters it writes to, and sources for modelling choices.
- **Style:** `///` above a declaration, `///<` after a struct field. Plain prose, with parameter names in backticks. No boilerplate `@param` tags that only repeat the name.
- Struct fields get a comment when their meaning or units aren't obvious from the name.

### 10. Code organisation
- One topic per header/source pair, with `snake_case` file names. Public headers live in `include/pianolab/<library>/`.
- Modelling code cites its sources (paper, book, or URL) in a comment next to the decision it informs.

## Example
From `engine/models/include/pianolab/models/sine_key.hpp`:
```cpp
// Plain data: configuration (defaults = built-in settings), derived coefficients, running state.
struct SineConfig { double gain = 0.25; double attackT60 = 0.005; double releaseT60 = 0.25; /*...*/ };
struct SineCoeffs { double phaseStep; double gain; double attackCoeff; double releaseCoeff; /*...*/ };
struct SineState  { double phase; double amplitude; double target; double coeff; bool released; };
struct SineKey    { SineCoeffs coeffs; SineState state; };

// Each setting declared once: name, member, range, unit, description.
inline constexpr std::tuple sineConfigFields{
    Field<SineConfig, double>{.name = "gain", .member = &SineConfig::gain, .min = 0.0, .max = 1.0, /*...*/},
    /*...*/
};

// Errors as values: always a usable config, plus an Error for each value that was replaced.
[[nodiscard]] Validated<SineConfig> validate(const SineConfig& config, std::string_view prefix = "");

// Setup: only accepts a validated config, so it can't fail.
[[nodiscard]] SineKey makeSineKey(double frequency, const Valid<SineConfig>& config, double sampleRate) noexcept;

// Pure: an event takes the old key and returns the new one.
[[nodiscard]] SineKey strike(SineKey key, float velocity) noexcept;

// Mutation on purpose: adds into the audio buffer, and returns the key's updated state.
[[nodiscard]] SineKey renderAdd(SineKey key, std::span<float> out) noexcept;
```
Key models are combined as `using PianoKey = std::variant<SineKey /*, ...*/>;`, and the keyboard accepts any type that provides these functions (the `KeyModel` concept in `engine_core/keyboard.hpp`).
