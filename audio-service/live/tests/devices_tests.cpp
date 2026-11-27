#include <pianolab/engine_core/error.hpp>
#include <pianolab/live/devices.hpp>
#include <pianolab/settings/settings.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace live = pianolab::live;
namespace settings = pianolab::settings;

namespace {

/// The messages of `errors`, for comparing in one check.
std::vector<std::string> messages(const std::vector<pianolab::engine_core::Error>& errors) {
    std::vector<std::string> result;
    result.reserve(errors.size());
    for (const auto& error : errors) {
        result.push_back(error.message);
    }
    return result;
}

/// Device types like a Windows PC with an audio interface: shared mode with two outputs (the
/// second is the default), exclusive mode with none, and ASIO with one.
std::vector<live::AudioDeviceType> windowsTypes() {
    return {
        {.name = "Windows Audio", .outputs = {"Speakers", "Headphones"}, .defaultOutput = 1},
        {.name = "Windows Audio (Exclusive Mode)", .outputs = {}, .defaultOutput = 0},
        {.name = "ASIO", .outputs = {"Focusrite USB ASIO"}, .defaultOutput = 0},
    };
}

/// Audio settings naming device type `type` and output `output` (empty: the default).
settings::AudioSettings audio(std::string type, std::string output = "") {
    settings::AudioSettings result;
    result.type = std::move(type);
    result.output = std::move(output);
    return result;
}

} // namespace

TEST_CASE("no audio settings choose the first type's default output") {
    const auto choice = live::chooseAudioOutput({}, windowsTypes());
    CHECK(choice.type == "Windows Audio");
    CHECK(choice.output == "Headphones");
    CHECK(choice.warnings.empty());
}

TEST_CASE("a named type and output are chosen") {
    const auto asio = live::chooseAudioOutput(audio("ASIO"), windowsTypes());
    CHECK(asio.type == "ASIO");
    CHECK(asio.output == "Focusrite USB ASIO");
    CHECK(asio.warnings.empty());

    const auto speakers = live::chooseAudioOutput(audio("", "Speakers"), windowsTypes());
    CHECK(speakers.type == "Windows Audio");
    CHECK(speakers.output == "Speakers");
    CHECK(speakers.warnings.empty());
}

TEST_CASE("an output named without a type picks the type that has it") {
    const auto choice = live::chooseAudioOutput(audio("", "Focusrite USB ASIO"), windowsTypes());
    CHECK(choice.type == "ASIO");
    CHECK(choice.output == "Focusrite USB ASIO");
    CHECK(choice.warnings.empty());

    const auto missing = live::chooseAudioOutput(audio("", "Monitors"), windowsTypes());
    CHECK(missing.type == "Windows Audio");
    CHECK(missing.output == "Headphones");
    CHECK(messages(missing.warnings) ==
          std::vector<std::string>{R"(audio.output: "Monitors" isn't available (outputs: )"
                                   R"("Speakers", "Headphones", "Focusrite USB ASIO"); using )"
                                   R"("Headphones")"});
}

TEST_CASE("a missing type or output is reported and the default used") {
    const auto noType = live::chooseAudioOutput(audio("asio"), windowsTypes());
    CHECK(noType.type == "Windows Audio");
    CHECK(noType.output == "Headphones");
    CHECK(messages(noType.warnings) ==
          std::vector<std::string>{"audio.type: \"asio\" isn't available (types with outputs: "
                                   "\"Windows Audio\", \"ASIO\"); using \"Windows Audio\""});

    const auto empty =
        live::chooseAudioOutput(audio("Windows Audio (Exclusive Mode)"), windowsTypes());
    CHECK(empty.type == "Windows Audio");
    CHECK(messages(empty.warnings) ==
          std::vector<std::string>{"audio.type: \"Windows Audio (Exclusive Mode)\" has no outputs "
                                   "(types with outputs: \"Windows Audio\", \"ASIO\"); using "
                                   "\"Windows Audio\""});

    const auto noOutput = live::chooseAudioOutput(audio("ASIO", "Speakers"), windowsTypes());
    CHECK(noOutput.type == "ASIO");
    CHECK(noOutput.output == "Focusrite USB ASIO");
    CHECK(messages(noOutput.warnings) ==
          std::vector<std::string>{"audio.output: \"Speakers\" isn't available as a \"ASIO\" "
                                   "output (outputs: \"Focusrite USB ASIO\"); using \"Focusrite "
                                   "USB ASIO\""});
}

TEST_CASE("with no outputs anywhere, nothing is chosen") {
    const std::vector<live::AudioDeviceType> none{{.name = "ASIO", .outputs = {}}};
    const auto choice = live::chooseAudioOutput(audio("ASIO"), none);
    CHECK(choice.type.empty());
    CHECK(choice.output.empty());
    CHECK(live::chooseAudioOutput({}, {}).type.empty());
}

TEST_CASE("an out-of-range default output index falls back to the first output") {
    const std::vector<live::AudioDeviceType> types{
        {.name = "ALSA", .outputs = {"hw:0"}, .defaultOutput = 5}};
    CHECK(live::chooseAudioOutput({}, types).output == "hw:0");
}

TEST_CASE("no MIDI inputs listed opens them all") {
    const auto choice = live::chooseMidiInputs({}, {"Digital Piano", "Pads"});
    CHECK(choice.inputs == std::vector<std::size_t>{0, 1});
    CHECK(choice.warnings.empty());
}

TEST_CASE("listed MIDI inputs open, in the system's order, same names included") {
    const auto choice = live::chooseMidiInputs({.inputs = {"Pads", "Digital Piano"}},
                                               {"Digital Piano", "Pads", "Pads", "Other"});
    CHECK(choice.inputs == std::vector<std::size_t>{0, 1, 2});
    CHECK(choice.warnings.empty());
}

TEST_CASE("a listed MIDI input that isn't there is reported") {
    const auto some =
        live::chooseMidiInputs({.inputs = {"Digital Piano", "Organ"}}, {"Digital Piano", "Pads"});
    CHECK(some.inputs == std::vector<std::size_t>{0});
    CHECK(messages(some.warnings) ==
          std::vector<std::string>{
              "midi.inputs: \"Organ\" isn't connected (inputs: \"Digital Piano\", \"Pads\")"});

    const auto none = live::chooseMidiInputs({.inputs = {"Organ"}}, {"Digital Piano", "Pads"});
    CHECK(none.inputs == std::vector<std::size_t>{0, 1});
    CHECK(messages(none.warnings) ==
          std::vector<std::string>{
              "midi.inputs: \"Organ\" isn't connected (inputs: \"Digital Piano\", \"Pads\")",
              "midi.inputs: none of the listed inputs is connected; opening every input"});

    const auto twice = live::chooseMidiInputs({.inputs = {"Organ", "Organ"}}, {"Pads"});
    CHECK(twice.inputs == std::vector<std::size_t>{0});
    CHECK(messages(twice.warnings) ==
          std::vector<std::string>{
              R"(midi.inputs: "Organ" isn't connected (inputs: "Pads"))",
              "midi.inputs: none of the listed inputs is connected; opening every input"});

    const auto nothingPlugged = live::chooseMidiInputs({.inputs = {"Organ"}}, {});
    CHECK(nothingPlugged.inputs.empty());
    CHECK(messages(nothingPlugged.warnings) ==
          std::vector<std::string>{"midi.inputs: \"Organ\" isn't connected (inputs: none)"});
}
