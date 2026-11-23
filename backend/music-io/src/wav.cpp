#include <pianolab/engine_core/error.hpp>
#include <pianolab/music_io/wav.hpp>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <memory>
#include <span>
#include <vector>

namespace pianolab::music_io {

using engine_core::Error;

std::expected<std::vector<std::byte>, Error> encodeWav(std::span<const float> samples,
                                                       double sampleRate) {
    if (samples.size() > maxWavSamples) {
        return std::unexpected(
            Error{"too many samples for one WAV file (it must stay under 4 GiB)"});
    }
    juce::MemoryBlock block;
    {
        std::unique_ptr<juce::OutputStream> stream =
            std::make_unique<juce::MemoryOutputStream>(block, false);
        const auto options =
            juce::AudioFormatWriterOptions{}
                .withSampleRate(sampleRate)
                .withNumChannels(1)
                .withBitsPerSample(32)
                .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
        juce::WavAudioFormat format;
        const std::unique_ptr<juce::AudioFormatWriter> writer =
            format.createWriterFor(stream, options);
        if (writer == nullptr) {
            return std::unexpected(Error{"couldn't create a WAV writer for these settings"});
        }

        // The writer takes an int count, so long renders go in chunks.
        constexpr std::size_t chunk = 65'536;
        for (std::size_t start = 0; start < samples.size(); start += chunk) {
            const auto part = samples.subspan(start, std::min(chunk, samples.size() - start));
            const std::array<const float*, 1> channels{part.data()};
            if (!writer->writeFromFloatArrays(channels.data(), 1, static_cast<int>(part.size()))) {
                return std::unexpected(Error{"couldn't write the WAV samples"});
            }
        }
    } // destroying the writer finishes the file's header and flushes the stream into `block`

    const auto* first = static_cast<const std::byte*>(block.getData());
    return std::vector<std::byte>(first, first + block.getSize());
}

} // namespace pianolab::music_io
