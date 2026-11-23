#include <pianolab/engine_core/note_event.hpp>
#include <pianolab/music_io/midi.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

using Catch::Matchers::WithinAbs;
namespace ec = pianolab::engine_core;
namespace mio = pianolab::music_io;

namespace {

/// Ticks per quarter note in the test files.
constexpr int ticksPerQuarter = 480;
/// 120 quarter notes per minute, as microseconds per quarter note.
constexpr int tempo120 = 500'000;
constexpr int middleC = 60;

/// A one-track MIDI file of `messages` (timestamps in ticks), at 120 quarter notes per minute.
std::vector<std::byte> midiFile(const std::vector<juce::MidiMessage>& messages) {
    juce::MidiMessageSequence track;
    track.addEvent(juce::MidiMessage::tempoMetaEvent(tempo120), 0.0);
    for (const auto& message : messages) {
        track.addEvent(message);
    }
    track.updateMatchedPairs();
    juce::MidiFile file;
    file.setTicksPerQuarterNote(ticksPerQuarter);
    file.addTrack(track);

    juce::MemoryOutputStream stream;
    REQUIRE(file.writeTo(stream));
    const auto* first = static_cast<const std::byte*>(stream.getData());
    return {first, first + stream.getDataSize()};
}

/// `message` stamped at `ticks`.
juce::MidiMessage at(juce::MidiMessage message, double ticks) {
    message.setTimeStamp(ticks);
    return message;
}

} // namespace

TEST_CASE("a MIDI note becomes a timed press and release") {
    const auto data = midiFile({at(juce::MidiMessage::noteOn(1, middleC, std::uint8_t{100}), 0.0),
                                at(juce::MidiMessage::noteOff(1, middleC), ticksPerQuarter)});

    const auto reading = mio::readMidi(data);

    REQUIRE(reading);
    CHECK(reading->warnings.empty());
    const auto& events = reading->performance.events;
    REQUIRE(events.size() == 2);
    const auto* on = std::get_if<ec::NoteOn>(&events[0].event);
    REQUIRE(on != nullptr);
    CHECK(on->note == middleC);
    CHECK_THAT(on->velocity, WithinAbs(100.0 / 127.0, 1e-6));
    CHECK_THAT(events[0].seconds, WithinAbs(0.0, 1e-9));
    CHECK(std::holds_alternative<ec::NoteOff>(events[1].event));
    CHECK_THAT(events[1].seconds, WithinAbs(0.5, 1e-9)); // one quarter at 120 per minute
}

TEST_CASE("a note-on with velocity 0 is a release") {
    const auto data =
        midiFile({at(juce::MidiMessage::noteOn(1, middleC, std::uint8_t{100}), 0.0),
                  at(juce::MidiMessage::noteOn(1, middleC, std::uint8_t{0}), ticksPerQuarter)});

    const auto reading = mio::readMidi(data);

    REQUIRE(reading);
    REQUIRE(reading->performance.events.size() == 2);
    CHECK(std::holds_alternative<ec::NoteOff>(reading->performance.events[1].event));
}

TEST_CASE("pedals are skipped with one warning") {
    const auto data =
        midiFile({at(juce::MidiMessage::controllerEvent(1, 64, 127), ticksPerQuarter),
                  at(juce::MidiMessage::noteOn(1, middleC, std::uint8_t{100}), 0.0),
                  at(juce::MidiMessage::noteOff(1, middleC), 2.0 * ticksPerQuarter),
                  at(juce::MidiMessage::controllerEvent(1, 64, 0), 2.0 * ticksPerQuarter)});

    const auto reading = mio::readMidi(data);

    REQUIRE(reading);
    CHECK(reading->performance.events.size() == 2);
    REQUIRE(reading->warnings.size() == 1);
    CHECK(reading->warnings[0].message ==
          "pedals are not supported yet; skipped 2 pedal events (the first at 0.5 s)");
}

TEST_CASE("a MIDI file without notes reads with a warning") {
    const auto reading = mio::readMidi(midiFile({}));

    REQUIRE(reading);
    CHECK(reading->performance.events.empty());
    REQUIRE(reading->warnings.size() == 1);
    CHECK(reading->warnings[0].message == "the file has no notes");
}

TEST_CASE("data that isn't a MIDI file is an error") {
    const std::array bytes{std::byte{'n'}, std::byte{'o'}, std::byte{'p'}, std::byte{'e'}};

    const auto reading = mio::readMidi(bytes);

    REQUIRE_FALSE(reading);
    CHECK(reading.error().message == "not a Standard MIDI file");
}
