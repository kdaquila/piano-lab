#include <pianolab/engine_core/note_event.hpp>
#include <pianolab/engine_core/performance.hpp>
#include <pianolab/engine_core/render_config.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <limits>
#include <variant>

namespace {

namespace ec = pianolab::engine_core;

/// MIDI note number of middle C (C4).
constexpr int middleC = 60;

/// A press of `note` at `seconds` with `velocity`.
ec::PerformanceEvent press(double seconds, int note, float velocity) {
    return {.seconds = seconds, .event = ec::NoteOn{.note = note, .velocity = velocity}};
}

/// A release of `note` at `seconds`.
ec::PerformanceEvent lift(double seconds, int note) {
    return {.seconds = seconds, .event = ec::NoteOff{.note = note}};
}

} // namespace

TEST_CASE("a valid performance passes unchanged, apart from sorting") {
    const ec::Performance performance{{lift(1.0, middleC), press(0.0, middleC, 0.5F)}};

    const auto validated = ec::validate(performance);

    CHECK(validated.errors.empty());
    REQUIRE(validated.config->events.size() == 2);
    CHECK(validated.config->events[0].seconds == 0.0);
    CHECK(validated.config->events[1].seconds == 1.0);
    CHECK(ec::lastEventSeconds(validated.config) == 1.0);
}

TEST_CASE("at the same instant a release comes before a press, so repeated notes re-strike") {
    const ec::Performance performance{
        {press(0.0, middleC, 0.5F), press(0.5, middleC, 0.5F), lift(0.5, middleC)}};

    const auto validated = ec::validate(performance);

    REQUIRE(validated.config->events.size() == 3);
    CHECK(std::holds_alternative<ec::NoteOff>(validated.config->events[1].event));
    CHECK(std::holds_alternative<ec::NoteOn>(validated.config->events[2].event));
}

TEST_CASE("events with impossible times are dropped and reported") {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const ec::Performance performance{{press(-1.0, middleC, 0.5F), press(nan, middleC, 0.5F),
                                       press(ec::maxPerformanceSeconds + 1.0, middleC, 0.5F),
                                       press(2.0, middleC, 0.5F)}};

    const auto validated = ec::validate(performance);

    CHECK(validated.config->events.size() == 1);
    REQUIRE(validated.errors.size() == 3);
    CHECK(validated.errors[0].message == "event 1: time -1 s is outside 0-7200 s; dropped");
}

TEST_CASE("presses off the keyboard are dropped and reported; their releases silently") {
    const ec::Performance performance{{press(0.0, 10, 0.5F), lift(1.0, 10)}};

    const auto validated = ec::validate(performance);

    CHECK(validated.config->events.empty());
    REQUIRE(validated.errors.size() == 1);
    CHECK(validated.errors[0].message ==
          "event 1 at 0 s: note 10 is outside the keyboard (21-108); dropped");
}

TEST_CASE("velocities are clamped, and NaN velocities dropped") {
    const ec::Performance performance{
        {press(0.0, middleC, 1.5F), press(0.0, 62, std::numeric_limits<float>::quiet_NaN())}};

    const auto validated = ec::validate(performance);

    REQUIRE(validated.config->events.size() == 1);
    CHECK(std::get<ec::NoteOn>(validated.config->events[0].event).velocity == 1.0F);
    REQUIRE(validated.errors.size() == 2);
    CHECK(validated.errors[0].message == "event 1 at 0 s: velocity 1.5 is outside 0-1; using 1");
    CHECK(validated.errors[1].message == "event 2 at 0 s: velocity is NaN; dropped");
}

TEST_CASE("toTimedEvents rounds times to the nearest sample") {
    const ec::Performance performance{{press(0.5, middleC, 0.5F), lift(1.00001, middleC)}};
    const auto validated = ec::validate(performance);
    const auto render = ec::validate(ec::RenderConfig{.sampleRate = 48'000.0}).config;

    const auto events = ec::toTimedEvents(validated.config, render);

    REQUIRE(events.size() == 2);
    CHECK(events[0].at == 24'000);
    CHECK(events[1].at == 48'000); // 48000.48 rounds down
}

TEST_CASE("an empty performance is valid and lasts no time") {
    const auto validated = ec::validate(ec::Performance{});

    CHECK(validated.errors.empty());
    CHECK(ec::lastEventSeconds(validated.config) == 0.0);
}

TEST_CASE("a zero-length note is pressed, then released, instead of sticking") {
    // The release is listed first, as some MIDI files do for same-tick notes.
    const ec::Performance performance{{lift(1.0, middleC), press(1.0, middleC, 0.5F)}};

    const auto validated = ec::validate(performance);

    REQUIRE(validated.config->events.size() == 2);
    CHECK(std::holds_alternative<ec::NoteOn>(validated.config->events[0].event));
    CHECK(std::holds_alternative<ec::NoteOff>(validated.config->events[1].event));
}

TEST_CASE("a repeated note with a zero-length note in between keeps every release in place") {
    // C4 held from 0, re-struck at 1 as a zero-length note, then a long note from 1 to 2.
    const ec::Performance performance{{press(0.0, middleC, 0.5F), press(1.0, middleC, 0.5F),
                                       lift(1.0, middleC), lift(1.0, middleC),
                                       press(1.0, middleC, 0.5F), lift(2.0, middleC)}};

    const auto validated = ec::validate(performance);

    // At 1 s: the release of the held note, then the presses, then the zero-length release.
    const auto& events = validated.config->events;
    REQUIRE(events.size() == 6);
    CHECK(std::holds_alternative<ec::NoteOff>(events[1].event));
    CHECK(std::holds_alternative<ec::NoteOn>(events[2].event));
    CHECK(std::holds_alternative<ec::NoteOn>(events[3].event));
    CHECK(std::holds_alternative<ec::NoteOff>(events[4].event));
}
