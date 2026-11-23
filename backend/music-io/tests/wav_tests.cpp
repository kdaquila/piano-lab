#include <pianolab/music_io/wav.hpp>

#include <catch2/catch_test_macros.hpp>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace mio = pianolab::music_io;

namespace {

/// The sample rate the tests write at, in hertz.
constexpr double sampleRate = 48'000.0;

/// A short test signal, including values beyond +-1 that integer formats would clip.
std::vector<float> testSignal() {
    return {0.0F, 0.25F, -0.5F, 1.0F, 1.5F, -2.0F, 1e-7F, -0.125F};
}

} // namespace

TEST_CASE("a WAV reads back as exactly the samples written") {
    const auto samples = testSignal();

    const auto bytes = mio::encodeWav(samples, sampleRate);

    REQUIRE(bytes);
    juce::WavAudioFormat format;
    const std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(
        new juce::MemoryInputStream(bytes->data(), bytes->size(), false), true));
    REQUIRE(reader != nullptr);
    CHECK(reader->sampleRate == sampleRate);
    CHECK(reader->numChannels == 1);
    CHECK(reader->bitsPerSample == 32);
    CHECK(reader->usesFloatingPointData);
    REQUIRE(std::cmp_equal(reader->lengthInSamples, samples.size()));

    juce::AudioBuffer<float> buffer(1, static_cast<int>(samples.size()));
    REQUIRE(reader->read(&buffer, 0, static_cast<int>(samples.size()), 0, true, false));
    for (std::size_t index = 0; index < samples.size(); ++index) {
        CHECK(buffer.getSample(0, static_cast<int>(index)) == samples[index]);
    }
}

TEST_CASE("encoding the same samples twice gives the same bytes") {
    const auto samples = testSignal();

    const auto first = mio::encodeWav(samples, sampleRate);
    const auto second = mio::encodeWav(samples, sampleRate);

    REQUIRE(first);
    REQUIRE(second);
    CHECK(*first == *second);
}

TEST_CASE("an empty render is a valid, empty WAV") {
    const auto bytes = mio::encodeWav({}, sampleRate);

    REQUIRE(bytes);
    CHECK_FALSE(bytes->empty());
}
