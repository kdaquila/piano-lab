# Milestone 0: Skeleton

> **Re-planned 2026-11-21** for the new stack (C++ engine, C++/JUCE backend, React UI) and architecture (local backend, web UI over the local network). See [vision.md](../vision.md) for the terms *engine*, *backend* and *UI*. Each step's open questions get settled, one at a time, before that step starts.

> **Re-planned again 2026-11-27:** the network-facing part moved out of C++ into a TypeScript **web server**, the web UI became touch-first for the desktop, the network became opt-in with pairing, and a threat-model step comes before the network opens. See [vision.md](../vision.md#security-first).
>
> **Terms changed with it**, to say what each part does: the **sound engine** (was *engine*, `engine/` → `sound-engine/`), the **audio service** (the C++ program, was *backend*, `backend/` → `audio-service/`), the **web server** (new, `web-server/`), the **web API** (new, `web-api/`) and the **web UI** (was *UI*, `ui/` → `web-ui/`). Steps 0.1–0.5 below were written with the old terms; the folders are renamed in step 0.6.

**Goal:** a walking skeleton through every layer of the architecture. MIDI in → one sine per note → audio out, with the web server serving a touch-first web UI on the same computer, **and, with network mode on, to a paired phone on the same network**. It's deliberately simple: a sine is a one-mode modal string, so this is the real engine at its simplest, not throwaway code.

**Done when:** on a Windows laptop, you can play your MIDI keyboard and hear a clean sine per note; adjust volume and release from the web UI by touch on the laptop, and from a paired phone with network mode on; and we have measured key-to-sound latency for each Windows audio mode.

## Steps

| Step | What | Status |
|---|---|---|
| [0.1](#01-c-project-skeleton) | C++ project skeleton: CMake, JUCE, tests, linting | Done ([#3](https://github.com/kdaquila/piano-lab/pull/3)) |
| [0.2](#02-engine-core-port) | Engine core: key model, 88-key keyboard, sine, headless tests | Done ([#4](https://github.com/kdaquila/piano-lab/pull/4)) |
| [0.3](#03-offline-render-to-wav) | Offline render to WAV: MIDI or MusicXML in, WAV out, no devices | Done ([#5](https://github.com/kdaquila/piano-lab/pull/5)) |
| [0.4](#04-piano-server-core) | Piano server core: audio out + MIDI in through JUCE, the live pipeline, recording machinery, logging | Done ([#6](https://github.com/kdaquila/piano-lab/pull/6)) |
| [0.5](#05-backend-configuration) | Backend configuration: user settings merged over the built-ins | In progress |
| [0.6 prelude](#06-prelude-developer-commands-through-npm) | Developer commands through npm: `npm run build`, `npm test`, `npm run check`, ... | Done ([#9](https://github.com/kdaquila/piano-lab/pull/9)) |
| [0.6](#06-web-server-and-web-api) | Web server and web API: TypeScript web server, audio service behind the service pipe, API v0, settings saving (localhost only) | In review |
| [0.7](#07-web-ui) | Web UI: touch-first, desktop first, served by the web server | Not started |
| [0.8](#08-threat-model-and-hardening) | Threat model and hardening, before the network opens | Not started |
| [0.9](#09-network-mode-and-pairing) | Network mode and pairing: opt-in, a paired phone as a remote | Not started |
| [0.10](#010-latency-measurement) | Latency measurement across Windows audio modes | Not started |

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
  - One check command that formats, lints, builds and tests: the same gate for you, me and agents. First `tools/check.ps1`; since [step 0.6 prelude](#06-prelude-developer-commands-through-npm) `npm run check`
- Tools to install at the start of the step if missing: LLVM (clang-format, clang-tidy), CMake, Ninja.
- **Editor: VS Code + clangd.** CMake presets export `compile_commands.json` so clangd (and any agent tooling) sees the exact build. The repo ships `.clang-format`, `.clang-tidy` and recommended VS Code extensions. Visual Studio 2022 stays available as the heavy-duty debugger.

- **The engine is JUCE-free: standard C++ only.** JUCE lives only in `backend/` (since the 2026-11-27 re-plan: in the audio service, `audio-service/`; networking lives only in the TypeScript web server). This keeps engine tests fast (no JUCE build), keeps the physics engine portable (render CLI, future Python package, plugin, other platforms) and cleanly MIT, with JUCE's terms only touching the backend binary. Utilities we'd otherwise take from JUCE (FIFOs, smoothed values) are small standard-C++ components.

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
- **One program, `piano-lab`, with subcommands for its roles:** `piano-lab render` (the 0.3 renderer, moved in; `piano-lab-render` is gone) and `piano-lab server start`. `server stop` and `server status` arrive in 0.6 as clients of the control API (since the 2026-11-27 re-plan, the user-facing `piano-lab` command belongs to the TypeScript web server; see 0.6).
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
- **Devices are named as Windows shows them**, e.g. `type = "ASIO"`, `output = "Focusrite USB ASIO"`, `inputs = ["Digital Piano"]`; an empty name means the default, and an `output` named without a `type` picks the type that has it. A named device that isn't there is logged, listing what is, and the default is used (a device that's there but won't open, e.g. ASIO busy in another program, falls back to the default output at the same sample rate and buffer size); if none of the listed MIDI inputs is there, every input opens. Two identical devices can't be told apart yet; an optional identifier can be added later without breaking files.
- **A sample rate or buffer size the device can't do** is logged and the device's closest one used (JUCE's own choice). `bufferSize = 0` means the device's own.
- **`render` sounds like the live piano:** it uses the install folder's `settings.toml` (only its piano sections) unless `--settings <file>` names another. A `--settings` file that's missing or broken stops the render (nothing is written), because the caller asked for it specifically; a broken install file is a warning and the built-ins are used.

## 0.6 prelude: developer commands through npm

**What:** one cargo/npm-like entry point for every developer task, replacing `tools/dev-shell.ps1` and `tools/check.ps1`.

**Includes:**
- `package.json` at the root, listing every task: `build:server`, `test:server`, `format:server`, `lint:server`, `check:server` and `start:server`, each one TypeScript file under `scripts/server/`, and `render`
- The plain `build`, `test`, `format`, `lint`, `check` and `start` run every part's task with npm-run-all2 (`run-s build:*`)
- The scripts load the Visual Studio x64 environment themselves, for the programs they start only

**Acceptance checks:**
- `npm run check` passes from a plain Windows PowerShell 5.1 terminal, and leaves its environment unchanged
- The README, agent rules and docs use the npm commands

**Decisions:**
- **TypeScript task scripts, run directly by Node** (type stripping, stable since Node 24.12; no tsx or build step), one file per task. Easier to read and maintain than PowerShell, just, Task or make, cross-platform, and Node arrives for the UI anyway.
- **npm-run-all2 for the aggregates**, rather than a task runner of our own: `package.json` stays the one list of tasks, and the UI's `:ui` tasks join the plain commands without edits. The plain commands take no arguments; presets go to the part (`npm run test:server -- release`).
- **Node 24.15 or newer** (npm-run-all2's minimum), enforced by `engines` with `engine-strict`.
- **`npm run check` type-checks the scripts** with `tsc` (TypeScript and `@types/node` as exact-pinned dev dependencies).
- **Naming:** plain names run every part; `:server` (and from 0.7, `:ui`) run one.

## 0.6 Web server and web API

**What:** the web server: a TypeScript program that serves the web UI and the public web API on `localhost`, and runs the audio service behind the service pipe. C++ never touches the network.

**Includes:**
- **The new names on disk, as the step's first commit:** `engine/` → `sound-engine/`, `backend/` → `audio-service/` (its `server/` library, which would clash with the web server, becomes `live/`), `ui/` → `web-ui/`, and the npm task suffix `:server` → `:audio` (e.g. `npm run test:audio -- release`). Docs, rules and scripts follow. The C++ include paths (`<pianolab/engine_core/…>`) stay.
- **`web-server/`**, an npm workspace: TypeScript on Node with Fastify, `@fastify/websocket` and TypeBox. It starts the audio service as a child process, restarts it if it dies (logging why), and stops it on exit.
- **`web-api/`**, an npm workspace holding the TypeBox schemas of the public API, used by the web server now and the web UI from 0.7
- **The service pipe:** the audio service reads commands from its stdin and writes events to its stdout, in a small fixed format. A pipe-reader thread hands commands to the audio thread through lock-free queues, as the MIDI inputs already do. Audio and MIDI never pass through the web server.
- **API v0** (HTTP for requests, one WebSocket for live traffic):
  - requests: status, stop, list devices, read and save settings, start and stop a recording (the 0.4 recorder machinery)
  - commands over the WebSocket: notes on and off (on-screen keys), live setting changes (volume and release first)
  - events over the WebSocket: active keys and an output level meter (throttled to about 30 Hz), logs and the engine's diagnostics, and the current settings and state
- **Saving settings from the app** (auto-save; see Decisions)
- **The user-facing `piano-lab` command becomes the web server's:** `piano-lab start` (web server and audio service together), `piano-lab stop` and `piano-lab status` (clients of the API), and `piano-lab render` (handed to the audio service binary). The audio service's own program is **`piano-lab-audio`**, with `run` (replacing `server start`; driven over the service pipe, and stopping when its stdin ends) and `render`.
- **`docs/web-api.md`:** the public, versioned web API, written for other people's frontends, scripts and AI tools
- Developer commands: `:web` tasks for the web server and the web API package (`build:web`, `test:web`, `lint:web`, ...) picked up by the plain `run-s` aggregates
- **The compiler's exploit mitigations** on our C++ (`/guard:cf`, `/CETCOMPAT`), checked by the build

**Acceptance checks:**
- The API's schemas are tested: valid messages pass, and malformed, oversized or unknown input is rejected with clear errors before any handler runs
- The service pipe format is unit-tested on both sides, and the audio service's reader survives garbage and truncated input (fuzzed in 0.8)
- A scripted test client on `localhost` can connect, change the volume and see the level change
- A flood of control messages causes no audio glitches
- Nothing listens on any address but `localhost`, and requests failing the `Host`/`Origin` checks are refused
- Settings changed through the API reach `settings.toml`; a hand-edited file is backed up to `settings.toml.bak` before being overwritten

**Decisions:**
- **A TypeScript web server instead of a C++ web server**, so no C++ ever parses network input. Memory-safety bugs in C++ parsing untrusted input are the classic way to break into a program, and AI-assisted attackers are good at finding them. Fastify validates every route against its TypeBox schema before our code runs; `ws` (under `@fastify/websocket`) handles WebSocket framing. Considered: cpp-httplib with nlohmann/json (the original plan), Rust (memory-safe too, but a third language for a marginal gain), bare `node:http` (validation by hand is where bugs hide), Express (no built-in schema validation), tRPC (assumes TypeScript clients, while our API is public).
- **The audio service runs as the web server's child process over stdin/stdout**, the simplest local IPC with no socket and no port. The audio service only ever sees commands the web server already validated.
- **The service pipe carries JSON Lines** (one JSON object per line), parsed in C++ by an established library, never by hand. The web server mostly forwards what it already validated, and the audio service can be run by hand and driven by typing commands, which helps debugging and tests. The library must work with exceptions off and report errors as values: **Glaze** (MIT; maps JSON onto our structs both ways) if a build test at the start of the step passes on our MSVC (its CI tests a newer toolset), otherwise **simdjson** (MIT/Apache; parsing, MSVC 2017+). Not nlohmann/json: with exceptions off, a missing key or wrong type aborts the program, against "the music never stops". The library is contained behind our own small interface in one `.cpp`, per the [dependency rules](../vision.md#dependency-rules), and the reader is fuzzed in 0.8. Considered: fixed binary messages (fastest, but a format and parser of our own, and opaque to debug, for ~30 messages a second).
- **The settings stay in C++.** The `Field` tables, validation, `settings.toml` reading and the starter-file writer already live there, so saving does too; the web server passes changes through and reports the audio service's own messages. 
- **Settings in the web API are generic, driven by the settings schema** (the `Field` tables). At startup the audio service describes every setting (name, type, range, unit, description, default) to the web server, which offers it as `GET /api/v0/settings/schema`, alongside `GET /api/v0/settings` for the current values and `POST /api/v0/settings` for partial updates (e.g. `{"sine.gain": 0.5}`); on the WebSocket, one `set` command for live changes and one `settings` event broadcast to every client. The web server checks the shape (bounded names, plain values, small payloads); the audio service checks names and ranges and **rejects** a bad value with the engine's own message, instead of replacing it with the default as it does for the file, so the client knows its change didn't apply. Every setting added in later milestones reaches the file, the web API and the web UI with no API change, under the same names everywhere. The web UI draws controls from the schema; friendly labels and grouping are UI choices. Considered: a specific message per control (`setVolume`, ...), typed end to end but needing five edits per new setting. Actions (notes, devices, recording, stop) stay specific messages; device *choices* are settings.
- **JSON over HTTP for requests, one fire-and-forget WebSocket for live traffic.** HTTP carries everything that waits for its own answer: listing devices, reading and saving settings, starting and stopping recordings, status, stop. It's "REST" in the casual sense: `GET` for reads, `POST` for anything that changes or does something, JSON in and out, one error shape, coarse status codes. The WebSocket has no request/response: clients send commands (notes, live setting changes), and the web server broadcasts events (active keys, level, logs, the current state) to every client, so a change made in one place shows up everywhere. A single ordered socket keeps on-screen note-ons and note-offs in order. Considered: WebSocket only with JSON-RPC 2.0 (sound, but nothing is `curl`-able and the CLI gets heavier) and HTTP with Server-Sent Events (on-screen notes over separate `POST`s can arrive out of order).
- **Settings auto-save.** Changes apply live and are written to `settings.toml` about a second after the last one, so a slider drag is one write. The whole file is regenerated from the field tables (the 0.5 starter-file writer), written to a temporary file and renamed into place.
- **piano-lab owns `settings.toml` while it runs.** It's read at startup only; to edit it by hand, stop piano-lab first. Safety net: unless the file on disk is exactly what piano-lab last read or wrote *and* exactly what it would write for those settings, it's copied to `settings.toml.bak` (and logged) before being overwritten. The second half also keeps comments and unknown settings from a file edited before piano-lab started. Considered: reloading hand edits live with a field-by-field merge (more machinery than one user needs) and a separate private working copy (two sources of truth).
- **Live setting changes keep ringing notes.** The main thread builds a new keyboard from the new settings and hands it to the audio thread by value through a lock-free queue (the keyboard is fixed-size, so nothing is allocated or freed there). Each key takes the new settings and keeps its sound: a tuning change bends a ringing note without a click, a release change reaches notes already fading, and a gain change is heard from the next strike.
- **Device changes rebuild the live side.** Changing any `audio.*` or `midi.*` setting stops the MIDI inputs and the audio callback, finishes any recording (logged), and builds the queues, recorder and devices afresh, the same code as at startup. It costs a short dropout, which is fine for something done between pieces, never while playing. Considered: a fixed set of 16 MIDI input queues so MIDI could change without touching audio (a limit for no real gain).
- **On-screen notes go through the main thread.** The pipe reader hands every command, notes included, to the main thread, which is then the on-screen queue's only writer. A rebuild runs on that thread too, so it can't race with a note being queued. JUCE's message loop wakes at once, so the added delay is negligible.
- **The `keys` event reports keys held down** (note-on to note-off, from any source), not keys still sounding: that's what a lit key means to a player, and it stays readable with long releases and, later, the pedal.
- **Recordings go in `recordings/` next to the program**, with everything else piano-lab writes, named by local time: `piano-lab 2026-11-27 15-04-32.wav`. A setting can move them later.
- **The web server listens on port 4440** (a nod to A440; not a well-known port, so unlikely to clash): `http://localhost:4440`. `[server] port` can change it from 0.9. Considered: 8080 (the classic, so often taken).
- **The web server and the web API are compiled with `tsc`** to JavaScript in each workspace's `dist/` (`build:web`), and run from there. Node won't strip types from files inside `node_modules`, so the published package (`npx piano-lab`, later) has to be JavaScript, and what's tested now is what ships. The code keeps to the task scripts' rules (`erasableSyntaxOnly`, strict). Considered: running the `.ts` directly, like the task scripts, with a build added only for publishing.
- **The web UI's tools for all the TypeScript:** oxlint, oxfmt and Vitest for the web server and the web API too, with one config at the repo root; oxfmt also checks the task scripts' formatting. Considered: Node's built-in `node:test` for the web server (one dependency fewer, but two test runners once the web UI arrives).
- **The web server finds the audio service from its own location**, with no option or environment variable: in the repository, the dev build (`build/dev/audio-service/app/piano-lab-audio.exe`); when piano-lab ships through npm, that step adds the platform package as a second place to look. Considered: a `--audio-service <path>` option (flexible, but a developer-only flag the shipped program wouldn't need).
- **The audio service is restarted whenever it stops unasked**, after a pause that doubles from 0.5 s up to 10 s and starts over once it has run for 30 s, so a crash loop doesn't spin; it never gives up. Each stop is logged with its exit code and the last lines it wrote to stderr. Requests waiting for a reply fail at once with a clear error.
- **The web API's paths carry its version: `/api/v0/…`** (e.g. `GET /api/v0/status`, the WebSocket at `/api/v0/live`), so a breaking change becomes `/api/v1/…` and both can be served side by side. Considered: plain `/api/…`, with the version only in `GET /api/status`.
- **One error shape for every failed request:** `{"error": {"code": "...", "message": "..."}}`, with a short code per HTTP status (`invalid-request` 400, `forbidden` 403, `not-found` 404, `too-large` 413, `unsupported-media-type` 415, `refused` 422 for a value the piano refused, `unavailable` 503 while the audio service starts or restarts, `timeout` 504, `internal` 500). Bodies and WebSocket messages are at most 64 KiB.
- **WebSocket messages are JSON objects with a `type`.** Commands: `note.on`, `note.off`, `settings.set`. Events: `keys`, `level`, `log` (from `info` up), `settings`, `service` (what the audio service is doing), and `error`, sent only to the client whose command failed. A client that connects is told the current `service` state, `settings` and `keys` at once. Level events are skipped for a client with more than 256 KiB waiting, so a slow client doesn't pile up meter readings.
- **The web server logs to the console** (time, level, message), its own lines and the audio service's alike; the audio service also keeps its log file. A log file for the web server's own lines can come with the install folder's layout, when piano-lab ships.
- **The `piano-lab` command is the web server package's own command** (its `bin`), as it will be when piano-lab ships. In the repository it's run with `npx piano-lab …`, which npm links for workspaces, so each worktree runs its own code. A small committed launcher (`web-server/bin/piano-lab.js`) loads the compiled code, so the link exists before the first build. `npm start` builds both parts and runs `piano-lab start`; `npm run start:audio` still runs the audio service alone, for typing pipe commands. `piano-lab stop` with nothing running says so and succeeds; `piano-lab status` exits 1 when nothing is running. Considered: `npm link` (a global command, pointing at one checkout).
- **The audio service runs detached** (its own process group, and on Windows its own hidden console), so Ctrl+C reaches only the web server, which then stops it in order: no race between the audio service stopping on its own and being restarted.
- **Glitches are counted by the audio service itself:** the audio thread times each block with the steady clock and counts the ones that took longer to render than they last (`lateBlocks` in the status), next to the driver's own xrun count where JUCE can read one (`driverXruns`, left out otherwise). It works on every device and gives the web UI a glitch counter later. Considered: the driver's count alone (JUCE returns -1 where a driver doesn't keep one).
- **The acceptance checks run against the real audio service as `npm run acceptance`**, not inside `npm run check`: they need a sound device and play quiet tones. They run a copy of `piano-lab-audio` in a temporary folder, so they never touch the developer's own settings. `npm run check` still type-checks them.
- **The exploit mitigations cover everything compiled with MSVC**, our code and the dependencies built with it, since Control Flow Guard only protects calls in code compiled for it. Not in the AddressSanitizer build, which doesn't support it. A test reads the built `piano-lab-audio.exe`'s headers with `dumpbin` and fails unless both are on.
- **TypeBox 1.x, from the `typebox` package** (`@sinclair/typebox` 0.34 is the older line), with `@fastify/type-provider-typebox` 6, which requires it. Checked 2026-11-27: Fastify 5.12, `@fastify/websocket` 11.3 (on `ws` 8).
- **`localhost` only in this step.** On this computer, the browser checks below are the protection, and local clients need no token. Network mode and pairing come in 0.9, after the threat model.
- **Browser protections, always on:** requests whose `Host` isn't `localhost`, a loopback address or (from 0.9) one of our own addresses are rejected (DNS rebinding), cross-site `Origin`s are rejected on `POST` and on the WebSocket handshake, and `POST` bodies must be `application/json`. Websites open in the user's browser can reach `localhost`, so this matters even with no network mode.
- **The web server's dependencies are few, exact-pinned and installed without install scripts**, per the [dependency rules](../vision.md#dependency-rules).

## 0.7 Web UI

**What:** the first real UI, served by the web server, designed for touch first on the desktop.

**Includes:**
- The web UI project (see Decisions below), **designed for touch first**: a laptop touchscreen or a touch monitor on the piano's music stand, working just as well with a mouse, and scaling down to a phone screen for 0.9
- A keyboard view that lights up active notes and can be played by touch, **multi-touch** so chords work
- Volume and release controls, audio and MIDI device selection
- A **log and error panel** showing what the audio service reports (the engine's diagnostics, device problems)
- Reconnects automatically if the connection drops
- The web server serves the built web UI and opens it when piano-lab starts, so there's nothing separate to install; it can also be installed as an app from the browser (a PWA, since `localhost` is a secure context)

**Acceptance checks:**
- Works with touch (including chords on the on-screen keyboard) and with a mouse, on a laptop screen and a large monitor
- UI controls change the sound in real time, without audible glitches while playing
- Component tests (Vitest + RTL) for the web UI logic, and at least one Playwright test, including touch input, driving the web UI against a running piano-lab
- `oxlint` and `oxfmt --check` are clean

**Decisions:**
- **Touch first:** large hit targets, nothing that needs hover, gestures where they help, and Pointer Events so every finger is tracked separately. Browser touch adds latency (step 0.10 measures it), so the MIDI keyboard stays the way to *play*; the on-screen keyboard is for trying things out.
- **A modern, customisable interface:** users decide what's on screen (e.g. show, hide or move the log panel), rather than the app offering fixed modes. 0.7 lays the groundwork; full customisation can grow over later milestones.
- **Developer commands:** the web UI adds its `:ui` tasks (`build:ui`, `test:ui`, ...) to `package.json`, and the plain `build`, `test`, `check`, ... pick them up through their `run-s` globs. `web-ui/` becomes an npm workspace of the root `package.json`.
- **Stack:** npm; TypeScript + React + Vite; **MUI** (Material UI) as the component library, styled with the `sx` prop. Other skins (e.g. Tailwind) are a fun later option, but prototyping shouldn't require building a design system.
- **State: Zustand**, with a disciplined, **normalized** store: flat collections keyed by id, no deep state trees.
- **Web API types: the shared TypeBox schemas** from `web-api/`. Incoming messages are validated at the boundary, with the same definitions the web server enforces, so there's one definition of the public API and no cross-language schema pipeline. `docs/web-api.md` stays the prose reference for other clients; JSON Schema export from TypeBox can be added later if MCP or third-party frontends need it.
- **Lint and format: oxlint + oxfmt.**
- **Tests:** Vitest + React Testing Library for units and components; Playwright for end-to-end.
- **Playwright MCP** is set up for agents, so they can open, drive and check the real UI while developing.
- **The web server opens the web UI in the user's default browser** (`http://localhost:…`), and the browser can install it as an app with its own window. It makes the architecture visible: the piano is a local program, and the web UI is just one client of it. No native window.

## 0.8 Threat model and hardening

**What:** a written threat model for piano-lab and a decided defence for each threat, done **before** the network opens in 0.9, so network mode is built against it instead of reviewed after the fact.

**Includes:**
- `docs/security.md`: what we protect (above all the user's computer; also their files and settings), who from (websites in the user's browser, other software on the computer, devices on the same network, people who know the Wi-Fi password, shared and public networks), and which defence covers each threat, or why a gap is accepted
- A review of what 0.6 and 0.7 built against it: the browser protections, input limits, file paths, privileges, the web server's dependencies, and the trust given to local clients
- The design of 0.9's pairing and network mode, checked against the threat model
- **Fuzz tests** for what the audio service parses (the service pipe format, MIDI and MusicXML files), alongside the existing AddressSanitizer runs, and for the web server's schemas
- A habit for dependency security advisories (both C++ and npm), and a way for users to get fixes quickly

**Acceptance checks:**
- Every threat in `docs/security.md` names its defence or its accepted gap
- Fuzz tests run in `npm run check`, or in a separate documented command if they're too slow for it
- Any change the review finds is made, or added to the plan

**Why here:** security-first rather than bolted on. AI-assisted attacks make small network-facing apps worth attacking, and nothing piano-lab handles is private, so the harm to prevent is someone using a bug in piano-lab to get into the user's computer.

## 0.9 Network mode and pairing

**What:** opt-in network mode, so a phone, a tablet or scripts on other machines can use piano-lab after pairing, with an honest user-manual section on what that means.

**Includes:**
- `[server] listen = "local"` (the default) or `"network"`, and `[server] port`, in `settings.toml`
- **Pairing:** with network mode on, the terminal shows a short code at startup; a device on the network enters it once (a pairing screen in the web UI; a QR code is optional). Codes are single-use and expire, and repeated wrong guesses lock pairing. A paired client gets a long random token, sent as an `HttpOnly`, `SameSite=Strict` cookie or as an `Authorization: Bearer` header (for scripts and other non-browser clients), required on every request and on the WebSocket handshake. Paired devices are remembered (hashed) in a file in the install folder, and can be unpaired.
- **Before pairing, almost nothing answers:** the web UI's static files and the pairing endpoint only
- The web UI works on a phone screen (layout checked in 0.7) and shows the pairing screen when needed
- **A user-manual section on network mode:** piano-lab is a local instrument for tinkerers, so it's as safe as the network it's on. What's encrypted (by the Wi-Fi, not by piano-lab), who you're trusting (everyone with the Wi-Fi password, and every device on the network), and how to harden it: the laptop's Mobile Hotspot as a private network for just the laptop and phone, or a separate network on the router (noting that guest networks usually stop their devices reaching each other)

**Acceptance checks:**
- With network mode off, nothing is reachable from another machine; with it on, nothing but the static files and the pairing endpoint answers an unpaired client
- Pairing is tested: a right code pairs once, an expired or reused code fails, repeated wrong guesses lock pairing, an unpaired or unknown token is refused on HTTP and on the WebSocket
- Manual: a phone on the same Wi-Fi pairs and then controls volume and release while you play

**Decisions:**
- **Opt-in, not the default**, so nobody who doesn't need it is exposed. For tinkerers, it's one line in `settings.toml`.
- **Clients on this computer don't pair**: they're trusted with the browser protections from 0.6 (revisited by the 0.8 threat model). Clients from the network always pair.
- **Traffic isn't encrypted.** Nothing piano-lab sends is private, and on a home network WPA2/WPA3 keeps it from anyone without the Wi-Fi password; the manual says so plainly. Considered during 0.6's discussion, for later if needed: the web server's own self-signed certificate, with an optional throwaway, name-constrained CA installed on the phone (the leading option; it also makes the web UI installable as an app on the phone); Plex-style certificates, a native phone app, or a hosted UI over WebRTC (all need fees or a cloud audio service, against [vision.md](../vision.md#distribution)); and Tailscale (an optional extra for people who already use it).
- **Windows Firewall asks** the first time network mode is used; allowing private networks only keeps piano-lab off public Wi-Fi.

## 0.10 Latency measurement

**What:** measure real key-to-sound latency and write it down.

**Includes:**
- Each Windows device type JUCE offers (shared, low-latency shared, exclusive, ASIO if available) at several buffer sizes
- The on-screen keyboard's touch-to-sound latency too, for comparison
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
