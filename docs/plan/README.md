# piano-lab plan

This folder holds the working plan for piano-lab: what gets built, in what order, and how we know each step works. The product vision and architecture live in [docs/vision.md](../vision.md); the physics, calibration and "voiced by taste" principles live in the original project summary doc. These files turn them into small, reviewable steps.

## How we work
- **One step at a time.** Each step is small enough to review in one sitting. We stop after each one so you can check it, listen to it and approve it before the next starts.
- **Every step has acceptance checks.** These are concrete things that must be true when the step is done, automated wherever possible, so an agent can tell whether it's finished without waiting for your ears.
- **One branch and one PR per step.** The PRs are the public record of what was done and why.
- **Plans change.** When a step teaches us something, we update the plan here instead of working around it.

## Milestones

The MVP is the **all-in-one Windows desktop**: a Windows laptop (ideally a touchscreen) running piano-lab, with the touch-first web UI on the same machine and, with network mode on, on a paired phone over the local network.

| Milestone | Goal | Status |
|---|---|---|
| [0: Skeleton](milestone-0.md) | C++ engine and JUCE service: MIDI in → one sine per note → audio out, with a TypeScript backend serving the touch-first React UI, and opt-in network mode with pairing | In progress |
| 1: Decay | Damped modes, headless metric tests, audio analysis tools for agents (pitch, partials, decay; spectrogram images) | Not started |
| 2: Partials | Several modes per note | Not started |
| 3: Stiffness | Inharmonic partials | Not started |
| 4: Hammer | Nonlinear strike, velocity-dependent brightness | Not started |
| 5: Unisons and bridge | Detuned string groups coupled at the bridge | Not started |
| 6: Soundboard | FEM modes, sympathetic resonance | Not started |
| 7: Room | Reverb stage | Not started |
| 8: Presets, A/B, calibration wizard | Completes the MVP | Not started |

### After the MVP (unordered)
- **mDNS discovery** (`piano.local`), so a phone finds the piano without an IP address (pairing itself is in step 0.9)
- **Distribution:** prebuilt binaries through npm (`npx piano-lab`), built in public CI with npm provenance; an "install with your AI agent" guide; no paid signing ([vision.md](../vision.md#distribution))
- **Bluetooth MIDI input**, for keyboards and apps that speak BLE-MIDI
- **One-click private hotspot:** the laptop's own Wi-Fi network for playing away from home
- **Headless appliance polish:** run as a background service, auto-start on boot, no screen needed
- **MCP server:** lets a user's own AI assistant control and voice the piano through the public control API
- **Quality presets for weaker hardware:** trade model detail and polyphony for CPU
- **Linux and macOS builds**
- **DAW plugins:** VST3 / CLAP, AU on macOS
- **Small boards** such as the Raspberry Pi

Only the current milestone is planned in detail. Later ones get broken down when we reach them.

## History
- **2026-11-21: stack changed from Rust to C++/JUCE + React**, and the architecture became a local backend with a web UI reachable over the local network. Steps 0.1–0.2 were first built in Rust (PRs on GitHub); their design decisions carry over, as listed in [vision.md](../vision.md#what-carries-over-from-the-rust-prototype).
- **2026-11-27: developer commands moved to npm** ([step 0.6 prelude](milestone-0.md#06-prelude-developer-commands-through-npm)). `package.json` at the root lists the tasks, each a TypeScript file under `scripts/` run directly by Node 24.15+ and type-checked by `tsc`. `build:server` and the like run one part; the plain names (`build`, `test`, `check`, ...) run every part through npm-run-all2. They load the Visual Studio environment themselves, which replaced `tools/dev-shell.ps1` and `tools/check.ps1`.
- **2026-11-27: security first, a TypeScript backend, and source-first distribution.** Nothing piano-lab handles is private, so security means keeping anyone from getting into the user's computer through it. The network-facing part moved out of C++ into a TypeScript server, and the terms changed with it: the **backend** is now that server (Fastify, TypeBox, `ws`), and the C++ program it runs as a child process behind a pipe is the **service** (`backend/` becomes `service/` in step 0.6). cpp-httplib and nlohmann/json left the plan. The UI became touch-first for the desktop; the network became opt-in, with pairing, after a threat-model step. Milestone 0's steps became 0.6 backend and control API, 0.7 UI, 0.8 threat model, 0.9 network mode and pairing, 0.10 latency. Distribution will be the public source plus prebuilt binaries through npm, with no paid code signing or app stores ([vision.md](../vision.md#distribution)).
