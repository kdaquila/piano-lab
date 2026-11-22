#include <pianolab/models/models.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("models builds and links", "[placeholder]") {
    REQUIRE(pianolab::models::libraryName() == "models");
}
