# piano-lab

A physically modelled grand piano. Strings, hammers, bridge and soundboard are simulated in real time rather than played back from samples, and the instrument is voiced by ear through A/B and ABX listening tests instead of being fitted to recordings.

The goal is a high-quality piano that's **free, for a large audience, with low friction**. A local program runs the physics, audio and MIDI, and serves a web UI you can use on the same computer or from a phone on your local network. See [docs/vision.md](docs/vision.md).

It's a hobby project, in early days. Today it plays a plain sine per key from a MIDI keyboard: the real engine at its simplest.

## Status
Working on Milestone 0, the walking skeleton (MIDI in → sine per note → audio out, with a web UI). See [docs/plan](docs/plan/README.md) for the roadmap and current step.

## Layout

| Folder | What's in it |
|---|---|
| `engine/` | **The engine:** the piano's physics and real-time plumbing, in standard C++ only. Never depends on JUCE, networking or UI code (enforced by the build). |
| `engine/engine-core` | Key models, the 88-key keyboard, note events and performances (what's played), offline rendering, tuning |
| `engine/models` | Physical models: strings, hammers, bridge, soundboard |
| `engine/calibration` | Fitting model parameters to measurements and listening tests |
| `engine/rt` | Real-time plumbing shared with the backend, such as lock-free queues |
| `backend/` | **The backend:** the program that runs the engine. Audio devices, MIDI and music files through [JUCE](https://juce.com), and later the web server and control protocol. |
| `backend/app` | `piano-lab`, the one program. Its subcommands are its roles: `render` and `server start` |
| `backend/server` | The live piano: the audio core (lock-free queues, the per-block audio work, recording) and JUCE's audio and MIDI devices |
| `backend/render` | Rendering a music file to WAV with no audio device. A test and research tool, built for AI agents |
| `backend/music-io` | Reading MIDI and MusicXML files into the engine's music type, and writing WAV files |
| `backend/logging` | The backend's log (console and file), with spdlog behind our own small interface |
| `ui/` | **The UI:** a React web app (from step 0.7) |
| `cmake/` | Build helpers: dependencies, warnings, tests, sanitizers |
| `tools/` | Developer scripts, such as the quality check |
| `docs/` | Vision, plan and [coding style](docs/coding-style.md) |

## Building (Windows)

### One-time setup
You need:
- **Visual Studio 2022** (Community is fine) with the **Desktop development with C++** workload, plus the **C++ AddressSanitizer** component for the sanitizer build
- **CMake** 3.28 or newer, **Ninja**, and **LLVM** 23 (for clang-format and clang-tidy). With winget:

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
- Optional: an editor with **clangd**. The repo ships editor-neutral settings: `.clangd` (where the build flags are) and `.editorconfig` (line endings, whitespace). In **VS Code**, open the folder and accept the recommended extensions (clangd, CMake Tools, EditorConfig). Format-on-save is up to you: turn it on in your own settings if you like it, since `tools/check.ps1` is what enforces formatting. Visual Studio stays available as a debugger.

The first configure downloads the pinned dependencies (JUCE, Catch2, spdlog, SPSCQueue, toml++) into a shared cache (`%LOCALAPPDATA%\CPM`, or set `CPM_SOURCE_CACHE`), so it takes a few minutes. Later configures are quick.

### Everyday commands
In each new terminal, set up the Visual Studio build environment first:
```powershell
. .\tools\dev-shell.ps1
```
Then configure, build and test:
```powershell
cmake --preset dev
```
```powershell
cmake --build --preset dev
```
```powershell
ctest --preset dev
```

The presets are `dev` (debug), `release` (optimised) and `asan` (engine and tests under AddressSanitizer, built without JUCE). Output goes to `build/<preset>/`.

### The quality check
One script runs everything: formatting, build, tests, clang-tidy, and the AddressSanitizer build and tests. It's the same gate for people and AI agents. It sets up the build environment itself and leaves your terminal's environment as it found it. It requires LLVM 23, so formatting is identical on every machine.
```powershell
.\tools\check.ps1
```
Add `-Fix` to let clang-format reformat files, or `-SkipAsan` for a faster run.

## Playing the piano
Plug in a MIDI keyboard, then start the server:
```powershell
.\build\dev\backend\app\piano-lab.exe server start
```
It runs until you press Ctrl+C. What it opened, and any problems, are shown on the console and logged to `logs\piano-lab.log` next to `piano-lab.exe`.

### Settings
Your settings live in `settings.toml`, next to `piano-lab.exe`. The first `server start` writes one listing every setting, commented out, with its description, range and built-in value: remove the `#` in front of a setting to change it. For example, to play through an ASIO interface from one keyboard:
```toml
[audio]
type = "ASIO"
output = "Focusrite USB ASIO"
bufferSize = 128

[midi]
inputs = ["Digital Piano"]
```
By default the piano plays through your default audio output (Windows Audio, at 48 kHz) from every MIDI input. Device names are the ones Windows shows; the log lists what's available when a name doesn't match, and the default is used instead. Problems in the file are logged with their line, and never stop the piano. Restart the server after changing the file.

## Rendering a music file to WAV
`piano-lab render` plays a MIDI file or MusicXML score (for example, exported from MuseScore) through the piano engine and writes a WAV file, with no audio device:
```powershell
.\build\dev\backend\app\piano-lab.exe render piece.musicxml -o piece.wav
```
See [docs/rendering.md](docs/rendering.md) for what it supports and how AI agents use it.

## Contributing
This is a solo project: outside contributions and pull requests aren't accepted. Forks are very welcome. See [CONTRIBUTING.md](CONTRIBUTING.md).

## License
Licensed under the [MIT License](LICENSE). The backend uses JUCE, which has its own licence terms (AGPLv3 or the JUCE licence), and its ASIO support uses Steinberg's ASIO SDK headers (the Steinberg ASIO licence or GPLv3); see [docs/vision.md](docs/vision.md#openness).
