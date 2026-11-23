# piano-lab: notes for AI agents

A physically modelled piano. The code is written by AI agents; the owner makes the decisions, reviews PRs and tests the UI.

## Read first
- [docs/vision.md](docs/vision.md): goals, architecture, technology and dependency rules
- [docs/plan/](docs/plan/README.md): the roadmap and the current step's plan and decisions

## Terms
- **engine** (`engine/`): the piano's physics and real-time plumbing. Standard C++ only, never JUCE. The build enforces this.
- **backend** (`backend/`): the program that runs the engine: JUCE audio and MIDI, and later the web server and protocol.
- **UI** (`ui/`): the React web app.

## The quality gate
`.\tools\check.ps1` (PowerShell, Windows) formats, builds, tests, lints and runs AddressSanitizer. It must pass before work is handed over.

## Hearing the piano
Agents can't listen, so render instead: `piano-lab render` turns a MIDI or MusicXML file into a WAV, deterministically. See [docs/rendering.md](docs/rendering.md).

## Where agent instructions live
- This file stays a short index. Topic rules live in `.claude/rules/`, one topic per file.
- **Give every rule a `paths:` frontmatter** so it loads only when matching files are touched. Leave it off only for rules that truly apply to every session (currently just `workflow.md`).
- Detailed human-readable guides stay in `docs/`; a rule points to them instead of copying them.
- Personal, uncommitted notes go in `CLAUDE.local.md` (git-ignored).
