#include <pianolab/engine_core/config.hpp>
#include <pianolab/engine_core/error.hpp>
#include <pianolab/engine_core/keyboard.hpp>
#include <pianolab/engine_core/render_config.hpp>
#include <pianolab/engine_core/tuning.hpp>
#include <pianolab/models/piano_key.hpp>
#include <pianolab/models/sine_key.hpp>

#include <iterator>
#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace pianolab::models {

static_assert(engine_core::KeyModel<SineKey>);
static_assert(engine_core::KeyModel<PianoKey>);

namespace {

/// Calls `visitor` with the model a key holds and returns its result.
///
/// Like std::visit, but it can never throw: std::visit throws if a variant is empty, which can
/// only happen when assigning a model throws, and ours never do. If it ever were empty, this
/// returns `fallback` instead, so the audio thread keeps going.
template <typename R, typename F, typename... Ts>
R dispatch(const std::variant<Ts...>& key, const F& visitor, R fallback) noexcept {
    R result = fallback;
    // Tries each model type in turn; exactly one matches.
    (void)((std::get_if<Ts>(&key) != nullptr && (result = visitor(*std::get_if<Ts>(&key)), true)) ||
           ...);
    return result;
}

} // namespace

PianoKey strike(const PianoKey& key, float velocity) noexcept {
    return dispatch<PianoKey>(
        key, [velocity](const auto& model) { return PianoKey{strike(model, velocity)}; }, key);
}

PianoKey release(const PianoKey& key) noexcept {
    return dispatch<PianoKey>(key, [](const auto& model) { return PianoKey{release(model)}; }, key);
}

PianoKey renderAdd(const PianoKey& key, std::span<float> out) noexcept {
    return dispatch<PianoKey>(
        key, [out](const auto& model) { return PianoKey{renderAdd(model, out)}; }, key);
}

bool isSilent(const PianoKey& key) noexcept {
    return dispatch<bool>(key, [](const auto& model) { return isSilent(model); }, true);
}

engine_core::Validated<PianoConfig> validate(const PianoConfig& config) {
    auto render = engine_core::validate(config.render, "render.");
    auto tuning = engine_core::validate(config.tuning, "tuning.");
    auto sine = validate(config.sine, "sine.");

    std::vector<engine_core::Error> errors;
    for (auto* part : {&render.errors, &tuning.errors, &sine.errors}) {
        errors.insert(errors.end(), std::make_move_iterator(part->begin()),
                      std::make_move_iterator(part->end()));
    }
    const PianoConfig checked{
        .render = *render.config, .tuning = *tuning.config, .sine = *sine.config};
    return {.config = engine_core::detail::makeValid(checked), .errors = std::move(errors)};
}

PianoKeyboard makePianoKeyboard(const engine_core::Valid<PianoConfig>& config) {
    const auto render = engine_core::validPart(config, &PianoConfig::render);
    const auto sine = engine_core::validPart(config, &PianoConfig::sine);
    const double a4Hz = config->tuning.a4Hz;
    const double sampleRate = render->sampleRate;
    return engine_core::makeKeyboard<PianoKey>(render, [&](int note) -> PianoKey {
        return makeSineKey(engine_core::equalTemperament(note, a4Hz), sine, sampleRate);
    });
}

} // namespace pianolab::models
