#pragma once

#include <span>

/// The sine key: the simplest possible key model, one sine per key. A sine is a string with a
/// single mode, so this is the real engine at its simplest.
namespace pianolab::models {

/// Settings shared by every sine key.
struct SineParams {
    /// Peak amplitude at full velocity. Kept well below 1.0 so several keys can sound together
    /// without clipping.
    double gain = 0.25;
    /// How fast a strike reaches its new level: the time to close 60 dB of the gap. Short enough
    /// to sound instant, long enough to avoid a click.
    double attackT60 = 0.005;
    /// How long the sound takes to fall by 60 dB after release (the damper).
    double releaseT60 = 0.25;
};

/// Values derived once from the settings, the key's pitch and the sample rate.
struct SineCoeffs {
    double phaseStep = 0.0; ///< cycles per sample
    double gain = 0.0;
    double attackCoeff = 0.0;
    double releaseCoeff = 0.0;
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

/// Below this amplitude (-100 dB) a released key counts as silent.
inline constexpr double silenceThreshold = 1e-5;

/// The per-sample smoothing coefficient that closes 60 dB of the gap to a target in `t60Seconds`.
[[nodiscard]] double t60Coeff(double t60Seconds, double sampleRate) noexcept;

/// A silent sine key, prepared for its pitch and the sample rate.
[[nodiscard]] SineKey makeSineKey(double frequency, const SineParams& params, double sampleRate);

/// The key is pressed: glide quickly toward the level for this velocity. The phase carries on,
/// so re-striking a ringing key changes its level smoothly instead of clicking.
[[nodiscard]] SineKey strike(SineKey key, float velocity) noexcept;

/// The key is released: glide slowly toward silence.
[[nodiscard]] SineKey release(SineKey key) noexcept;

/// Adds the key's sound into `out` and returns the key's new state.
[[nodiscard]] SineKey renderAdd(SineKey key, std::span<float> out) noexcept;

/// True when the key is released and quiet enough to skip rendering.
[[nodiscard]] bool isSilent(const SineKey& key) noexcept;

} // namespace pianolab::models
