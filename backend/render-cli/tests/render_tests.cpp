#include <pianolab/music_io/files.hpp>
#include <pianolab/render/render.hpp>

#include <catch2/catch_test_macros.hpp>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
namespace mio = pianolab::music_io;
namespace render = pianolab::render;

namespace {

/// A score playing A4 for one quarter note at 120 per minute, with `extra` before the note.
std::string scoreXml(std::string_view extra = "") {
    return std::string(R"(<?xml version="1.0" encoding="UTF-8"?>
<score-partwise version="4.0">
  <part-list><score-part id="P1"><part-name>Piano</part-name></score-part></part-list>
  <part id="P1"><measure number="1"><attributes><divisions>1</divisions></attributes>)") +
           std::string(extra) +
           R"(<note><pitch><step>A</step><octave>4</octave></pitch><duration>1</duration></note>
  </measure></part>
</score-partwise>)";
}

/// An empty folder for one test's files, under the system's temporary folder.
fs::path scratchFolder(std::string_view name) {
    const fs::path folder = fs::temp_directory_path() / "piano-lab-render-tests" / name;
    fs::remove_all(folder);
    fs::create_directories(folder);
    return folder;
}

/// Writes `text` to `path`.
void writeText(const fs::path& path, std::string_view text) {
    const auto* first = reinterpret_cast<const std::byte*>(text.data());
    REQUIRE(mio::writeBytes(path, std::span(first, text.size())));
}

/// The samples of the mono WAV file at `path`.
std::vector<float> readWav(const fs::path& path) {
    const auto bytes = mio::readBytes(path);
    REQUIRE(bytes);
    juce::WavAudioFormat format;
    const std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(
        new juce::MemoryInputStream(bytes->data(), bytes->size(), false), true));
    REQUIRE(reader != nullptr);
    REQUIRE(reader->numChannels == 1);
    const auto length = static_cast<int>(reader->lengthInSamples);
    juce::AudioBuffer<float> buffer(1, length);
    REQUIRE(reader->read(&buffer, 0, length, 0, true, false));
    const float* samples = buffer.getReadPointer(0);
    return {samples, samples + length};
}

/// The arguments as a list, for `parseArguments`.
std::vector<std::string_view> args(std::initializer_list<std::string_view> list) {
    return list;
}

} // namespace

TEST_CASE("the output defaults to the input's path with .wav") {
    const auto options = render::parseArguments(args({"songs/piece.musicxml"}));

    REQUIRE(options);
    CHECK(options->input == fs::path("songs/piece.musicxml"));
    CHECK(options->output == fs::path("songs/piece.wav"));
    CHECK_FALSE(options->sampleRate);
}

