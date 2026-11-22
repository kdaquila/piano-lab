// Headless acceptance tests for the sine key model (Milestone 0, step 0.2), ported from the Rust
// prototype. Every test renders through the real keyboard and also checks that the engine had
// nothing to recover from.

#include <pianolab/engine_core/diagnostics.hpp>
#include <pianolab/engine_core/keyboard.hpp>
#include <pianolab/engine_core/note_event.hpp>
#include <pianolab/engine_core/render_offline.hpp>
#include <pianolab/engine_core/tuning.hpp>
#include <pianolab/models/piano_key.hpp>
#include <pianolab/models/sine_key.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <span>
#include <stdexcept>
#include <variant>
#include <vector>

namespace {

namespace ec = pianolab::engine_core;
namespace models = pianolab::models;
using Catch::Matchers::WithinAbs;

constexpr double sampleRate = 48'000.0;
constexpr std::size_t blockSize = 256;
constexpr int a4 = 69;

models::PianoKeyboard makeKeyboard() {
    return models::makeSineKeyboard(sampleRate, blockSize);
}

std::size_t seconds(double s) {
    return static_cast<std::size_t>(std::lround(s * sampleRate));
}

float peak(std::span<const float> samples) {
    float result = 0.0F;
    for (const float sample : samples) {
        result = std::max(result, std::abs(sample));
    }
    return result;
}

double decibels(float amplitude) {
    return 20.0 * std::log10(static_cast<double>(amplitude));
}

/// Frequency from the spacing of rising zero crossings, interpolated between samples for
/// sub-sample accuracy.
double measureFrequency(std::span<const float> samples) {
    std::vector<double> crossings;
    for (std::size_t i = 0; i + 1 < samples.size(); ++i) {
        const double a = samples[i];
        const double b = samples[i + 1];
        if (a < 0.0 && b >= 0.0) {
            crossings.push_back(static_cast<double>(i) + a / (a - b));
        }
    }
    REQUIRE(crossings.size() >= 2);
    const auto periods = static_cast<double>(crossings.size() - 1);
    return periods * sampleRate / (crossings.back() - crossings.front());
}

const models::SineKey& sineKey(const models::PianoKeyboard& keyboard, int note) {
    const auto index = ec::keyIndex(note);
    if (!index) {
        throw std::out_of_range("sineKey: note is off the keyboard");
    }
    return std::get<models::SineKey>(keyboard.keys.at(*index));
}

} // namespace

TEST_CASE("A4 plays at 440 Hz") {
    auto keyboard = makeKeyboard();
    const std::array events{ec::noteOn(0, a4, 1.0F)};
    const auto out = ec::renderOffline(keyboard, events, seconds(1.0));

    // Skip the first 10 ms so the attack has settled.
    const double hz = measureFrequency(std::span(out).subspan(seconds(0.01)));
    CHECK_THAT(hz, WithinAbs(440.0, 0.1));
    CHECK(ec::total(keyboard.diagnostics) == 0);
}

TEST_CASE("every key plays its own pitch") {
    // A0 (27.5 Hz), middle C and C8 (4186 Hz): the extremes and the middle.
    for (const int note : {21, 60, 108}) {
        CAPTURE(note);
        const double expected = ec::equalTemperament(note);
        auto keyboard = makeKeyboard();
        const std::array events{ec::noteOn(0, note, 1.0F)};
        const auto out = ec::renderOffline(keyboard, events, seconds(1.0));

        const double hz = measureFrequency(std::span(out).subspan(seconds(0.05)));
        CHECK_THAT(hz, WithinAbs(expected, expected * 1e-4));
        CHECK(ec::total(keyboard.diagnostics) == 0);
    }
}

TEST_CASE("output is neither silent nor clipping") {
    auto keyboard = makeKeyboard();
    const std::array events{ec::noteOn(0, a4, 1.0F)};
    const auto out = ec::renderOffline(keyboard, events, seconds(0.5));

    const float level = peak(out);
    CHECK(level > 0.1F);
    CHECK(level < 1.0F);
    CHECK(ec::total(keyboard.diagnostics) == 0);
}

