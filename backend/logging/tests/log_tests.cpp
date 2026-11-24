#include <pianolab/logging/log.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace lg = pianolab::logging;

namespace {

/// One message a test log received.
struct Logged {
    lg::LogLevel level;
    std::string message;
};

/// A log that collects messages into `into`, as tests elsewhere can do to check what was logged.
lg::Log collectingLog(std::vector<Logged>& into) {
    return {.write = [&into](lg::LogLevel level, std::string_view message) {
        into.push_back({.level = level, .message = std::string(message)});
    }};
}

/// The whole text file at `path`.
std::string readText(const std::filesystem::path& path) {
    std::ifstream file(path);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

} // namespace

TEST_CASE("messages are formatted and reach the log's sink with their level") {
    std::vector<Logged> logged;
    const auto log = collectingLog(logged);

    lg::debug(log, "opened {} at {} Hz", "speakers", 48000);
    lg::info(log, "ready");
    lg::warning(log, "{} problems", 3);
    lg::error(log, "braces {{}} stay braces");

    REQUIRE(logged.size() == 4);
    CHECK(logged[0].level == lg::LogLevel::Debug);
    CHECK(logged[0].message == "opened speakers at 48000 Hz");
    CHECK(logged[1].level == lg::LogLevel::Info);
    CHECK(logged[2].message == "3 problems");
    CHECK(logged[3].level == lg::LogLevel::Error);
    CHECK(logged[3].message == "braces {} stay braces");
}

TEST_CASE("a log without a sink ignores messages") {
    const lg::Log log;
    lg::info(log, "nobody hears this");
    SUCCEED();
}

TEST_CASE("the standard log writes to its file, creating the folder") {
    const auto folder = std::filesystem::temp_directory_path() / "piano-lab-log-test";
    std::filesystem::remove_all(folder);
    const auto file = folder / "logs" / "piano-lab.log";
    {
        const auto log = lg::makeStandardLog(
            {.file = file, .consoleLevel = lg::LogLevel::Error, .fileLevel = lg::LogLevel::Info});
        REQUIRE(log);
        lg::debug(*log, "too detailed for the file");
        lg::info(*log, "a message with {} in it", "{braces}");
        lg::warning(*log, "something to know");
    } // dropping the log closes the file

    const std::string text = readText(file);
    CHECK(text.contains("a message with {braces} in it"));
    CHECK(text.contains("[info]"));
    CHECK(text.contains("[warning] something to know"));
    CHECK_FALSE(text.contains("too detailed"));
    std::error_code error;
    std::filesystem::remove_all(folder, error);
}

TEST_CASE("a log file that can't be opened is an error") {
    const auto folder = std::filesystem::temp_directory_path() / "piano-lab-log-blocked";
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    // A file where the log's folder should be.
    std::ofstream(folder / "logs") << "in the way";

    const auto log = lg::makeStandardLog({.file = folder / "logs" / "piano-lab.log"});

    REQUIRE_FALSE(log);
    CHECK(log.error().message.contains("piano-lab.log"));
    std::error_code error;
    std::filesystem::remove_all(folder, error);
}