TEST_CASE("options set the output and the sample rate") {
    const auto options =
        render::parseArguments(args({"--sample-rate", "44100", "in.mid", "-o", "out.wav"}));

    REQUIRE(options);
    CHECK(options->output == fs::path("out.wav"));
    CHECK(options->sampleRate == 44'100.0);
}

TEST_CASE("help needs no input") {
    const auto options = render::parseArguments(args({"--help"}));

    REQUIRE(options);
    CHECK(options->help);
}

TEST_CASE("bad arguments are errors saying what's wrong") {
    CHECK(render::parseArguments(args({})).error().message == "no input file given");
    CHECK(render::parseArguments(args({"a.mid", "--fast"})).error().message ==
          "unknown option '--fast'");
    CHECK(render::parseArguments(args({"a.mid", "-o"})).error().message == "-o needs a file name");
    CHECK(render::parseArguments(args({"a.mid", "--sample-rate", "48k"})).error().message ==
          "--sample-rate: '48k' isn't a whole number of hertz");
    CHECK(render::parseArguments(args({"a.mid", "b.mid"})).error().message ==
          "only one input file can be rendered at a time; got 'b.mid' too");
    CHECK(render::parseArguments(args({"a.wav"})).error().message ==
          "the output file would overwrite the input");
}

TEST_CASE("a score renders to a WAV that sounds, then falls silent") {
    const auto folder = scratchFolder("renders");
    writeText(folder / "a4.musicxml", scoreXml());

    const auto summary = render::renderFile({.input = folder / "a4.musicxml",
                                             .output = folder / "a4.wav",
                                             .sampleRate = std::nullopt,
                                             .help = false});

    REQUIRE(summary);
    CHECK(summary->warnings.empty());
    const auto samples = readWav(folder / "a4.wav");
    // Half a second of note, then the release tail, well before the 30 s cap.
    CHECK(summary->seconds > 0.5);
    CHECK(summary->seconds < 2.0);
    CHECK(samples.size() == static_cast<std::size_t>(std::lround(summary->seconds * 48'000.0)));
    const float peak = std::ranges::max(samples, {}, [](float s) { return std::abs(s); });
    CHECK(std::abs(peak) > 0.05F);
    CHECK(std::abs(samples.back()) < 1e-4F);
}

TEST_CASE("rendering the same file twice gives byte-identical WAVs") {
    const auto folder = scratchFolder("deterministic");
    writeText(folder / "a4.musicxml", scoreXml());

    REQUIRE(render::renderFile({.input = folder / "a4.musicxml",
                                .output = folder / "first.wav",
                                .sampleRate = std::nullopt,
                                .help = false}));
    REQUIRE(render::renderFile({.input = folder / "a4.musicxml",
                                .output = folder / "second.wav",
                                .sampleRate = std::nullopt,
                                .help = false}));

    const auto first = mio::readBytes(folder / "first.wav");
    const auto second = mio::readBytes(folder / "second.wav");
    REQUIRE(first);
    REQUIRE(second);
    CHECK(*first == *second);
}

TEST_CASE("unsupported notation is a warning, and the rest still renders") {
    const auto folder = scratchFolder("warnings");
    writeText(folder / "grace.musicxml",
              scoreXml("<note><grace/><pitch><step>B</step><octave>4</octave></pitch></note>"));

    const auto summary = render::renderFile({.input = folder / "grace.musicxml",
                                             .output = folder / "grace.wav",
                                             .sampleRate = std::nullopt,
                                             .help = false});

    REQUIRE(summary);
    REQUIRE(summary->warnings.size() == 1);
    CHECK(summary->warnings[0].message.starts_with("grace notes are not supported yet"));
    CHECK(fs::exists(folder / "grace.wav"));
}

TEST_CASE("an invalid sample rate falls back to the built-in one, with a warning") {
    const auto folder = scratchFolder("sample-rate");
    writeText(folder / "a4.musicxml", scoreXml());

    const auto summary = render::renderFile({.input = folder / "a4.musicxml",
                                             .output = folder / "a4.wav",
                                             .sampleRate = 1'000.0,
                                             .help = false});

    REQUIRE(summary);
    REQUIRE(summary->warnings.size() == 1);
    CHECK(summary->warnings[0].message.starts_with("render.sampleRate: 1000 Hz is outside"));
}

TEST_CASE("an unreadable input or unwritable output is an error, and nothing is written") {
    const auto folder = scratchFolder("errors");

    const auto missing = render::renderFile({.input = folder / "missing.mid",
                                             .output = folder / "missing.wav",
                                             .sampleRate = std::nullopt,
                                             .help = false});
    REQUIRE_FALSE(missing);
    CHECK(missing.error().message.starts_with("can't read"));
    CHECK_FALSE(fs::exists(folder / "missing.wav"));

    writeText(folder / "notes.txt", "C4 E4 G4");
    const auto unknown = render::renderFile({.input = folder / "notes.txt",
                                             .output = folder / "notes.wav",
                                             .sampleRate = std::nullopt,
                                             .help = false});
    REQUIRE_FALSE(unknown);
    CHECK(unknown.error().message.contains("unknown file type '.txt'"));

    writeText(folder / "a4.musicxml", scoreXml());
    const auto unwritable = render::renderFile({.input = folder / "a4.musicxml",
                                                .output = folder / "no-such-folder" / "a4.wav",
                                                .sampleRate = std::nullopt,
                                                .help = false});
    REQUIRE_FALSE(unwritable);
    CHECK(unwritable.error().message.starts_with("can't write"));
}
