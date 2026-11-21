# Milestone 0: Skeleton

**Goal:** a walking skeleton through every layer. MIDI in → one sine per note → audio out, with a React UI (keyboard view, volume, release). It's deliberately simple: a sine is a one-mode modal string, so this is the real engine at its simplest, not throwaway code.

**Done when:** you can play your MIDI keyboard, hear a clean sine per note, adjust volume and release from the UI, and we have a measured key-to-sound latency to compare against the 5–10 ms target.

## Steps

| Step | What | Status |
|---|---|---|
| [0.1](#01-repo-and-workspace-skeleton) | Repo and workspace skeleton | In review |
| [0.2](#02-sine-voice-in-engine-core) | Sine voice in `engine-core`, with a headless test | Not started |
| [0.3](#03-audio-out) | Audio out through cpal (WASAPI) | Not started |
| [0.4](#04-midi-in) | MIDI in through midir | Not started |
| [0.5](#05-tauri--react-shell) | Tauri + React shell | Not started |
| [0.6](#06-latency-measurement) | Latency measurement | Not started |

---

## 0.1 Repo and workspace skeleton

**What:** set up the repository and an empty Cargo workspace matching the architecture in the design doc. No audio code yet.

**Includes:**
- `git init`, a Rust/Node `.gitignore`
- A Cargo workspace with library crates `engine-core`, `models`, `calibration`, `rt` and a binary crate `render-cli`. Each one is empty apart from a placeholder test.
- `LICENSE` (Apache-2.0)
- `README.md` with a short description and the solo-project stance (no outside contributions, forks welcome)
- `CONTRIBUTING.md` repeating that stance
- `rust-toolchain.toml` pinning the stable channel

**Not included:** the `app` crate (Tauri comes in 0.5), any dependencies beyond the standard library, GitHub setup.

**Acceptance checks:**
- `cargo build --workspace` and `cargo test --workspace` pass
- `cargo clippy --workspace -- -D warnings` and `cargo fmt --check` pass
- The dependency rule holds: `engine-core` depends on nothing else in the workspace

**Decisions:**
- Crates use plain names (`engine-core`, not `piano-lab-engine-core`). Publishing as a library is a distant, secondary goal; proving it can sound like a nice piano comes first. If we ever publish, renaming is a mechanical change.

**Open questions:**
- When to create the GitHub repo (the design doc says "public from day one").

## 0.2 Sine voice in engine-core

**What:** the smallest real engine. A voice trait, a sine voice, and a voice pool that renders into an audio buffer. It's pure code with no devices, threads or files, so it can be fully tested headless.

**Includes:**
- A trait for a sound-producing voice (note on, note off, render a block)
- A sine voice with a simple linear or exponential release
- A voice pool with polyphony and simple voice stealing
- A minimal offline render function: a list of note events in, samples out

**Acceptance checks (automated):**
- Rendering A4 (MIDI 69) gives a measured frequency of 440 Hz ± 0.1
- The output is neither silent nor clipping (peak below 1.0)
- After note off and the release time, the output decays to silence
- Rendering allocates no memory inside the per-block render call (the real-time safety rule, checked by a test allocator or a review rule)

**Open questions:**
- How strictly to enforce "no allocation on the audio thread" from day one: a test-time allocator check or a code-review rule?

## 0.3 Audio out

**What:** the `rt` crate opens the default output device through cpal using WASAPI and plays the engine's output.

**Includes:**
- Opening the device at 48 kHz, with a configurable buffer size
- Running the engine's render inside the audio callback
- A small test binary that plays a fixed tone for a few seconds

**Not included:** ASIO (later, once latency is measured), device selection UI.

**Acceptance checks:**
- You hear a clean, steady tone with no clicks or dropouts (manual, your ears)
- The callback never blocks or allocates (code review)

**Open questions:**
- Shared or exclusive-mode WASAPI for the first pass? Exclusive gives lower latency but takes over the device.

## 0.4 MIDI in

**What:** `rt` reads note on/off from a MIDI device through midir and sends it to the audio thread through an rtrb lock-free queue.

**Includes:**
- Listing MIDI inputs and opening one
- Turning MIDI messages into engine events (note on with velocity, note off, sustain pedal ignored for now)
- A test binary: play your keyboard and hear sines

**Acceptance checks:**
- Turning raw MIDI bytes into engine events is unit-tested
- Playing chords and fast passages has no stuck notes or glitches (manual)

## 0.5 Tauri + React shell

**What:** the `app` crate, a Tauri app with a React + Vite frontend that hosts the engine.

**Includes:**
- A keyboard view that lights up active notes and can be clicked to play
- Volume and release controls
- MIDI device selection
- UI → engine commands over Tauri IPC, and engine → UI meters throttled to around 30 Hz. Audio never crosses IPC.

**Acceptance checks:**
- The UI controls change the sound in real time
- MIDI and on-screen keys both play
- No audible glitches when interacting with the UI while playing

## 0.6 Latency measurement

**What:** measure real key-to-sound latency and record it.

**Includes:**
- Reporting the buffer size and the latency cpal reports
- A physical loopback measurement or a simple recorded tap test (method decided when we get here)
- Writing the results down in `docs/`

**Acceptance checks:**
- A measured number at 64, 128 and 256-sample buffers, compared against the 5–10 ms target
