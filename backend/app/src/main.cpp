// piano-lab: the one program. Its subcommands are its roles:
//   piano-lab render ...      render a music file to WAV, fast and offline
//   piano-lab server start    run the piano live against audio and MIDI devices

#include <pianolab/logging/log.hpp>
#include <pianolab/music_io/files.hpp>
#include <pianolab/render/render.hpp>
#include <pianolab/server/server.hpp>

#include <juce_core/juce_core.h>

#include <atomic>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <format>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

/// The program's help text.
constexpr std::string_view usage = R"(usage: piano-lab <command> [options]

A physically modelled piano.

commands:
  render <input> [-o <output.wav>] [--sample-rate <hz>]
                  render a MIDI or MusicXML file to a WAV file, with no audio device
  server start    run the piano: plays the default audio output from every MIDI input,
                  until Ctrl+C

Run 'piano-lab render --help' for more on rendering.
)";

/// Set when the user asks the server to stop (Ctrl+C). Lock-free, so a signal handler may set it.
std::atomic<bool> stopRequested{false};
static_assert(std::atomic<bool>::is_always_lock_free);

/// Signal handler for Ctrl+C and termination requests: asks the server to stop.
extern "C" void requestStop(int /*signal*/) {
    stopRequested.store(true);
}

/// `piano-lab render <arguments>`: renders a file and returns the exit code: 0 when a WAV was
/// written (warnings on stderr), 1 when nothing was written.
int runRender(std::span<const std::string_view> arguments) {
    const auto options = pianolab::render::parseArguments(arguments);
    if (!options) {
        std::cerr << "error: " << options.error().message << "\n\n" << pianolab::render::usage;
        return 1;
    }
    if (options->help) {
        std::cout << pianolab::render::usage;
        return 0;
    }

    const auto summary = pianolab::render::renderFile(*options);
    if (!summary) {
        std::cerr << "error: " << summary.error().message << '\n';
        return 1;
    }
    for (const auto& warning : summary->warnings) {
        std::cerr << "warning: " << warning.message << '\n';
    }
    const std::size_t issues = summary->warnings.size();
    std::cout << std::format("wrote {} ({:.2f} s); {} issue{} reported\n",
                             pianolab::music_io::displayPath(summary->output), summary->seconds,
                             issues, issues == 1 ? "" : "s");
    return 0;
}

/// Where the server's log file goes: `piano-lab/logs/piano-lab.log` in the user's application
/// data folder (on Windows, `%APPDATA%`).
std::filesystem::path logFile() {
    const juce::File folder =
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
    const std::string text = folder.getFullPathName().toStdString();
    return std::filesystem::path(std::u8string(text.begin(), text.end())) / "piano-lab" / "logs" /
           "piano-lab.log";
}

/// `piano-lab server start`: runs the piano until Ctrl+C. Returns the exit code, 0.
int runServer() {
    const auto file = logFile();
    auto standard = pianolab::logging::makeStandardLog({.file = file});
    const pianolab::logging::Log log = standard ? *standard : pianolab::logging::makeConsoleLog();
    if (standard) {
        pianolab::logging::info(log, "log file: {}", pianolab::music_io::displayPath(file));
    } else {
        pianolab::logging::warning(log, "{}; logging to the console only",
                                   standard.error().message);
    }

    std::signal(SIGINT, requestStop);
    std::signal(SIGTERM, requestStop);
#ifdef SIGBREAK
    std::signal(SIGBREAK, requestStop); // Ctrl+Break, and closing the console window, on Windows
#endif
    pianolab::server::runServer(log, stopRequested);
    return 0;
}

/// Runs the command line (UTF-8, without the program name) and returns the exit code.
int run(std::span<const std::string_view> arguments) {
    try {
        if (arguments.empty()) {
            std::cerr << usage;
            return 1;
        }
        const std::string_view command = arguments.front();
        const auto rest = arguments.subspan(1);
        if (command == "-h" || command == "--help" || command == "help") {
            std::cout << usage;
            return 0;
        }
        if (command == "render") {
            return runRender(rest);
        }
        if (command == "server" && rest.size() == 1 && rest.front() == "start") {
            return runServer();
        }
        if (command == "server") {
            std::cerr << "error: 'server' needs an action: 'piano-lab server start'\n\n" << usage;
            return 1;
        }
        std::cerr << std::format("error: unknown command '{}'\n\n", command) << usage;
        return 1;
    } catch (const std::exception& e) {
        // The one catch-all at the program's edge (coding style, rule 8). C I/O here, because it
        // never throws: an exception can't escape main.
        // NOLINTNEXTLINE(modernize-use-std-print)
        std::fprintf(stderr, "error: %s\n", e.what());
    } catch (...) {
        std::fputs("error: unknown exception\n", stderr);
    }
    return 1;
}

/// The program's arguments after its name, converted to UTF-8 text.
template <typename C> std::vector<std::string> utf8Arguments(int argc, C** argv) {
    std::vector<std::string> arguments;
    for (const C* argument :
         std::span(argv, static_cast<std::size_t>(argc)).subspan(argc > 0 ? 1 : 0)) {
        const std::u8string text = std::filesystem::path(argument).u8string();
        arguments.emplace_back(text.begin(), text.end());
    }
    return arguments;
}

/// Runs the command line already converted to UTF-8.
int runUtf8(const std::vector<std::string>& arguments) {
    const std::vector<std::string_view> views(arguments.begin(), arguments.end());
    return run(views);
}

} // namespace

#ifdef _WIN32
/// Entry point on Windows: `wmain` receives the arguments as UTF-16, so file names with any
/// characters arrive intact (plain `main` would get them in the system's code page).
int wmain(int argc, wchar_t** argv) {
    try {
        return runUtf8(utf8Arguments(argc, argv));
    } catch (...) {
        std::fputs("error: couldn't read the command line\n", stderr);
        return 1;
    }
}
#else
/// Entry point: the arguments arrive as UTF-8 on Linux and macOS.
int main(int argc, char** argv) {
    try {
        return runUtf8(utf8Arguments(argc, argv));
    } catch (...) {
        std::fputs("error: couldn't read the command line\n", stderr);
        return 1;
    }
}
#endif
