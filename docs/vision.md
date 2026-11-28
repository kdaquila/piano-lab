# piano-lab vision

*Revised 2026-11-21. This supersedes the "Software" section of the original project summary (the Google Doc). The physics, guided calibration and "voiced by taste" sections of that summary still stand.*

## The goal
A high-quality, physically modelled piano that is **free, for a large audience, with low friction**. Anyone with a MIDI keyboard and an ordinary computer should be able to get a great-sounding, deeply customisable piano without buying software or building a studio.

## The key idea: a local piano you can play, tweak and hack on
piano-lab is a local instrument: it runs on your own computer, next to your keyboard, and everything about it is open to change, from the sound to the code to the API.

- **The audio service** runs the sound engine (the piano's physics) and owns the audio and MIDI devices. It never touches the network.
- **The web server** is the audio service's only door to the outside: it serves the web UI and the web API, and passes validated commands to the audio service.
- **The web UI** is **designed for touch first**: a laptop's touchscreen or a touch monitor on the piano's music stand, and a mouse just as well. It runs on the same computer by default.
- **On your local network, if you choose:** one setting lets a phone or tablet act as a remote, and lets scripts and tools on other machines drive the piano, after pairing. It's a local instrument for tinkerers, so it's as safe as the network it's on: secure your network, like anything else on it. See [Security first](#security-first).

In one sentence: the **web UI** calls the **web API**, served by the **web server**, which drives the **audio service** over the **service pipe**, and the audio service runs the **sound engine** on your audio and MIDI devices.

**Terms used in these docs** (each named for what it does):
- The **sound engine** is the piano's physics and real-time plumbing: a library of standard C++ with no dependencies (`sound-engine/`). "The engine" for short.
- The **audio service** is the C++ program that runs the sound engine. It owns the audio and MIDI devices (through JUCE), reads and writes the settings, records, renders files, and talks only to the web server (`audio-service/`).
- The **web server** is the TypeScript program on Node that faces the outside: it serves the web UI and the web API (HTTP and WebSocket), pairs devices, and starts the audio service (`web-server/`).
- The **web API** is the public, versioned API every client uses, defined once as TypeBox schemas shared by the web server and the web UI (`web-api/`), and documented in `docs/web-api.md`.
- The **web UI** is the React app (`web-ui/`).
- The **service pipe** is the private link between the web server and the audio service: the audio service's stdin and stdout, carrying a small fixed format. It isn't public, and only the web server uses it.

```
                  ┌──────────── audio service (C++) ───────────┐
MIDI keyboard ───▶│ sound engine · audio · MIDI · settings ·   │───▶ speakers / DAC
                  │ recording                                  │
                  └──────────────────────┬─────────────────────┘
                                         │ service pipe (validated commands only)
                  ┌──────────────────────┴─────────────────────┐
                  │ web server (TypeScript, Node)              │
                  │ serves the web UI and the web API (HTTP +  │
                  │ WebSocket) · pairing                       │
                  └──────────────────────┬─────────────────────┘
                                         │ the web API, the same for every client
            ┌────────────────────────────┼────────────────────────┐
   web UI on this computer    phone / tablet / scripts     DAW plugin window
   (browser or installed      on the local network         (later)
   app; localhost, default)   (opt-in, paired)
```

## An open web API
The web UI has no special access: it talks to piano-lab through the same **documented, versioned web API** anyone can use. On the computer itself, the web server simply opens the web UI in the user's browser, which makes this visible: the piano is a local program, and the web UI is one client of it.

That opens the door to things we'd never build ourselves:
- **Custom frontends:** a minimal stage UI, a teaching tool, a tablet layout, a hardware control surface
- **Scripts and research tools** that drive the piano directly
- **AI control through MCP:** an MCP server for piano-lab would let a user's own AI assistant voice the piano in plain words ("make the bass less boomy"). This fulfils the original summary's optional LLM assistant with no AI service built in: the user brings their own, and it stays entirely optional.

## Ways to run it

| Mode | Who it's for | When |
|---|---|---|
| **All-in-one desktop** | A Windows laptop (ideally with a touchscreen) or a desktop with a touch monitor, on or near the piano. Everything on one machine; a phone or scripts on the local network if you opt in. | **First: the MVP** |
| **Headless box** | An old desktop or laptop behind the piano, controlled from a phone | Possible from the MVP with network mode on. Making it a polished appliance (auto-start on boot, no screen needed, network discovery) comes after the MVP. |
| **DAW plugin** | Producers and home studios (VST3 / CLAP, AU on macOS) | Later |
| **Small boards** (e.g. Raspberry Pi) | Tinkerers wanting a tiny appliance | Later, via low-cost quality presets |

The owner's own setup is a headless box built into a piano-style cabinet, with a phone or tablet on the music shelf as the only screen: see [docs/hardware](hardware/README.md). It reaches piano-lab over the local network, not `localhost`, which is why network mode (step 0.9) matters.

## Platforms
- **Windows first.** It's where most users are, and the development machine.
- **Linux and macOS later.** The stack is chosen to be cross-platform from day one, so this is porting and testing, not a rewrite.

## Technology

| Part | Choice | Why |
|---|---|---|
| Sound engine | **Standard C++23**, no dependencies | Fast to build and test, portable to anything (offline rendering, plugin, other platforms, a future Python package), and cleanly MIT |
| Audio service | **C++ with JUCE 9** | Mature, cross-platform audio devices (WASAPI shared, low-latency and exclusive; ASIO; ALSA/JACK; CoreAudio), MIDI, and plugin formats when we need them. Avoids the recurring gaps we hit in younger Rust audio crates. |
| Web server | **TypeScript on Node**, with **Fastify** (HTTP), **@fastify/websocket** (on **ws**) and **TypeBox** schemas | Keeps C++ off the network: everything that parses network input is memory-safe, and every route's schema is validated by Fastify before our code runs. Node is already the runtime `npx piano-lab` brings, and the TypeBox schemas are shared with the web UI. Considered: C++ with cpp-httplib and nlohmann/json (C++ parsing untrusted input is the riskiest code we could write), Rust (memory-safe too, but a third language for a marginal gain), Express (no built-in schema validation) and tRPC (assumes TypeScript clients; our API is public). |
| Web API | **JSON over HTTP** for requests, one **WebSocket** for live traffic; **TypeBox** schemas in one package shared by the web server and the web UI | A human-readable public API for frontends, scripts and AI tools, `curl`-able; type-safe runtime validators for every message with TypeScript types derived from them, so there's one definition of the API |
| Service pipe | The audio service runs as the web server's child process, talking **JSON Lines** over its **stdin/stdout**, parsed in C++ by an established library (Glaze, or simdjson) | No network socket in C++ at all; the audio service only ever reads commands the web server has already validated |
| Real-time plumbing | **rigtorp/SPSCQueue** (MIT), wrapped as our own `rt::SpscQueue<T>` | Fixed-capacity single-producer/single-consumer queues between the MIDI, pipe-reader and audio threads; never allocates after setup |
| Settings | **TOML** files, read with **toml++** (MIT) | Friendly to edit by hand, with comments; one `settings.toml` in the install folder holds only what the user changed |
| Logging | **spdlog** (MIT), behind our own small `Log` interface | Levels, a coloured console and rotating log files out of the box; our interface keeps it swappable and lets tests collect messages instead |
| Web UI | **TypeScript + React + Vite**, **MUI** components (`sx` prop for styling), **Zustand** state | One touch-first UI for touchscreens, phones, mouse and keyboard, and later the plugin window. MUI gives a rich component set without building a design system while prototyping; other skins (e.g. Tailwind) can come later. |
| Web UI tooling | npm, **oxlint + oxfmt**, **Vitest + React Testing Library**, **Playwright** (plus Playwright MCP for agents) | Fast lint/format; unit, component and end-to-end tests; agents can drive and check the real UI during development |
| Build | CMake | Standard for C++/JUCE |
| Developer commands | **npm scripts**, written in **TypeScript** and run directly by Node | One familiar entry point (`npm run build`, `npm test`, `npm run check`) for every part; easier to read and maintain than shell scripts, and cross-platform |
| Code quality | Strict compiler warnings as errors, clang-tidy, clang-format, sanitizers in tests | Agents write most of the code; tooling and tests are their feedback loop |

Two languages, each doing what it's best at: C++ for everything that runs the instrument, TypeScript for everything you look at and everything that faces the network.

### Build only the real design
Every user-facing surface (a command, an option, a mode, a file) is part of the final design. If a step seems to need something temporary, to be torn down once the real thing exists, that's a sign to reorder the steps or expand the design instead. A capability can wait until its real home exists (e.g. choosing the audio device arrives with configuration, not as a stopgap command-line option). Internal test harnesses that bypass or mock parts of the stack are ordinary testing practice, not temporary entry points.

### Dependency rules
- **Pin every dependency** to an exact version or commit (C++ via CPM.cmake; npm packages as exact versions in `package.json`, locked by `package-lock.json`), and upgrade deliberately.
- **Hide third-party libraries behind our own small interfaces.** Only one place in our code talks to each library, so swapping it later is a contained change (e.g. toml++ lives only inside the audio service's `settings` library).
- **Header-only libraries are contained:** included from exactly one `.cpp` file, marked as system includes so our warning rules don't apply to them, and configured (`#define`s) in one place, on the CMake target. This keeps compile times, Windows-header clashes and configuration mismatches from spreading. *Exception:* small template libraries that include only standard headers (e.g. the SPSC queue) may be included wherever their wrapper type is used, since they carry none of those costs.
- **The sound engine is JUCE-free, standard C++ only.** Nothing in it depends on JUCE, networking, JSON, UI code or the audio service; JUCE lives in the audio service, and networking in the web server. The build enforces this: it refuses to configure if an engine library links anything outside its allowed list. The physics engine stays portable, fast to test and cleanly MIT.
- **Watch security advisories** for everything the web server depends on, since it parses network input, and keep those up to date.
- **The web server's dependencies are few and earn their place.** Each one is attack surface and supply-chain risk, so it's exact-pinned, installed without install scripts, and only added when it does real security or correctness work (e.g. Fastify's schema validation).

## The music never stops
An error must never interrupt someone playing, in any build. The engine recovers from every problem it can (and there's a defined fallback for each), records it, and keeps playing. The audio service and the web server log everything (it's a single-user local program, so they can log freely), and the web UI shows problems in a log and error panel the user can show or hide like any other part of the interface. See [docs/coding-style.md](coding-style.md).

## Performance: scale by presets, not by targeting the weakest machine
We don't tune for small boards up front. Instead, quality is **customisable**: presets can trade quality for CPU (fewer modes per string, simpler soundboard, lower polyphony for sympathetic resonance, etc.), so weaker hardware still plays flawlessly at reduced detail. The original target stays the reference: the sustain-pedal worst case under 50% of one core on a normal Windows laptop.

## Security first
piano-lab listens for connections, so it's treated as something worth attacking, even as a piano app: AI-assisted attacks make small targets worthwhile. Nothing it handles is private (it's music and settings), so the thing to protect is **the user's computer**: no bug in piano-lab may let anyone run code on it. Most of the effort therefore goes into keeping the attack surface small.
- **C++ never touches the network.** All network input is parsed by the web server, in memory-safe TypeScript, with every route's schema enforced by Fastify. The audio service only reads commands the web server already validated, over the service pipe. Memory-safety bugs in C++ that parses untrusted input are the classic way in; we don't write such code.
- **`localhost` by default.** Out of the box, nothing on the network can reach piano-lab at all.
- **Network mode is opt-in** (`[server] listen = "network"`), and every device on the network must **pair** first: a short code shown at startup, entered once. Before pairing, almost nothing answers.
- **Browser protections, always on:** `Host` and `Origin` checks and JSON-only requests, so websites in your own browser can't drive the piano, even on `localhost`.
- **Deny by default, least privilege:** a small API with strict schemas and size limits; no file paths or uploads from the network; never run as admin; write only inside the install folder.
- **The compiler's exploit mitigations** on the C++ (MSVC's Control Flow Guard and CET shadow-stack compatibility, plus ASLR and DEP), and **fuzz tests** for whatever the audio service parses (the service pipe, music files).
- **A written threat model** (step 0.8, `docs/security.md`) that every security decision is checked against.
- **Traffic isn't encrypted.** In network mode, piano-lab relies on the network's own security (WPA2/WPA3 at home). The user manual says so plainly, with ways to harden it, e.g. putting the laptop and phone on their own network such as the laptop's Mobile Hotspot. A self-signed certificate with an optional throwaway, name-constrained CA on the phone stays an option for later; any such route must cost nothing and need no cloud service of ours.
- **Later:** discovery via mDNS, so a phone can find the piano without typing an IP address.

## Distribution
- **Source-first.** The public source is the product; building it is one command, and an agent can do it for you.
- **No fees and no signing services, ever.** It's a free app: no paid code-signing certificates, no notarisation, no app stores, no cloud portals.
- **Prebuilt binaries through npm** (later): public GitHub Actions builds compile the audio service for each platform and publish it as npm packages (one per platform, picked automatically, the way esbuild and oxlint ship), next to the web server and the built web UI. `npx piano-lab` then starts the web server, which starts the audio service, with only Node installed. npm's provenance attestation shows which commit built each binary. To check first: how Windows' Smart App Control treats such binaries, and the JUCE and ASIO licence terms (see [Openness](#openness)).
- **"Install with your AI agent":** a short prompt in the README pointing an agent at step-by-step setup instructions written for agents, tested in a clean Windows Sandbox.

## Openness
- **MIT license.** Anyone can do anything with it, including closed-source commercial use. (Switched from Apache-2.0 on 2026-11-21 for simplicity and familiarity. JUCE itself comes under AGPLv3 or its own license, and the ASIO support uses Steinberg's ASIO SDK headers, bundled with JUCE, under the Steinberg ASIO licence or GPLv3. So a distributed app binary has to follow JUCE's terms and, if built with ASIO, one of the ASIO licences; that gets checked before the first release. The `PIANOLAB_ASIO` build option makes an ASIO-free build one switch.)
- **Cited modelling choices.** The code is annotated with the papers, books and sources behind each modelling decision, so it's transparent on GitHub where every idea comes from. This also serves researchers and documents prior art.
- **Durable publication** of releases (e.g. Zenodo with a DOI) once there's a product.

## What carries over from the Rust prototype
Steps 0.1–0.2 were built in Rust before this revision. The code will be replaced, but its design decisions carry over to C++:
- **One fixed model per key (88), no voice pool or stealing.** Re-striking excites the same model, as on a real piano.
- **Sample-accurate note events** within each audio block.
- **`float` audio buffers, `double` model state** where precision matters.
- **No allocation while rendering, enforced by an automated test.** In C++: a test binary that replaces the global allocator and fails on any allocation inside the checked region.
- **Headless metric tests** as the agents' feedback loop: pitch, level, release, timing, click-free re-strikes, determinism.
