#include <pianolab/music_io/files.hpp>
#include <pianolab/music_io/wav.hpp>
#include <pianolab/music_io/wav_file.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace mio = pianolab::music_io;

namespace {

/// The sample rate the tests write at, in hertz.
constexpr double sampleRate = 48'000.0;

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

/// A ramp of `count` samples, so every sample differs.
std::vector<float> ramp(std::size_t count) {
    std::vector<float> samples(count);
    for (std::size_t index = 0; index < count; ++index) {
        samples[index] = static_cast<float>(index) / static_cast<float>(count) - 0.5F;
    }
    return samples;
}

} // namespace

TEST_CASE("a WAV written in pieces is the same file as one encoded at once") {
    const TempFolder folder("piano-lab-wav-file-pieces");
    const auto path = folder.path / "take.wav";
    const auto samples = ramp(1000);

    auto writer = mio::WavFileWriter::create(path, sampleRate);
    REQUIRE(writer);
    const std::span<const float> all(samples);
    REQUIRE(writer->append(all.first(300)));
    REQUIRE(writer->append(all.subspan(300, 1)));
    REQUIRE(writer->append(all.subspan(301)));
    CHECK(writer->samplesWritten() == samples.size());
    CHECK_FALSE(std::filesystem::exists(path)); // still being written under its .partial name
    REQUIRE(writer->finish());

    const auto written = mio::readBytes(path);
    const auto expected = mio::encodeWav(samples, sampleRate);
    REQUIRE(written);
    REQUIRE(expected);
    CHECK(*written == *expected);
    auto partial = path;
    partial += ".partial";
    CHECK_FALSE(std::filesystem::exists(partial));
}

TEST_CASE("a WAV writer that is dropped still finishes its file") {
    const TempFolder folder("piano-lab-wav-file-dropped");
    const auto path = folder.path / "take.wav";
    {
        auto writer = mio::WavFileWriter::create(path, sampleRate);
        REQUIRE(writer);
        REQUIRE(writer->append(ramp(10)));
    }
    const auto written = mio::readBytes(path);
    const auto expected = mio::encodeWav(ramp(10), sampleRate);
    REQUIRE(written);
    REQUIRE(expected);
    CHECK(*written == *expected);
}

TEST_CASE("finishing twice is harmless, and appending after finishing is an error") {
    const TempFolder folder("piano-lab-wav-file-twice");
    auto writer = mio::WavFileWriter::create(folder.path / "take.wav", sampleRate);
    REQUIRE(writer);
    REQUIRE(writer->finish());
    CHECK(writer->finish());
    CHECK_FALSE(writer->append(ramp(4)));
}

TEST_CASE("a WAV file in a folder that doesn't exist can't be created") {
    const TempFolder folder("piano-lab-wav-file-missing");
    const auto writer =
        mio::WavFileWriter::create(folder.path / "no-such-folder" / "take.wav", sampleRate);
    CHECK_FALSE(writer);
}
