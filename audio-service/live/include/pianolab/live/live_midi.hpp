#pragma once

#include <pianolab/engine_core/note_event.hpp>

#include <cstdint>
#include <span>
#include <variant>

/// The piano server: runs the engine live against audio and MIDI devices.
namespace pianolab::live {

/// A note event from a live input (a MIDI keyboard, later the UI), with the time it arrived in
/// seconds on the system's high-resolution clock. The audio thread applies events at the start
/// of the next block; the arrival time keeps them in order across inputs, and allows exact
/// timing later.
struct LiveEvent {
    engine_core::NoteEvent event;
    double arrivedSeconds = 0.0;
};

/// A MIDI message the piano doesn't use (clock, program change, pitch bend, other controllers,
/// system messages), or one too short to read. Ignored silently.
struct IgnoredMidi {};

/// A pedal message (sustain, sostenuto or soft): something the piano should do but can't yet.
/// Counted and reported, then skipped.
struct UnsupportedPedal {
    int controller = 0; ///< MIDI controller number: 64 sustain, 66 sostenuto, 67 soft
};

/// What one MIDI message means to the piano.
using MidiMeaning = std::variant<IgnoredMidi, engine_core::NoteEvent, UnsupportedPedal>;

/// Reads one complete MIDI message (status byte first). Every channel plays the piano. A note-on
/// becomes `NoteOn` with velocity `v / 127` (as in the MIDI file reader); a note-on with
/// velocity 0 and a note-off become `NoteOff`. Real-time safe and pure.
[[nodiscard]] MidiMeaning readMidiMessage(std::span<const std::uint8_t> bytes) noexcept;

} // namespace pianolab::live
