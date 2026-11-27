#pragma once

#include <pianolab/engine_core/error.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <functional>
#include <string_view>
#include <utility>

/// The audio service's logging: one small interface that every part of the audio service writes to,
/// with a library (spdlog) doing the real work behind it. Tests and tools can hand in their own
/// `Log` instead, e.g. one that collects messages to check them.
namespace pianolab::logging {

/// How serious a message is, from least to most.
enum class LogLevel : std::uint8_t { Debug, Info, Warning, Error };

/// Where log messages go. Every sink must accept calls from several threads at once. Never call
/// it from the audio thread: writing a message may allocate, lock and do I/O.
struct Log {
    /// Writes one finished message at a level.
    std::function<void(LogLevel, std::string_view)> write;
};

/// Writes `message` at `level`, doing nothing if `log` has no sink.
inline void write(const Log& log, LogLevel level, std::string_view message) {
    if (log.write) {
        log.write(level, message);
    }
}

/// Formats a message with `std::format` and writes it at the debug level.
template <typename... Ts>
void debug(const Log& log, std::format_string<Ts...> format, Ts&&... args) {
    write(log, LogLevel::Debug, std::format(format, std::forward<Ts>(args)...));
}

/// Formats a message with `std::format` and writes it at the info level.
template <typename... Ts>
void info(const Log& log, std::format_string<Ts...> format, Ts&&... args) {
    write(log, LogLevel::Info, std::format(format, std::forward<Ts>(args)...));
}

/// Formats a message with `std::format` and writes it at the warning level.
template <typename... Ts>
void warning(const Log& log, std::format_string<Ts...> format, Ts&&... args) {
    write(log, LogLevel::Warning, std::format(format, std::forward<Ts>(args)...));
}

/// Formats a message with `std::format` and writes it at the error level.
template <typename... Ts>
void error(const Log& log, std::format_string<Ts...> format, Ts&&... args) {
    write(log, LogLevel::Error, std::format(format, std::forward<Ts>(args)...));
}

/// Where the standard log writes, and what it lets through.
struct LogSettings {
    /// The log file. Empty: no file, console only.
    std::filesystem::path file;
    /// The least serious level shown on the console (stderr).
    LogLevel consoleLevel = LogLevel::Info;
    /// The least serious level written to the file.
    LogLevel fileLevel = LogLevel::Debug;
    /// The file's size limit in bytes; when reached it is renamed (`.1.log`, `.2.log`, ...) and a
    /// new one started.
    std::size_t maxFileBytes = std::size_t{5} * 1024 * 1024;
    /// How many old files to keep besides the current one.
    std::size_t keptFiles = 3;
};

/// The standard log: a coloured console on stderr plus a rotating log file, each line stamped
/// with the time and level. Thread-safe. Every message reaches the file at once, so nothing is
/// lost if the program dies (the audio service logs a few lines a second at most). Creates the
/// file's folder if needed. Returns an `Error` if the file can't be opened; callers can fall back
/// to `makeConsoleLog`.
[[nodiscard]] std::expected<Log, engine_core::Error> makeStandardLog(const LogSettings& settings);

/// The standard log without a file: only the coloured console on stderr. Can't fail.
[[nodiscard]] Log makeConsoleLog(LogLevel consoleLevel = LogLevel::Info);

} // namespace pianolab::logging
