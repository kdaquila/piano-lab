#pragma once

#include <pianolab/engine_core/config.hpp>
#include <pianolab/engine_core/keyboard.hpp>
#include <pianolab/engine_core/render_config.hpp>
#include <pianolab/engine_core/tuning.hpp>
#include <pianolab/models/sine_key.hpp>

#include <cstddef>
#include <span>
#include <variant>

/// The set of key models the engine can play, as one type the keyboard holds.
namespace pianolab::models {

/// Any key model. A new model is added here, and the compiler then checks every place that
/// handles keys knows about it.
using PianoKey = std::variant<SineKey>;

/// Strikes the model `key` holds with `velocity` (0-1) and returns the struck key.
[[nodiscard]] PianoKey strike(const PianoKey& key, float velocity) noexcept;

/// Releases the model `key` holds (the damper comes down) and returns the released key.
[[nodiscard]] PianoKey release(const PianoKey& key) noexcept;

/// Adds the sound of the model `key` holds into `out` and returns the key's new state.
/// Real-time safe: never allocates or throws.
[[nodiscard]] PianoKey renderAdd(const PianoKey& key, std::span<float> out) noexcept;

/// True when the model `key` holds is quiet enough to skip rendering.
[[nodiscard]] bool isSilent(const PianoKey& key) noexcept;

/// The keyboard the backend plays.
using PianoKeyboard = engine_core::Keyboard<PianoKey>;

/// The whole piano's configuration: everything needed to build a `PianoKeyboard`. The defaults
/// are the built-in settings; user settings are merged over them before validation (step 0.5).
struct PianoConfig {
    engine_core::RenderConfig render;
    engine_core::TuningConfig tuning;
    SineConfig sine;
};

/// Validates the whole piano configuration. Always returns a usable one: every out-of-range
/// value is replaced with its built-in default and reported as an `Error` naming the setting,
/// e.g. "sine.attackT60: ...".
[[nodiscard]] engine_core::Validated<PianoConfig> validate(const PianoConfig& config);

/// A keyboard of sine keys in equal temperament, built from a validated configuration. Setup
/// code: call before audio starts. Can't fail, because the configuration is already valid.
[[nodiscard]] PianoKeyboard makePianoKeyboard(const engine_core::Valid<PianoConfig>& config);

} // namespace pianolab::models
