// The audio service's main-thread logic, driven line by line as the web server would drive it,
// with stand-ins for the devices: commands, replies, setting changes and saving, events, and the
// pipe's line splitting.

#include <pianolab/engine_core/error.hpp>
#include <pianolab/engine_core/note_event.hpp>
#include <pianolab/live/audio_core.hpp>
#include <pianolab/live/pipe_io.hpp>
#include <pianolab/live/recorder.hpp>
#include <pianolab/live/service.hpp>
#include <pianolab/logging/log.hpp>
#include <pianolab/models/piano_key.hpp>
#include <pianolab/music_io/files.hpp>
#include <pianolab/pipe/messages.hpp>
#include <pianolab/settings/settings.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace ec = pianolab::engine_core;
namespace fs = std::filesystem;
namespace lg = pianolab::logging;
namespace live = pianolab::live;
namespace pipe = pianolab::pipe;
namespace settings = pianolab::settings;

namespace {

/// What the stand-in devices were asked to do.
struct Calls {
    std::vector<ec::NoteEvent> notes;
    std::vector<pianolab::models::PianoConfig> pianos;
    std::vector<settings::Settings> restarts;
    bool recording = false;
};

/// Stand-in devices that record what they're asked into `calls`.
live::ServiceHooks fakeHooks(Calls& calls) {
    return {
        .playNote = [&calls](const ec::NoteEvent& event) { calls.notes.push_back(event); },
        .changePiano =
            [&calls](const pianolab::models::PianoConfig& piano) { calls.pianos.push_back(piano); },
        .restartDevices =
            [&calls](const settings::Settings& changed) { calls.restarts.push_back(changed); },
        .listDevices =
            [] {
                return pipe::Devices{.audio = {{.type = "ASIO", .outputs = {"Focusrite"}}},
                                     .midi = {"Digital Piano"}};
            },
        .status =
            [&calls] {
                pipe::Status status;
                status.recording = calls.recording;
                return status;
            },
        .startRecording = [&calls] -> std::expected<fs::path, ec::Error> {
            if (calls.recording) {
                return std::unexpected(ec::Error{"already recording"});
            }
            calls.recording = true;
            return fs::path("take.wav");
        },
        .stopRecording = [&calls] -> std::expected<live::RecordingSummary, ec::Error> {
            if (!calls.recording) {
                return std::unexpected(ec::Error{"nothing is being recorded"});
            }
            calls.recording = false;
            return live::RecordingSummary{.file = "take.wav", .seconds = 2.5};
        },
    };
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

/// An empty folder for one test's files, under the system's temporary folder.
fs::path scratchFolder(std::string_view name) {
    const fs::path folder = fs::temp_directory_path() / "piano-lab-service-tests" / name;
    fs::remove_all(folder);
    fs::create_directories(folder);
    return folder;
}

/// The text of the file at `path`.
std::string readText(const fs::path& path) {
    const auto bytes = pianolab::music_io::readBytes(path);
    REQUIRE(bytes);
    return {reinterpret_cast<const char*>(bytes->data()), bytes->size()};
}

/// A time to hand the service; only differences matter.
const live::Clock::time_point start{};

/// Everything one test drives: the service's state, its stand-in devices and its log.
struct Service {
    live::ServiceState state;
    Calls calls;
    live::ServiceHooks hooks = fakeHooks(calls);
    std::vector<Logged> logged;
    lg::Log log = collectingLog(logged);

    /// A service saving its settings to `file`.
    explicit Service(fs::path file)
        : state{.settings = {}, .settingsFile = std::move(file), .settingsText = {}, .saveAt = {}} {
    }
    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;
    Service(Service&&) = delete;
    Service& operator=(Service&&) = delete;
    ~Service() = default;

    /// The lines the service writes for `line`, arriving at `now`.
    std::vector<std::string> send(std::string_view line, live::Clock::time_point now = start) {
        return live::handleLine(state, hooks, log, line, now);
    }
};

} // namespace

TEST_CASE("notes from the screen are played, with no reply") {
    Service service("unused.toml");
    CHECK(service.send(R"({"jsonrpc":"2.0","method":"note.on","params":{"key":60,"velocity":0.5}})")
              .empty());
    CHECK(service.send(R"({"jsonrpc":"2.0","method":"note.off","params":{"key":60}})").empty());
    REQUIRE(service.calls.notes.size() == 2);
    const auto* on = std::get_if<ec::NoteOn>(&service.calls.notes[0]);
    REQUIRE(on != nullptr);
    CHECK(on->note == 60);
    CHECK(on->velocity == 0.5F);
    CHECK(std::holds_alternative<ec::NoteOff>(service.calls.notes[1]));
}

TEST_CASE("the settings and their schema are read by name") {
    Service service("unused.toml");
    service.state.settings.piano.sine.gain = 0.5;
    const auto values = service.send(R"({"jsonrpc":"2.0","id":1,"method":"settings.get"})");
    REQUIRE(values.size() == 1);
    CHECK(values[0].starts_with(R"({"jsonrpc":"2.0","id":1,"result":{)"));
    CHECK(values[0].contains(R"("sine.gain":0.5)"));
    CHECK(values[0].contains(R"("midi.inputs":[])"));

    const auto schema = service.send(R"({"jsonrpc":"2.0","id":2,"method":"settings.schema"})");
    REQUIRE(schema.size() == 1);
    CHECK(schema[0].contains(R"({"name":"audio.bufferSize","kind":"number","min":0,"max":8192,)"
                             R"("unit":"samples",)"));
    CHECK(schema[0].contains(R"("name":"midi.inputs","kind":"textList")"));
}

TEST_CASE("a piano setting changes the playing piano, then is saved a second later") {
    const fs::path file = scratchFolder("piano") / "settings.toml";
    Service service(file);

    const auto lines = service.send(
        R"({"jsonrpc":"2.0","id":3,"method":"settings.set","params":{"sine.gain":0.5}})");
    REQUIRE(lines.size() == 2);
    CHECK(lines[0].starts_with(R"({"jsonrpc":"2.0","id":3,"result":{)"));
    CHECK(lines[1].starts_with(R"({"jsonrpc":"2.0","method":"settings","params":{"values":{)"));
    CHECK(lines[1].contains(R"("sine.gain":0.5)"));
    REQUIRE(service.calls.pianos.size() == 1);
    CHECK(service.calls.pianos[0].sine.gain == 0.5);
    CHECK(service.calls.restarts.empty());

    live::saveIfDue(service.state, service.log, start + live::saveDelay / 2);
    CHECK_FALSE(fs::exists(file));
    live::saveIfDue(service.state, service.log, start + live::saveDelay);
    CHECK(readText(file).contains("\ngain = 0.5\n"));
    CHECK_FALSE(service.state.saveAt.has_value());
}

TEST_CASE("changes in a burst are saved once, a second after the last") {
    const fs::path file = scratchFolder("burst") / "settings.toml";
    Service service(file);
    for (int step = 1; step <= 5; ++step) {
        const auto at = start + step * live::saveDelay / 4;
        static_cast<void>(service.send(
            std::format(
                R"({{"jsonrpc":"2.0","method":"settings.set","params":{{"sine.gain":{}}}}})",
                step / 10.0),
            at));
        live::saveIfDue(service.state, service.log, at);
        CHECK_FALSE(fs::exists(file));
    }
    live::saveIfDue(service.state, service.log, start + 5 * live::saveDelay / 4 + live::saveDelay);
    CHECK(readText(file).contains("\ngain = 0.5\n"));
}

TEST_CASE("a device setting reopens the devices") {
    Service service("unused.toml");
    const auto lines = service.send(R"({"jsonrpc":"2.0","id":4,"method":"settings.set","params":)"
                                    R"({"midi.inputs":["Digital Piano"],"sine.gain":0.5}})");
    REQUIRE(lines.size() == 2);
    REQUIRE(service.calls.restarts.size() == 1);
    CHECK(service.calls.restarts[0].midi.inputs == std::vector<std::string>{"Digital Piano"});
    CHECK(service.calls.restarts[0].piano.sine.gain == 0.5); // the new devices get the new piano
    CHECK(service.calls.pianos.empty());
}

TEST_CASE("bad setting changes are rejected whole, and nothing changes") {
    Service service("unused.toml");
    const auto lines = service.send(R"({"jsonrpc":"2.0","id":5,"method":"settings.set","params":)"
                                    R"({"sine.gain":0.5,"sine.releaseT60":100,"sine.gian":1}})");
    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == R"({"jsonrpc":"2.0","id":5,"error":{"code":-32000,"message":)"
                      R"("sine.releaseT60: 100 s is outside 0.01-30 s; )"
                      R"(unknown setting 'sine.gian'"}})");
    CHECK(service.calls.pianos.empty());
    CHECK(service.state.settings.piano.sine.gain == pianolab::models::SineConfig{}.gain);
    CHECK_FALSE(service.state.saveAt.has_value());
}

