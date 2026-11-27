// Headless acceptance tests for the sine key model (Milestone 0, step 0.2), ported from the Rust
// prototype. Every test renders through the real keyboard, built from a validated configuration
// by the test harness, and also checks that the engine had nothing to recover from.

#include "harness.hpp"

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
#include <string>
#include <variant>
#include <vector>

namespace {

namespace ec = pianolab::engine_core;
namespace models = pianolab::models;
using Catch::Matchers::WithinAbs;

using models::testing::blockSize;
using models::testing::sampleRate;
constexpr int a4 = 69;

/// A fresh keyboard of sine keys from the test configuration.
models::PianoKeyboard makeKeyboard() {
    return models::testing::makeTestKeyboard();
}

/// The number of samples in `s` seconds at the test sample rate.
std::size_t seconds(double s) {
    return static_cast<std::size_t>(std::lround(s * sampleRate));
}

/// The largest absolute sample value.
float peak(std::span<const float> samples) {
    float result = 0.0F;
    for (const float sample : samples) {
        result = std::max(result, std::abs(sample));
    }
    return result;
}

/// An amplitude in decibels relative to full scale (1.0 = 0 dB).
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

/// The sine key for MIDI `note`, which must be on the keyboard (A0-C8).
const models::SineKey& sineKey(const models::PianoKeyboard& keyboard, int note) {
    return std::get<models::SineKey>(
        keyboard.keys.at(static_cast<std::size_t>(note - ec::lowestNote)));
}

} // namespace

TEST_CASE("A4 plays at 440 Hz") {
    auto keyboard = makeKeyboard();
    const std::array events{ec::noteOn(0, a4, 1.0F)};
    const auto out = ec::renderOffline(keyboard, events, seconds(1.0));

    // Skip the first 10 ms so the attack has settled.
    const double hz = measureFrequency(std::span(out).subspan(seconds(0.01)));
    CHECK_THAT(hz, WithinAbs(440.0, 0.1));
    CHECK(keyboard.diagnostics.total == 0);
}

TEST_CASE("every key plays its own pitch") {
    // A0 (27.5 Hz), middle C and C8 (4186 Hz): the extremes and the middle.
    for (const int note : {21, 60, 108}) {
        CAPTURE(note);
        const double expected = ec::equalTemperament(note, ec::TuningConfig{}.a4Hz);
        auto keyboard = makeKeyboard();
        const std::array events{ec::noteOn(0, note, 1.0F)};
        const auto out = ec::renderOffline(keyboard, events, seconds(1.0));

        const double hz = measureFrequency(std::span(out).subspan(seconds(0.05)));
        CHECK_THAT(hz, WithinAbs(expected, expected * 1e-4));
        CHECK(keyboard.diagnostics.total == 0);
    }
}

TEST_CASE("output is neither silent nor clipping") {
    auto keyboard = makeKeyboard();
    const std::array events{ec::noteOn(0, a4, 1.0F)};
    const auto out = ec::renderOffline(keyboard, events, seconds(0.5));

    const float level = peak(out);
    CHECK(level > 0.1F);
    CHECK(level < 1.0F);
    CHECK(keyboard.diagnostics.total == 0);
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
    CHECK(keyboard.diagnostics.total == 0);
}

TEST_CASE("a note starts on its exact sample") {
    // Deliberately not on a block boundary.
    const std::size_t start = 1000;
    auto keyboard = makeKeyboard();
    const std::array events{ec::noteOn(start, a4, 1.0F)};
    const auto out = ec::renderOffline(keyboard, events, seconds(0.1));

    CHECK(std::ranges::all_of(std::span(out).first(start), [](float s) { return s == 0.0F; }));
    CHECK(out[start + 2] != 0.0F);
    CHECK(keyboard.diagnostics.total == 0);
}

