#pragma once

#include <pianolab/logging/log.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/// The service pipe's ends in this process: lines in from stdin, lines out to stdout. The
/// messages themselves are `pianolab::pipe`'s business.
namespace pianolab::live {

/// A line being put together from what stdin delivers, in whatever pieces it comes.
struct LineSplitter {
    std::string partial; ///< the line so far, at most `pipe::maxLineBytes` + 1 bytes
};

/// Adds `chunk` to `splitter` and returns every line it completes, without its line ending
/// ("\n" or "\r\n"). Empty lines are skipped, so a person typing commands can press Enter
/// freely. A line longer than `pipe::maxLineBytes` is cut short at one byte over, so memory stays
/// bounded and `pipe::readRequest` still rejects it as too long.
[[nodiscard]] std::vector<std::string> splitLines(LineSplitter& splitter, std::string_view chunk);

/// Lines from stdin, waiting for the main thread. Shared by the reader thread and the main
/// thread; everything in it is guarded by `mutex`.
struct Inbox {
    std::mutex mutex;
    std::vector<std::string> lines; ///< complete lines, oldest first
    bool closed = false;            ///< stdin has ended (or failed): nothing more will come
    /// Called (under `mutex`) when lines arrive or stdin ends, to wake the main thread. The main
    /// thread clears it before whatever it calls goes away.
    std::function<void()> wake;
};

/// Starts a thread reading stdin into `inbox` until stdin ends. The thread is detached: it may be
/// blocked reading when the program stops, and it only ever touches `inbox`, which it shares.
/// Reads the raw handle, not the C library's `stdin`, so it never holds a lock the program's exit
/// would wait for.
void startStdinReader(const std::shared_ptr<Inbox>& inbox);

/// Takes every line waiting in `inbox`, oldest first.
[[nodiscard]] std::vector<std::string> takeLines(Inbox& inbox);

/// Sets (or, with an empty function, clears) what wakes the main thread.
void setWake(Inbox& inbox, std::function<void()> wake);

/// Whether stdin has ended.
[[nodiscard]] bool inputClosed(Inbox& inbox);

/// Writes `line` and a line ending to stdout, whole, even with several threads writing. Writes
/// the raw handle, so nothing is translated or held back. Returns false if stdout is gone (the
/// web server stopped reading), in which case the line is lost.
bool writeLine(std::string_view line);

/// `log`, also sending each message at `level` or above to stdout as a `log` event, so the web
/// server can show it.
[[nodiscard]] logging::Log withLogEvents(logging::Log log, logging::LogLevel level);

} // namespace pianolab::live
