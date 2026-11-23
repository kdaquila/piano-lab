#include <pianolab/music_io/files.hpp>
#include <pianolab/render/render.hpp>

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

/// Runs the command on its arguments (UTF-8, without the program name) and returns the exit
/// code: 0 when a WAV was written (warnings on stderr), 1 when nothing was written.
int run(std::span<const std::string_view> arguments) {
    try {
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
    } catch (const std::exception& e) {
        // C I/O here, because it never throws: an exception can't escape main.
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

/// Runs the command on arguments already converted to UTF-8.
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
