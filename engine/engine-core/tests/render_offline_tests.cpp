#include "test_key.hpp"

#include <pianolab/engine_core/diagnostics.hpp>
#include <pianolab/engine_core/keyboard.hpp>
#include <pianolab/engine_core/note_event.hpp>
#include <pianolab/engine_core/render_offline.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>

namespace {

using pianolab::engine_core::Problem;
using pianolab::engine_core::testing::ConstantKey;
namespace ec = pianolab::engine_core;

constexpr std::size_t maxBlock = 16;
constexpr int middleC = 60;

/// A keyboard of constant test keys, all released.
auto makeTestKeyboard() {
    return ec::makeKeyboard<ConstantKey>(maxBlock, [](int /*note*/) { return ConstantKey{}; });
}

/// How many samples equal `value` exactly.
std::size_t countEqual(const std::vector<float>& samples, float value) {
    return static_cast<std::size_t>(std::ranges::count(samples, value));
}

} // namespace

TEST_CASE("renderOffline places events at absolute times across blocks") {
    auto keyboard = makeTestKeyboard();
    // Starts and stops mid-block, spanning several blocks.
    const std::array events{ec::noteOn(5, middleC, 1.0F), ec::noteOff(41, middleC)};

    const auto out = ec::renderOffline(keyboard, events, 64);

    REQUIRE(out.size() == 64);
    CHECK(out[4] == 0.0F);
    CHECK(out[5] == 1.0F);
    CHECK(out[40] == 1.0F);
    CHECK(out[41] == 0.0F);
    CHECK(countEqual(out, 1.0F) == 36);
    CHECK(ec::total(keyboard.diagnostics) == 0);
}

TEST_CASE("renderOffline handles a length that isn't a whole number of blocks") {
    auto keyboard = makeTestKeyboard();
    const std::array events{ec::noteOn(0, middleC, 1.0F)};

    const auto out = ec::renderOffline(keyboard, events, 37);

    CHECK(countEqual(out, 1.0F) == 37);
}

TEST_CASE("renderOffline ignores events after the end") {
    auto keyboard = makeTestKeyboard();
    const std::array events{ec::noteOn(100, middleC, 1.0F)};

    const auto out = ec::renderOffline(keyboard, events, 32);

    CHECK(countEqual(out, 0.0F) == 32);
    CHECK(ec::total(keyboard.diagnostics) == 0);
}

TEST_CASE("renderOffline sorts unsorted events and records it") {
    auto keyboard = makeTestKeyboard();
    const std::array events{ec::noteOff(20, middleC), ec::noteOn(10, middleC, 1.0F)};

    const auto out = ec::renderOffline(keyboard, events, 32);

    CHECK(countEqual(out, 1.0F) == 10);
    CHECK(ec::count(keyboard.diagnostics, Problem::EventOutOfOrder) == 1);
}
