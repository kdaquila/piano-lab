#include <pianolab/engine_core/config.hpp>
#include <pianolab/engine_core/error.hpp>
#include <pianolab/music_io/files.hpp>
#include <pianolab/music_io/wav.hpp>
#include <pianolab/music_io/wav_file.hpp>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <system_error>
#include <utility>

namespace pianolab::music_io {

using engine_core::Error;

/// The open file: where it's going, and JUCE's writer, which owns the output stream.
struct WavFileWriter::Impl {
    std::filesystem::path path;    ///< the final name
    std::filesystem::path partial; ///< the name while it's being written
    std::unique_ptr<juce::AudioFormatWriter> writer;
    std::size_t written = 0; ///< samples written so far
};

namespace {

/// `path` as a JUCE file (JUCE wants an absolute path).
juce::File juceFile(const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(path, error);
    const std::u8string text = (error ? path : absolute).u8string();
    return {juce::String::fromUTF8(reinterpret_cast<const char*>(text.data()),
                                   static_cast<int>(text.size()))};
}

} // namespace

std::expected<WavFileWriter, Error> WavFileWriter::create(const std::filesystem::path& path,
                                                          double sampleRate) {
    auto impl = std::make_unique<Impl>();
    impl->path = path;
    impl->partial = path;
    impl->partial += ".partial";

    const juce::File file = juceFile(impl->partial);
    (void)file.deleteFile(); // a leftover from an earlier failed recording
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(file);
    if (!static_cast<juce::FileOutputStream&>(*stream).openedOk()) {
        return std::unexpected(Error{std::format("can't create {}", displayPath(impl->partial))});
    }
    const auto options =
        juce::AudioFormatWriterOptions{}
            .withSampleRate(sampleRate)
            .withNumChannels(1)
            .withBitsPerSample(32)
            .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    juce::WavAudioFormat format;
    impl->writer = format.createWriterFor(stream, options);
    if (impl->writer == nullptr) {
        stream.reset();
        (void)file.deleteFile();
        return std::unexpected(Error{
            std::format("can't write a WAV file at {} Hz", engine_core::formatValue(sampleRate))});
    }
    return WavFileWriter(std::move(impl));
}

WavFileWriter::WavFileWriter(std::unique_ptr<Impl> impl) noexcept : impl(std::move(impl)) {}
WavFileWriter::WavFileWriter(WavFileWriter&& other) noexcept = default;
WavFileWriter& WavFileWriter::operator=(WavFileWriter&& other) noexcept = default;

WavFileWriter::~WavFileWriter() {
    try {
        [[maybe_unused]] const auto finished = finish(); // nobody is left to tell
    } catch (...) { // NOLINT(bugprone-empty-catch): a destructor must not throw
    }
}

std::expected<void, Error> WavFileWriter::append(std::span<const float> samples) {
    if (!impl || !impl->writer) {
        return std::unexpected(Error{"the WAV file is already finished"});
    }
    if (samples.size() > maxWavSamples - impl->written) {
        return std::unexpected(Error{
            std::format("{} is full (a WAV file must stay under 4 GiB)", displayPath(impl->path))});
    }
    // The writer takes an int count, so large pieces go in chunks.
    constexpr std::size_t chunk = 65'536;
    for (std::size_t start = 0; start < samples.size(); start += chunk) {
        const auto part = samples.subspan(start, std::min(chunk, samples.size() - start));
        const std::array<const float*, 1> channels{part.data()};
        if (!impl->writer->writeFromFloatArrays(channels.data(), 1,
                                                static_cast<int>(part.size()))) {
            return std::unexpected(
                Error{std::format("can't write {}", displayPath(impl->partial))});
        }
        impl->written += part.size();
    }
    return {};
}

std::size_t WavFileWriter::samplesWritten() const noexcept {
    return impl ? impl->written : 0;
}

std::expected<void, Error> WavFileWriter::finish() {
    if (!impl || !impl->writer) {
        return {};
    }
    // A recording can't be made again, so on failure the partial file is kept, not deleted:
    // whatever reached the disk may still be recoverable.
    const bool flushed = impl->writer->flush();
    impl->writer.reset(); // completes the header and closes the file
    if (!flushed) {
        return std::unexpected(
            Error{std::format("can't finish writing {}; what was written is in {}",
                              displayPath(impl->path), displayPath(impl->partial))});
    }
    std::error_code error;
    std::filesystem::rename(impl->partial, impl->path, error);
    if (error) {
        return std::unexpected(
            Error{std::format("can't move {} to {}: {}", displayPath(impl->partial),
                              displayPath(impl->path), error.message())});
    }
    return {};
}

} // namespace pianolab::music_io
