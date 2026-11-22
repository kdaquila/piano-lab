#include <pianolab/engine_core/keyboard.hpp>
#include <pianolab/engine_core/tuning.hpp>
#include <pianolab/models/piano_key.hpp>
#include <pianolab/models/sine_key.hpp>

#include <cstddef>
#include <span>
#include <stdexcept>
#include <variant>

namespace pianolab::models {

static_assert(engine_core::KeyModel<SineKey>);
static_assert(engine_core::KeyModel<PianoKey>);

namespace {

/// Calls `visitor` with the model a key holds and returns its result.
///
/// Like std::visit, but it can never throw: std::visit throws if a variant is empty, which can
/// only happen when assigning a model throws, and ours never do. If it ever were empty, this
/// returns `fallback` instead, so the audio thread keeps going.
template <typename Result, typename Visitor, typename... Models>
Result dispatch(const std::variant<Models...>& key, const Visitor& visitor,
                Result fallback) noexcept {
    Result result = fallback;
    // Tries each model type in turn; exactly one matches.
    (void)((std::get_if<Models>(&key) != nullptr &&
            (result = visitor(*std::get_if<Models>(&key)), true)) ||
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

PianoKeyboard makeSineKeyboard(double sampleRate, std::size_t maxBlock, const SineParams& params) {
    if (!(sampleRate > 0.0)) {
        throw std::invalid_argument("makeSineKeyboard: sampleRate must be positive");
    }
    return engine_core::makeKeyboard<PianoKey>(maxBlock, [&](int note) -> PianoKey {
        return makeSineKey(engine_core::equalTemperament(note), params, sampleRate);
    });
}

} // namespace pianolab::models
