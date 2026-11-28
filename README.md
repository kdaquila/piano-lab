# piano-lab

A physically modelled grand piano. Strings, hammers, bridge and soundboard are simulated in real time rather than played back from samples, and the instrument is voiced by ear through A/B and ABX listening tests instead of being fitted to recordings.

The goal is a high-quality piano that's **free, for a large audience, with low friction**. A local program runs the physics, audio and MIDI, and serves a touch-first web UI on the same computer, or, if you turn on network mode, to a paired phone on your local network. See [docs/vision.md](docs/vision.md).

It's a hobby project, in early days. Today it plays a plain sine per key from a MIDI keyboard: the real engine at its simplest.

## Status
Working on Milestone 0, the walking skeleton (MIDI in → sine per note → audio out, with a web UI). See [docs/plan](docs/plan/README.md) for the roadmap and current step.

## Layout

| Folder | What's in it |
|---|---|
| `sound-engine/` | **The sound engine:** the piano's physics and real-time plumbing, in standard C++ only. Never depends on JUCE, networking or UI code (enforced by the build). |
| `sound-engine/engine-core` | Key models, the 88-key keyboard, note events and performances (what's played), offline rendering, tuning |
| `sound-engine/models` | Physical models: strings, hammers, bridge, soundboard |
| `sound-engine/calibration` | Fitting model parameters to measurements and listening tests |
| `sound-engine/rt` | Real-time plumbing shared with the audio service, such as lock-free queues |
| `audio-service/` | **The audio service:** the C++ program that runs the engine. Audio devices, MIDI and music files through [JUCE](https://juce.com), settings and recording. It never touches the network: from step 0.6 the TypeScript web server (`web-server/`) drives it. |
| `audio-service/app` | `piano-lab-audio`, the audio service's program. Its subcommands are its roles: `run` (started by the web server) and `render` |
| `audio-service/live` | The live piano: the audio core (lock-free queues, the per-block audio work, recording), JUCE's audio and MIDI devices, and answering the web server's commands |
| `audio-service/render` | Rendering a music file to WAV with no audio device. A test and research tool, built for AI agents |
| `audio-service/music-io` | Reading MIDI and MusicXML files into the engine's music type, and writing WAV files |
| `audio-service/logging` | The audio service's log (console and file), with spdlog behind our own small interface |
| `audio-service/settings` | `settings.toml`: reading it, describing and changing settings by name, and saving it, all from the settings' field tables |
| `audio-service/pipe` | The service pipe's messages (JSON-RPC 2.0, one per line), read and written with Glaze |
| `web-server/` | **The web server:** TypeScript on Node. Serves the web UI and the web API on `http://localhost:4440`, and runs the audio service, talking to it over the service pipe (being built in step 0.6) |
| `web-api/` | **The web API:** its TypeBox schemas, shared by the web server and the web UI |
| `web-ui/` | **The web UI:** a touch-first React app (from step 0.7) |
| `cmake/` | Build helpers: dependencies, warnings, tests, sanitizers |
| `scripts/` | The developer tasks behind the npm commands (build, test, check, ...), in TypeScript |
| `docs/` | Vision, plan and [coding style](docs/coding-style.md) |

## Building (Windows)

### One-time setup
You need:
- **Visual Studio 2022** (Community is fine) with the **Desktop development with C++** workload, plus the **C++ AddressSanitizer** component for the sanitizer build
- **Node.js** 24.15 or newer (the LTS release), which runs the developer commands. **CMake** 3.28 or newer, **Ninja**, and **LLVM** 23 (for clang-format and clang-tidy). With winget:

  ```powershell
  winget install --id OpenJS.NodeJS.LTS --exact
  ```
  ```powershell
  winget install --id Kitware.CMake --exact
  ```
  ```powershell
  winget install --id Ninja-build.Ninja --exact
  ```
  ```powershell
  winget install --id LLVM.LLVM --exact
  ```
  LLVM's installer may not add itself to PATH. If `clang-format --version` fails, add `C:\Program Files\LLVM\bin` to your user PATH.
- Optional: an editor with **clangd**. The repo ships editor-neutral settings: `.clangd` (where the build flags are) and `.editorconfig` (line endings, whitespace). In **VS Code**, open the folder and accept the recommended extensions (clangd, CMake Tools, EditorConfig). Format-on-save is up to you: turn it on in your own settings if you like it, since `npm run check` is what enforces formatting. Visual Studio stays available as a debugger.

Then, in the repository folder, install the web server's packages and the developer tools that come from npm (TypeScript, Vitest, oxlint, oxfmt, and npm-run-all2 for running every part's tasks). Packages' install scripts never run (`.npmrc`):
```powershell
npm install
```

The first build downloads the pinned C++ dependencies (JUCE, Catch2, spdlog, SPSCQueue, toml++) into a shared cache (`%LOCALAPPDATA%\CPM`, or set `CPM_SOURCE_CACHE`), so it takes a few minutes. Later builds are quick.

### Everyday commands
Everything runs through npm, from any terminal. The commands set up the Visual Studio build environment themselves, for the programs they start, and leave your terminal as it was.

| Command | What it does |
|---|---|
| `npm run build` | Builds the debug preset (configuring first when needed) |
| `npm test` | Builds, then runs the tests |
| `npm run format` | Reformats the C++ with clang-format, and the TypeScript with oxfmt |
| `npm run lint` | Runs clang-tidy on every C++ file, and oxlint on the TypeScript |
| `npm run check` | The quality gate (below) |
| `npm start` | Builds, then runs piano-lab: the web server and the audio service (see [Playing the piano](#playing-the-piano)) |
| `npm run acceptance` | Builds, then runs the end-to-end checks against the real audio service (needs a sound device; plays quiet tones) |
| `npm run render -- <file>` | Builds, then renders a music file to WAV (see [Rendering](#rendering-a-music-file-to-wav)) |

Each one runs every part of the project. To run one part only, add its name: `npm run build:audio` builds just the C++ (the sound engine and the audio service), and `npm run build:web` just the TypeScript (the web server and the web API, compiled to their `dist/` folders). The web UI joins with its own `:ui` commands in step 0.7.

`build:audio` and `test:audio` take a preset after `--`: `dev` (debug, the default), `release` (optimised) or `asan` (the sound engine and its tests under AddressSanitizer, built without JUCE). For example `npm run test:audio -- release`. Output goes to `build/<preset>/`.

### The quality check
One command runs everything: a type check of the task scripts; for the C++, formatting (checked, never changed), the build and tests, clang-tidy, and the AddressSanitizer build and tests; for the TypeScript, formatting, the build, the tests and oxlint. It's the same gate for people and AI agents. It requires LLVM 23, so formatting is identical on every machine.
```powershell
npm run check
```
If it reports badly formatted files, `npm run format` fixes them.

## Playing the piano
Plug in a MIDI keyboard, then build and start the piano:
```powershell
npm start
```
That builds both parts and runs `piano-lab start`: the web server, on `http://localhost:4440`, which runs the audio service. It runs until you press Ctrl+C. What it opened, and any problems, are shown on the console; the audio service also logs to `logs\piano-lab-audio.log` next to `piano-lab-audio.exe`.

From another terminal in the repository folder, `npx` runs the `piano-lab` command:
```powershell
npx piano-lab status
```
```powershell
npx piano-lab stop
```
`npx piano-lab help` lists every command. The web API itself is described in [docs/web-api.md](docs/web-api.md); for example, `curl http://localhost:4440/api/v0/status` shows the same status as JSON.

### Talking to the audio service directly
For debugging, `npm run start:audio` runs the audio service alone (the program is `.\build\dev\audio-service\app\piano-lab-audio.exe run`). It takes commands on its input, one JSON line each, the same ones the web server sends. For example, this shows what's open:
```json
{"jsonrpc":"2.0","id":1,"method":"status"}
```

### Settings
Your settings live in `settings.toml`, next to `piano-lab-audio.exe`. The first run writes one listing every setting, commented out, with its description, range and built-in value: remove the `#` in front of a setting to change it. For example, to play through an ASIO interface from one keyboard:
```toml
[audio]
type = "ASIO"
output = "Focusrite USB ASIO"
bufferSize = 128

[midi]
inputs = ["Digital Piano"]
```
By default the piano plays through your default audio output (Windows Audio, at 48 kHz) from every MIDI input. Device names are the ones Windows shows; the log lists what's available when a name doesn't match, and the default is used instead. Problems in the file are logged with their line, and never stop the piano.

piano-lab saves settings changed while it runs (from the app, or through the web API) back to this file, a second after the last change. So edit the file while piano-lab is stopped. If the file holds anything piano-lab didn't write, such as your own comments, it's first copied to `settings.toml.bak`.

## Rendering a music file to WAV
`piano-lab render` plays a MIDI file or MusicXML score (for example, exported from MuseScore) through the piano engine and writes a WAV file, with no audio device:
```powershell
npm run render -- piece.musicxml -o piece.wav
```
File names are relative to the folder you run it from. `.\build\dev\audio-service\app\piano-lab-audio.exe render` does the same without building first.
See [docs/rendering.md](docs/rendering.md) for what it supports and how AI agents use it.

## Contributing
This is a solo project: outside contributions and pull requests aren't accepted. Forks are very welcome. See [CONTRIBUTING.md](CONTRIBUTING.md).

## License
Licensed under the [MIT License](LICENSE). The audio service uses JUCE, which has its own licence terms (AGPLv3 or the JUCE licence), and its ASIO support uses Steinberg's ASIO SDK headers (the Steinberg ASIO licence or GPLv3); see [docs/vision.md](docs/vision.md#openness).
