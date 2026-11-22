#pragma once

#include <span>

// A trivial key model for testing the keyboard on its own: outputs a constant 1.0 while held.
namespace pianolab::engine_core::testing {

struct ConstantKey {
    bool held = false;
    float value = 1.0F;
};

[[nodiscard]] inline ConstantKey strike(ConstantKey key, float /*velocity*/) noexcept {
    key.held = true;
    return key;
}

[[nodiscard]] inline ConstantKey release(ConstantKey key) noexcept {
    key.held = false;
    return key;
}

[[nodiscard]] inline ConstantKey renderAdd(ConstantKey key, std::span<float> out) noexcept {
    for (float& sample : out) {
        sample += key.value;
    }
    return key;
}

[[nodiscard]] inline bool isSilent(const ConstantKey& key) noexcept {
    return !key.held;
}

} // namespace pianolab::engine_core::testing
