#pragma once

#include <pianolab/engine_core/error.hpp>

#include <cstddef>
#include <expected>
#include <span>
#include <vector>

namespace pianolab::music_io {

/// A mono WAV file of `samples` at `sampleRate` hertz, as 32-bit float: exactly the samples
/// given, with no rounding and no clipping (values beyond +-1 are kept). Deterministic: the same
/// samples always give the same bytes. Returns an `Error` if the sample rate can't be written.
[[nodiscard]] std::expected<std::vector<std::byte>, engine_core::Error>
encodeWav(std::span<const float> samples, double sampleRate);

} // namespace pianolab::music_io
