# The piano-lab web API, v0

piano-lab's web API is how every client talks to the piano, piano-lab's own web UI included. It's public and versioned, and written for other people's frontends, scripts and AI tools. Its schemas live in [`web-api/`](../web-api/src) as TypeBox definitions; this page describes them in prose.

- **Where:** `http://localhost:4440`, while piano-lab runs (`npm start`, or `piano-lab start`). It answers on this computer only. piano-lab's own web UI is served from the same address, at `/`.
- **Two halves:** JSON over HTTP for everything that waits for its own answer, and one WebSocket for live traffic (notes, live setting changes, and events sent to every client).
- **Version:** every path starts with `/api/v0`. A breaking change gets a new version, `/api/v1`, and both can be served side by side. v0 may still change while piano-lab is in its first milestones.

## Ground rules
- **Requests must be addressed to `localhost`**, `127.0.0.1` or `[::1]` (the `Host` header). Anything else is refused with 403, so a website can't reach piano-lab by pointing its own name at this computer (DNS rebinding).
- **Pages from other websites are refused.** A `POST`, or a WebSocket handshake, whose `Origin` isn't one of piano-lab's own pages gets a 403. Scripts and command-line tools send no `Origin`, and aren't affected.
- **Bodies are JSON.** A `POST` with a body must send `Content-Type: application/json`; anything else gets a 415. Bodies and WebSocket messages are at most 64 KiB. A `POST` that takes nothing (stop, and starting or stopping a recording) takes no body, or `{}`.
- **Input is checked before anything runs:** unknown fields, wrong types and oversized values are refused with a message saying where.

## Errors
Every failed request answers with the same shape, and an HTTP status that matches its `code`:
```json
{ "error": { "code": "refused", "message": "sine.gain: 7 is outside 0-1" } }
```

| `code` | Status | Meaning |
|---|---|---|
| `invalid-request` | 400 | The request doesn't match the API; the message says where |
| `forbidden` | 403 | Refused by the ground rules above (`Host` or `Origin`) |
| `not-found` | 404 | No such path, or not with this method |
| `too-large` | 413 | The body is over 64 KiB |
| `unsupported-media-type` | 415 | A body that isn't `application/json` |
| `refused` | 422 | Well-formed, but the piano refused it, e.g. a setting out of range, or stopping a recording when none is running |
| `unavailable` | 503 | The audio service isn't running right now (it's starting, or restarting after a problem), or isn't keeping up with what it's sent |
| `timeout` | 504 | The audio service didn't answer in time |
| `internal` | 500 | Something went wrong inside piano-lab |

## Requests (HTTP)

### `GET /api/v0/status`
What's running.
```json
{
  "version": "0.1.0",
  "audioService": "running",
  "audio": {
    "audioType": "Windows Audio",
    "audioOutput": "Speakers",
    "sampleRate": 48000,
    "bufferSize": 480,
    "midiInputs": ["Digital Piano"],
    "recording": false,
    "lateBlocks": 0,
    "driverXruns": 0
  }
}
```
- `audioService` is `starting`, `running`, `restarting` (it stopped unexpectedly and is being started again) or `stopping`.
- `audio` is `null` unless the audio service is `running` and answers; `null` while it's `running` means it didn't answer in time. Its `audioType` and `audioOutput` are empty when no output is open.
- `lateBlocks` counts the blocks the piano took longer to render than they last, since the audio device opened: each is almost certainly an audible glitch. `driverXruns` is the audio driver's own count, left out when the driver doesn't keep one.

### `POST /api/v0/stop`
Stops piano-lab: the audio service saves its settings and stops, then the web server. Answers `202` with `{}` before stopping.

### `GET /api/v0/devices`
The audio outputs, by kind of device, and the MIDI inputs, as the system names them.
```json
{
  "audio": [
    { "type": "Windows Audio", "outputs": ["Speakers"] },
    { "type": "ASIO", "outputs": ["Focusrite USB ASIO"] }
  ],
  "midi": ["Digital Piano"]
}
```
To choose devices, change the `audio.*` and `midi.*` settings.

### Settings
Settings are generic: every one has a dotted name (`sine.gain`, `audio.bufferSize`), and the same names are used in `settings.toml`, in this API and in the web UI. A setting added in a later version of piano-lab appears here with no change to the API.