TEST_CASE("setting a value it already has changes nothing") {
    Service service("unused.toml");
    const auto lines = service.send(
        R"({"jsonrpc":"2.0","id":6,"method":"settings.set","params":{"tuning.a4Hz":440}})");
    REQUIRE(lines.size() == 1); // the reply, but no settings event
    CHECK(service.calls.pianos.empty());
    CHECK_FALSE(service.state.saveAt.has_value());
}

TEST_CASE("a hand-edited settings file is kept as .bak before saving") {
    const fs::path folder = scratchFolder("backup");
    const fs::path file = folder / "settings.toml";
    REQUIRE(settings::writeStarterSettings(file));
    Service service(file);
    service.state.settingsText = settings::starterSettings();

    // Edited by hand while running.
    const std::string edited = settings::starterSettings() + "# my note\n";
    const auto* first = reinterpret_cast<const std::byte*>(edited.data());
    REQUIRE(pianolab::music_io::writeBytes(file, std::span(first, edited.size())));

    static_cast<void>(
        service.send(R"({"jsonrpc":"2.0","method":"settings.set","params":{"sine.gain":0.5}})"));
    live::saveIfDue(service.state, service.log, start, true);
    CHECK(readText(folder / "settings.toml.bak") == edited);
    CHECK(readText(file).contains("\ngain = 0.5\n"));
    REQUIRE_FALSE(service.logged.empty());
    CHECK(service.logged.front().level == lg::LogLevel::Warning);
    CHECK(service.logged.front().message.contains("had changed outside piano-lab"));
}

