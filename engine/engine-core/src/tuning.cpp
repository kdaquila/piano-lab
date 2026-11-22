#include <pianolab/engine_core/tuning.hpp>

#include <cmath>

namespace pianolab::engine_core {

double equalTemperament(int note, double a4) noexcept {
    constexpr int a4Note = 69;
    constexpr double semitonesPerOctave = 12.0;
    return a4 * std::exp2(static_cast<double>(note - a4Note) / semitonesPerOctave);
}

} // namespace pianolab::engine_core
