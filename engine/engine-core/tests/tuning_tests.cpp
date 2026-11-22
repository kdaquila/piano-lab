#include <pianolab/engine_core/tuning.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;
namespace ec = pianolab::engine_core;

TEST_CASE("equal temperament hits the reference pitches") {
    CHECK_THAT(ec::equalTemperament(69), WithinAbs(440.0, 1e-9));
    CHECK_THAT(ec::equalTemperament(21), WithinAbs(27.5, 1e-9));      // A0
    CHECK_THAT(ec::equalTemperament(60), WithinAbs(261.6256, 1e-3));  // middle C
    CHECK_THAT(ec::equalTemperament(108), WithinAbs(4186.009, 1e-3)); // C8
}

TEST_CASE("an octave doubles the frequency") {
    CHECK_THAT(ec::equalTemperament(81), WithinAbs(2.0 * ec::equalTemperament(69), 1e-9));
}

TEST_CASE("the A4 reference can be changed") {
    CHECK_THAT(ec::equalTemperament(69, 442.0), WithinAbs(442.0, 1e-9));
    CHECK_THAT(ec::equalTemperament(57, 442.0), WithinAbs(221.0, 1e-9));
}