TEST_CASE("devices, status and recordings are answered from the devices side") {
    Service service("unused.toml");
    CHECK(service.send(R"({"jsonrpc":"2.0","id":7,"method":"devices.list"})") ==
          std::vector<std::string>{R"({"jsonrpc":"2.0","id":7,"result":{"audio":[{"type":"ASIO",)"
                                   R"("outputs":["Focusrite"]}],"midi":["Digital Piano"]}})"});
    CHECK(service.send(R"({"jsonrpc":"2.0","id":8,"method":"recording.start"})") ==
          std::vector<std::string>{R"({"jsonrpc":"2.0","id":8,"result":{"file":"take.wav"}})"});
    CHECK(service.send(R"({"jsonrpc":"2.0","id":9,"method":"recording.start"})") ==
          std::vector<std::string>{
              R"({"jsonrpc":"2.0","id":9,"error":{"code":-32000,"message":"already recording"}})"});
    CHECK(service.send(R"({"jsonrpc":"2.0","id":10,"method":"status"})")[0].contains(
        R"("recording":true)"));
    CHECK(service.send(R"({"jsonrpc":"2.0","id":11,"method":"recording.stop"})") ==
          std::vector<std::string>{
              R"({"jsonrpc":"2.0","id":11,"result":{"file":"take.wav","seconds":2.5}})"});
}

