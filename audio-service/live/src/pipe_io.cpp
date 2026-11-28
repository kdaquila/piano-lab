#include <pianolab/live/pipe_io.hpp>
#include <pianolab/logging/log.hpp>
#include <pianolab/pipe/messages.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>

#include <cerrno>
#endif

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace pianolab::live {

namespace {

/// Reads what stdin has, up to `buffer`'s size, waiting until there's something. Returns how
/// many bytes it read, or nothing when stdin has ended or failed.
std::optional<std::size_t> readStdin(std::span<char> buffer) {
#ifdef _WIN32
    DWORD count = 0;
    const DWORD size =
        static_cast<DWORD>(std::min<std::size_t>(buffer.size(), std::numeric_limits<DWORD>::max()));
    if (ReadFile(GetStdHandle(STD_INPUT_HANDLE), buffer.data(), size, &count, nullptr) == 0 ||
        count == 0) {
        return std::nullopt;
    }
    return count;
#else
    while (true) {
        const auto count = ::read(STDIN_FILENO, buffer.data(), buffer.size());
        if (count > 0) {
            return static_cast<std::size_t>(count);
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        return std::nullopt;
    }
#endif
}

/// Writes all of `bytes` to stdout. Returns false if it can't.
bool writeStdout(std::string_view bytes) {
    while (!bytes.empty()) {
#ifdef _WIN32
        DWORD written = 0;
        const DWORD size = static_cast<DWORD>(
            std::min<std::size_t>(bytes.size(), std::numeric_limits<DWORD>::max()));
        if (WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), bytes.data(), size, &written, nullptr) ==
                0 ||
            written == 0) {
            return false;
        }
        bytes.remove_prefix(written);
#else
        const auto written = ::write(STDOUT_FILENO, bytes.data(), bytes.size());
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            return false;
        }
        bytes.remove_prefix(static_cast<std::size_t>(written));
#endif
    }
    return true;
}

/// Keeps lines from different threads whole on stdout.
std::mutex& stdoutMutex() {
    static std::mutex mutex;
    return mutex;
}

/// The pipe's name for a log level.
std::string_view levelName(logging::LogLevel level) {
    switch (level) {
    case logging::LogLevel::Debug:
        return "debug";
    case logging::LogLevel::Info:
        return "info";
    case logging::LogLevel::Warning:
        return "warning";
    case logging::LogLevel::Error:
        return "error";
    }
    return "info";
}

} // namespace

std::vector<std::string> splitLines(LineSplitter& splitter, std::string_view chunk) {
    std::vector<std::string> lines;
    while (!chunk.empty()) {
        const auto end = chunk.find('\n');
        const std::string_view piece = chunk.substr(0, end);
        // Keep at most one byte over the limit: enough for the reader to know it's too long.
        const std::size_t room =
            pipe::maxLineBytes + 1 - std::min(splitter.partial.size(), pipe::maxLineBytes + 1);
        splitter.partial.append(piece.substr(0, room));
        if (end == std::string_view::npos) {
            break;
        }
        std::string line = std::exchange(splitter.partial, {});
        if (line.ends_with('\r')) {
            line.pop_back();
        }
        if (!line.empty()) {
            lines.push_back(std::move(line));
        }
        chunk.remove_prefix(end + 1);
    }
    return lines;
}

void startStdinReader(const std::shared_ptr<Inbox>& inbox) {
    std::thread([inbox] {
        LineSplitter splitter;
        std::array<char, 4'096> buffer{};
        while (const auto count = readStdin(buffer)) {
            auto lines = splitLines(splitter, std::string_view(buffer.data(), *count));
            if (lines.empty()) {
                continue;
            }
            const std::scoped_lock lock(inbox->mutex);
            for (auto& line : lines) {
                inbox->lines.push_back(std::move(line));
            }
            if (inbox->wake) {
                inbox->wake();
            }
        }
        const std::scoped_lock lock(inbox->mutex);
        inbox->closed = true;
        if (inbox->wake) {
            inbox->wake();
        }
    }).detach();
}

std::vector<std::string> takeLines(Inbox& inbox) {
    const std::scoped_lock lock(inbox.mutex);
    return std::exchange(inbox.lines, {});
}

void setWake(Inbox& inbox, std::function<void()> wake) {
    const std::scoped_lock lock(inbox.mutex);
    inbox.wake = std::move(wake);
}

bool inputClosed(Inbox& inbox) {
    const std::scoped_lock lock(inbox.mutex);
    return inbox.closed;
}

bool writeLine(std::string_view line) {
    std::string text(line);
    text += '\n';
    const std::scoped_lock lock(stdoutMutex());
    return writeStdout(text);
}

logging::Log withLogEvents(logging::Log log, logging::LogLevel level) {
    return {.write = [log = std::move(log), level](logging::LogLevel messageLevel,
                                                   std::string_view message) {
        logging::write(log, messageLevel, message);
        if (messageLevel >= level) {
            static_cast<void>(writeLine(pipe::eventLine(pipe::LogLine{
                .level = std::string(levelName(messageLevel)), .message = std::string(message)})));
        }
    }};
}

} // namespace pianolab::live
