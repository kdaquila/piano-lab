#include "test_key.hpp"

#include <pianolab/engine_core/diagnostics.hpp>
#include <pianolab/engine_core/keyboard.hpp>
#include <pianolab/engine_core/note_event.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <vector>

namespace {

using pianolab::engine_core::Keyboard;
using pianolab::engine_core::TimedEvent;
using pianolab::engine_core::testing::ConstantKey;
namespace ec = pianolab::engine_core;

constexpr std::size_t maxBlock = 64;
constexpr int middleC = 60;
constexpr int e4 = 64;

/// A keyboard of constant test keys, all released.
Keyboard<ConstantKey> makeTestKeyboard() {
    return ec::makeKeyboard<ConstantKey>(pianolab::engine_core::testing::testRender(maxBlock),
                                         [](int /*note*/) { return ConstantKey{}; });
}

/// True when every sample equals `value` exactly.
bool allEqual(std::span<const float> samples, float value) {
    return std::ranges::all_of(samples, [value](float sample) { return sample == value; });
}

} // namespace

TEST_CASE("key range covers A0 to C8") {
    STATIC_REQUIRE(ec::highestNote == 108);
    STATIC_REQUIRE(!ec::keyIndex(20).has_value());
    STATIC_REQUIRE(ec::keyIndex(21) == 0);
    STATIC_REQUIRE(ec::keyIndex(108) == 87);
    STATIC_REQUIRE(!ec::keyIndex(109).has_value());
}

TEST_CASE("events land on their exact sample") {
    auto keyboard = makeTestKeyboard();
    std::array<float, maxBlock> out{};
    const std::array events{ec::noteOn(10, middleC, 1.0F), ec::noteOff(40, middleC)};

    ec::render(keyboard, out, events);

    CHECK(allEqual(std::span(out).first(10), 0.0F));
    CHECK(allEqual(std::span(out).subspan(10, 30), 1.0F));
    CHECK(allEqual(std::span(out).subspan(40), 0.0F));
    CHECK(keyboard.diagnostics.total == 0);
}

TEST_CASE("keys sum into one buffer") {
    auto keyboard = makeTestKeyboard();
    std::array<float, 8> out{};
    const std::array events{ec::noteOn(0, middleC, 1.0F), ec::noteOn(0, e4, 1.0F)};

    ec::render(keyboard, out, events);

    CHECK(allEqual(out, 2.0F));
    CHECK(keyboard.diagnostics.total == 0);
}

TEST_CASE("render overwrites what was in the buffer") {
    auto keyboard = makeTestKeyboard();
    std::array<float, 8> out{};
    out.fill(5.0F);

    ec::render(keyboard, out, std::span<const TimedEvent>{});

    CHECK(allEqual(out, 0.0F));
}

// --- Recovery: the engine keeps going and records what it fixed ---------------------------------

TEST_CASE("notes off the keyboard are ignored and recorded") {
    auto keyboard = makeTestKeyboard();
    std::array<float, 8> out{};
    const std::array events{ec::noteOn(0, 5, 1.0F), ec::noteOn(0, 120, 1.0F), ec::noteOff(0, 120)};

    ec::render(keyboard, out, events);

    CHECK(allEqual(out, 0.0F));
    CHECK(keyboard.diagnostics.total == 3);
    CHECK(keyboard.diagnostics.last.contains("note outside"));
}

TEST_CASE("an oversized block is rendered in chunks and recorded") {
    auto keyboard = makeTestKeyboard();
    std::vector<float> out(maxBlock * 3 + 5);
    const std::array events{ec::noteOn(100, middleC, 1.0F)};

    ec::render(keyboard, out, events);

    CHECK(allEqual(std::span(out).first(100), 0.0F));
    CHECK(allEqual(std::span(out).subspan(100), 1.0F));
    CHECK(keyboard.diagnostics.last.contains("maxBlock"));
    CHECK(keyboard.diagnostics.total == 1);
}

TEST_CASE("an out-of-order event is moved forward and recorded") {
    auto keyboard = makeTestKeyboard();
    std::array<float, 32> out{};
    // The note-off at 10 comes after the note-on at 20 in the list, so it happens at 20.
    const std::array events{ec::noteOn(20, middleC, 1.0F), ec::noteOff(10, middleC)};

    ec::render(keyboard, out, events);

    CHECK(allEqual(out, 0.0F));
    CHECK(keyboard.diagnostics.total == 1);
    CHECK(keyboard.diagnostics.last.contains("before the previous"));
}

TEST_CASE("an event past the block is applied at its end and recorded") {
    auto keyboard = makeTestKeyboard();
    std::array<float, 16> out{};
    const std::array events{ec::noteOn(500, middleC, 1.0F)};

    ec::render(keyboard, out, events);
    CHECK(allEqual(out, 0.0F));
    CHECK(keyboard.diagnostics.total == 1);
    CHECK(keyboard.diagnostics.last.contains("past the end"));

    // The note was still struck, so it sounds from the next block.
    ec::render(keyboard, out, std::span<const TimedEvent>{});
    CHECK(allEqual(out, 1.0F));
}

TEST_CASE("out-of-range velocities are clamped and recorded") {
    auto keyboard = makeTestKeyboard();
    std::array<float, 4> out{};
    const std::array events{ec::noteOn(0, middleC, 2.0F), ec::noteOn(0, e4, -1.0F),
                            ec::noteOn(0, 67, std::numeric_limits<float>::quiet_NaN())};

    ec::render(keyboard, out, events);

    CHECK(keyboard.diagnostics.total == 3);
    CHECK(keyboard.diagnostics.last.contains("velocity"));
}

TEST_CASE("non-finite samples become silence and are recorded") {
    auto keyboard = ec::makeKeyboard<ConstantKey>(
        pianolab::engine_core::testing::testRender(maxBlock), [](int note) {
            return ConstantKey{.held = false,
                               .value =
                                   note == middleC ? std::numeric_limits<float>::infinity() : 1.0F};
        });
    std::array<float, 4> out{};
    const std::array events{ec::noteOn(0, middleC, 1.0F)};

    ec::render(keyboard, out, events);

    CHECK(allEqual(out, 0.0F));
    CHECK(keyboard.diagnostics.total == 4);
    CHECK(keyboard.diagnostics.last.contains("NaN or infinite"));
}
