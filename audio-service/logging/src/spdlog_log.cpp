// The only file that talks to spdlog: it implements our Log interface (log.hpp) with it.

#include <pianolab/engine_core/error.hpp>
#include <pianolab/logging/log.hpp>

#include <spdlog/logger.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace pianolab::logging {

using engine_core::Error;

namespace {

/// Each line: time, level (coloured on the console), message.
constexpr const char* linePattern = "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v";

/// spdlog's level for one of ours.
spdlog::level::level_enum toSpdlog(LogLevel level) noexcept {
    switch (level) {
    case LogLevel::Debug:
        return spdlog::level::debug;
    case LogLevel::Info:
        return spdlog::level::info;
    case LogLevel::Warning:
        return spdlog::level::warn;
    case LogLevel::Error:
        return spdlog::level::err;
    }
    return spdlog::level::err;
}

/// `path` as spdlog takes file names: UTF-16 on Windows (so any user name in the path works),
/// UTF-8 elsewhere.
spdlog::filename_t fileName(const std::filesystem::path& path) {
#ifdef SPDLOG_WCHAR_FILENAMES
    return path.wstring();
#else
    const std::u8string name = path.u8string();
    return {name.begin(), name.end()};
#endif
}

/// A coloured stderr sink letting through `level` and above.
spdlog::sink_ptr consoleSink(LogLevel level) {
    auto sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    sink->set_level(toSpdlog(level));
    return sink;
}

/// A `Log` writing to `sinks`, flushing after every message.
Log logTo(std::vector<spdlog::sink_ptr> sinks) {
    auto logger = std::make_shared<spdlog::logger>("piano-lab", sinks.begin(), sinks.end());
    logger->set_pattern(linePattern);
    logger->set_level(spdlog::level::trace); // each sink filters for itself
    logger->flush_on(spdlog::level::trace);
    return Log{.write = [logger = std::move(logger)](LogLevel level, std::string_view message) {
        logger->log(toSpdlog(level), "{}", message);
    }};
}

} // namespace

std::expected<Log, Error> makeStandardLog(const LogSettings& settings) {
    std::vector<spdlog::sink_ptr> sinks{consoleSink(settings.consoleLevel)};
    if (!settings.file.empty()) {
        const std::u8string name = settings.file.u8string();
        const std::string shown(name.begin(), name.end());
        std::error_code ignored; // a missing folder is reported below, when opening fails
        std::filesystem::create_directories(settings.file.parent_path(), ignored);
        try {
            auto file = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                fileName(settings.file), settings.maxFileBytes, settings.keptFiles);
            file->set_level(toSpdlog(settings.fileLevel));
            sinks.push_back(std::move(file));
        } catch (...) { // spdlog reports a file it can't open by throwing
            return std::unexpected(Error{std::format("can't open the log file {}", shown)});
        }
    }
    return logTo(std::move(sinks));
}

Log makeConsoleLog(LogLevel consoleLevel) {
    return logTo({consoleSink(consoleLevel)});
}

} // namespace pianolab::logging
