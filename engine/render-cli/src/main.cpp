#include <pianolab/engine_core/keyboard.hpp>

#include <cstdio>
#include <exception>
#include <iostream>

// A placeholder until the render CLI is built in Milestone 1.
int main() {
    try {
        std::cout << "piano-lab render-cli (placeholder)\n"
                  << "  keyboard: " << pianolab::engine_core::keyCount << " keys\n";
        return 0;
    } catch (const std::exception& e) {
        // C I/O here, because it never throws: an exception can't escape main.
        std::fprintf(stderr, "error: %s\n", e.what());
    } catch (...) {
        std::fputs("error: unknown exception\n", stderr);
    }
    return 1;
}
