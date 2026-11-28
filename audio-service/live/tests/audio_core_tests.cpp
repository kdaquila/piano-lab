// The audio core, driven block by block the way a sound card drives it, through the real queues,
// recorder and monitor: the audio service's live pipeline, tested without any hardware.

#include <pianolab/engine_core/keyboard.hpp>
#include <pianolab/engine_core/note_event.hpp>
#include <pianolab/live/audio_core.hpp>
#include <pianolab/live/monitor.hpp>
#include <pianolab/live/recorder.hpp>
#include <pianolab/logging/log.hpp>
#include <pianolab/models/piano_key.hpp>
#include <pianolab/music_io/files.hpp>

#include <catch2/catch_test_macros.hpp>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace ec = pianolab::engine_core;
namespace lg = pianolab::logging;
namespace live = pianolab::live;
namespace models = pianolab::models;

namespace {

/// The sample rate the tests run at, in hertz.
constexpr double sampleRate = 48'000.0;
/// The keyboard's maxBlock in these tests, in samples.
constexpr std::size_t maxBlock = 256;
/// The device block size the tests play in, in samples.
constexpr std::size_t blockSize = 128;

/// The built-in piano with the test sample rate and maxBlock, validated. Fails the test if
/// anything needed fixing.
ec::Valid<models::PianoConfig> testConfig() {
    models::PianoConfig config;
    config.render.sampleRate = sampleRate;
    config.render.maxBlock = maxBlock;
    auto validated = models::validate(config);
    REQUIRE(validated.errors.empty());
    return validated.config;
}

/// Raw MIDI bytes of a note-on (channel 1).
std::array<std::uint8_t, 3> noteOnBytes(int note, int velocity) {
    return {0x90, static_cast<std::uint8_t>(note), static_cast<std::uint8_t>(velocity)};
}

/// Raw MIDI bytes of a note-off (channel 1).
std::array<std::uint8_t, 3> noteOffBytes(int note) {
    return {0x80, static_cast<std::uint8_t>(note), 0};
}

/// Plays `blocks` device blocks through the audio core and returns all the output.
std::vector<float> play(live::AudioState& state, live::AudioLinks& links, std::size_t blocks,
                        std::size_t size = blockSize) {
    std::vector<float> output;
    std::vector<float> block(size);
    for (std::size_t index = 0; index < blocks; ++index) {
        live::processBlock(state, links, block);
        output.insert(output.end(), block.begin(), block.end());
    }
    return output;
}

/// What the engine alone renders for `firstBlockEvents` applied at the start, over `blocks`
/// device blocks: the reference the live pipeline must match.
std::vector<float> reference(const std::vector<ec::TimedEvent>& firstBlockEvents,
                             std::size_t blocks) {
    auto keyboard = models::makePianoKeyboard(testConfig());
    std::vector<float> output;
    std::vector<float> block(blockSize);
    for (std::size_t index = 0; index < blocks; ++index) {
        ec::render(keyboard, block,
                   index == 0 ? std::span<const ec::TimedEvent>(firstBlockEvents)
                              : std::span<const ec::TimedEvent>{});
        output.insert(output.end(), block.begin(), block.end());
    }
    return output;
}

/// True if any sample is not zero.
bool hasSound(const std::vector<float>& samples) {
    return std::ranges::any_of(samples, [](float sample) { return sample != 0.0F; });
}

/// One message a test log received.
struct Logged {
    lg::LogLevel level;
    std::string message;
};

/// A log that collects messages into `into`.
lg::Log collectingLog(std::vector<Logged>& into) {
    return {.write = [&into](lg::LogLevel level, std::string_view message) {
        into.push_back({.level = level, .message = std::string(message)});
    }};
}

/// A fresh, empty folder for one test's files, removed when the test ends.
struct TempFolder {
    std::filesystem::path path;

