#include <pianolab/engine_core/diagnostics.hpp>

#include <string_view>

namespace pianolab::engine_core {

void record(Diagnostics& diagnostics, std::string_view message) noexcept {
    ++diagnostics.total;
    diagnostics.last = message;
}

} // namespace pianolab::engine_core
