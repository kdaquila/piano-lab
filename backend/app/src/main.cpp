#include <pianolab/engine_core/keyboard.hpp>

#include <juce_core/juce_core.h>

#include <cstdio>
#include <exception>
#include <iostream>

int main() {
    try {
        std::cout << "piano-lab (placeholder)\n"
                  << "  keyboard: " << pianolab::engine_core::keyCount << " keys, "
                  << juce::SystemStats::getJUCEVersion() << '\n';
        return 0;
    } catch (const std::exception& e) {
        // C I/O here, because it never throws: an exception can't escape main.
        std::fprintf(stderr, "error: %s\n", e.what());
    } catch (...) {
        std::fputs("error: unknown exception\n", stderr);
    }
    return 1;
}