    /// Makes the folder `name` under the system's temporary folder, emptying it first.
    explicit TempFolder(const std::string& name)
        : path(std::filesystem::temp_directory_path() / name) {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    TempFolder(const TempFolder&) = delete;
    TempFolder& operator=(const TempFolder&) = delete;
    TempFolder(TempFolder&&) = delete;
    TempFolder& operator=(TempFolder&&) = delete;
    /// Removes the folder and everything in it.
    ~TempFolder() {
        try {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        } catch (...) { // NOLINT(bugprone-empty-catch): a destructor must not throw
        }
    }
};

/// The samples of the mono WAV file at `path`.
std::vector<float> readWav(const std::filesystem::path& path) {
    const auto bytes = pianolab::music_io::readBytes(path);
    REQUIRE(bytes);
    juce::WavAudioFormat format;
    const std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(
        new juce::MemoryInputStream(bytes->data(), bytes->size(), false), true));
    REQUIRE(reader != nullptr);
    REQUIRE(reader->sampleRate == sampleRate);
    const auto length = static_cast<int>(reader->lengthInSamples);
    juce::AudioBuffer<float> buffer(1, length);
    REQUIRE(reader->read(&buffer, 0, length, 0, true, false));
    const float* samples = buffer.getReadPointer(0);
    return {samples, samples + length};
}

} // namespace

TEST_CASE("a live note plays from the start of the next block, exactly as the engine renders it") {
    auto links = live::makeAudioLinks(1);
    auto state = live::makeAudioState(testConfig());

    live::receiveMidi(*links, 0, noteOnBytes(69, 100), 1.0);
    const auto output = play(state, *links, 40);

    CHECK(output == reference({ec::noteOn(0, 69, 100.0F / 127.0F)}, 40));
    CHECK(hasSound(output));
    CHECK(state.keyboard.diagnostics.total == 0);
}

TEST_CASE("with no notes, the output is silent") {
    auto links = live::makeAudioLinks(1);
    auto state = live::makeAudioState(testConfig());
    CHECK_FALSE(hasSound(play(state, *links, 10)));
}

TEST_CASE("notes from several inputs are applied in the order they arrived") {
    auto links = live::makeAudioLinks(2);
    auto state = live::makeAudioState(testConfig());

    // Queued in the opposite order to their arrival: input 0's release arrived after input 1's
    // press, so the note is pressed and released (a short note), not left sounding.
    live::receiveMidi(*links, 0, noteOffBytes(60), 2.0);
    live::receiveMidi(*links, 1, noteOnBytes(60, 127), 1.0);
    const auto output = play(state, *links, 400);

    CHECK(output == reference({ec::noteOn(0, 60, 1.0F), ec::noteOff(0, 60)}, 400));
    CHECK(output != reference({ec::noteOff(0, 60), ec::noteOn(0, 60, 1.0F)}, 400));
}

TEST_CASE("a device block longer than the keyboard's maxBlock is rendered without complaint") {
    auto links = live::makeAudioLinks(1);
    auto state = live::makeAudioState(testConfig());

    live::receiveMidi(*links, 0, noteOnBytes(57, 90), 0.0);
    const auto output = play(state, *links, 3, maxBlock * 4 + 13);

    CHECK(hasSound(output));
    CHECK(state.keyboard.diagnostics.total == 0);
}

TEST_CASE("notes past the per-block limit wait for the next block") {
    // Two inputs, so more notes wait than one block takes without either queue overflowing.
    auto links = live::makeAudioLinks(2);
    auto state = live::makeAudioState(testConfig());
    const std::size_t perInput = live::maxEventsPerBlock / 2 + 10;
    for (std::size_t index = 0; index < perInput; ++index) {
        live::receiveMidi(*links, 0, noteOnBytes(60, 64), static_cast<double>(index));
        live::receiveMidi(*links, 1, noteOnBytes(62, 64), static_cast<double>(index));
    }
    REQUIRE(links->droppedEvents.load() == 0);

    (void)play(state, *links, 1);
    CHECK(links->inputs[1]->tryPop().has_value()); // some were still waiting
    (void)play(state, *links, 1);
    CHECK_FALSE(links->inputs[1]->tryPop().has_value()); // and played in the next block
}

