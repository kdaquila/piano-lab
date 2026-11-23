#include <pianolab/engine_core/note_event.hpp>
#include <pianolab/music_io/musicxml.hpp>
#include <pianolab/music_io/reading.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <juce_core/juce_core.h>

#include <cstddef>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

using Catch::Matchers::WithinAbs;
namespace ec = pianolab::engine_core;
namespace mio = pianolab::music_io;

namespace {

/// The velocity MuseScore gives mf, which notes play at when a score has no dynamics.
constexpr double mfVelocity = 80.0 / 127.0;

/// A partwise score with one part holding `measures`.
std::string score(std::string_view measures) {
    return std::format(R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE score-partwise PUBLIC "-//Recordare//DTD MusicXML 4.0 Partwise//EN"
  "http://www.musicxml.org/dtds/partwise.dtd">
<score-partwise version="4.0">
  <part-list><score-part id="P1"><part-name>Piano</part-name></score-part></part-list>
  <part id="P1">{}</part>
</score-partwise>)",
                       measures);
}

/// A measure numbered `number` holding `content`, with one division per quarter note.
std::string measure(int number, std::string_view content) {
    return std::format(
        R"(<measure number="{}"><attributes><divisions>1</divisions></attributes>{}</measure>)",
        number, content);
}

/// A note of `step` (e.g. "C") in `octave`, lasting `quarters`, with `extra` elements inside.
std::string note(std::string_view step, int octave, int quarters, std::string_view extra = "") {
    return std::format(
        "<note>{}<pitch><step>{}</step><octave>{}</octave></pitch><duration>{}</duration></note>",
        extra, step, octave, quarters);
}

/// A rest lasting `quarters`.
std::string rest(int quarters) {
    return std::format("<note><rest/><duration>{}</duration></note>", quarters);
}

/// A played note, paired up from the reader's press and release events.
struct Played {
    int pitch = 0;
    double on = 0.0;
    double off = 0.0;
    float velocity = 0.0F;
};

/// The notes in a reading. The reader emits each note's press followed by its release.
std::vector<Played> played(const mio::Reading& reading) {
    std::vector<Played> notes;
    const auto& events = reading.performance.events;
    REQUIRE(events.size() % 2 == 0);
    for (std::size_t index = 0; index < events.size(); index += 2) {
        const auto* on = std::get_if<ec::NoteOn>(&events[index].event);
        REQUIRE(on != nullptr);
        REQUIRE(std::holds_alternative<ec::NoteOff>(events[index + 1].event));
        notes.push_back({.pitch = on->note,
                         .on = events[index].seconds,
                         .off = events[index + 1].seconds,
                         .velocity = on->velocity});
    }
    return notes;
}

/// Reads `xml`, requiring success.
mio::Reading read(const std::string& xml) {
    auto reading = mio::readMusicXml(xml);
    REQUIRE(reading);
    return *reading;
}

} // namespace

TEST_CASE("a quarter note plays for half a second at the default tempo, at mf") {
    const auto reading = read(score(measure(1, note("C", 4, 1))));

    CHECK(reading.warnings.empty());
    const auto notes = played(reading);
    REQUIRE(notes.size() == 1);
    CHECK(notes[0].pitch == 60);
    CHECK_THAT(notes[0].on, WithinAbs(0.0, 1e-9));
    CHECK_THAT(notes[0].off, WithinAbs(0.5, 1e-9));
    CHECK_THAT(notes[0].velocity, WithinAbs(mfVelocity, 1e-6));
}

TEST_CASE("pitches follow step, alter and octave") {
    const auto reading = read(
        score(measure(1, "<note><pitch><step>F</step><alter>1</alter><octave>3</octave></pitch>"
                         "<duration>1</duration></note>"
                         "<note><pitch><step>B</step><alter>-1</alter><octave>2</octave></pitch>"
                         "<duration>1</duration></note>" +
                             note("A", 0, 1) + note("C", 8, 1))));

    const auto notes = played(reading);
    REQUIRE(notes.size() == 4);
    CHECK(notes[0].pitch == 54);  // F#3
    CHECK(notes[1].pitch == 46);  // Bb2
    CHECK(notes[2].pitch == 21);  // A0
    CHECK(notes[3].pitch == 108); // C8
}

TEST_CASE("rests and measures move time on") {
    const auto reading =
        read(score(measure(1, rest(3) + note("D", 4, 1)) + measure(2, note("E", 4, 2))));

    const auto notes = played(reading);
    REQUIRE(notes.size() == 2);
    CHECK_THAT(notes[0].on, WithinAbs(1.5, 1e-9));
    CHECK_THAT(notes[1].on, WithinAbs(2.0, 1e-9));
    CHECK_THAT(notes[1].off, WithinAbs(3.0, 1e-9));
}

