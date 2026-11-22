#include <pianolab/engine_core/engine_core.hpp>
#include <pianolab/models/models.hpp>

#include <cstdio>
#include <exception>
#include <iostream>

int main() {
    try {
        std::cout << "piano-lab render-cli (placeholder)\n"
                  << "  uses " << pianolab::engine_core::libraryName() << " and "
                  << pianolab::models::libraryName() << '\n';
        return 0;
    } catch (const std::exception& e) {
        // C I/O here, because it never throws: an exception can't escape main.
        std::fprintf(stderr, "error: %s\n", e.what());
    } catch (...) {
        std::fputs("error: unknown exception\n", stderr);
    }
    return 1;
}
