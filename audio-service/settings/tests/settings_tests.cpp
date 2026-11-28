#include <pianolab/engine_core/error.hpp>
#include <pianolab/models/piano_key.hpp>
#include <pianolab/music_io/files.hpp>
#include <pianolab/settings/settings.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace fs = std::filesystem;
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

/// Settings read from `text`, which must be valid TOML.
settings::Loaded parse(std::string_view text) {
    auto loaded = settings::parseSettings(text, "settings.toml");
    REQUIRE(loaded);
    return *loaded;
}

/// `text` with every commented-out setting (a line like "# name = value") uncommented.
std::string uncommentSettings(const std::string& text) {
    std::istringstream lines(text);
    std::string result;
    std::string line;
    while (std::getline(lines, line)) {
        const auto equals = line.find(" = ");
        const bool isSetting =
            line.starts_with("# ") && equals != std::string::npos && line.find(' ', 2) == equals;
        result += (isSetting ? line.substr(2) : line) + '\n';
    }
    return result;
}

/// An empty folder for one test's files, under the system's temporary folder.
fs::path scratchFolder(std::string_view name) {
    const fs::path folder = fs::temp_directory_path() / "piano-lab-settings-tests" / name;
    fs::remove_all(folder);
    fs::create_directories(folder);
    return folder;
}

/// Writes `text` to `path`.
void writeText(const fs::path& path, std::string_view text) {
    const auto* first = reinterpret_cast<const std::byte*>(text.data());
    REQUIRE(pianolab::music_io::writeBytes(path, std::span(first, text.size())));
}

} // namespace

