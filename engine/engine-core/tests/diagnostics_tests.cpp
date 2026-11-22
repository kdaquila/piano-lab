#include <pianolab/engine_core/diagnostics.hpp>

#include <catch2/catch_test_macros.hpp>

using pianolab::engine_core::Diagnostics;
namespace ec = pianolab::engine_core;

TEST_CASE("diagnostics start empty") {
    const Diagnostics diagnostics;
    CHECK(diagnostics.total == 0);
    CHECK(diagnostics.last.empty());
}

TEST_CASE("recording counts problems and remembers the last message") {
    Diagnostics diagnostics;
    ec::record(diagnostics, "first problem");
    ec::record(diagnostics, "second problem");

    CHECK(diagnostics.total == 2);
    CHECK(diagnostics.last == "second problem");
}
