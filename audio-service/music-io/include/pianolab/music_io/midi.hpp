#pragma once

#include <pianolab/engine_core/error.hpp>
#include <pianolab/music_io/reading.hpp>

#include <cstddef>
#include <expected>
#include <span>

namespace pianolab::music_io {

/// Reads a Standard MIDI file (types 0 and 1) into a performance, timed by the file's tempo map.
///
/// Every channel plays the piano, and velocities map linearly (`velocity / 127`). Pedals
/// (controllers 64, 66 and 67) aren't supported yet: they're skipped with one warning. Other
/// controllers, program changes and pitch bend are ignored silently. Returns an `Error` if
/// `data` isn't a MIDI file.
[[nodiscard]] std::expected<Reading, engine_core::Error> readMidi(std::span<const std::byte> data);

} // namespace pianolab::music_io