TEST_CASE("a recording holds exactly what was played") {
    const TempFolder folder("piano-lab-recording-exact");
    const auto path = folder.path / "take.wav";
    auto links = live::makeAudioLinks(1);
    auto state = live::makeAudioState(testConfig());
    live::Recorder recorder(*links);

    REQUIRE(recorder.start(path, sampleRate));
    CHECK(recorder.recording());
    live::receiveMidi(*links, 0, noteOnBytes(64, 80), 0.0);
    auto played = play(state, *links, 30);
    CHECK_FALSE(recorder.drain()); // the writer thread's job, done here step by step
    live::receiveMidi(*links, 0, noteOffBytes(64), 1.0);
    const auto more = play(state, *links, 30, 333); // odd sizes split into several chunks
    played.insert(played.end(), more.begin(), more.end());
    const auto summary = recorder.stop();

    REQUIRE(summary);
    CHECK(summary->file == path);
    CHECK(summary->seconds == static_cast<double>(played.size()) / sampleRate);
    CHECK_FALSE(recorder.recording());
    CHECK(readWav(path) == played);
    CHECK(links->droppedRecordingSamples.load() == 0);
}

TEST_CASE("nothing is recorded before a recording starts or after it stops") {
    const TempFolder folder("piano-lab-recording-bounds");
    auto links = live::makeAudioLinks(1);
    auto state = live::makeAudioState(testConfig());
    live::Recorder recorder(*links);

    live::receiveMidi(*links, 0, noteOnBytes(64, 80), 0.0);
    (void)play(state, *links, 5); // before: not recorded
    REQUIRE(recorder.start(folder.path / "take.wav", sampleRate));
    const auto played = play(state, *links, 7);
    REQUIRE(recorder.stop());
    (void)play(state, *links, 5); // after: not recorded

    CHECK(readWav(folder.path / "take.wav") == played);
}

TEST_CASE("audio from a stopped recording never ends up in the next one") {
    const TempFolder folder("piano-lab-recording-sessions");
    auto links = live::makeAudioLinks(1);
    auto state = live::makeAudioState(testConfig());
    live::Recorder recorder(*links);

    REQUIRE(recorder.start(folder.path / "first.wav", sampleRate));
    (void)play(state, *links, 3);
    REQUIRE(recorder.stop());
    // A block the audio thread was in the middle of when the first recording stopped.
    live::RecordedChunk late;
    late.session = links->recordingSession.load() == 0 ? 1 : 0;
    late.count = 64;
    REQUIRE(recorder.start(folder.path / "second.wav", sampleRate));
    REQUIRE(links->recording.tryPush(late));
    const auto played = play(state, *links, 2);
    REQUIRE(recorder.stop());

    CHECK(readWav(folder.path / "second.wav") == played);
}

TEST_CASE("starting a second recording at once, or stopping when idle, is an error") {
    const TempFolder folder("piano-lab-recording-errors");
    auto links = live::makeAudioLinks(1);
    live::Recorder recorder(*links);

    CHECK_FALSE(recorder.stop());
    REQUIRE(recorder.start(folder.path / "a.wav", sampleRate));
    CHECK_FALSE(recorder.start(folder.path / "b.wav", sampleRate));
    CHECK(recorder.stop());
    CHECK_FALSE(recorder.start(folder.path / "missing-folder" / "c.wav", sampleRate));
    CHECK_FALSE(recorder.recording());
}