TEST_CASE("velocity scales loudness") {
    const auto loudness = [](float velocity) {
        auto keyboard = makeKeyboard();
        const std::array events{ec::noteOn(0, a4, velocity)};
        return peak(ec::renderOffline(keyboard, events, seconds(0.2)));
    };
    const float soft = loudness(0.25F);
    const float loud = loudness(1.0F);
    CHECK(soft > 0.0F);
    CHECK(soft < loud * 0.5F);
}

TEST_CASE("release decays to silence") {
    auto keyboard = makeKeyboard();
    const std::size_t releaseAt = seconds(0.5);
    const std::array events{ec::noteOn(0, a4, 1.0F), ec::noteOff(releaseAt, a4)};
    const auto out = ec::renderOffline(keyboard, events, seconds(1.5));

    // Release T60 is 0.25 s, so one second after release it's long gone.
    const auto tail = std::span(out).subspan(releaseAt + seconds(1.0) - blockSize);
    CHECK(decibels(peak(tail)) < -90.0);
    CHECK(models::isSilent(sineKey(keyboard, a4)));
    CHECK(ec::total(keyboard.diagnostics) == 0);
}

TEST_CASE("a note starts on its exact sample") {
    // Deliberately not on a block boundary.
    const std::size_t start = 1000;
    auto keyboard = makeKeyboard();
    const std::array events{ec::noteOn(start, a4, 1.0F)};
    const auto out = ec::renderOffline(keyboard, events, seconds(0.1));

    CHECK(std::ranges::all_of(std::span(out).first(start), [](float s) { return s == 0.0F; }));
    CHECK(out[start + 2] != 0.0F);
    CHECK(ec::total(keyboard.diagnostics) == 0);
}

TEST_CASE("re-striking a ringing key does not click") {
    auto keyboard = makeKeyboard();
    const std::size_t restrikeAt = seconds(0.3) + 123; // mid-block, arbitrary phase
    const std::array events{ec::noteOn(0, a4, 0.3F), ec::noteOn(restrikeAt, a4, 1.0F)};
    const auto out = ec::renderOffline(keyboard, events, seconds(0.6));

    // A smooth sine of amplitude A changes by at most 2*pi*f/SR*A per sample. Allow twice that
    // for the attack glide; a click would be far bigger.
    const double maxAmplitude = models::SineParams{}.gain;
    const double limit = 2.0 * 2.0 * std::numbers::pi * 440.0 / sampleRate * maxAmplitude;
    double maxStep = 0.0;
    for (std::size_t i = 1; i < out.size(); ++i) {
        maxStep = std::max(maxStep, static_cast<double>(std::abs(out[i] - out[i - 1])));
    }
    CHECK(maxStep < limit);
    CHECK(ec::total(keyboard.diagnostics) == 0);
}

TEST_CASE("rendering is deterministic") {
    const std::array events{ec::noteOn(0, 60, 0.8F), ec::noteOn(500, 64, 0.6F),
                            ec::noteOn(900, 67, 0.7F), ec::noteOff(seconds(0.3), 64)};
    const auto render = [&events] {
        auto keyboard = makeKeyboard();
        return ec::renderOffline(keyboard, events, seconds(0.5));
    };
    CHECK(render() == render());
}

TEST_CASE("the T60 coefficient closes 60 dB in the given time") {
    const double coeff = models::t60Coeff(0.25, sampleRate);
    const double remaining = std::pow(1.0 - coeff, 0.25 * sampleRate);
    CHECK_THAT(remaining, WithinAbs(1e-3, 1e-9));
}

TEST_CASE("makeSineKeyboard rejects a non-positive sample rate") {
    REQUIRE_THROWS_AS(models::makeSineKeyboard(0.0, blockSize), std::invalid_argument);
}
