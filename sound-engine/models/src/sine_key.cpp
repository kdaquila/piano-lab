#include <pianolab/engine_core/config.hpp>
#include <pianolab/engine_core/error.hpp>
#include <pianolab/models/sine_key.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace pianolab::models {

engine_core::Validated<SineConfig> validate(const SineConfig& config, std::string_view prefix) {
    std::vector<engine_core::Error> errors;
    SineConfig checked = engine_core::checkFields(config, prefix, sineConfigFields, errors);
    return {.config = engine_core::detail::makeValid(checked), .errors = std::move(errors)};
}

double t60Coeff(double t60Seconds, double sampleRate) noexcept {
    // An exponential glide closes a fraction c of the remaining gap each sample, so after n
    // samples the gap is (1 - c)^n. A 60 dB drop is a factor of 1000: (1 - c)^n = 1/1000, so
    // c = 1 - exp(-ln(1000) / n).
    constexpr double sixtyDecibels = 1000.0;
    const double samples = std::max(t60Seconds * sampleRate, 1.0);
    return 1.0 - std::exp(-std::log(sixtyDecibels) / samples);
}

SineKey makeSineKey(double frequency, const engine_core::Valid<SineConfig>& config,
                    double sampleRate) noexcept {
    return {
        .coeffs =
            {
                .phaseStep = frequency / sampleRate,
                .gain = config->gain,
                .attackCoeff = t60Coeff(config->attackT60, sampleRate),
                .releaseCoeff = t60Coeff(config->releaseT60, sampleRate),
                .silenceThreshold = config->silenceThreshold,
            },
        .state = {},
    };
}

SineKey strike(SineKey key, float velocity) noexcept {
    key.state.target = static_cast<double>(velocity) * key.coeffs.gain;
    key.state.coeff = key.coeffs.attackCoeff;
    key.state.released = false;
    return key;
}

SineKey release(SineKey key) noexcept {
    key.state.target = 0.0;
    key.state.coeff = key.coeffs.releaseCoeff;
    key.state.released = true;
    return key;
}

SineKey renderAdd(SineKey key, std::span<float> out) noexcept {
    // Mutation on purpose: this is the per-sample hot loop, so it updates a local copy of the
    // state in place and returns it at the end.
    SineState& state = key.state;
    for (float& sample : out) {
        sample +=
            static_cast<float>(state.amplitude * std::sin(2.0 * std::numbers::pi * state.phase));
        state.phase += key.coeffs.phaseStep;
        if (state.phase >= 1.0) {
            state.phase -= 1.0;
        }
        state.amplitude += (state.target - state.amplitude) * state.coeff;
    }
    if (state.released && state.amplitude < key.coeffs.silenceThreshold) {
        state.amplitude = 0.0;
    }
    return key;
}

bool isSilent(const SineKey& key) noexcept {
    return key.state.released && key.state.amplitude < key.coeffs.silenceThreshold;
}

} // namespace pianolab::models
