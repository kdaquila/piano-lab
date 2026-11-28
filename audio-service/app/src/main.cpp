// piano-lab-audio: the audio service's program. Its subcommands are its roles:
//   piano-lab-audio run           run the piano live, driven over the service pipe (stdin/stdout)
//   piano-lab-audio render ...    render a music file to WAV, fast and offline

#include <pianolab/live/pipe_io.hpp>
#include <pianolab/live/server.hpp>
#include <pianolab/logging/log.hpp>
#include <pianolab/music_io/files.hpp>
#include <pianolab/render/render.hpp>
#include <pianolab/settings/settings.hpp>

#include <juce_core/juce_core.h>

#include <atomic>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <format>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

/// The program's help text.
constexpr std::string_view usage = R"(usage: piano-lab-audio <command> [options]

The audio service of piano-lab, a physically modelled piano. piano-lab's web server runs it;
running it yourself is for development and debugging.

commands:
  run             run the piano until Ctrl+C or until its input ends: plays the audio output
                  from the MIDI inputs chosen in settings.toml (by default, the default output
                  from every input), and takes service pipe commands (JSON-RPC 2.0, one per
                  line) on stdin, answering and reporting on stdout
  render <input> [-o <output.wav>] [--sample-rate <hz>] [--settings <file>]
                  render a MIDI or MusicXML file to a WAV file, with no audio device

Settings live in settings.toml, next to piano-lab-audio; 'run' writes one listing every
setting if there isn't one, and saves changes made through the pipe. Logs go to the logs
folder next to it, recordings to the recordings folder.

Run 'piano-lab-audio render --help' for more on rendering.
)";

/// Set when the user asks the server to stop (Ctrl+C). Lock-free, so a signal handler may set it.
std::atomic<bool> stopRequested{false};
static_assert(std::atomic<bool>::is_always_lock_free);

/// Signal handler for Ctrl+C and termination requests: asks the server to stop.
extern "C" void requestStop(int /*signal*/) {
    stopRequested.store(true);
}

/// The folder piano-lab is installed in: the one holding the program. Its settings and logs
/// live here, so it keeps everything it writes in one place.
std::filesystem::path installFolder() {
    const juce::File program = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    const std::string text = program.getParentDirectory().getFullPathName().toStdString();
    return {std::u8string(text.begin(), text.end())};
}

/// The settings file, `settings.toml` in the install folder.
std::filesystem::path settingsFile() {
    return installFolder() / std::string(pianolab::settings::fileName);
}

/// `piano-lab-audio render <arguments>`: renders a file and returns the exit code: 0 when a WAV was
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

    const auto summary = pianolab::render::renderFile(*options, settingsFile());
    if (!summary) {
        std::cerr << "error: " << summary.error().message << '\n';
        return 1;
    }
    for (const auto& warning : summary->warnings) {
        std::cerr << "warning: " << warning.message << '\n';
    }
    const std::size_t issues = summary->warnings.size();
    const std::string settings =
        summary->settings.empty()
            ? std::string("the built-in settings")
            : "settings from " + pianolab::music_io::displayPath(summary->settings);
    std::cout << std::format("wrote {} ({:.2f} s) with {}; {} issue{} reported\n",
                             pianolab::music_io::displayPath(summary->output), summary->seconds,
                             settings, issues, issues == 1 ? "" : "s");
    return 0;
}

/// Where the audio service's log file goes: `logs/piano-lab-audio.log` in the install folder.
std::filesystem::path logFile() {
    return installFolder() / "logs" / "piano-lab-audio.log";
}

/// The settings the audio service starts with, and the settings file's text as read (or written),
/// for the `.bak` safety net when saving; nothing when the file couldn't be read.
struct StartingSettings {
    pianolab::settings::Settings settings;
    std::optional<std::string> text;
};

/// The audio service's settings: `file` merged over the built-ins. When there's no `file`, writes
/// the starter file there and uses the built-ins. Logs every problem; never fails.
StartingSettings serverSettings(const std::filesystem::path& file,
                                const pianolab::logging::Log& log) {
    namespace settings = pianolab::settings;
    using pianolab::music_io::displayPath;
    std::error_code error;
    const bool exists = std::filesystem::exists(file, error);
    if (error) {
        // Never write the starter file over a file that might be there.
        pianolab::logging::error(log, "can't check for {}: {}; using the built-in settings",
                                 displayPath(file), error.message());
        return {};
    }
    if (!exists) {
        const auto written = settings::writeStarterSettings(file);
        if (!written) {
            pianolab::logging::warning(log, "{}; using the built-in settings",
                                       written.error().message);
            return {};
        }
        pianolab::logging::info(log, "wrote a settings file listing every setting: {}",
                                displayPath(file));
        return {.settings = {}, .text = settings::starterSettings()};
    }
    auto loaded = settings::loadSettings(file);
    if (!loaded) {
        pianolab::logging::error(log, "{}; using the built-in settings", loaded.error().message);
        return {};
    }
    pianolab::logging::info(log, "settings: {}", displayPath(file));
    for (const auto& problem : loaded->warnings) {
        pianolab::logging::warning(log, "{}", problem.message);
    }
    for (const auto& problem : loaded->deviceWarnings) {
        pianolab::logging::warning(log, "{}", problem.message);
    }
    return {.settings = std::move(loaded->settings), .text = std::move(loaded->text)};
}

/// `piano-lab-audio run`: runs the piano until Ctrl+C or until stdin ends. Returns the exit
/// code, 0.
int runAudioService() {
    const auto file = logFile();
    auto standard = pianolab::logging::makeStandardLog({.file = file});
    // Everything at info level and above also reaches the web server, as `log` events.
    const pianolab::logging::Log log =
        pianolab::live::withLogEvents(standard ? *standard : pianolab::logging::makeConsoleLog(),
                                      pianolab::logging::LogLevel::Info);
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
    auto starting = serverSettings(settingsFile(), log);
    pianolab::live::runService({.settings = std::move(starting.settings),
                                .settingsFile = settingsFile(),
                                .settingsText = std::move(starting.text),
                                .recordingsFolder = installFolder() / "recordings",
                                .version = PIANOLAB_VERSION},
                               log, stopRequested);
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
        if (command == "run" && rest.empty()) {
            return runAudioService();
        }
        if (command == "run") {
            std::cerr << "error: 'run' takes no arguments\n\n" << usage;
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
