#include <pianolab/engine_core/keyboard.hpp>

#include <cmath>
#include <cstddef>
#include <span>

namespace pianolab::engine_core {

std::size_t replaceNonFinite(std::span<float> out, Diagnostics& diagnostics) noexcept {
    std::size_t fixed = 0;
    for (float& sample : out) {
        if (!std::isfinite(sample)) {
            sample = 0.0F;
            ++fixed;
            record(diagnostics, Problem::NonFiniteSample);
        }
    }
    return fixed;
}

} // namespace pianolab::engine_core
