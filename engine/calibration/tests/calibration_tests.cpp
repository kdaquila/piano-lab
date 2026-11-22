#include <pianolab/calibration/calibration.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("calibration builds and links", "[placeholder]") {
    REQUIRE(pianolab::calibration::libraryName() == "calibration");
}
