#pragma once

#include <pianolab/engine_core/keyboard.hpp>
#include <pianolab/models/sine_key.hpp>

#include <cstddef>
#include <span>
#include <variant>

/// The set of key models the engine can play, as one type the keyboard holds.
namespace pianolab::models {

/// Any key model. A new model is added here, and the compiler then checks every place that
/// handles keys knows about it.
using PianoKey = std::variant<SineKey>;

// The key-model functions for PianoKey: each forwards to the model it currently holds.

[[nodiscard]] PianoKey strike(const PianoKey& key, float velocity) noexcept;
[[nodiscard]] PianoKey release(const PianoKey& key) noexcept;
[[nodiscard]] PianoKey renderAdd(const PianoKey& key, std::span<float> out) noexcept;
[[nodiscard]] bool isSilent(const PianoKey& key) noexcept;

/// The keyboard the backend plays.
using PianoKeyboard = engine_core::Keyboard<PianoKey>;

/// A keyboard of sine keys in equal temperament. Setup code: call before audio starts; throws on
/// bad settings.
[[nodiscard]] PianoKeyboard makeSineKeyboard(double sampleRate, std::size_t maxBlock,
                                             const SineParams& params = {});

} // namespace pianolab::models
