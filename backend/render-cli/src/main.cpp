#include <pianolab/music_io/files.hpp>
#include <pianolab/render/render.hpp>

#include <cstddef>
#include <cstdio>
#include <exception>
#include <format>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

/// Entry point of `piano-lab-render`. Exit code 0: a WAV was written (warnings on stderr);
/// 1: nothing was written.
int main(int argc, char** argv) {
    try {
        const std::span<char*> raw(argv, static_cast<std::size_t>(argc));
        const auto afterProgramName = raw.subspan(raw.empty() ? 0 : 1);
        const std::vector<std::string_view> arguments(afterProgramName.begin(),
                                                      afterProgramName.end());

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
