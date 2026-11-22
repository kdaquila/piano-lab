#pragma once

namespace pianolab::engine_core {

/// Standard concert pitch for A4 (MIDI note 69), in hertz.
inline constexpr double a4Hz = 440.0;

/// The frequency of a MIDI note in 12-tone equal temperament, given the pitch of A4.
///
/// Real pianos are stretch-tuned; that belongs to calibration later. This is the plain textbook
/// starting point: f = a4 * 2^((note - 69) / 12).
[[nodiscard]] double equalTemperament(int note, double a4 = a4Hz) noexcept;

} // namespace pianolab::engine_core
