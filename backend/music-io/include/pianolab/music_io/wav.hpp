#pragma once

#include <pianolab/engine_core/error.hpp>

#include <cstddef>
#include <expected>
#include <span>
#include <vector>

namespace pianolab::music_io {

/// The most samples one mono 32-bit float WAV file can hold. A WAV file's sizes are 32-bit
/// numbers, so the whole file must stay under 4 GiB; this leaves room for the header.
inline constexpr std::size_t maxWavSamples = ((std::size_t{1} << 32U) - 4096) / sizeof(float);

/// A mono WAV file of `samples` at `sampleRate` hertz, as 32-bit float: exactly the samples
/// given, with no rounding and no clipping (values beyond +-1 are kept). Deterministic: the same
/// samples always give the same bytes. Returns an `Error` if there are more than `maxWavSamples`
/// samples or the sample rate can't be written.
[[nodiscard]] std::expected<std::vector<std::byte>, engine_core::Error>
encodeWav(std::span<const float> samples, double sampleRate);

} // namespace pianolab::music_io