TEST_CASE("when the recorder falls behind, the missing samples are counted and logged") {
    const TempFolder folder("piano-lab-recording-behind");
    auto links = live::makeAudioLinks(1);
    auto state = live::makeAudioState(testConfig());
    live::Recorder recorder(*links);
    REQUIRE(recorder.start(folder.path / "take.wav", sampleRate));

    // More than the queue holds, without the writer draining it.
    const std::size_t chunks = links->recording.capacity() + 10;
    (void)play(state, *links, chunks, live::recordedChunkSamples);

    CHECK(links->droppedRecordingSamples.load() == 10 * live::recordedChunkSamples);
    std::vector<Logged> logged;
    live::MonitorState monitor;
    live::reportNews(monitor, *links, collectingLog(logged));
    REQUIRE(logged.size() == 1);
    CHECK(logged[0].level == lg::LogLevel::Warning);
    CHECK(logged[0].message.contains("missing from the recording"));
    REQUIRE(recorder.stop());
}

TEST_CASE("the engine's problems reach the monitor and are logged once") {
    auto links = live::makeAudioLinks(1);
    auto state = live::makeAudioState(testConfig());
    std::vector<Logged> logged;
    const auto log = collectingLog(logged);
    live::MonitorState monitor;

    live::receiveMidi(*links, 0, noteOnBytes(10, 100), 0.0); // below A0: not on the keyboard
    (void)play(state, *links, 2);
    live::reportNews(monitor, *links, log);
    live::reportNews(monitor, *links, log);

    REQUIRE(logged.size() == 1);
    CHECK(logged[0].level == lg::LogLevel::Warning);
    CHECK(logged[0].message.contains("1 more problem (1 so far)"));
    CHECK(logged[0].message.contains("note outside A0-C8"));
}

TEST_CASE("a full input queue drops notes and counts them") {
    auto links = live::makeAudioLinks(1);
    const std::size_t capacity = links->inputs[0]->capacity();

    for (std::size_t index = 0; index < capacity + 5; ++index) {
        live::receiveMidi(*links, 0, noteOnBytes(60, 64), 0.0);
    }

    CHECK(links->droppedEvents.load() == 5);
    std::vector<Logged> logged;
    live::MonitorState monitor;
    live::reportNews(monitor, *links, collectingLog(logged));
    REQUIRE(logged.size() == 1);
    CHECK(logged[0].message.contains("5 live notes were dropped"));
}

TEST_CASE("pedals are reported once and otherwise ignored") {
    auto links = live::makeAudioLinks(1);
    const std::array<std::uint8_t, 3> sustainDown{0xB0, 64, 127};
    live::receiveMidi(*links, 0, sustainDown, 0.0);
    live::receiveMidi(*links, 0, sustainDown, 0.1);
    std::vector<Logged> logged;
    live::MonitorState monitor;

    live::reportNews(monitor, *links, collectingLog(logged));
    live::reportNews(monitor, *links, collectingLog(logged));
    live::receiveMidi(*links, 0, sustainDown, 0.2);
    live::reportNews(monitor, *links, collectingLog(logged));

    REQUIRE(logged.size() == 1);
    CHECK(logged[0].message.contains("pedals aren't supported yet"));
    CHECK_FALSE(links->inputs[0]->tryPop().has_value());
}

TEST_CASE("MIDI for an input that doesn't exist is ignored") {
    auto links = live::makeAudioLinks(1);
    live::receiveMidi(*links, 3, noteOnBytes(60, 64), 0.0);
    CHECK_FALSE(links->inputs[0]->tryPop().has_value());
    CHECK(links->droppedEvents.load() == 0);
}

TEST_CASE("the recorder's own thread writes the recording in the background") {
    const TempFolder folder("piano-lab-recording-thread");
    auto links = live::makeAudioLinks(1);
    auto state = live::makeAudioState(testConfig());
    std::vector<float> played;
    {
        live::Recorder recorder(*links);
        recorder.startThread(lg::Log{});
        REQUIRE(recorder.start(folder.path / "take.wav", sampleRate));
        live::receiveMidi(*links, 0, noteOnBytes(72, 100), 0.0);
        played = play(state, *links, 50);
        REQUIRE(recorder.stop());
    }
    CHECK(readWav(folder.path / "take.wav") == played);
}