TEST_CASE("chord notes start together") {
    const auto reading = read(score(measure(1, note("C", 4, 2) + note("E", 4, 2, "<chord/>") +
                                                   note("G", 4, 2, "<chord/>") + note("C", 5, 1))));

    const auto notes = played(reading);
    REQUIRE(notes.size() == 4);
    CHECK_THAT(notes[1].on, WithinAbs(0.0, 1e-9));
    CHECK_THAT(notes[2].on, WithinAbs(0.0, 1e-9));
    CHECK_THAT(notes[3].on, WithinAbs(1.0, 1e-9));
}

TEST_CASE("tied notes play as one long note, across a barline") {
    const auto reading = read(score(measure(1, note("C", 4, 2, R"(<tie type="start"/>)")) +
                                    measure(2, note("C", 4, 2, R"(<tie type="stop"/>)"))));

    const auto notes = played(reading);
    REQUIRE(notes.size() == 1);
    CHECK_THAT(notes[0].off, WithinAbs(2.0, 1e-9));
}

TEST_CASE("backup lets a second voice play alongside the first") {
    const auto reading =
        read(score(measure(1, note("C", 5, 4) + "<backup><duration>4</duration></backup>" +
                                  note("C", 3, 2) + note("G", 3, 2)) +
                   measure(2, note("D", 5, 1))));

    const auto notes = played(reading);
    REQUIRE(notes.size() == 4);
    CHECK_THAT(notes[1].on, WithinAbs(0.0, 1e-9));
    CHECK_THAT(notes[2].on, WithinAbs(1.0, 1e-9));
    CHECK_THAT(notes[3].on, WithinAbs(2.0, 1e-9)); // the next measure starts after both voices
}

TEST_CASE("divisions can be finer than a quarter note") {
    const auto reading = read(score(
        R"(<measure number="1"><attributes><divisions>4</divisions></attributes>)" +
        std::string("<note><pitch><step>C</step><octave>4</octave></pitch><duration>1</duration>"
                    "</note><note><pitch><step>D</step><octave>4</octave></pitch>"
                    "<duration>3</duration></note></measure>")));

    const auto notes = played(reading);
    REQUIRE(notes.size() == 2);
    CHECK_THAT(notes[0].off, WithinAbs(0.125, 1e-9)); // a sixteenth at 120
    CHECK_THAT(notes[1].off, WithinAbs(0.5, 1e-9));
}

TEST_CASE("tempo changes from <sound tempo> apply from where they appear") {
    const auto reading =
        read(score(measure(1, R"(<direction><direction-type><words>Adagio</words></direction-type>)"
                              R"(<sound tempo="60"/></direction>)" +
                                  note("C", 4, 2)) +
                   measure(2, R"(<sound tempo="120"/>)" + note("D", 4, 2))));

    const auto notes = played(reading);
    REQUIRE(notes.size() == 2);
    CHECK_THAT(notes[0].off, WithinAbs(2.0, 1e-9)); // two quarters at 60
    CHECK_THAT(notes[1].off, WithinAbs(3.0, 1e-9)); // then two at 120
}

TEST_CASE("a metronome mark sets the tempo when there's no <sound tempo>") {
    const auto reading = read(
        score(measure(1, "<direction><direction-type><metronome><beat-unit>quarter</beat-unit>"
                         "<beat-unit-dot/><per-minute>40</per-minute></metronome></direction-type>"
                         "</direction>" +
                             note("C", 4, 3))));

    // A dotted quarter at 40 is 60 quarters per minute.
    CHECK_THAT(played(reading)[0].off, WithinAbs(3.0, 1e-9));
}

TEST_CASE("dynamics: markings, <sound dynamics> and a note's own dynamics") {
    const auto reading = read(score(measure(
        1, R"(<direction><direction-type><dynamics><p/></dynamics></direction-type></direction>)" +
               note("C", 4, 1) +
               R"(<direction><direction-type><dynamics><f/></dynamics></direction-type>)"
               R"(<sound dynamics="100"/></direction>)" +
               note("D", 4, 1) +
               R"(<note dynamics="50"><pitch><step>E</step><octave>4</octave></pitch>)"
               "<duration>1</duration></note>")));

    const auto notes = played(reading);
    REQUIRE(notes.size() == 3);
    CHECK_THAT(notes[0].velocity, WithinAbs(49.0 / 127.0, 1e-6)); // p, as MuseScore plays it
    CHECK_THAT(notes[1].velocity, WithinAbs(90.0 / 127.0, 1e-6)); // 100% of forte wins
    CHECK_THAT(notes[2].velocity, WithinAbs(45.0 / 127.0, 1e-6)); // the note's own 50%
}