TEST_CASE("re-striking a ringing key does not click") {
    auto keyboard = makeKeyboard();
    const std::size_t restrikeAt = seconds(0.3) + 123; // mid-block, arbitrary phase
    const std::array events{ec::noteOn(0, a4, 0.3F), ec::noteOn(restrikeAt, a4, 1.0F)};
    const auto out = ec::renderOffline(keyboard, events, seconds(0.6));

    // A smooth sine of amplitude A changes by at most 2*pi*f/SR*A per sample. Allow twice that
    // for the attack glide; a click would be far bigger.
    const double maxAmplitude = models::SineConfig{}.gain;
    const double limit = 2.0 * 2.0 * std::numbers::pi * 440.0 / sampleRate * maxAmplitude;
    double maxStep = 0.0;
    for (std::size_t i = 1; i < out.size(); ++i) {
        maxStep = std::max(maxStep, static_cast<double>(std::abs(out[i] - out[i - 1])));
    }
    CHECK(maxStep < limit);
    CHECK(keyboard.diagnostics.total == 0);
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

// --- Configuration
// --------------------------------------------------------------------------------

TEST_CASE("the built-in piano configuration is valid") {
    CHECK(models::validate(models::PianoConfig{}).errors.empty());
}

TEST_CASE("piano validation fixes every part and names the setting") {
    models::PianoConfig config;
    config.render.sampleRate = 0.0;
    config.tuning.a4Hz = 1000.0;
    config.sine.attackT60 = -0.1;

    const auto result = models::validate(config);

    REQUIRE(result.errors.size() == 3);
    CHECK(result.errors[0].message.starts_with("render.sampleRate: 0 Hz is outside"));
    CHECK(result.errors[1].message.starts_with("tuning.a4Hz: 1000 Hz is outside"));
    CHECK(result.errors[2].message ==
          "sine.attackT60: -0.1 s is outside 0.0005-1 s; using the default 0.005 s");
    CHECK(result.config->render.sampleRate == ec::RenderConfig{}.sampleRate);
    CHECK(result.config->tuning.a4Hz == ec::TuningConfig{}.a4Hz);
    CHECK(result.config->sine.attackT60 == models::SineConfig{}.attackT60);
}

TEST_CASE("a fixed configuration still makes a working keyboard") {
    models::PianoConfig config = models::testing::testConfig();
    config.render.maxBlock = 0;
    const auto result = models::validate(config);
    REQUIRE(result.errors.size() == 1);

    auto keyboard = models::makePianoKeyboard(result.config);
    const std::array events{ec::noteOn(0, a4, 1.0F)};
    const auto out = ec::renderOffline(keyboard, events, seconds(0.1));

    CHECK(peak(out) > 0.1F);
    CHECK(keyboard.diagnostics.total == 0);
}

TEST_CASE("the tuning reference changes the pitch") {
    models::PianoConfig config = models::testing::testConfig();
    config.tuning.a4Hz = 432.0;
    auto keyboard = models::testing::makeTestKeyboard(config);
    const std::array events{ec::noteOn(0, a4, 1.0F)};
    const auto out = ec::renderOffline(keyboard, events, seconds(1.0));

    CHECK_THAT(measureFrequency(std::span(out).subspan(seconds(0.01))), WithinAbs(432.0, 0.1));
}

TEST_CASE("the sine settings change the sound") {
    models::PianoConfig config = models::testing::testConfig();
    config.sine.gain = 0.5;
    auto keyboard = models::testing::makeTestKeyboard(config);
    const std::array events{ec::noteOn(0, a4, 1.0F)};
    const auto out = ec::renderOffline(keyboard, events, seconds(0.2));

    CHECK_THAT(peak(out), WithinAbs(0.5, 0.01));
}

// --- Live setting changes
// -------------------------------------------------------------------------

namespace {

/// The largest change between neighbouring samples: a click shows up as a big one.
double maxStep(std::span<const float> samples) {
    double result = 0.0;
    for (std::size_t i = 1; i < samples.size(); ++i) {
        result = std::max(result, static_cast<double>(std::abs(samples[i] - samples[i - 1])));
    }
    return result;
}

} // namespace

TEST_CASE("a ringing key takes new settings without a click") {
    auto keyboard = makeKeyboard();
    const std::array events{ec::noteOn(0, a4, 1.0F)};
    auto out = ec::renderOffline(keyboard, events, seconds(0.2));

    models::PianoConfig retuned = models::testing::testConfig();
    retuned.tuning.a4Hz = 442.0;
    retuned.render.maxBlock = 128;
    ec::adoptSettings(keyboard, models::testing::makeTestKeyboard(retuned));
    CHECK(keyboard.maxBlock == 128);

    const auto after = ec::renderOffline(keyboard, {}, seconds(0.5));
    out.insert(out.end(), after.begin(), after.end());

    // As in the re-strike test: a smooth sine at full gain changes by at most 2*pi*f/SR*A per
    // sample, and a click would be far bigger.
    const double limit =
        2.0 * 2.0 * std::numbers::pi * 442.0 / sampleRate * models::SineConfig{}.gain;
    CHECK(maxStep(out) < limit);
    CHECK_THAT(measureFrequency(after), WithinAbs(442.0, 0.1));
    CHECK_THAT(peak(after), WithinAbs(models::SineConfig{}.gain, 0.01));
    CHECK(keyboard.diagnostics.total == 0);
}

TEST_CASE("a fading key takes the new release time") {
    const auto fadeWith = [](double releaseT60) {
        auto keyboard = makeKeyboard();
        const std::array events{ec::noteOn(0, a4, 1.0F), ec::noteOff(seconds(0.1), a4)};
        static_cast<void>(ec::renderOffline(keyboard, events, seconds(0.15)));
        models::PianoConfig config = models::testing::testConfig();
        config.sine.releaseT60 = releaseT60;
        ec::adoptSettings(keyboard, models::testing::makeTestKeyboard(config));
        const auto later = ec::renderOffline(keyboard, {}, seconds(0.5));
        return peak(std::span(later).last(seconds(0.05)));
    };
    // At the change the key has faded to about 0.063. With the built-in 0.25 s it is silent half
    // a second later; with 10 s it has only fallen to about 0.045 (10^(-3 * 0.5 / 10) of it).
    CHECK(fadeWith(models::SineConfig{}.releaseT60) < 1e-3F);
    CHECK(fadeWith(10.0) > 0.03F);
}

TEST_CASE("a gain change is heard from the next strike") {
    auto keyboard = makeKeyboard();
    const std::array first{ec::noteOn(0, a4, 1.0F)};
    static_cast<void>(ec::renderOffline(keyboard, first, seconds(0.1)));

    models::PianoConfig louder = models::testing::testConfig();
    louder.sine.gain = 0.5;
    ec::adoptSettings(keyboard, models::testing::makeTestKeyboard(louder));
    const auto ringing = ec::renderOffline(keyboard, {}, seconds(0.1));
    CHECK_THAT(peak(ringing), WithinAbs(models::SineConfig{}.gain, 0.01));

    const std::array again{ec::noteOn(0, a4, 1.0F)};
    const auto struck = ec::renderOffline(keyboard, again, seconds(0.2));
    CHECK_THAT(peak(std::span(struck).last(seconds(0.05))), WithinAbs(0.5, 0.01));
}
