# piano-lab vision

*Revised 2026-11-21. This supersedes the "Software" section of the original project summary (the Google Doc). The physics, guided calibration and "voiced by taste" sections of that summary still stand.*

## The goal
A high-quality, physically modelled piano that is **free, for a large audience, with low friction**. Anyone with a MIDI keyboard and an ordinary computer should be able to get a great-sounding, deeply customisable piano without buying software or building a studio.

## The key idea: a local piano, controlled from anywhere
A piano-computer workstation desk is a luxury, and most homes don't have room for a home studio. So piano-lab is split in two:

- **The backend** runs locally, next to the keyboard. It runs the engine (the piano's physics) and handles the audio and the MIDI, and it can run **headless**, with no screen at all.
- **The UI (frontend)** is a web app the backend serves itself. You open it on the same machine, or on **a phone or tablet on the local network**.

The power is in that split. An old desktop or laptop tucked behind a digital piano becomes a standalone instrument, with all settings on your phone. A laptop on the piano does everything in one box. Same backend, same UI, same protocol.

**Terms used in these docs:**
- The **engine** is the piano's physics and real-time plumbing: a library of standard C++ with no dependencies (`engine/`).
- The **backend** is the program that runs the engine. It owns the audio and MIDI devices (through JUCE), serves the UI and speaks the control protocol (`backend/`).
- The **UI** is the React web app (`ui/`).

```
                     ┌──────────── piano-lab backend (C++) ────────────┐
MIDI keyboard ──────▶│ engine (physics) · audio device · MIDI · presets│──▶ speakers / DAC
                     │ embedded web server: serves the React app +     │
                     │ a WebSocket control protocol                    │
                     └──────────────────────┬──────────────────────────┘
                                            │ same protocol everywhere
              ┌─────────────────────────────┼──────────────────────────┐
     phone / tablet browser       the machine's own window         DAW plugin window
     on the local network         (WebView or browser → localhost)  (later)
```

## An open control API
The UI has no special access: it talks to the backend through the same **documented, versioned control protocol** anyone can use. On the laptop, the backend simply opens the UI in the user's default browser, which makes this visible: the piano is a local service, and the UI is one client of it.

That opens the door to things we'd never build ourselves:
- **Custom frontends:** a minimal stage UI, a teaching tool, a tablet layout, a hardware control surface
- **Scripts and research tools** that drive the piano directly
- **AI control through MCP:** an MCP server for piano-lab would let a user's own AI assistant voice the piano in plain words ("make the bass less boomy"). This fulfils the original summary's optional LLM assistant with no AI service built in: the user brings their own, and it stays entirely optional.

## Ways to run it

| Mode | Who it's for | When |
|---|---|---|
| **All-in-one desktop** | A Windows laptop on or near the piano. Backend and UI on one machine, phone control available too. | **First: the MVP** |
| **Headless box** | An old desktop or laptop behind the piano, controlled from a phone | The backend and UI work headless from the MVP. Making it a polished appliance (auto-start on boot, no screen needed, network discovery) comes after the MVP. |
| **DAW plugin** | Producers and home studios (VST3 / CLAP, AU on macOS) | Later |
| **Small boards** (e.g. Raspberry Pi) | Tinkerers wanting a tiny appliance | Later, via low-cost quality presets |

## Platforms
- **Windows first.** It's where most users are, and the development machine.
- **Linux and macOS later.** The stack is chosen to be cross-platform from day one, so this is porting and testing, not a rewrite.

## Technology

| Part | Choice | Why |
|---|---|---|
| Engine | **Standard C++20**, no dependencies | Fast to build and test, portable to anything (render CLI, plugin, other platforms, a future Python package), and cleanly MIT |
| Backend | **C++ with JUCE 9** | Mature, cross-platform audio devices (WASAPI shared, low-latency and exclusive; ASIO; ALSA/JACK; CoreAudio), MIDI, and plugin formats when we need them. Avoids the recurring gaps we hit in younger Rust audio crates. |
| Web server | **cpp-httplib** (MIT): HTTP + WebSocket, single header | JUCE has no web server. Simplest to integrate; its thread-per-connection model is fine for a handful of clients. CivetWeb is the fallback. |
| Protocol | **JSON over WebSocket**, with **nlohmann/json** (MIT) | Human-readable public API for frontends, scripts and AI tools; typed struct mapping and clear errors for bad input |
| Real-time plumbing | **rigtorp/SPSCQueue** (MIT), wrapped as our own `rt::SpscQueue<T>` | Fixed-capacity single-producer/single-consumer queues between the MIDI, server and audio threads; never allocates after setup |
| UI | **TypeScript + React + Vite**, **MUI** components (`sx` prop for styling), **Zustand** state | One mobile-first UI for phone, desktop and plugin window. MUI gives a rich component set without building a design system while prototyping; other skins (e.g. Tailwind) can come later. |
| UI tooling | npm, **oxlint + oxfmt**, **Vitest + React Testing Library**, **Playwright** (plus Playwright MCP for agents) | Fast lint/format; unit, component and end-to-end tests; agents can drive and check the real UI during development |
| Protocol types | **TypeBox** | Type-safe runtime validators for every message, with TypeScript types derived from them |
| Build | CMake | Standard for C++/JUCE |
| Code quality | Strict compiler warnings as errors, clang-tidy, clang-format, sanitizers in tests | Agents write most of the code; tooling and tests are their feedback loop |

Two languages, each doing what it's best at: C++ for everything that runs the instrument, TypeScript for everything you look at.

### Dependency rules
- **Pin every dependency** to an exact version or commit (via CPM.cmake), and upgrade deliberately.
- **Hide third-party libraries behind our own small interfaces.** Only one place in our code talks to each library, so swapping it later is a contained change (e.g. cpp-httplib lives only inside the control server).
- **Header-only libraries are contained:** included from exactly one `.cpp` file, marked as system includes so our warning rules don't apply to them, and configured (`#define`s) in one place, on the CMake target. This keeps compile times, Windows-header clashes and configuration mismatches from spreading. *Exception:* small template libraries that include only standard headers (e.g. the SPSC queue) may be included wherever their wrapper type is used, since they carry none of those costs.
- **The engine is JUCE-free, standard C++ only.** Nothing in `engine/` depends on JUCE, networking, JSON, UI code or `backend/`; those live in `backend/`. The build enforces this: it refuses to configure if an engine library links anything outside its allowed list. The physics engine stays portable, fast to test and cleanly MIT.
- **Watch security advisories** for anything that parses network input (the web server, JSON), and keep those up to date.

## Performance: scale by presets, not by targeting the weakest machine
We don't tune for small boards up front. Instead, quality is **customisable**: presets can trade quality for CPU (fewer modes per string, simpler soundboard, lower polyphony for sympathetic resonance, etc.), so weaker hardware still plays flawlessly at reduced detail. The original target stays the reference: the sustain-pedal worst case under 50% of one core on a normal Windows laptop.

## Remote control, done safely (later)
- **Local network only** by default
- **Pairing**: a PIN or code shown by the backend before a phone can control it
- **Discovery** via mDNS, so a phone can find `piano.local` without typing IP addresses

## Openness
- **MIT license.** Anyone can do anything with it, including closed-source commercial use. (Switched from Apache-2.0 on 2026-11-21 for simplicity and familiarity. JUCE itself comes under AGPLv3 or its own license, so a distributed app binary has to follow JUCE's terms; that gets checked before the first release.)
- **Cited modelling choices.** The code is annotated with the papers, books and sources behind each modelling decision, so it's transparent on GitHub where every idea comes from. This also serves researchers and documents prior art.
- **Durable publication** of releases (e.g. Zenodo with a DOI) once there's a product.

## What carries over from the Rust prototype
Steps 0.1–0.2 were built in Rust before this revision. The code will be replaced, but its design decisions carry over to C++:
- **One fixed model per key (88), no voice pool or stealing.** Re-striking excites the same model, as on a real piano.
- **Sample-accurate note events** within each audio block.
- **`float` audio buffers, `double` model state** where precision matters.
- **No allocation while rendering, enforced by an automated test.** In C++: a test binary that replaces the global allocator and fails on any allocation inside the checked region.
- **Headless metric tests** as the agents' feedback loop: pitch, level, release, timing, click-free re-strikes, determinism.
