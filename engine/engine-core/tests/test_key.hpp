#pragma once

#include <span>

/// Test helpers for engine-core.
namespace pianolab::engine_core::testing {

/// A trivial key model for testing the keyboard on its own: outputs a constant `value` while
/// held, and nothing otherwise.
struct ConstantKey {
    bool held = false;
    float value = 1.0F;
};

/// Marks the key as held; the velocity is ignored.
[[nodiscard]] inline ConstantKey strike(ConstantKey key, float /*velocity*/) noexcept {
    key.held = true;
    return key;
}

/// Marks the key as not held.
[[nodiscard]] inline ConstantKey release(ConstantKey key) noexcept {
    key.held = false;
    return key;
}

/// Adds the key's constant value to every sample in `out`.
[[nodiscard]] inline ConstantKey renderAdd(ConstantKey key, std::span<float> out) noexcept {
    for (float& sample : out) {
        sample += key.value;
    }
    return key;
}

/// True when the key isn't held.
[[nodiscard]] inline bool isSilent(const ConstantKey& key) noexcept {
    return !key.held;
}

} // namespace pianolab::engine_core::testing