TEST_CASE("lines that aren't requests get an error reply; bad notifications are logged") {
    Service service("unused.toml");
    const auto broken = service.send("hello");
    REQUIRE(broken.size() == 1);
    CHECK(broken[0].starts_with(R"({"jsonrpc":"2.0","id":null,"error":{"code":-32700,)"));

    CHECK(service.send(R"({"jsonrpc":"2.0","method":"note.on","params":{"key":"C4"}})").empty());
    REQUIRE(service.logged.size() == 1);
    CHECK(service.logged[0].level == lg::LogLevel::Warning);
    CHECK(service.calls.notes.empty());
}

TEST_CASE("keys and level events report what the audio side published") {
    auto links = live::makeAudioLinks(1);
    live::EventState state;
    CHECK(live::liveEvents(state, *links).empty());

    links->heldKeys[0].store((std::uint64_t{1} << 39U) | (std::uint64_t{1} << 43U)); // C4, E4
    links->peak.store(0.25F);
    const auto events = live::liveEvents(state, *links);
    REQUIRE(events.size() == 2);
    CHECK(std::get<pipe::ActiveKeys>(events[0]).keys == std::vector<int>{60, 64});
    CHECK(std::get<pipe::Level>(events[1]).peak == 0.25);

    // Nothing new: one level at 0 to say it fell silent, then nothing at all.
    const auto quiet = live::liveEvents(state, *links);
    REQUIRE(quiet.size() == 1);
    CHECK(std::get<pipe::Level>(quiet[0]).peak == 0.0);
    CHECK(live::liveEvents(state, *links).empty());
}

TEST_CASE("held keys map to MIDI notes across the whole keyboard") {
    CHECK(live::heldNotes({}).empty());
    CHECK(live::heldNotes({1, std::uint64_t{1} << (87U - 64U)}) == std::vector<int>{21, 108});
}

TEST_CASE("recordings never replace an earlier one") {
    const fs::path folder = scratchFolder("recordings");
    const auto first = live::recordingPath(folder, "2026-11-27 15-04-32");
    CHECK(first.filename() == "piano-lab 2026-11-27 15-04-32.wav");
    REQUIRE(pianolab::music_io::writeBytes(first, {}));
    const auto second = live::recordingPath(folder, "2026-11-27 15-04-32");
    CHECK(second.filename() == "piano-lab 2026-11-27 15-04-32 (2).wav");
}

TEST_CASE("stdin is split into lines, whatever pieces it arrives in") {
    live::LineSplitter splitter;
    CHECK(live::splitLines(splitter, R"({"a":)").empty());
    CHECK(live::splitLines(splitter, "1}\r\n\n{\"b\":2}\n{\"c\"") ==
          std::vector<std::string>{R"({"a":1})", R"({"b":2})"});
    CHECK(live::splitLines(splitter, ":3}\n") == std::vector<std::string>{R"({"c":3})"});
}

TEST_CASE("an over-long line is cut short, so it's rejected without unbounded memory") {
    live::LineSplitter splitter;
    const std::string chunk(pipe::maxLineBytes / 2, 'x');
    for (int index = 0; index < 5; ++index) {
        CHECK(live::splitLines(splitter, chunk).empty());
    }
    CHECK(splitter.partial.size() == pipe::maxLineBytes + 1);
    const auto lines = live::splitLines(splitter, "y\n{}\n");
    REQUIRE(lines.size() == 2);
    CHECK(lines[0].size() == pipe::maxLineBytes + 1);
    CHECK(pipe::readRequest(lines[0]).error().message == "line too long");
    CHECK(lines[1] == "{}");
}