TEST_CASE("an empty file gives the built-in settings") {
    const auto loaded = parse("");
    CHECK(loaded.warnings.empty());
    CHECK(loaded.settings.piano.sine.gain == pianolab::models::PianoConfig{}.sine.gain);
    CHECK(loaded.settings.audio.type.empty());
    CHECK(loaded.settings.audio.sampleRate == 48'000.0);
    CHECK(loaded.settings.midi.inputs.empty());
}

TEST_CASE("settings merge over the built-ins") {
    const auto loaded = parse(R"(
# a comment
[tuning]
a4Hz = 442

[sine]
releaseT60 = 1.5

[render]
maxBlock = 256

[audio]
type = "ASIO"
output = "Focusrite USB ASIO"
bufferSize = 128

[midi]
inputs = ["Digital Piano", "Pads"]
)");
    CHECK(loaded.warnings.empty());
    const pianolab::models::PianoConfig builtIn;
    CHECK(loaded.settings.piano.tuning.a4Hz == 442.0);
    CHECK(loaded.settings.piano.sine.releaseT60 == 1.5);
    CHECK(loaded.settings.piano.sine.gain == builtIn.sine.gain);
    CHECK(loaded.settings.piano.render.maxBlock == 256);
    CHECK(loaded.settings.piano.render.sampleRate == builtIn.render.sampleRate);
    CHECK(loaded.settings.audio.type == "ASIO");
    CHECK(loaded.settings.audio.output == "Focusrite USB ASIO");
    CHECK(loaded.settings.audio.sampleRate == 48'000.0);
    CHECK(loaded.settings.audio.bufferSize == 128);
    CHECK(loaded.settings.midi.inputs == std::vector<std::string>{"Digital Piano", "Pads"});
}

TEST_CASE("dotted keys and inline tables work like sections") {
    const auto loaded = parse("tuning.a4Hz = 432.5\nsine = { gain = 0.5 }\n");
    CHECK(loaded.warnings.empty());
    CHECK(loaded.settings.piano.tuning.a4Hz == 432.5);
    CHECK(loaded.settings.piano.sine.gain == 0.5);
}

TEST_CASE("unknown settings and sections are reported in line order and ignored") {
    const auto loaded = parse(R"([sine]
gian = 0.5
[piano]
volume = 3
[render]
sampleRate = 44100
)");
    CHECK(messages(loaded.warnings) ==
          std::vector<std::string>{
              "settings.toml:2: unknown setting 'sine.gian'; ignored",
              "settings.toml:3: unknown section or setting 'piano'; ignored",
              "settings.toml:6: render.sampleRate can't be set here: the audio device decides it "
              "(ask for one with audio.sampleRate), and 'piano-lab render --sample-rate' offline; "
              "ignored",
          });
    CHECK(loaded.settings.piano.render.sampleRate ==
          pianolab::models::PianoConfig{}.render.sampleRate);
}

TEST_CASE("a value of the wrong kind keeps the built-in value") {
    const auto loaded = parse(R"([sine]
gain = "loud"
[render]
maxBlock = -4
[audio]
output = 3
bufferSize = 64.5
[midi]
inputs = [
  "Digital Piano",
  2,
]
)");
    const auto piano = messages(loaded.warnings);
    REQUIRE(piano.size() == 2);
    CHECK(piano[0] ==
          "settings.toml:2: sine.gain: expected a number, found text; using the built-in value");
    CHECK(piano[1] == "settings.toml:4: render.maxBlock: expected a whole number, 0 or more, found "
                      "a negative whole number; using the built-in value");
    const auto devices = messages(loaded.deviceWarnings);
    REQUIRE(devices.size() == 3);
    CHECK(devices[0] == "settings.toml:6: audio.output: expected text in quotes, found a whole "
                        "number; using the built-in value");
    CHECK(devices[1] == "settings.toml:7: audio.bufferSize: expected a whole number, 0 or more, "
                        "found a number; using the built-in value");
    CHECK(devices[2] == R"(settings.toml:11: midi.inputs: expected a list of names in quotes, )"
                        R"(like ["A", "B"], but entry 2 is a whole number; using the built-in )"
                        R"(value)");
    const pianolab::models::PianoConfig builtIn;
    CHECK(loaded.settings.piano.sine.gain == builtIn.sine.gain);
    CHECK(loaded.settings.piano.render.maxBlock == builtIn.render.maxBlock);
    CHECK(loaded.settings.audio.output.empty());
    CHECK(loaded.settings.audio.bufferSize == 0);
    CHECK(loaded.settings.midi.inputs.empty());
}

TEST_CASE("a section that isn't a table is reported and ignored") {
    const auto loaded = parse("sine = 3\nmidi = 4\n");
    CHECK(messages(loaded.warnings) ==
          std::vector<std::string>{
              "settings.toml:1: sine: expected a section ([sine]), found a whole number; ignored"});
    CHECK(messages(loaded.deviceWarnings) ==
          std::vector<std::string>{
              "settings.toml:2: midi: expected a section ([midi]), found a whole number; ignored"});
}

TEST_CASE("problems with the device sections are kept apart from the piano's") {
    const auto loaded = parse("[audio]\nvolume = 2\n[tuning]\npitch = 1\n[extra]\n");
    CHECK(messages(loaded.deviceWarnings) ==
          std::vector<std::string>{"settings.toml:2: unknown setting 'audio.volume'; ignored"});
    CHECK(messages(loaded.warnings) ==
          std::vector<std::string>{"settings.toml:4: unknown setting 'tuning.pitch'; ignored",
                                   "settings.toml:5: unknown section or setting 'extra'; ignored"});
}

TEST_CASE("text that isn't TOML is an error with its line and column") {
    const auto loaded = settings::parseSettings("[sine]\ngain = = 0.5\n", "settings.toml");
    REQUIRE_FALSE(loaded);
    CHECK(loaded.error().message.starts_with("settings.toml:2:"));
}

TEST_CASE("out-of-range values are left for validation, which names the file's setting") {
    const auto loaded = parse("[sine]\nattackT60 = -0.1\n[audio]\nsampleRate = 1\n");
    CHECK(loaded.warnings.empty());
    const auto piano = pianolab::models::validate(loaded.settings.piano);
    CHECK(messages(piano.errors) ==
          std::vector<std::string>{"sine.attackT60: -0.1 s is outside 0.0005-1 s; using the "
                                   "default 0.005 s"});
    const auto audio = settings::validate(loaded.settings.audio);
    CHECK(messages(audio.errors) ==
          std::vector<std::string>{"audio.sampleRate: 1 Hz is outside 8000-384000 Hz; using the "
                                   "default 48000 Hz"});
    CHECK(audio.config->sampleRate == 48'000.0);
}

TEST_CASE("the starter file sets nothing as written") {
    const auto loaded = parse(settings::starterSettings());
    CHECK(loaded.warnings.empty());
    CHECK(loaded.settings.piano.tuning.a4Hz == pianolab::models::PianoConfig{}.tuning.a4Hz);
}

TEST_CASE("the starter file, uncommented, is exactly the built-in settings") {
    const std::string starter = settings::starterSettings();
    const std::string uncommented = uncommentSettings(starter);
    REQUIRE(uncommented != starter);
    CHECK(uncommented.contains("\na4Hz = 440\n"));
    CHECK(uncommented.contains("\nsilenceThreshold = 1e-05\n"));
    CHECK(uncommented.contains("\ntype = \"\"\n"));
    CHECK(uncommented.contains("\ninputs = []\n"));

    const auto loaded = parse(uncommented);
    CHECK(messages(loaded.warnings).empty());
    CHECK(messages(loaded.deviceWarnings).empty());
    const pianolab::models::PianoConfig builtIn;
    CHECK(loaded.settings.piano.render.maxBlock == builtIn.render.maxBlock);
    CHECK(loaded.settings.piano.tuning.a4Hz == builtIn.tuning.a4Hz);
    CHECK(loaded.settings.piano.sine.gain == builtIn.sine.gain);
    CHECK(loaded.settings.piano.sine.attackT60 == builtIn.sine.attackT60);
    CHECK(loaded.settings.piano.sine.releaseT60 == builtIn.sine.releaseT60);
    CHECK(loaded.settings.piano.sine.silenceThreshold == builtIn.sine.silenceThreshold);
    const settings::AudioSettings audio;
    CHECK(loaded.settings.audio.type == audio.type);
    CHECK(loaded.settings.audio.output == audio.output);
    CHECK(loaded.settings.audio.sampleRate == audio.sampleRate);
    CHECK(loaded.settings.audio.bufferSize == audio.bufferSize);
    CHECK(loaded.settings.midi.inputs.empty());
}

TEST_CASE("the starter file describes each setting with its range") {
    const std::string starter = settings::starterSettings();
    CHECK(starter.contains("# Pitch of A4; 440 Hz is standard concert pitch (380-500 Hz)\n"
                           "# a4Hz = 440\n"));
    CHECK(starter.contains("\n[audio]\n"));
    CHECK_FALSE(starter.contains("sampleRate = 48000\n\n# Longest"));
}

TEST_CASE("settings files are read and written") {
    const fs::path folder = scratchFolder("files");
    const fs::path file = folder / "settings.toml";

    CHECK_FALSE(settings::loadSettings(file));

    REQUIRE(settings::writeStarterSettings(file));
    const auto starter = settings::loadSettings(file);
    REQUIRE(starter);
    CHECK(starter->warnings.empty());
    CHECK(starter->deviceWarnings.empty());

    writeText(file, "\xEF\xBB\xBF[tuning]\na4Hz = 415\nnonsense = 1\n");
    const auto loaded = settings::loadSettings(file);
    REQUIRE(loaded);
    CHECK(loaded->settings.piano.tuning.a4Hz == 415.0);
    REQUIRE(loaded->warnings.size() == 1);
    CHECK(loaded->warnings.front().message ==
          pianolab::music_io::displayPath(file) + ":3: unknown setting 'tuning.nonsense'; ignored");
}

TEST_CASE("every setting is described, in the file's order") {
    const auto infos = settings::describeSettings();
    REQUIRE(infos.size() == 11);
    CHECK(infos.front().name == "audio.type");
    CHECK(infos.back().name == "render.maxBlock");

    const auto find = [&](std::string_view name) {
        const auto info = std::ranges::find(infos, name, &settings::SettingInfo::name);
        REQUIRE(info != infos.end());
        return *info;
    };
    const auto gain = find("sine.gain");
    CHECK(gain.kind == settings::Kind::Number);
    CHECK(gain.min == 0.0);
    CHECK(gain.max == 1.0);
    CHECK(std::get<double>(gain.defaultValue) == pianolab::models::SineConfig{}.gain);
    CHECK_FALSE(gain.integer);
    CHECK_FALSE(gain.restartsDevices);

    const auto bufferSize = find("audio.bufferSize");
    CHECK(bufferSize.integer);
    CHECK(bufferSize.unit == "samples");
    CHECK(bufferSize.restartsDevices);

    const auto inputs = find("midi.inputs");
    CHECK(inputs.kind == settings::Kind::TextList);
    CHECK_FALSE(inputs.min.has_value());
    CHECK(inputs.restartsDevices);
    CHECK(find("audio.output").kind == settings::Kind::Text);
}

TEST_CASE("values are read by name") {
    settings::Settings current;
    current.piano.tuning.a4Hz = 442.0;
    current.audio.bufferSize = 128;
    current.midi.inputs = {"Pads"};
    const auto values = settings::valuesOf(current);
    CHECK(values.size() == settings::describeSettings().size());
    CHECK(std::get<double>(values.at("tuning.a4Hz")) == 442.0);
    CHECK(std::get<double>(values.at("audio.bufferSize")) == 128.0);
    CHECK(std::get<std::string>(values.at("audio.type")).empty());
    CHECK(std::get<std::vector<std::string>>(values.at("midi.inputs")) ==
          std::vector<std::string>{"Pads"});
}

TEST_CASE("valid changes are applied by name") {
    const auto changed = settings::withChanges(
        {}, {{"sine.gain", 0.5},
             {"render.maxBlock", 256.0},
             {"audio.type", std::string("ASIO")},
             {"midi.inputs", std::vector<std::string>{"Digital Piano", "Pads"}}});
    REQUIRE(changed);
    CHECK(changed->piano.sine.gain == 0.5);
    CHECK(changed->piano.render.maxBlock == 256);
    CHECK(changed->audio.type == "ASIO");
    CHECK(changed->midi.inputs == std::vector<std::string>{"Digital Piano", "Pads"});
    CHECK(changed->piano.tuning.a4Hz == pianolab::models::PianoConfig{}.tuning.a4Hz);
}

TEST_CASE("bad changes are rejected together, each with its reason") {
    const auto changed = settings::withChanges({}, {{"sine.gain", 2.0},
                                                    {"sine.gian", 0.5},
                                                    {"tuning.a4Hz", std::string("high")},
                                                    {"audio.sampleRate", 1.0},
                                                    {"audio.bufferSize", 64.5},
                                                    {"render.maxBlock", 0.0},
                                                    {"render.sampleRate", 44'100.0},
                                                    {"audio.output", 3.0},
                                                    {"midi.inputs", std::string("Pads")}});
    REQUIRE_FALSE(changed);
    CHECK(messages(changed.error()) ==
          std::vector<std::string>{
              "audio.output: expected text, found a number",
              "audio.sampleRate: 1 Hz is outside 8000-384000 Hz",
              "audio.bufferSize: expected a whole number, 0 or more, found 64.5",
              "midi.inputs: expected a list of names, found text",
              "tuning.a4Hz: expected a number, found text",
              "sine.gain: 2 is outside 0-1",
              "render.maxBlock: 0 samples is outside 1-8192 samples",
              "unknown setting 'render.sampleRate'",
              "unknown setting 'sine.gian'",
          });
}

TEST_CASE("numbers that can't be valid are rejected, not converted") {
    for (const double bad : {-1.0, 1e300, std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity()}) {
        INFO(bad);
        CHECK_FALSE(settings::withChanges({}, {{"render.maxBlock", bad}}));
        CHECK_FALSE(settings::withChanges({}, {{"sine.gain", bad}}));
    }
    const auto huge = settings::withChanges({}, {{"audio.bufferSize", 1e20}});
    REQUIRE_FALSE(huge);
    CHECK(messages(huge.error()) ==
          std::vector<std::string>{"audio.bufferSize: 1e+20 samples is outside 0-8192 samples"});
}

TEST_CASE("the file sets what differs from the built-ins, and reads back the same") {
    settings::Settings current;
    current.piano.sine.gain = 0.5;
    current.piano.tuning.a4Hz = 442.0;
    current.audio.type = R"(My "ASIO" \ device)";
    current.midi.inputs = {"Digital Piano", "Pads"};
    const std::string text = settings::settingsText(current);
    CHECK(text.contains("\ngain = 0.5\n"));
    CHECK(text.contains("\na4Hz = 442\n"));
    CHECK(text.contains("\n# releaseT60 = 0.25\n"));
    CHECK(text.contains("\ninputs = [\"Digital Piano\", \"Pads\"]\n"));

    const auto loaded = parse(text);
    CHECK(loaded.warnings.empty());
    CHECK(loaded.deviceWarnings.empty());
    CHECK(settings::valuesOf(loaded.settings) == settings::valuesOf(current));
    CHECK(settings::settingsText(loaded.settings) == text);
}

TEST_CASE("saving replaces the file, keeping a copy only of one piano-lab didn't write") {
    const fs::path folder = scratchFolder("saving");
    const fs::path file = folder / "settings.toml";
    const fs::path backup = folder / "settings.toml.bak";
    settings::Settings current;

    // No file yet: nothing to keep.
    const auto first = settings::saveSettings(file, current, std::nullopt);
    REQUIRE(first);
    CHECK_FALSE(first->backup.has_value());
    CHECK_FALSE(fs::exists(backup));

    // Its own file, unchanged since: replaced without a copy.
    current.piano.sine.gain = 0.5;
    const auto second = settings::saveSettings(file, current, first->text);
    REQUIRE(second);
    CHECK_FALSE(second->backup.has_value());
    const auto reloaded = settings::loadSettings(file);
    REQUIRE(reloaded);
    CHECK(reloaded->settings.piano.sine.gain == 0.5);
    CHECK(reloaded->text == second->text);

    // Edited by hand since: copied first, exactly as it was.
    const std::string edited = second->text + "# my note\n";
    writeText(file, edited);
    current.piano.sine.gain = 0.75;
    const auto third = settings::saveSettings(file, current, second->text);
    REQUIRE(third);
    REQUIRE(third->backup == backup);
    const auto kept = pianolab::music_io::readBytes(backup);
    REQUIRE(kept);
    CHECK(std::string(reinterpret_cast<const char*>(kept->data()), kept->size()) == edited);

    // A value changed by hand in piano-lab's own format still counts as an edit.
    fs::remove(backup);
    std::string retuned = third->text;
    const auto at = retuned.find("\ngain = 0.75\n");
    REQUIRE(at != std::string::npos);
    retuned.replace(at, 13, "\ngain = 0.8\n");
    writeText(file, retuned);
    const auto fourth = settings::saveSettings(file, current, third->text);
    REQUIRE(fourth);
    CHECK(fourth->backup == backup);

    // A hand-written file read at startup: copied, since its comment would be lost.
    writeText(file, "[sine]\ngain = 0.5 # quieter\n");
    const auto handWritten = settings::loadSettings(file);
    REQUIRE(handWritten);
    fs::remove(backup);
    const auto fifth = settings::saveSettings(file, handWritten->settings, handWritten->text);
    REQUIRE(fifth);
    CHECK(fifth->backup == backup);
}
