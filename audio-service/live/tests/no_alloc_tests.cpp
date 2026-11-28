// The real-time rule for the live pipeline: the audio callback's work (taking notes from the
// queues, rendering, feeding the recorder and the monitor) must never allocate. This executable
// includes count_allocations.cpp, which replaces the global allocator to count allocations.

#include <pianolab/live/audio_core.hpp>
#include <pianolab/models/piano_key.hpp>
#include <pianolab/testing/count_allocations.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace live = pianolab::live;
using pianolab::testing::countAllocations;

namespace {

// Somewhere to put results so the compiler can't optimise the allocation away.
void* volatile sink = nullptr;

} // namespace

TEST_CASE("the allocation checker catches a deliberate allocation") {
    CHECK(countAllocations([] {
              std::vector<int> numbers(100);
              sink = numbers.data();
          }) >= 1);
}

TEST_CASE("the audio callback's work never allocates") {
    // Setup may allocate.
    pianolab::models::PianoConfig config;
    config.render.maxBlock = 256;
    const auto validated = pianolab::models::validate(config);
    REQUIRE(validated.errors.empty());
    auto links = live::makeAudioLinks(2);
    auto state = live::makeAudioState(validated.config);
    std::vector<float> block(1000);   // longer than maxBlock: split into pieces
    links->recordingSession.store(1); // as if recording
    // A setting change waiting (the main thread's side), for the audio thread to adopt.
    REQUIRE(links->keyboards.tryPush(pianolab::models::makePianoKeyboard(validated.config)));

    const auto count = countAllocations([&] {
        // Notes on both inputs (on the MIDI threads' side), including problems: a note off the
        // keyboard, and more notes than one block takes.
        for (int note = 21; note <= 108; ++note) {
            const std::array<std::uint8_t, 3> on{0x90, static_cast<std::uint8_t>(note), 100};
            live::receiveMidi(*links, static_cast<std::size_t>(note % 2), on, note * 0.001);
        }
        const std::array<std::uint8_t, 3> offKeyboard{0x90, 5, 100};
        live::receiveMidi(*links, 0, offKeyboard, 1.0);
        const std::array<std::uint8_t, 3> pedal{0xB0, 64, 127};
        live::receiveMidi(*links, 1, pedal, 1.0);
        for (std::size_t index = 0; index < live::maxEventsPerBlock + 50; ++index) {
            const std::array<std::uint8_t, 3> off{0x80, 60, 0};
            live::receiveMidi(*links, 0, off, 2.0);
        }

        // The audio thread's side: blocks until the recording queue is full and overflows.
        for (int index = 0; index < 600; ++index) {
            live::processBlock(state, *links, block);
        }
    });

    CHECK(count == 0);
    CHECK(state.keyboard.diagnostics.total > 0);      // the off-keyboard note was recorded
    CHECK(links->droppedRecordingSamples.load() > 0); // the overflow path ran too
}