TEST_CASE("a dynamic in the upper staff applies to the lower staff after a backup") {
    const auto reading = read(score(measure(
        1, R"(<direction><direction-type><dynamics><pp/></dynamics></direction-type></direction>)" +
               note("C", 5, 2) + "<backup><duration>2</duration></backup>" + note("C", 3, 2))));

    const auto notes = played(reading);
    REQUIRE(notes.size() == 2);
    CHECK_THAT(notes[1].velocity, WithinAbs(33.0 / 127.0, 1e-6));
}

TEST_CASE("unsupported notation is reported once per kind, with where it first appears") {
    const auto reading = read(score(
        measure(1, note("D", 4, 0, "<grace/>") + note("C", 4, 1)) +
        measure(2, note("D", 4, 0, "<grace/>") + note("C", 4, 1) +
                       R"(<barline location="right"><repeat direction="backward"/></barline>)")));

    CHECK(played(reading).size() == 2);
    REQUIRE(reading.warnings.size() == 2);
    CHECK(reading.warnings[0].message ==
          "grace notes are not supported yet; skipped (2 times, first in part P1, measure 1)");
    CHECK(reading.warnings[1].message == "repeats, endings and jumps are not supported yet; "
                                         "played straight through (in part P1, measure 2)");
}

TEST_CASE("visual-only notation is ignored silently") {
    const auto reading = read(score(measure(
        1, "<print new-system=\"yes\"/>" + note("C", 4, 1, "") +
               "<note><pitch><step>D</step><octave>4</octave></pitch><duration>1</duration>"
               "<stem>up</stem><beam number=\"1\">begin</beam><notations><slur type=\"start\"/>"
               "<technical><fingering>2</fingering></technical></notations>"
               "<lyric><text>la</text></lyric></note>")));

    CHECK(reading.warnings.empty());
    CHECK(played(reading).size() == 2);
}

TEST_CASE("a score without notes reads with a warning") {
    const auto reading = read(score(measure(1, rest(4))));

    CHECK(reading.performance.events.empty());
    REQUIRE(reading.warnings.size() == 1);
    CHECK(reading.warnings[0].message == "the score has no notes");
}

TEST_CASE("input that isn't a partwise MusicXML score is an error") {
    const auto notXml = mio::readMusicXml("this is not < xml");
    REQUIRE_FALSE(notXml);
    CHECK(notXml.error().message.starts_with("not valid XML"));

    const auto timewise = mio::readMusicXml("<score-timewise/>");
    REQUIRE_FALSE(timewise);
    CHECK(timewise.error().message.starts_with("timewise MusicXML isn't supported"));

    const auto other = mio::readMusicXml("<html/>");
    REQUIRE_FALSE(other);
    CHECK(other.error().message == "not a MusicXML score (the root element is <html>)");
}

TEST_CASE("a compressed .mxl file reads like the score inside it") {
    const std::string container =
        R"(<?xml version="1.0" encoding="UTF-8"?><container><rootfiles>)"
        R"(<rootfile full-path="music/piece.musicxml"/></rootfiles></container>)";
    const std::string xml = score(measure(1, note("A", 4, 1)));
    juce::ZipFile::Builder builder;
    builder.addEntry(
        std::make_unique<juce::MemoryInputStream>(container.data(), container.size(), false), 9,
        "META-INF/container.xml", juce::Time());
    builder.addEntry(std::make_unique<juce::MemoryInputStream>(xml.data(), xml.size(), false), 9,
                     "music/piece.musicxml", juce::Time());
    juce::MemoryOutputStream zipped;
    REQUIRE(builder.writeToStream(zipped, nullptr));
    const auto* first = static_cast<const std::byte*>(zipped.getData());
    const std::vector<std::byte> bytes(first, first + zipped.getDataSize());

    const auto reading = mio::readCompressedMusicXml(bytes);

    REQUIRE(reading);
    const auto notes = played(*reading);
    REQUIRE(notes.size() == 1);
    CHECK(notes[0].pitch == 69);
}

TEST_CASE("data that isn't a zip is not an .mxl file") {
    const std::vector<std::byte> bytes(16, std::byte{0});

    const auto reading = mio::readCompressedMusicXml(bytes);

    REQUIRE_FALSE(reading);
    CHECK(reading.error().message == "not a compressed MusicXML (.mxl) file");
}
