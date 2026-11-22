#include <pianolab/engine_core/config.hpp>
#include <pianolab/engine_core/error.hpp>
#include <pianolab/engine_core/tuning.hpp>

#include <cmath>
#include <string_view>
#include <utility>
#include <vector>

namespace pianolab::engine_core {

Validated<TuningConfig> validate(const TuningConfig& config, std::string_view prefix) {
    std::vector<Error> errors;
    TuningConfig checked = checkFields(config, prefix, tuningConfigFields, errors);
    return {.config = detail::makeValid(checked), .errors = std::move(errors)};
}

double equalTemperament(int note, double a4Hz) noexcept {
    constexpr int a4Note = 69;
    constexpr double semitonesPerOctave = 12.0;
    return a4Hz * std::exp2(static_cast<double>(note - a4Note) / semitonesPerOctave);
}

} // namespace pianolab::engine_core
