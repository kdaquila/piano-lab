#include <pianolab/engine_core/note_event.hpp>
#include <pianolab/server/live_midi.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <utility>
#include <variant>
#include <vector>

namespace ec = pianolab::engine_core;
namespace sv = pianolab::server;

namespace {

/// The note event `bytes` means, failing the test if it means something else.
ec::NoteEvent noteEvent(const std::vector<std::uint8_t>& bytes) {
    const auto meaning = sv::readMidiMessage(bytes);
    REQUIRE(std::holds_alternative<ec::NoteEvent>(meaning));
    return std::get<ec::NoteEvent>(meaning);
}

} // namespace

TEST_CASE("a note-on becomes a key press with its velocity scaled to 0-1") {
    const auto event = noteEvent({0x90, 60, 127});
    REQUIRE(std::holds_alternative<ec::NoteOn>(event));
    CHECK(std::get<ec::NoteOn>(event).note == 60);
    CHECK(std::get<ec::NoteOn>(event).velocity == 1.0F);

    const auto soft = noteEvent({0x90, 21, 1});
    CHECK(std::get<ec::NoteOn>(soft).velocity == 1.0F / 127.0F);
}

TEST_CASE("a note-off, and a note-on with velocity 0, release the key") {
    const auto off = noteEvent({0x80, 64, 40});
    REQUIRE(std::holds_alternative<ec::NoteOff>(off));
    CHECK(std::get<ec::NoteOff>(off).note == 64);

    const auto zero = noteEvent({0x90, 64, 0});
    REQUIRE(std::holds_alternative<ec::NoteOff>(zero));
    CHECK(std::get<ec::NoteOff>(zero).note == 64);
}

TEST_CASE("every MIDI channel plays the piano") {
    for (std::uint8_t channel = 0; channel < 16; ++channel) {
        const auto event = noteEvent({static_cast<std::uint8_t>(0x90U | channel), 72, 100});
        CHECK(std::holds_alternative<ec::NoteOn>(event));
    }
}

TEST_CASE("pedal messages are recognised but not played") {
    for (const std::uint8_t controller : std::array<std::uint8_t, 3>{64, 66, 67}) {
        const std::array<std::uint8_t, 3> bytes{0xB3, controller, 127};
        const auto meaning = sv::readMidiMessage(bytes);
        REQUIRE(std::holds_alternative<sv::UnsupportedPedal>(meaning));
        CHECK(std::cmp_equal(std::get<sv::UnsupportedPedal>(meaning).controller, controller));
    }
}

TEST_CASE("other messages, and messages too short to read, are ignored") {
    const std::vector<std::vector<std::uint8_t>> ignored{
        {0xB0, 7, 100},   // volume controller
        {0xE0, 0, 64},    // pitch bend
        {0xC0, 5},        // program change
        {0xF8},           // clock
        {0x90, 60},       // a note-on cut short
        {},               // nothing at all
        {0x3C, 0x40, 1}}; // data bytes without a status byte
    for (const auto& bytes : ignored) {
        CHECK(std::holds_alternative<sv::IgnoredMidi>(sv::readMidiMessage(bytes)));
    }
}
