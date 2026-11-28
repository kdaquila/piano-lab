#pragma once

#include <cstddef>
#include <variant>

namespace pianolab::engine_core {

/// A key is pressed. `note` is a MIDI note number (A0 = 21 ... C8 = 108); `velocity` is
/// normalised to 0.0-1.0.
struct NoteOn {
    int note = 0;
    float velocity = 0.0F;
};

/// A key is released.
struct NoteOff {
    int note = 0;
};

/// Something a player does to the keyboard.
using NoteEvent = std::variant<NoteOn, NoteOff>;

/// A note event stamped with the sample it happens at.
///
/// What `at` counts from depends on where the event is used: within `render` it is the offset
/// into the current block; within `renderOffline` it is the absolute sample from the start.
struct TimedEvent {
    std::size_t at = 0;
    NoteEvent event;
};

/// A key press of MIDI `note` with `velocity` (0-1), happening at sample `at`.
[[nodiscard]] constexpr TimedEvent noteOn(std::size_t at, int note, float velocity) noexcept {
    return {.at = at, .event = NoteOn{.note = note, .velocity = velocity}};
}

/// A key release of MIDI `note`, happening at sample `at`.
[[nodiscard]] constexpr TimedEvent noteOff(std::size_t at, int note) noexcept {
    return {.at = at, .event = NoteOff{.note = note}};
}

} // namespace pianolab::engine_core