TEST_CASE("a new keyboard's settings reach the ringing piano at the next block") {
    auto links = live::makeAudioLinks(1);
    auto state = live::makeAudioState(testConfig());
    live::receiveMidi(*links, 0, noteOnBytes(69, 100), 0.0);
    static_cast<void>(play(state, *links, 10));

    models::PianoConfig changed = *testConfig();
    changed.tuning.a4Hz = 442.0;
    changed.render.maxBlock = 64;
    const auto validated = models::validate(changed);
    REQUIRE(validated.errors.empty());
    REQUIRE(links->keyboards.tryPush(models::makePianoKeyboard(validated.config)));

    const auto& a4 = std::get<models::SineKey>(state.keyboard.keys.at(69 - ec::lowestNote));
    const double before = a4.state.amplitude;
    static_cast<void>(play(state, *links, 1));
    CHECK(a4.coeffs.phaseStep == 442.0 / sampleRate);
    CHECK(state.keyboard.maxBlock == 64);
    CHECK(a4.state.amplitude > before * 0.9); // still ringing
    CHECK_FALSE(links->keyboards.tryPop().has_value());
}

TEST_CASE("the keys held down are published as they change") {
    auto links = live::makeAudioLinks(2);
    auto state = live::makeAudioState(testConfig());
    CHECK(live::heldKeys(*links) == live::HeldKeys{});

    live::receiveMidi(*links, 0, noteOnBytes(21, 100), 0.0);  // A0: key 0
    live::receiveMidi(*links, 1, noteOnBytes(108, 100), 0.1); // C8: key 87
    live::receiveMidi(*links, 0, noteOnBytes(60, 100), 0.2);  // C4: key 39
    live::receiveMidi(*links, 0, noteOnBytes(5, 100), 0.3);   // off the keyboard: ignored
    static_cast<void>(play(state, *links, 1));
    CHECK(live::heldKeys(*links) ==
          live::HeldKeys{(std::uint64_t{1} << 0U) | (std::uint64_t{1} << 39U),
                         std::uint64_t{1} << (87U - 64U)});

    live::receiveMidi(*links, 1, noteOffBytes(60), 0.4);
    live::receiveMidi(*links, 0, noteOffBytes(21), 0.5);
    static_cast<void>(play(state, *links, 1));
    CHECK(live::heldKeys(*links) == live::HeldKeys{0, std::uint64_t{1} << (87U - 64U)});
}

TEST_CASE("the peak is the loudest sample since it was last taken") {
    auto links = live::makeAudioLinks(1);
    auto state = live::makeAudioState(testConfig());
    static_cast<void>(play(state, *links, 2));
    CHECK(live::takePeak(*links) == 0.0F);

    live::receiveMidi(*links, 0, noteOnBytes(69, 127), 0.0);
    const auto output = play(state, *links, 40);
    float loudest = 0.0F;
    for (const float sample : output) {
        loudest = std::max(loudest, std::abs(sample));
    }
    CHECK(loudest > 0.1F);
    CHECK(live::takePeak(*links) == loudest);
    CHECK(live::takePeak(*links) == 0.0F); // taken
}

TEST_CASE("a block counts as late only when rendering it took longer than it lasts") {
    auto links = live::makeAudioLinks(1);
    // 480 samples at 48 kHz last 10 ms.
    live::timeBlock(*links, 480, 48'000.0, 0.002);
    live::timeBlock(*links, 480, 48'000.0, 0.010);
    CHECK(links->lateBlocks.load() == 0);
    live::timeBlock(*links, 480, 48'000.0, 0.0101);
    live::timeBlock(*links, 480, 48'000.0, 0.5);
    CHECK(links->lateBlocks.load() == 2);
    live::timeBlock(*links, 480, 0.0, 1.0); // no sample rate yet: nothing to compare with
    CHECK(links->lateBlocks.load() == 2);
}
