# piano-lab: notes for AI agents

A physically modelled piano. The code is written by AI agents; the owner makes the decisions, reviews PRs and tests the UI.

## Read first
- [docs/vision.md](docs/vision.md): goals, architecture, technology and dependency rules
- [docs/plan/](docs/plan/README.md): the roadmap and the current step's plan and decisions

## Terms
Each part is named for what it does.
- **sound engine** (`sound-engine/`): the piano's physics and real-time plumbing. Standard C++ only, never JUCE. The build enforces this.
- **audio service** (`audio-service/`): the C++ program that runs the sound engine: JUCE audio and MIDI, settings, recording, rendering files. It never touches the network; it talks only to the web server, over the service pipe.
- **web server** (`web-server/`, from step 0.6): the TypeScript program on Node that serves the web UI and the web API, pairs devices, and starts the audio service. All network input is parsed here.
- **web API** (`web-api/`, from step 0.6): the public API's TypeBox schemas, shared by the web server and the web UI; documented in `docs/web-api.md`.
- **web UI** (`web-ui/`, from step 0.7): the React app.
- **service pipe**: the private link between the web server and the audio service (the audio service's stdin and stdout).

## The quality gate
`npm run check` type-checks the task scripts, checks formatting, builds, tests, lints and runs AddressSanitizer. It must pass before work is handed over. The other commands (`npm run build`, `npm test`, `npm run format`, ...) are listed in the README; they live in `scripts/`.

## Hearing the piano
Agents can't listen, so render instead: `npm run render -- <file>` (`piano-lab render`) turns a MIDI or MusicXML file into a WAV, deterministically. See [docs/rendering.md](docs/rendering.md).

## Where agent instructions live
- This file stays a short index. Topic rules live in `.claude/rules/`, one topic per file.
- **Give every rule a `paths:` frontmatter** so it loads only when matching files are touched. Leave it off only for rules that truly apply to every session (currently just `workflow.md`).
- Detailed human-readable guides stay in `docs/`; a rule points to them instead of copying them.
- Personal, uncommitted notes go in `CLAUDE.local.md` (git-ignored).
