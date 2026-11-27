# Rendering music to WAV

`piano-lab render` plays a music file through the piano engine and writes the sound to a WAV file, with no audio device. It's how AI agents (and tests) get the sound the piano makes, so they can measure it, and how you can hear an agent's test piece.

## Running it
`npm run render` builds the dev preset, then runs it:
```powershell
npm run render -- <input> [-o <output.wav>] [--sample-rate <hz>] [--settings <file>]
```
Paths are relative to the folder npm was run from. Once built, `.\build\dev\audio-service\app\piano-lab.exe render` takes the same arguments.
- **Input:** a MIDI file (`.mid`, `.midi`) or a MusicXML score (`.musicxml`, `.xml`, or compressed `.mxl`).
- **Output:** a 32-bit float, mono WAV. By default it's the input's path with `.wav`, at 48 kHz.
- **Settings:** the piano sounds as it does live. It uses the piano sections (`[tuning]`, `[sine]`, `[render]`) of `settings.toml` next to `piano-lab.exe`, if there is one, or of the file given with `--settings` instead; the summary line says which. To try a variation without touching the owner's settings, write a small file holding only what changes (e.g. `[sine]` `releaseT60 = 1.0`) and pass it with `--settings`. A `--settings` file that's missing or isn't valid TOML is an error; problems inside it are warnings. The sample rate is always `--sample-rate` (48 kHz by default), never the settings file.
- **Length:** the render runs until the piano falls silent after the last note, so release tails are included. It stops 30 s after the last event at most, with a warning (usually a note that's never released).
- **Reports:** everything skipped or fixed goes to stderr, one `warning:` line each, and a summary goes to stdout. The exit code is **0 when a WAV was written** (even with warnings) and **1 when nothing could be written** (bad arguments, an unreadable input or `--settings` file, an unwritable output, an output that names the input, or a render too long for one WAV file: about 6 hours at 48 kHz). A failed write never leaves a half-written file behind.
- **Deterministic:** the same input and settings always give a byte-identical file, so renders can be compared exactly.
- **Any file name works**, including non-Latin ones. Events later than two hours into a piece are treated as a broken file: dropped, with a warning.

WAV files are git-ignored; don't commit renders.

## What gets played
Each file is turned into the engine's `Performance`: a list of key presses and releases with times in seconds. The conversion keeps what changes the sound, and drops the rest:
- **Played:** notes, rests, chords, ties, multiple voices and staves, several parts (all on the one piano), tempo and dynamics.
- **Dropped silently:** visual-only notation (layout, beams, stems, slurs, fingering, lyrics).
- **Dropped with a warning:** notation that changes the sound but isn't supported yet: repeats and jumps (played straight through), grace notes, fermatas, ornaments, articulations, arpeggios, glissandos, hairpins, accent dynamics (sf, fp) and pedals. One warning per kind, saying where it first appears.

**MIDI:** every channel plays the piano, and velocity maps to `velocity / 127`. Pedals are reported and skipped.

**MusicXML dynamics:** a note's own `dynamics` attribute wins; otherwise the latest `<sound dynamics="…">` or marking (`p`, `mf`, …) in that part. With no dynamics at all, notes play at mf. Tempo comes from `<sound tempo="…">` (quarter notes per minute) or a metronome mark, and is 120 by default.

## Writing a test piece (for agents)
Write short MusicXML: it's plain text, and the owner can open it in MuseScore to look at and play. A minimal score, two quarter notes then a chord, at ♩ = 90 and mf:
```xml
<?xml version="1.0" encoding="UTF-8"?>
<score-partwise version="4.0">
  <part-list><score-part id="P1"><part-name>Piano</part-name></score-part></part-list>
  <part id="P1">
    <measure number="1">
      <attributes><divisions>1</divisions></attributes>
      <direction>
        <direction-type><dynamics><mf/></dynamics></direction-type>
        <sound tempo="90"/>
      </direction>
      <note><pitch><step>C</step><octave>4</octave></pitch><duration>1</duration></note>
      <note><pitch><step>E</step><octave>4</octave></pitch><duration>1</duration></note>
      <note><pitch><step>G</step><octave>4</octave></pitch><duration>2</duration></note>
      <note><chord/><pitch><step>C</step><octave>5</octave></pitch><duration>2</duration></note>
    </measure>
  </part>
</score-partwise>
```
- `<divisions>` is how many duration units make a quarter note; `<duration>` counts them. With divisions 4, a sixteenth note is duration 1.
- Sharps and flats: `<alter>1</alter>` or `<alter>-1</alter>` inside `<pitch>`, after `<step>`.
- Exact loudness: `<note dynamics="80">` is 80% of forte (MIDI velocity 90 × 0.8 = 72).
- For sample-exact timing ("release after exactly 0.137 s"), write a C++ test against the engine instead: notation is the wrong tool for that.

## Where it lives
- `sound-engine/engine-core`: the `Performance` type, its validation, and rendering until silent
- `audio-service/music-io`: the MIDI and MusicXML readers and the WAV writer
- `audio-service/settings`: reading `settings.toml`
- `audio-service/render`: the rendering pipeline; `audio-service/app`: the `piano-lab render` command

The decisions behind it are in [the plan, step 0.3](plan/milestone-0.md#03-offline-render-to-wav).
