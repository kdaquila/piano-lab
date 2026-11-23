#include <pianolab/engine_core/keyboard.hpp>

#include <juce_core/juce_core.h>

#include <cstdio>
#include <exception>
#include <iostream>

/// Entry point. A placeholder until step 0.4: prints the key count and the JUCE version.
int main() {
    try {
        std::cout << "piano-lab (placeholder)\n"
                  << "  keyboard: " << pianolab::engine_core::keyCount << " keys, "
                  << juce::SystemStats::getJUCEVersion() << '\n';
        return 0;
    } catch (const std::exception& e) {
        // C I/O here, because it never throws (std::println can): an exception can't escape main.
        // NOLINTNEXTLINE(modernize-use-std-print)
        std::fprintf(stderr, "error: %s\n", e.what());
    } catch (...) {
        std::fputs("error: unknown exception\n", stderr);
    }
    return 1;
}
