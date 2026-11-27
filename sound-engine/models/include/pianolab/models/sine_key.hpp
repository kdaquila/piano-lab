#pragma once

#include <pianolab/engine_core/config.hpp>

#include <span>
#include <string_view>
#include <tuple>

/// The sine key: the simplest possible key model, one sine per key. A sine is a string with a
/// single mode, so this is the real engine at its simplest.
namespace pianolab::models {

/// Settings shared by every sine key. The defaults are the built-in settings.
struct SineConfig {
    /// Peak amplitude at full velocity. Kept well below 1.0 so several keys can sound together
    /// without clipping.
    double gain = 0.25;
    /// How fast a strike reaches its new level: the time to close 60 dB of the gap, in seconds.
    /// Short enough to sound instant, long enough to avoid a click.
    double attackT60 = 0.005;
    /// How long the sound takes to fall by 60 dB after release (the damper), in seconds.
    double releaseT60 = 0.25;
    /// Below this amplitude a released key counts as silent and stops being rendered.
    /// 1e-5 is -100 dB.
    double silenceThreshold = 1e-5;
};

/// Every `SineConfig` setting, declared once: name, range, unit and description.
inline constexpr std::tuple sineConfigFields{
    engine_core::Field<SineConfig, double>{
        .name = "gain",
        .member = &SineConfig::gain,
        .min = 0.0,
        .max = 1.0,
        .unit = "",
        .description = "Peak amplitude of one key at full velocity",
    },
    engine_core::Field<SineConfig, double>{
        .name = "attackT60",
        .member = &SineConfig::attackT60,
        .min = 0.0005,
        .max = 1.0,
        .unit = "s",
        .description = "Time for a strike to settle within 60 dB of its level",
    },
    engine_core::Field<SineConfig, double>{
        .name = "releaseT60",
        .member = &SineConfig::releaseT60,
        .min = 0.01,
        .max = 30.0,
        .unit = "s",
        .description = "Time for the sound to fall by 60 dB after the key is released",
    },
    engine_core::Field<SineConfig, double>{
        .name = "silenceThreshold",
        .member = &SineConfig::silenceThreshold,
        .min = 1e-9,
        .max = 1e-2,
        .unit = "",
        .description = "Amplitude below which a released key stops being rendered",
    },
};

/// Validates a sine configuration. Always returns a usable one: out-of-range values are
/// replaced with the built-in defaults, each reported as an `Error` prefixed with `prefix`.
[[nodiscard]] engine_core::Validated<SineConfig> validate(const SineConfig& config,
                                                          std::string_view prefix = "");

/// Values derived once from the configuration, the key's pitch and the sample rate.
struct SineCoeffs {
    double phaseStep = 0.0; ///< cycles per sample
    double gain = 0.0;
    double attackCoeff = 0.0;  ///< per-sample glide coefficient while striking
    double releaseCoeff = 0.0; ///< per-sample glide coefficient after release
    double silenceThreshold = 0.0;
};

/// The running state. All double precision, for accuracy over long notes.
struct SineState {
    double phase = 0.0;     ///< position in the cycle, 0-1
    double amplitude = 0.0; ///< current level
    double target = 0.0;    ///< the level the amplitude glides toward
    double coeff = 0.0;     ///< the current glide speed (attack or release)
    bool released = true;
};

/// One key of the sine model: its fixed coefficients and its running state.
struct SineKey {
    SineCoeffs coeffs;
    SineState state;
};

/// The per-sample smoothing coefficient that closes 60 dB of the gap to a target in
/// `t60Seconds`, at `sampleRate` samples per second.
[[nodiscard]] double t60Coeff(double t60Seconds, double sampleRate) noexcept;

/// A silent sine key at `frequency` hertz, prepared for `sampleRate`.
[[nodiscard]] SineKey makeSineKey(double frequency, const engine_core::Valid<SineConfig>& config,
                                  double sampleRate) noexcept;

/// The key is pressed: glide quickly toward the level for this velocity. The phase carries on,
/// so re-striking a ringing key changes its level smoothly instead of clicking.
[[nodiscard]] SineKey strike(SineKey key, float velocity) noexcept;

/// The key is released: glide slowly toward silence.
[[nodiscard]] SineKey release(SineKey key) noexcept;

/// Adds the key's sound into `out` and returns the key's new state.
[[nodiscard]] SineKey renderAdd(SineKey key, std::span<float> out) noexcept;

/// `live` with the settings of `fresh` (the same key, built from new settings), keeping its
/// sound: the phase and level carry on, so a ringing key changes pitch or speed without a
/// click. The glide in progress takes the new attack or release speed; the level it glides
/// toward stays, so a gain change is heard from the next strike.
[[nodiscard]] SineKey adopt(SineKey live, const SineKey& fresh) noexcept;

/// True when the key is released and quieter than its silence threshold.
[[nodiscard]] bool isSilent(const SineKey& key) noexcept;

} // namespace pianolab::models
