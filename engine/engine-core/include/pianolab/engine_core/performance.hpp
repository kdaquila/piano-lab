#pragma once

#include <pianolab/engine_core/config.hpp>
#include <pianolab/engine_core/note_event.hpp>
#include <pianolab/engine_core/render_config.hpp>

#include <vector>

/// What a pianist plays, independent of any sample rate or file format. The backend's file
/// readers (MIDI, MusicXML) turn files into a `Performance`; the engine turns it into
/// sample-exact events.
namespace pianolab::engine_core {

/// A note event at a time in seconds from the start of the performance.
struct PerformanceEvent {
    double seconds = 0.0;
    NoteEvent event;
};

/// What the pianist's hands do, as a flat list of timed note events. A starting point, expected
/// to grow (the sustain pedal is the obvious next event).
///
/// The events may be in any order; validation sorts them.
struct Performance {
    std::vector<PerformanceEvent> events;
};

/// The latest time an event may happen at, in seconds (six hours). Anything later is a broken
/// file, not music.
inline constexpr double maxPerformanceSeconds = 6.0 * 60.0 * 60.0;

/// Validates a performance. Always returns a playable one, sorted by time with releases before
/// presses at the same instant (so a repeated note is re-struck, not cut off). Events with a
/// NaN, negative or too-late time, presses off the keyboard and presses with a NaN velocity are
/// dropped; velocities outside 0-1 are clamped. Each fix is reported as an `Error`. Releases
/// of notes off the keyboard are dropped silently, since their press was already reported.
[[nodiscard]] Validated<Performance> validate(const Performance& performance);

/// The time of the last event in seconds, or 0 for an empty performance.
[[nodiscard]] double lastEventSeconds(const Valid<Performance>& performance) noexcept;

/// The performance as events stamped with absolute sample positions at the configured sample
/// rate, rounded to the nearest sample and in time order. Ready for `renderOffline` or
/// `renderUntilSilent`.
[[nodiscard]] std::vector<TimedEvent> toTimedEvents(const Valid<Performance>& performance,
                                                    const Valid<RenderConfig>& render);

} // namespace pianolab::engine_core
