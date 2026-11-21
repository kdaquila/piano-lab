# piano-lab

A physically modelled grand piano. Strings, hammers, bridge and soundboard are simulated in real time rather than played back from samples, and the instrument is voiced by ear through A/B and ABX listening tests instead of being fitted to recordings.

It's a hobby project: a standalone Windows app with a Rust engine and a Tauri + React UI. Early days, nothing playable yet.

## Status
Working on Milestone 0, the walking skeleton (MIDI in → sine per note → audio out). See [docs/plan](docs/plan/README.md) for the roadmap and current step.

## Layout

| Crate | Role |
|---|---|
| `engine-core` | Traits and orchestration. Knows nothing about devices, files or UI. |
| `models` | Model implementations (strings, hammers, soundboard) behind those traits. |
| `calibration` | Calibration file format: TOML parameters plus binary modal-data sidecars. |
| `rt` | Real-time runtime: audio and MIDI devices, lock-free queues, model swapping. |
| `render-cli` | Deterministic offline renderer; doubles as test harness and research tool. |

## Building
Needs Rust stable with the MSVC toolchain on Windows.

```
cargo build --workspace
cargo test --workspace
```

## Contributing
This is a solo project: outside contributions and pull requests aren't accepted. Forks are very welcome. See [CONTRIBUTING.md](CONTRIBUTING.md).

## License
Licensed under the [Apache License, Version 2.0](LICENSE).
