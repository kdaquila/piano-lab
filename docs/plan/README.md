# piano-lab plan

This folder holds the working plan for piano-lab: what gets built, in what order, and how we know each step works. The product vision and architecture live in [docs/vision.md](../vision.md); the physics, calibration and "voiced by taste" principles live in the original project summary doc. These files turn them into small, reviewable steps.

## How we work
- **One step at a time.** Each step is small enough to review in one sitting. We stop after each one so you can check it, listen to it and approve it before the next starts.
- **Every step has acceptance checks.** These are concrete things that must be true when the step is done, automated wherever possible, so an agent can tell whether it's finished without waiting for your ears.
- **One branch and one PR per step.** The PRs are the public record of what was done and why.
- **Plans change.** When a step teaches us something, we update the plan here instead of working around it.

## Milestones

The MVP is the **all-in-one Windows desktop**: a Windows laptop running the backend, with the web UI in a local window and on a phone over the local network.

| Milestone | Goal | Status |
|---|---|---|
| [0: Skeleton](milestone-0.md) | C++ engine and JUCE backend: MIDI in → one sine per note → audio out, with the React UI served over the local network | In progress |
| 1: Decay | Damped modes, headless metric tests, audio analysis tools for agents (pitch, partials, decay; spectrogram images) | Not started |
| 2: Partials | Several modes per note | Not started |
| 3: Stiffness | Inharmonic partials | Not started |
| 4: Hammer | Nonlinear strike, velocity-dependent brightness | Not started |
| 5: Unisons and bridge | Detuned string groups coupled at the bridge | Not started |
| 6: Soundboard | FEM modes, sympathetic resonance | Not started |
| 7: Room | Reverb stage | Not started |
| 8: Presets, A/B, calibration wizard | Completes the MVP | Not started |

### After the MVP (unordered)
- **Remote control, done safely:** pairing PIN, mDNS discovery (`piano.local`)
- **Headless appliance polish:** run as a background service, auto-start on boot, no screen needed
- **MCP server:** lets a user's own AI assistant control and voice the piano through the public control API
- **Quality presets for weaker hardware:** trade model detail and polyphony for CPU
- **Linux and macOS builds**
- **DAW plugins:** VST3 / CLAP, AU on macOS
- **Small boards** such as the Raspberry Pi

Only the current milestone is planned in detail. Later ones get broken down when we reach them.

## History
- **2026-11-21: stack changed from Rust to C++/JUCE + React**, and the architecture became a local backend with a web UI reachable over the local network. Steps 0.1–0.2 were first built in Rust (PRs on GitHub); their design decisions carry over, as listed in [vision.md](../vision.md#what-carries-over-from-the-rust-prototype).
