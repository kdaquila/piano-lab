# piano-lab plan

This folder holds the working plan for piano-lab: what gets built, in what order, and how we know each step works. The high-level design (physics, architecture, roadmap) lives in the project summary doc. These files turn that roadmap into small, reviewable steps.

## How we work
- **One step at a time.** Each step is small enough to review in one sitting. We stop after each one so you can check it, listen to it and approve it before the next starts.
- **Every step has acceptance checks.** These are concrete things that must be true when the step is done, automated wherever possible, so an agent can tell whether it's finished without waiting for your ears.
- **Plans change.** When a step teaches us something, we update the plan here instead of working around it.

## Milestones

| Milestone | Goal | Status |
|---|---|---|
| [0: Skeleton](milestone-0.md) | MIDI in → one sine per note → audio out, with a basic UI | In progress (0.1 in review) |
| 1: Decay | Damped modes, render CLI, headless metric tests | Not started |
| 2: Partials | Several modes per note | Not started |
| 3: Stiffness | Inharmonic partials | Not started |
| 4: Hammer | Nonlinear strike, velocity-dependent brightness | Not started |
| 5: Unisons and bridge | Detuned string groups coupled at the bridge | Not started |
| 6: Soundboard | FEM modes, sympathetic resonance | Not started |
| 7: Room | Reverb stage | Not started |
| 8: Presets, A/B, calibration wizard | Completes the MVP | Not started |

Only the current milestone is planned in detail. Later ones get broken down when we reach them.
