#pragma once

#include <pianolab/engine_core/config.hpp>

#include <cstddef>
#include <tuple>

namespace pianolab::engine_core {

/// How the engine renders audio. The defaults are the built-in settings.
struct RenderConfig {
    /// Samples per second of the output.
    double sampleRate = 48'000.0;
    /// The longest stretch of samples handed to a key model at once. Longer blocks are split.
    std::size_t maxBlock = 512;
};

/// Every `RenderConfig` setting, declared once: name, range, unit and description.
inline constexpr std::tuple renderConfigFields{
    Field<RenderConfig, double>{
        .name = "sampleRate",
        .member = &RenderConfig::sampleRate,
        .min = 8'000.0,
        .max = 384'000.0,
        .unit = "Hz",
        .description = "Samples per second of the audio output",
    },
    Field<RenderConfig, std::size_t>{
        .name = "maxBlock",
        .member = &RenderConfig::maxBlock,
        .min = 1,
        .max = 8'192,
        .unit = "samples",
        .description = "Longest stretch of samples rendered at once; longer blocks are split",
    },
};

/// Validates a render configuration. Always returns a usable one: out-of-range values are
/// replaced with the built-in defaults, each reported as an `Error` prefixed with `prefix`
/// (e.g. "render.").
[[nodiscard]] Validated<RenderConfig> validate(const RenderConfig& config,
                                               std::string_view prefix = "");

} // namespace pianolab::engine_core
