// The service pipe's messages: every command reads as it should, anything else becomes the
// right JSON-RPC error (never a crash), and replies and events are written as JSON-RPC 2.0.

#include <pianolab/pipe/messages.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <string>
#include <variant>
#include <vector>

namespace pipe = pianolab::pipe;

namespace {

/// Reads `line`, failing the test if it isn't a valid request.
pipe::Request read(const std::string& line) {
    const auto request = pipe::readRequest(line);
    INFO(line);
    REQUIRE(request.has_value());
    return *request;
}

/// Reads `line`, failing the test if it is a valid request.
pipe::RpcError rejected(const std::string& line) {
    const auto request = pipe::readRequest(line);
    INFO(line);
    REQUIRE_FALSE(request.has_value());
    return request.error();
}

} // namespace

TEST_CASE("note commands read as notifications, with their parameters") {
    const auto on =
        read(R"({"jsonrpc":"2.0","method":"note.on","params":{"key":60,"velocity":0.8}})");
    CHECK_FALSE(on.id.has_value());
    const auto* noteOn = std::get_if<pipe::NoteOn>(&on.command);
    REQUIRE(noteOn != nullptr);
    CHECK(noteOn->key == 60);
    CHECK(noteOn->velocity == 0.8);

    const auto off = read(R"({"jsonrpc":"2.0","method":"note.off","params":{"key":60}})");
    REQUIRE(std::holds_alternative<pipe::NoteOff>(off.command));
    CHECK(std::get<pipe::NoteOff>(off.command).key == 60);
}

TEST_CASE("requests keep their id, and methods without parameters need none") {
    const auto status = read(R"({"jsonrpc":"2.0","id":7,"method":"status"})");
    CHECK(status.id == 7);
    CHECK(std::holds_alternative<pipe::GetStatus>(status.command));

    CHECK(std::holds_alternative<pipe::GetSettings>(
        read(R"({"jsonrpc":"2.0","id":1,"method":"settings.get","params":{}})").command));
    CHECK(std::holds_alternative<pipe::GetSchema>(
        read(R"({"jsonrpc":"2.0","id":2,"method":"settings.schema"})").command));
    CHECK(std::holds_alternative<pipe::ListDevices>(
        read(R"({"jsonrpc":"2.0","id":3,"method":"devices.list"})").command));
    CHECK(std::holds_alternative<pipe::StartRecording>(
        read(R"({"jsonrpc":"2.0","id":4,"method":"recording.start"})").command));
    CHECK(std::holds_alternative<pipe::StopRecording>(
        read(R"({"jsonrpc":"2.0","id":5,"method":"recording.stop"})").command));
}

TEST_CASE("settings.set reads numbers, texts and lists of texts by name") {
    const auto request =
        read(R"({"jsonrpc":"2.0","id":9,"method":"settings.set","params":)"
             R"({"sine.gain":0.5,"audio.type":"ASIO","midi.inputs":["Digital Piano","Pads"]}})");
    const auto* set = std::get_if<pipe::SetSettings>(&request.command);
    REQUIRE(set != nullptr);
    REQUIRE(set->values.size() == 3);
    CHECK(std::get<double>(set->values.at("sine.gain")) == 0.5);
    CHECK(std::get<std::string>(set->values.at("audio.type")) == "ASIO");
    CHECK(std::get<std::vector<std::string>>(set->values.at("midi.inputs")) ==
          std::vector<std::string>{"Digital Piano", "Pads"});
}

TEST_CASE("text that isn't JSON is a parse error") {
    for (const std::string& line : {std::string("not json"), std::string("{"), std::string(""),
                                    std::string("\xff\xfe\x00garbage", 10)}) {
        CHECK(rejected(line).code == pipe::error_code::parseError);
    }
}

TEST_CASE("JSON that isn't a JSON-RPC 2.0 request is an invalid request") {
    CHECK(rejected(R"([1,2,3])").code == pipe::error_code::invalidRequest);
    CHECK(rejected(R"({"jsonrpc":"2.0"})").code == pipe::error_code::invalidRequest);
    CHECK(rejected(R"({"method":"status"})").code == pipe::error_code::invalidRequest);
    CHECK(rejected(R"({"jsonrpc":"2.0","method":"status","extra":1})").code ==
          pipe::error_code::invalidRequest);

    const auto oldVersion = rejected(R"({"jsonrpc":"1.0","id":3,"method":"status"})");
    CHECK(oldVersion.code == pipe::error_code::invalidRequest);
    CHECK(oldVersion.id == 3);

    // We only ever send whole-number ids, so anything else is a mistake.
    const auto textId = rejected(R"({"jsonrpc":"2.0","id":"a","method":"status"})");
    CHECK(textId.code == pipe::error_code::invalidRequest);
    CHECK_FALSE(textId.id.has_value());
}

TEST_CASE("errors keep the request's id when it can be read") {
    const auto unknown = rejected(R"({"jsonrpc":"2.0","id":12,"method":"self.destruct"})");
    CHECK(unknown.code == pipe::error_code::methodNotFound);
    CHECK(unknown.id == 12);
    CHECK(unknown.message.contains("self.destruct"));

    const auto broken = rejected(R"({"jsonrpc":"2.0","id":13,"method":"status","x":)");
    CHECK(broken.id == 13);
}

