# Milestone 0: Skeleton

> **Re-planned 2026-11-21** for the new stack (C++ engine, C++/JUCE backend, React UI) and architecture (local backend, web UI over the local network). See [vision.md](../vision.md) for the terms *engine*, *backend* and *UI*. Each step's open questions get settled, one at a time, before that step starts.

**Goal:** a walking skeleton through every layer of the new architecture. MIDI in → one sine per note → audio out, with the backend serving a React UI that works in a local window **and on a phone on the same network**. It's deliberately simple: a sine is a one-mode modal string, so this is the real engine at its simplest, not throwaway code.

**Done when:** on a Windows laptop, you can play your MIDI keyboard and hear a clean sine per note; adjust volume and release from your phone; and we have measured key-to-sound latency for each Windows audio mode.

## Steps

| Step | What | Status |
|---|---|---|
| [0.1](#01-c-project-skeleton) | C++ project skeleton: CMake, JUCE, tests, linting | Done ([#3](https://github.com/kdaquila/piano-lab/pull/3)) |
| [0.2](#02-engine-core-port) | Engine core: key model, 88-key keyboard, sine, headless tests | Done ([#4](https://github.com/kdaquila/piano-lab/pull/4)) |
| [0.3](#03-offline-render-to-wav) | Offline render to WAV: MIDI or MusicXML in, WAV out, no devices | Done ([#5](https://github.com/kdaquila/piano-lab/pull/5)) |
| [0.4](#04-piano-server-core) | Piano server core: audio out + MIDI in through JUCE, the live pipeline, recording machinery, logging | Done ([#6](https://github.com/kdaquila/piano-lab/pull/6)) |
| [0.5](#05-backend-configuration) | Backend configuration: user settings merged over the built-ins | In progress |
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
- `piano-lab-render` (`backend/render-cli`): `piano-lab-render <input> [-o out.wav] [--sample-rate N]` (since 0.4: `piano-lab render`, in `backend/render`)
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
- `piano-lab render` takes configuration (including `silenceThreshold`) from step 0.5: done there, through `settings.toml` and `--settings`. Setting it in dB is left for later.

## 0.4 Piano server core

**What:** the backend running the engine live, against real devices through JUCE: `piano-lab server start`.

**Includes:**
- **One program, `piano-lab`, with subcommands for its roles:** `piano-lab render` (the 0.3 renderer, moved in; `piano-lab-render` is gone) and `piano-lab server start`. `server stop` and `server status` arrive in 0.6 as clients of the control API.
- **`server start`** runs in the foreground until Ctrl+C, with zero-setup defaults and no options: the first device type's default audio output (Windows Audio, shared, on Windows) at 48 kHz if the device allows (otherwise its own rate), with the device's own buffer size, and **every MIDI input**.
- **ASIO compiled in** (the `PIANOLAB_ASIO` build option, on by default on Windows), using the ASIO SDK headers bundled with JUCE. Never the default device; selectable from 0.5.
- **The live pipeline:** each MIDI input's thread turns raw MIDI into engine events and hands them to the audio thread through its own lock-free queue. Each audio block takes every waiting event and applies it at the start of the block, in arrival order across inputs; renders; copies the output to the recorder while recording; and publishes the engine's diagnostics when they change.
- **The audio core is free of JUCE and devices** (`backend/server`: `audio_core`, `live_midi`, `recorder`, `monitor`), so tests drive it block by block the way a sound card would. `server.cpp` is the thin JUCE layer: device callbacks and the main loop.
- **Recording machinery:** audio thread → lock-free queue → recorder thread → a streamed 32-bit float WAV (written as `.partial`, renamed when finished). Each recording has a session number, so audio from a stopped recording never lands in the next. Starting and stopping recordings is driven by the control API from 0.6.
- **Logging:** our own `Log` interface with spdlog behind it: a coloured console and a rotating file (`%APPDATA%\piano-lab\logs\piano-lab.log`, 5 MB × 3; since 0.5, `logs\piano-lab.log` in the install folder). The main thread (JUCE's message thread) checks ten times a second for news from the audio side (engine diagnostics, dropped notes or recording samples, the first pedal message) and logs it. Device problems are logged and never stop the server: with no audio device it runs silently, and MIDI inputs that fail to open are skipped.

**Acceptance checks:**
- Turning raw MIDI messages into engine events is unit-tested
- The live pipeline, driven block by block through the real queues by a test harness, sounds exactly like the engine rendering the same events; a recording reads back as exactly what was played
- The audio callback's work never allocates (allocation test, including queue overflow and engine problems) and never locks (review)
- A queue stress test: two threads hammering a queue lose, duplicate and reorder nothing
- You hear a clean sine per key with no clicks or dropouts, including chords and fast passages (manual)

**Decisions:**
- **Build only the real design: no temporary entry points.** The original plan had command-line options for devices "until configuration arrives" and a `--record` flag. Both would have been torn down later, so they were dropped: device choice arrives with configuration (0.5) and the API (0.6), recording control with the API (0.6). See [vision.md](../vision.md#build-only-the-real-design).
- **No file-output mode.** Offline sound is `piano-lab render`'s job; the live pipeline is tested by the internal harness, and end to end, from 0.6, by driving a running server through the API. A server writing to a file instead of a sound card can come later as a real output option, through the API.
- **Live events play at the start of the next audio block.** Lowest latency and simplest; the cost is up to one block of timing jitter (2.7 ms at 128 samples). Each event keeps its arrival time, so placing events exactly by timestamp (one block later) stays a contained change; step 0.8 measures both.
- **Every MIDI input opens by default**, each with its own single-producer queue (JUCE may call each input on its own thread). Hot-plugging comes later.
- **ASIO: compiled in, not the default.** Windows Audio (shared) works on every PC with no setup; ASIO needs a driver (an audio interface's, or a generic one). The licence point joins the pre-release licence check ([vision.md](../vision.md#openness)).
- **Lock-free queues: rigtorp/SPSCQueue v2.0.1**, wrapped as `rt::SpscQueue<T>` (header-only, in `engine/rt`). One single-producer/single-consumer queue per thread pair. Fixed capacity, allocated at startup, so the audio thread never allocates. Single numbers (dropped counts, the recording session) are atomics.
- **Logging: spdlog 1.17.0**, using `std::format`, behind `pianolab::logging::Log` (a plain struct holding a sink function), so tests collect messages and the library stays swappable.
- **JUCE's message loop runs on the main thread**, because JUCE's Windows MIDI and ASIO code rely on it. A 100 ms timer on it does the logging and watches for Ctrl+C, so there's no separate logger thread.
- ThreadSanitizer joins once Linux builds exist (it isn't available with MSVC).

## 0.5 Backend configuration

**What:** users' own settings, merged over the built-in configuration: the piano's settings, and the audio and MIDI devices.

**Includes:**
- **`settings.toml` in the install folder**, next to `piano-lab.exe`. It holds only what the user changed: anything missing uses the built-in value, and having no file at all is normal.
- **`backend/settings`:** reads the file through the engine's `Field` tables, so every setting is still declared once, and merges it over the built-in `PianoConfig`; the engine's `validate` then checks it as before. Loading returns `std::expected<…, Error>` (an `Error` only when the file can't be read or isn't valid TOML); everything else (unknown settings, wrong types, values out of range) is reported with its line and the built-in value is used.
- **Device settings:** `[audio]` `type` (e.g. `"ASIO"`), `output`, `sampleRate` and `bufferSize`, and `[midi]` `inputs`. 0.4 opened the defaults; this is where they become the user's choice.
- **A starter file:** when `server start` finds no `settings.toml`, it writes one listing every setting as a commented-out line with its description, range and built-in value. Generated from the same tables, so it can't go stale.
- **`piano-lab render`** uses the piano part of the same file, or another file with `--settings <file>`, and says which it used.
- **Logs move to the install folder** (`logs\piano-lab.log`), so the program keeps all its files in one place.

**Acceptance checks:**
- Reading is unit-tested: values merge over the built-ins; unknown settings, wrong types and bad TOML are reported with their line; out-of-range values fall back with the engine's own message
- The starter file, with every line uncommented, reads back as exactly the built-in settings with nothing reported
- Choosing devices is unit-tested without JUCE: a named type, output or MIDI input that isn't there is reported and the default used; no inputs listed opens them all
- `render --settings` changes the sound (e.g. `tuning.a4Hz`); a missing `--settings` file is an error, and a broken install `settings.toml` is a warning
- Manual: choosing ASIO, an output, a buffer size and a MIDI input in `settings.toml` opens exactly those, and a misspelled one is logged and the default used

**Decisions:**
- **Format: TOML, parsed with toml++ 3.5.1** (MIT, header-only, TOML 1.1). Comments make it friendly to edit by hand. toml++ runs with `TOML_EXCEPTIONS=0`, so parsing returns a result instead of throwing, and it lives in one `.cpp` behind our own `settings` interface ([dependency rules](../vision.md#dependency-rules)). Considered: JSON with comments (one parser shared with the 0.6 protocol) and toml11.
- **Location: the install folder, never `%APPDATA%`.** The program keeps everything it writes next to itself, logs included. An install under Program Files isn't writable without admin rights, which matters for the logs now and for saving settings later.
- **No environment variables.** It's a desktop app with no server deployments, so one file is enough.
- **Read only, plus a starter file.** The program never rewrites the user's file in 0.5. Saving arrives with the control API (0.6) and will reuse the starter file's writer: the whole file is generated from the tables, with the user's values uncommented. That also sidesteps TOML libraries not keeping comments when they rewrite a file.
- **The file's sections are the engine's own names** (`[render]`, `[tuning]`, `[sine]`) plus `[audio]` and `[midi]`, so the engine's messages ("sine.gain: …") name exactly what's in the file. `render.sampleRate` isn't in the file: the audio device decides it live, and `--sample-rate` for `render`. The audio device's wish is `audio.sampleRate`.
- **Devices are named as Windows shows them**, e.g. `type = "ASIO"`, `output = "Focusrite USB ASIO"`, `inputs = ["Digital Piano"]`; an empty name means the default. A named device that isn't there is logged, listing what is, and the default is used; if none of the listed MIDI inputs is there, every input opens. Two identical devices can't be told apart yet; an optional identifier can be added later without breaking files.
- **A sample rate or buffer size the device can't do** is logged and the device's closest one used (JUCE's own choice). `bufferSize = 0` means the device's own.
- **`render` sounds like the live piano:** it uses the install folder's `settings.toml` (only its piano sections) unless `--settings <file>` names another. A `--settings` file that's missing or broken stops the render (nothing is written), because the caller asked for it specifically; a broken install file is a warning and the built-ins are used.

## 0.6 Control server

**What:** the backend serves the UI and talks to it over the local network.

**Includes:**
- An embedded HTTP server that serves the UI's static files, and a WebSocket endpoint for control
- **Protocol v0:** UI → backend: volume, release time, note on/off (on-screen keys), device selection. Backend → UI: active keys and an output level meter, throttled to around 30 Hz.
- **Recording and devices through the API:** start and stop a recording (the recorder machinery from 0.4), list the available audio and MIDI devices, and choose them
- `piano-lab server stop` and `piano-lab server status`: small clients of the API
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
