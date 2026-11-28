#pragma once

#include <pianolab/engine_core/config.hpp>

#include <string_view>
#include <tuple>

namespace pianolab::engine_core {

/// How the keyboard is tuned. The defaults are the built-in settings.
struct TuningConfig {
    /// The pitch of A4 (MIDI note 69) in hertz. 440 Hz is standard concert pitch.
    double a4Hz = 440.0;
};

/// Every `TuningConfig` setting, declared once: name, range, unit and description.
inline constexpr std::tuple tuningConfigFields{
    Field<TuningConfig, double>{
        .name = "a4Hz",
        .member = &TuningConfig::a4Hz,
        .min = 380.0,
        .max = 500.0,
        .unit = "Hz",
        .description = "Pitch of A4; 440 Hz is standard concert pitch",
    },
};

/// Validates a tuning configuration. Always returns a usable one: out-of-range values are
/// replaced with the built-in defaults, each reported as an `Error` prefixed with `prefix`.
[[nodiscard]] Validated<TuningConfig> validate(const TuningConfig& config,
                                               std::string_view prefix = "");

/// The frequency of a MIDI note in 12-tone equal temperament, given the pitch of A4 in hertz.
///
/// Real pianos are stretch-tuned; that belongs to calibration later. This is the plain textbook
/// starting point: f = a4 * 2^((note - 69) / 12).
[[nodiscard]] double equalTemperament(int note, double a4Hz) noexcept;

} // namespace pianolab::engine_core
