#pragma once

// A lightweight harness for driving the engine in tests, without the backend: build a
// configuration from the built-in defaults, override what the test cares about, validate it,
// and make the keyboard. Tests use the same entry points the backend will.

#include <pianolab/models/piano_key.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>

/// Test helpers for the models library.
namespace pianolab::models::testing {

/// The sample rate the tests render at, in hertz.
inline constexpr double sampleRate = 48'000.0;
/// The block size the tests render in, in samples.
inline constexpr std::size_t blockSize = 256;

/// The built-in configuration with the test sample rate and block size.
inline PianoConfig testConfig() {
    PianoConfig config;
    config.render.sampleRate = sampleRate;
    config.render.maxBlock = blockSize;
    return config;
}

/// A keyboard built from `config` (by default `testConfig()`). Fails the test if the
/// configuration needed any fixing, so tests can't silently run on replaced values.
inline PianoKeyboard makeTestKeyboard(const PianoConfig& config = testConfig()) {
    const auto validated = validate(config);
    REQUIRE(validated.errors.empty());
    return makePianoKeyboard(validated.config);
}

} // namespace pianolab::models::testing
