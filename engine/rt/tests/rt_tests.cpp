#include <pianolab/rt/rt.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("rt builds and links", "[placeholder]") {
    REQUIRE(pianolab::rt::libraryName() == "rt");
}
