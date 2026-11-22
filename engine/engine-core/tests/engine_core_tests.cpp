#include <pianolab/engine_core/engine_core.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("engine-core builds and links", "[placeholder]") {
    REQUIRE(pianolab::engine_core::libraryName() == "engine-core");
}