TEST_CASE("wrong parameters are invalid params") {
    const std::array lines{
        R"({"jsonrpc":"2.0","method":"note.on"})",                     // none
        R"({"jsonrpc":"2.0","method":"note.on","params":{"key":60}})", // one missing
        R"({"jsonrpc":"2.0","method":"note.on","params":{"key":"sixty","velocity":1}})",
        R"({"jsonrpc":"2.0","method":"note.off","params":{"key":60,"extra":true}})",
        R"({"jsonrpc":"2.0","id":4,"method":"status","params":{"verbose":true}})",
        R"({"jsonrpc":"2.0","id":5,"method":"settings.set","params":{"sine.gain":true}})",
        R"({"jsonrpc":"2.0","id":6,"method":"settings.set","params":[0.5]})",
    };
    for (const char* line : lines) {
        CHECK(rejected(line).code == pipe::error_code::invalidParams);
    }
}

TEST_CASE("anything after the message is rejected") {
    CHECK_FALSE(pipe::readRequest(R"({"jsonrpc":"2.0","method":"status"} trailing)").has_value());
    CHECK_FALSE(
        pipe::readRequest(R"({"jsonrpc":"2.0","method":"status"}{"jsonrpc":"2.0"})").has_value());
}

TEST_CASE("over-long lines are rejected without being read") {
    const std::string line = R"({"jsonrpc":"2.0","method":"status","pad":")" +
                             std::string(pipe::maxLineBytes, 'x') + R"("})";
    CHECK(rejected(line).message == "line too long");
}

TEST_CASE("replies are JSON-RPC 2.0 results and errors") {
    CHECK(pipe::resultLine(7, std::monostate{}) == R"({"jsonrpc":"2.0","id":7,"result":null})");
    CHECK(pipe::resultLine(8, pipe::SettingValues{{"sine.gain", 0.5}}) ==
          R"({"jsonrpc":"2.0","id":8,"result":{"sine.gain":0.5}})");
    CHECK(pipe::resultLine(9, pipe::RecordingStopped{.file = "take.wav", .seconds = 2.5}) ==
          R"({"jsonrpc":"2.0","id":9,"result":{"file":"take.wav","seconds":2.5}})");

    CHECK(pipe::errorLine({.id = 3, .code = pipe::error_code::failed, .message = "nope"}) ==
          R"({"jsonrpc":"2.0","id":3,"error":{"code":-32000,"message":"nope"}})");
    CHECK(pipe::errorLine({.id = {}, .code = pipe::error_code::parseError, .message = "bad"}) ==
          R"({"jsonrpc":"2.0","id":null,"error":{"code":-32700,"message":"bad"}})");
}

TEST_CASE("the settings schema leaves out what a setting doesn't have") {
    const std::vector<pipe::SettingInfo> schema{
        {.name = "sine.gain",
         .kind = "number",
         .min = 0.0,
         .max = 1.0,
         .unit = "",
         .description = "Peak amplitude",
         .defaultValue = 0.25,
         .integer = false,
         .restartsDevices = false},
        {.name = "audio.output",
         .kind = "text",
         .min = {},
         .max = {},
         .unit = "",
         .description = "Output device",
         .defaultValue = std::string(),
         .integer = false,
         .restartsDevices = true},
    };
    CHECK(pipe::resultLine(1, schema) ==
          R"({"jsonrpc":"2.0","id":1,"result":[)"
          R"({"name":"sine.gain","kind":"number","min":0,"max":1,"unit":"",)"
          R"("description":"Peak amplitude","defaultValue":0.25,"integer":false,)"
          R"("restartsDevices":false},)"
          R"({"name":"audio.output","kind":"text","unit":"","description":"Output device",)"
          R"("defaultValue":"","integer":false,"restartsDevices":true}]})");
}

TEST_CASE("events are JSON-RPC 2.0 notifications named after the event") {
    CHECK(pipe::eventLine(pipe::Ready{.version = "0.1.0"}) ==
          R"({"jsonrpc":"2.0","method":"ready","params":{"version":"0.1.0"}})");
    CHECK(
        pipe::eventLine(pipe::LogLine{.level = "warning", .message = R"(say "hi")"}) ==
        R"({"jsonrpc":"2.0","method":"log","params":{"level":"warning","message":"say \"hi\""}})");
    CHECK(pipe::eventLine(pipe::ActiveKeys{.keys = {60, 64, 67}}) ==
          R"({"jsonrpc":"2.0","method":"keys","params":{"keys":[60,64,67]}})");
    CHECK(pipe::eventLine(pipe::Level{.peak = 0.25}) ==
          R"({"jsonrpc":"2.0","method":"level","params":{"peak":0.25}})");
    CHECK(pipe::eventLine(pipe::SettingsChanged{
              .values = {{"midi.inputs", std::vector<std::string>{"Pads"}}}}) ==
          R"({"jsonrpc":"2.0","method":"settings","params":{"values":{"midi.inputs":["Pads"]}}})");
}

TEST_CASE("a written event or reply is one line") {
    const auto line = pipe::eventLine(pipe::LogLine{.level = "info", .message = "two\nlines"});
    CHECK_FALSE(line.contains('\n'));
}