A value is a number, a text (at most 256 characters) or a list of texts (at most 64). Names are dotted words of at most 64 characters, and one message holds at most 256 settings.

#### `GET /api/v0/settings/schema`
Every setting's description, for drawing controls:
```json
[
  {
    "name": "sine.gain",
    "kind": "number",
    "min": 0,
    "max": 1,
    "unit": "",
    "description": "Peak amplitude of one key at full velocity",
    "defaultValue": 0.25,
    "integer": false,
    "restartsDevices": false
  },
  {
    "name": "midi.inputs",
    "kind": "textList",
    "unit": "",
    "description": "MIDI inputs to open, as the system names them; empty for all of them",
    "defaultValue": [],
    "integer": false,
    "restartsDevices": true
  }
]
```
- `kind` is `number`, `text` or `textList` (a list of texts).
- `min` and `max` are given for numbers only; `integer` says whole numbers only.
- `restartsDevices`: changing it reopens the audio or MIDI devices, with a short dropout.

#### `GET /api/v0/settings`
Every setting's current value, by name: `{"audio.bufferSize": 0, "sine.gain": 0.25, "midi.inputs": [], ...}`.

#### `POST /api/v0/settings`
Changes some settings: the body names only the ones to change.
```powershell
curl.exe -X POST http://localhost:4440/api/v0/settings -H "Content-Type: application/json" -d '{\"sine.gain\": 0.4}'
```
- **All or nothing:** if any name is unknown or any value out of range, nothing changes, and the reply is a 422 `refused` with the piano's own message.
- On success, the reply is every setting's value after the change, and every WebSocket client gets a `settings` event.
- Changes apply live: a ringing note keeps sounding, a tuning change bends it, and a gain change is heard from the next note. Device settings reopen the devices.
- They're saved to `settings.toml` about a second after the last change.

### Recording
- **`POST /api/v0/recording/start`** starts recording the output to a new WAV file in the `recordings` folder: `{"file": ".../piano-lab 2026-11-27 15-04-32.wav"}`.
- **`POST /api/v0/recording/stop`** stops it and finishes the file: `{"file": "...", "seconds": 12.5}`.

## Live traffic (WebSocket)
Connect to `ws://localhost:4440/api/v0/live`. There are no requests and replies: clients send commands, and piano-lab sends events to every client, so a change made anywhere shows up everywhere. Every message is one JSON object, as text, with a `type`.

### Commands (client to piano-lab)
| `type` | Fields | |
|---|---|---|
| `note.on` | `key` (MIDI note number, 0–127; the piano's keys are 21–108), `velocity` (0–1) | A key pressed, e.g. on screen |
| `note.off` | `key` | A key released |
| `settings.set` | `values` (settings by name, as for `POST /api/v0/settings`) | Change settings live |

```json
{ "type": "note.on", "key": 60, "velocity": 0.8 }
```
On one connection, commands are carried out in the order they're sent. If the audio service falls far behind (about 1 MiB of commands waiting), new ones are dropped, each answered with an `error` event, until it catches up.

### Events (piano-lab to clients)
| `type` | Fields | When |
|---|---|---|
| `service` | `state` (as in the status) | On connecting, and whenever it changes |
| `settings` | `values` (every setting) | On connecting, and after any change, from any client |
| `keys` | `keys` (MIDI note numbers held down, lowest first) | On connecting, and whenever they change, from any source: a MIDI keyboard or a client |
| `level` | `peak` (the loudest sample since the last one, 0 to 1; more when clipping) | About 30 times a second while anything sounds, then one `0` |
| `log` | `level` (`info`, `warning` or `error`), `message`, `at` (when it was logged, in milliseconds since 1970, UTC) | A line from piano-lab's log, e.g. a device that couldn't be opened. On connecting, the last 100 lines come first, so a client hears about problems from before it connected |
| `error` | `message` | To one client only: a command it sent was invalid, or refused by the piano |

A client that falls behind skips `level` events until it catches up; every other event is always sent.

## Trying it
With piano-lab running, from PowerShell:
```powershell
curl.exe http://localhost:4440/api/v0/status
```
```powershell
npx piano-lab status
```
For the WebSocket, any client works: for example, `npx wscat -c ws://localhost:4440/api/v0/live`, then type `{"type":"note.on","key":69,"velocity":0.8}`.
