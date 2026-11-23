# Milestone 0: Skeleton

> **Re-planned 2026-11-21** for the new stack (C++ engine, C++/JUCE backend, React UI) and architecture (local backend, web UI over the local network). See [vision.md](../vision.md) for the terms *engine*, *backend* and *UI*. Each step's open questions get settled, one at a time, before that step starts.

**Goal:** a walking skeleton through every layer of the new architecture. MIDI in → one sine per note → audio out, with the backend serving a React UI that works in a local window **and on a phone on the same network**. It's deliberately simple: a sine is a one-mode modal string, so this is the real engine at its simplest, not throwaway code.

**Done when:** on a Windows laptop, you can play your MIDI keyboard and hear a clean sine per note; adjust volume and release from your phone; and we have measured key-to-sound latency for each Windows audio mode.

## Steps

| Step | What | Status |
|---|---|---|
| [0.1](#01-c-project-skeleton) | C++ project skeleton: CMake, JUCE, tests, linting | Done ([#3](https://github.com/kdaquila/piano-lab/pull/3)) |
| [0.2](#02-engine-core-port) | Engine core: key model, 88-key keyboard, sine, headless tests | Done ([#4](https://github.com/kdaquila/piano-lab/pull/4)) |
| [0.3](#03-offline-render-to-wav) | Offline render to WAV: MIDI or MusicXML in, WAV out, no devices | In progress |
| [0.4](#04-headless-backend-app) | Headless backend app: audio out + MIDI in through JUCE, recording to WAV | Not started |
| [0.5](#05-backend-configuration) | Backend configuration: user settings merged over the built-ins | Not started |
| [0.6](#06-control-server) | Control server: embedded HTTP + WebSocket, protocol v0 | Not started |
| [0.7](#07-react-ui) | React UI: mobile-first, served by the backend, local window + phone | Not started |
| [0.8](#08-latency-measurement) | Latency measurement across Windows audio modes | Not started |

---

## 0.1 C++ project skeleton

**What:** replace the Rust workspace with a C++ project that builds, tests and lints cleanly. No audio code yet.

**Includes:**
- CMake project with JUCE pulled in as a dependency
- Three top-level folders, one per part of the architecture:
  - `engine/`: `engine-core` (pure, no devices/files/UI), `models`, `calibration`, `rt` (real-time plumbing such as lock-free queues), `render-cli` (moved to `backend/` in 0.3). Standard C++ only.
  - `backend/`: `juce` (the JUCE modules, compiled once) and `app` (devices, MIDI, and later the server and protocol)
  - `ui/`: the React app, from step 0.7
- A test framework with one placeholder test per engine library
- Strict compiler warnings as errors, `clang-format`, `clang-tidy`
- Deleting the Rust crates, `Cargo.toml`, `Cargo.lock` and `rust-toolchain.toml` outright, with no `prototype/` folder. They stay in git history and in PR #1, and get read from there when porting.
- Installing any missing tools (e.g. CMake/Ninja/LLVM) and documenting setup in the README

**Acceptance checks:**
- Configure, build and test pass from a clean checkout with one documented command each
- `clang-format` and `clang-tidy` report nothing
- `engine-core` depends on nothing else in the project, and nothing in `engine/` depends on JUCE (the build refuses to configure if an engine library links anything outside its allowed list; the `asan` preset builds the engine without JUCE at all)

**Decisions:**
- **Layout: `engine/` + `backend/` + `ui/`**, so the "engine never touches JUCE" rule is visible in the folder structure. Headers live in `include/pianolab/<library>/`, so includes read `#include <pianolab/engine_core/…>`.
- **Pinned versions:** JUCE 9.0.6, Catch2 v3.16.1, CPM.cmake v0.43.2, each checked by SHA-256. JUCE 9 (July 2026) keeps JUCE 8's free Starter tier and needs only C++17. Tools: CMake 4.4, Ninja 1.13, LLVM 23 (clang-format and clang-tidy versions matter for stable formatting).
- **JUCE is compiled once, in its own static library** (`backend/juce`). Our strict warnings stay off JUCE's code, its `#define` configuration lives in one place, and its headers reach our code as system includes.
- **Compiler and build:** MSVC is the primary Windows compiler (best JUCE support, debugger, AddressSanitizer). clang-tidy runs from day one as a second compiler's eyes on the code. CMake + Ninja + CMake presets, so adding clang-cl, Linux or macOS builds later is a few lines.
- **No cross-compiling.** Each OS gets a native build (MSVC / Apple Clang / GCC or Clang), on cloud runners once CI is turned on. Portability comes from CMake, strict warnings (`/W4 /permissive-`) and multi-compiler checks, not from the toolchain.

- **Dependencies via CPM.cmake:** one small file in the repo (a wrapper around CMake's FetchContent) with a shared download cache. Every dependency is pinned to an exact version tag or commit, for reproducible builds. Moving to vcpkg later stays possible if the list grows heavy.

- **C++23** (changed from C++20 on 2026-11-22). Every target compiler now supports `std::expected`, our `Result` type: MSVC 19.33+, GCC 12+, Clang 16+, Apple Clang 15+. JUCE 9 and Catch2 build cleanly with it (verified). Features are used only when **all** targets support them (MSVC 19.44, GCC 13, Clang 17, Apple Clang 15); `.claude/rules/cpp23.md` lists which ones, from the cppreference support table. Known costs, for later: building on Linux needs GCC 13+ or Clang 17+ (e.g. Ubuntu 24.04, Debian 13; not Ubuntu 22.04), shipped Linux binaries need care with the C++ runtime version, and macOS development needs Xcode 15+.

- **Tests: Catch2 v3**, run through CTest. Readable pytest-like assertions, float matchers (`WithinAbs`, `WithinRel`) for numeric audio checks, and built-in benchmarking for the CPU-budget tests later. Engine tests use real objects headless, so no mocking framework is needed.

- **Code quality bundle:**
  - Warnings: `/W4 /permissive- /WX` on our code (not on dependencies)
  - clang-format: LLVM base style, 4-space indents, 100-column lines
  - Naming: `PascalCase` types, `camelCase` functions and variables
  - clang-tidy: the bugprone, performance, modernize and concurrency groups, plus curated readability and Core Guidelines checks
  - An AddressSanitizer test preset
  - The ported no-allocation test (arrives in 0.2, with the rendering code it checks)
  - One check script (`tools/check.ps1`) that formats, lints, builds and tests: the same gate for you, me and agents
- Tools to install at the start of the step if missing: LLVM (clang-format, clang-tidy), CMake, Ninja.
- **Editor: VS Code + clangd.** CMake presets export `compile_commands.json` so clangd (and any agent tooling) sees the exact build. The repo ships `.clang-format`, `.clang-tidy` and recommended VS Code extensions. Visual Studio 2022 stays available as the heavy-duty debugger.

- **The engine is JUCE-free: standard C++ only.** JUCE (and cpp-httplib, nlohmann/json) live only in `backend/`. This keeps engine tests fast (no JUCE build), keeps the physics engine portable (render CLI, future Python package, plugin, other platforms) and cleanly MIT, with JUCE's terms only touching the backend binary. Utilities we'd otherwise take from JUCE (FIFOs, smoothed values) are small standard-C++ components.

## 0.2 Engine core port

**What:** port the Rust prototype's engine to C++, keeping its design. Pure code, fully tested headless.

**Includes:**
- The `KeyModel` concept (strike, release, render-add, is-silent), note events with sample offsets, the 88-key `Keyboard`, `Diagnostics`, offline render, equal-temperament tuning
- The sine key model (`double` phase and amplitude, attack glide, exponential release, continuous phase) and `PianoKey`, the variant of all key models
- **Configuration types with validators:** `RenderConfig`, `TuningConfig`, `SineConfig` and the combined `PianoConfig`, each with a declared-once field table and a `validate` function; the `Error` type
- The same headless tests as the prototype, plus tests for every recovery path and for validation, all driving the engine through a small config-based test harness

**Carried-over decisions:** one fixed model per key, no voice stealing; sample-accurate events; `float` buffers with `double` model state; no allocation while rendering, enforced by a test.

**Decisions:**
- **Pragmatic functional style**, written up in [docs/coding-style.md](../coding-style.md): plain structs, free functions, immutable by default, mutable only on purpose, no inheritance, and no purist FP.
- **Key models are a `std::variant`** (`models::PianoKey`), not a virtual interface. Each model is plain data plus free functions that take the key and return the new one (`strike`, `release`, `renderAdd`, `isSilent`). All 88 keys sit inline in a `std::array`, and a key's model can be swapped by assigning a new value.
- **The keyboard is a template over a `KeyModel` concept.** The variant has to name every model, and models live above `engine-core`, so `engine-core` defines what a key model must provide (a concept) and a keyboard for any such type. `models` then defines `PianoKey` and `PianoKeyboard = Keyboard<PianoKey>`. This keeps `engine-core` independent of the models, and lets its tests use a trivial test key.
- **Dispatch never throws.** `std::visit` can throw if a variant is empty, so `PianoKey` uses a small non-throwing dispatch that falls back to leaving the key unchanged.
- **The music never stops for an error** (coding-style rule 8). The engine recovers and records instead of asserting: oversized blocks are rendered in chunks, out-of-order events are moved forward, late events are applied at the block's end, off-keyboard notes are ignored, velocities are clamped, and NaN or infinite samples become silence. Each is recorded in the keyboard's fixed-size `Diagnostics` (a total plus the last message; messages are string literals, so recording never allocates). Tests check that diagnostics stay at zero in normal use, and test each recovery directly.
- **Errors are values, and there is one error type.** `Error{message}`; no error enums or hierarchies yet. Fallible operations will return `std::expected<T, Error>` (from step 0.5, e.g. loading a user config file). Our code doesn't throw.
- **Configuration: built-in defaults plus validators, declared once.** Each library defines plain config structs whose default values are the built-in settings (`RenderConfig` and `TuningConfig` in `engine-core`; `SineConfig` and the combined `PianoConfig` in `models`). Next to each struct, a table of `Field`s declares every setting once: name, member, valid range, unit and description. A generic checker uses the table, so `validate` *always* returns a usable `Valid<Config>`, replacing bad values (out of range, or NaN) with the built-in default and adding an `Error` like "sine.attackT60: -0.1 s is outside 0.0005-1 s; using the default 0.005 s". The same tables will drive user-file loading, the protocol and the UI later.
- **Engine setup only accepts `Valid` configs**, so it can't fail: `makeKeyboard(Valid<RenderConfig>, …)` and `makePianoKeyboard(Valid<PianoConfig>)`. `Valid<T>` can only be created by validation (a private constructor); `validPart` hands out a valid part of a valid whole.
- **Tests drive the engine through a small harness** (`models/tests/harness.hpp`): start from the built-in `PianoConfig`, override what the test needs, validate (failing the test if anything had to be fixed), and build the keyboard. These are the same entry points the backend will use.
- **What stays a constant:** facts and definitions rather than choices, such as 88 keys, A0 = MIDI 21, A4 = MIDI 69, 12 semitones per octave, and "60 dB = a factor of 1000". They're named constants with doc comments.
- **The no-allocation test counts instead of aborting.** It replaces the global `operator new`/`delete` in its own test binary and counts allocations during a checked region. Self-tests prove it catches ordinary and over-aligned allocations, and it was confirmed to fail when an allocation was planted in the sine model's render loop.

**Acceptance checks (automated):**
- A4 renders at 440 Hz ± 0.1; A0, middle C and C8 at their pitches
- Not silent, not clipping; velocity scales loudness
- Release falls below −90 dB and the key reports silent
- Note starts on its exact sample; re-strikes don't click; renders are deterministic
- No allocation while rendering: a test binary replaces the global allocator and fails on any allocation in the checked region (confirmed to catch a deliberate allocation)
- Tests also pass under AddressSanitizer

## 0.3 Offline render to WAV

**What:** render music files to WAV with the real engine and no audio devices, so AI agents (and tests) can get the produced sound and measure it.

**Includes:**
- **Engine:** a `Performance` type (what the pianist plays: note events with times in seconds), its validation, conversion to sample-exact events, and rendering until the piano falls silent
- **Backend readers:** Standard MIDI files (`.mid`) and MusicXML (`.musicxml`, `.xml`, and compressed `.mxl`), each turned into a `Performance`
- A WAV writer: 32-bit float, mono
- `piano-lab-render` (`backend/render-cli`): `piano-lab-render <input> [-o out.wav] [--sample-rate N]`
- Tests: readers on small inputs built in code, a WAV reads back as what was rendered, renders are byte-identical across runs, and bad input is reported, never fatal

**Acceptance checks:**
- `piano-lab-render` turns a MuseScore export (MIDI or MusicXML) into a WAV that plays in an ordinary audio player
- The same input always gives a byte-identical file
- Unsupported notation is reported with where it was found, and the rest still renders

**Why before devices:** agents can't listen, so a deterministic, fast way to get audio into files they can measure (and later look at as spectrograms) is the foundation for voicing the piano with their help. Hearing it live isn't urgent.

**Decisions:**
- **Input is standard music files, not a format of our own.** MIDI is the path for real pieces exported from MuseScore or a DAW; MusicXML is the path for agents, who write short test sequences as text. Either one opens in MuseScore, so the owner can look at and play anything an agent wrote.
- **The engine owns the music type; the backend converts files into it**, the same pattern as configuration. The engine never sees a file format, so `render-cli` moved from `engine/` to `backend/`, where JUCE reads MIDI, XML and zip files.
- **`Performance` is a flat list of timed events in seconds**: what the hands (and later feet) do, not a score. It matches live play, where a key's duration isn't known until it's released, so files and live input share one model. It is a starting point, expected to grow (sustain pedal is the obvious next event). Validation sorts it, drops events with impossible times (beyond two hours counts as a broken file) or notes off the keyboard, and clamps velocities, reporting each. At the same instant, a release that ends an earlier note comes before the presses, so repeated notes re-strike; a release of a note pressed at that same instant comes after it, so zero-length MIDI notes sound briefly instead of sticking.
- **Reading is deliberately lossy.** Each reader keeps what affects the sound, drops visual-only notation silently (layout, beams, stems, fingering, lyrics), and drops anything that changes the sound but isn't supported yet *with a report*, once per kind with where it first appears.
- **The initial MusicXML subset** (partwise scores): notes, rests, chords, ties, multiple voices and staves (`backup`/`forward`), several parts (all played on the one piano), tempo (`<sound tempo>`, quarter notes per minute; default 120) and dynamics. Reported and skipped for now: repeats, endings and jumps (played straight through), grace notes, fermatas, ornaments, tremolos, articulations, arpeggios, glissandos, hairpins, pedal markings and microtones.
- **Dynamics:** a note's own `dynamics` attribute wins; otherwise the latest `<sound dynamics>` or dynamic marking in that part. Both MusicXML scales are percentages of forte, MIDI velocity 90 (MusicXML 4.0 spec). Markings without a `<sound>` use MuseScore's velocities (ppp 16, pp 33, p 49, mp 64, mf 80, f 96, ff 112, fff 126); with no dynamics at all, notes play at mf.
- **MIDI:** every channel plays the piano. Velocity maps linearly (`v / 127`). Pedals (controllers 64, 66, 67) are reported once and skipped; other controllers, program changes and pitch bend are ignored silently.
- **WAV output is 32-bit float, mono, 48 kHz by default.** Float stores exactly what the engine produced (so byte-identical tests test the engine) and keeps overloads above 1.0 visible to analysis instead of clipping them.
- **A render runs until every key is silent**, with a 30-second safety cap after the last event that is reported if hit. "Silent" is each model's own threshold (`sine.silenceThreshold`, -100 dB by default), which becomes user-settable with configuration in 0.5.
- **Exit codes:** 0 whenever a WAV was written, with reports on stderr and a summary line; 1 only when nothing could be written (bad arguments, unreadable input, unwritable output, an output that is the input, or a render too long for one WAV file). WAVs are written to a temporary file and renamed into place, so a failed write leaves no half-written file.

**Follow-ups:**
- The `asan` preset skips the backend, so the readers don't get AddressSanitizer runs yet.
- `render-cli` takes configuration (including `silenceThreshold`, probably in dB) from step 0.5.

## 0.4 Headless backend app

**What:** the backend as a console app, running the engine against real devices through JUCE.

**Includes:**
- Opening an audio output device at 48 kHz, with **Windows Audio (shared)** as the zero-setup default and the other JUCE device types (low-latency shared, exclusive, ASIO) selectable
- Opening a MIDI input and turning note on/off into engine events, passed to the audio thread through a lock-free queue
- Command-line options to list and choose devices and buffer size (until configuration arrives in 0.5)
- **Recording:** `--record take.wav` writes what's played to a file while playing live. The audio thread hands samples to a writer thread through a lock-free queue, so recording can't cause dropouts.
- **File-output mode:** runs the full backend pipeline (threads, queues, MIDI handling) writing to a WAV file instead of a sound card, with events from a file, for testing the backend without hardware
- **Logging:** a non-audio thread regularly picks up the engine's diagnostics and logs them (console and a log file). Setup failures (e.g. a device that won't open) are caught, logged, and fall back to a default instead of exiting.

**Acceptance checks:**
- Turning raw MIDI messages into engine events is unit-tested
- You hear a clean sine per key with no clicks or dropouts, including chords and fast passages (manual)
- A recording matches what was heard; file-output mode produces the expected WAV
- The audio callback never allocates or locks (allocation test plus review)

**Decisions:**
- **Lock-free queues: rigtorp/SPSCQueue**, wrapped as `rt::SpscQueue<T>` in a small JUCE-free `rt` library. One single-producer/single-consumer queue per thread pair (MIDI → audio, server → audio, audio → server, audio → recorder). Fixed capacity, allocated at startup, so the audio thread never allocates. Simple settings like volume can be single atomics.
- A **stress test** (two threads hammering a queue; nothing lost, duplicated or reordered) runs from this step. ThreadSanitizer joins once Linux builds exist (it isn't available with MSVC).

**Open questions:**
- ASIO in this step or later? (Needs the ASIO SDK at build time; it's now available under GPLv3 or Steinberg's license.)

## 0.5 Backend configuration

**What:** users' own settings, merged over the built-in configuration.

**Includes:**
- User settings holding only what the user changed, merged over the built-in `PianoConfig` and then validated with the engine's `validate`. Loading returns `std::expected<…, Error>`, and every problem is logged rather than stopping the app.
- Device settings learned in 0.4 (audio device, buffer size, MIDI input) move from command-line options into configuration

**Open questions:**
- Format and location: a config file (JSON with comments, TOML, …), `.env` / environment variables, or both; and the parsing library (reflect-cpp or nlohmann/json; see the 0.2 discussion)

## 0.6 Control server

**What:** the backend serves the UI and talks to it over the local network.

**Includes:**
- An embedded HTTP server that serves the UI's static files, and a WebSocket endpoint for control
- **Protocol v0:** UI → backend: volume, release time, note on/off (on-screen keys), device selection. Backend → UI: active keys and an output level meter, throttled to around 30 Hz.
- Messages cross from the server thread to the audio thread through lock-free queues; audio never goes over the network
- Listening on the local network so a phone can connect (pairing and discovery come after the MVP)
- **The protocol is a public, documented API**, not an internal detail: written up in `docs/protocol.md`, with a version number, so other people's frontends, scripts and AI tools can use it

**Acceptance checks:**
- Protocol messages are unit-tested (encode/decode, validation of bad input)
- A scripted test client can connect, change volume and see the level change
- A flood of control messages causes no audio glitches

**Decisions:**
- **cpp-httplib** for HTTP + WebSocket, contained behind our own `ControlServer` interface per the [dependency rules](../vision.md#dependency-rules). CivetWeb is the fallback if its newer WebSocket support gives trouble.

- **JSON messages, using nlohmann/json.** A public API used by other people's frontends, scripts and AI tools (MCP) has to be human-readable and debuggable, and at ~30 messages a second binary formats buy nothing. Each message is a plain C++ struct mapped to JSON. Malformed network input is rejected with clear errors. nlohmann/json is contained in the protocol code only, per the dependency rules. A published JSON Schema for the API can follow later (useful for MCP, third-party frontends and generating the UI's TypeScript types).

## 0.7 React UI

**What:** the first real UI, served by the backend.

**Includes:**
- The UI project (see Decisions below), **designed for a phone screen first**, scaling up to desktop
- A keyboard view that lights up active notes and can be tapped to play
- Volume and release controls, audio and MIDI device selection
- A **log and error panel** showing what the backend reports (the engine's diagnostics, device problems)
- Reconnects automatically if the connection drops
- The backend bundles the built UI, so there's nothing separate to install

**Acceptance checks:**
- Works from a phone on the same Wi-Fi and from the laptop itself
- UI controls change the sound in real time, without audible glitches while playing
- Component tests (Vitest + RTL) for the UI logic, and at least one Playwright test driving the UI against a running backend
- `oxlint` and `oxfmt --check` are clean

**Decisions:**
- **A modern, customisable interface:** users decide what's on screen (e.g. show, hide or move the log panel), rather than the app offering fixed modes. 0.7 lays the groundwork; full customisation can grow over later milestones.
- **Stack:** npm; TypeScript + React + Vite; **MUI** (Material UI) as the component library, styled with the `sx` prop. Other skins (e.g. Tailwind) are a fun later option, but prototyping shouldn't require building a design system.
- **State: Zustand**, with a disciplined, **normalized** store: flat collections keyed by id, no deep state trees.
- **Protocol types: TypeBox** validators for every message, with TypeScript types derived from them. Incoming backend messages are validated at the boundary.
- **No cross-language schema pipeline.** C++ message structs are hand-written; the protocol is documented in prose in `docs/protocol.md`. In a small monorepo where agents change both sides in one PR, the C++ protocol tests plus Playwright end-to-end tests are the safety net. JSON Schema export from TypeBox can be added later if MCP or third-party frontends need it.
- **Lint and format: oxlint + oxfmt.**
- **Tests:** Vitest + React Testing Library for units and components; Playwright for end-to-end.
- **Playwright MCP** is set up for agents, so they can open, drive and check the real UI while developing.
- **On the laptop, the backend opens the UI in the user's default browser** (`http://localhost:…`). It's the same experience as on the phone, and it makes the architecture visible to users: the piano is a local service, and the UI is just one client of it. No native window for now.

## 0.8 Latency measurement

**What:** measure real key-to-sound latency and write it down.

**Includes:**
- Each Windows device type JUCE offers (shared, low-latency shared, exclusive, ASIO if available) at several buffer sizes
- A physical loopback or recorded tap test (method decided when we get here)
- Results written up in `docs/`

**Acceptance checks:**
- A measured number per mode and buffer size, compared against the 5–10 ms target
- A recommendation for the default mode

---

## Superseded: Rust prototype (steps 0.1–0.2, first pass)
Built before the stack change; kept here for the record.
- **0.1 Repo and workspace skeleton (Rust):** Cargo workspace with plain crate names, Apache-2.0 (since switched to MIT), README/CONTRIBUTING (solo project, forks welcome), public GitHub repo with commits under the GitHub noreply email.
- **0.2 Sine key model (Rust):** merged in [PR #1](https://github.com/kdaquila/piano-lab/pull/1). Its design decisions and tests are carried into the C++ steps above.
- **Why it changed:** the Rust audio ecosystem kept showing gaps (for example, cpal supports only shared-mode WASAPI and has no low-latency shared mode), and the goals grew to include cross-platform builds, DAW plugins and remote control, where JUCE is far more complete. See [vision.md](../vision.md).
