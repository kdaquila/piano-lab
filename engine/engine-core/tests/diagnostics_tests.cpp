#include <pianolab/engine_core/diagnostics.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <set>
#include <string_view>

using pianolab::engine_core::Diagnostics;
using pianolab::engine_core::Problem;
namespace ec = pianolab::engine_core;

TEST_CASE("diagnostics start empty") {
    const Diagnostics diagnostics;
    CHECK(ec::total(diagnostics) == 0);
    CHECK_FALSE(diagnostics.last.has_value());
}

TEST_CASE("recording counts each problem and remembers the last") {
    Diagnostics diagnostics;
    ec::record(diagnostics, Problem::OversizedBlock);
    ec::record(diagnostics, Problem::OversizedBlock);
    ec::record(diagnostics, Problem::NonFiniteSample);

    CHECK(ec::count(diagnostics, Problem::OversizedBlock) == 2);
    CHECK(ec::count(diagnostics, Problem::NonFiniteSample) == 1);
    CHECK(ec::count(diagnostics, Problem::EventOutOfOrder) == 0);
    CHECK(ec::total(diagnostics) == 3);
    CHECK(diagnostics.last == Problem::NonFiniteSample);
}

TEST_CASE("every problem has its own name") {
    std::set<std::string_view> names;
    for (std::size_t index = 0; index < ec::problemCount; ++index) {
        const auto name = ec::problemName(static_cast<Problem>(index));
        CHECK(name != "unknown");
        names.insert(name);
    }
    CHECK(names.size() == ec::problemCount);
}
