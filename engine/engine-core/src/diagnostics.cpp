#include <pianolab/engine_core/diagnostics.hpp>

#include <cstdint>
#include <numeric>
#include <string_view>

namespace pianolab::engine_core {

void record(Diagnostics& diagnostics, Problem problem) noexcept {
    ++diagnostics.counts[static_cast<std::size_t>(problem)];
    diagnostics.last = problem;
}

std::uint64_t count(const Diagnostics& diagnostics, Problem problem) noexcept {
    return diagnostics.counts[static_cast<std::size_t>(problem)];
}

std::uint64_t total(const Diagnostics& diagnostics) noexcept {
    return std::accumulate(diagnostics.counts.begin(), diagnostics.counts.end(), std::uint64_t{0});
}

std::string_view problemName(Problem problem) noexcept {
    switch (problem) {
    case Problem::OversizedBlock:
        return "oversized-block";
    case Problem::EventOutOfOrder:
        return "event-out-of-order";
    case Problem::EventPastBlock:
        return "event-past-block";
    case Problem::NoteOutsideKeyboard:
        return "note-outside-keyboard";
    case Problem::VelocityOutOfRange:
        return "velocity-out-of-range";
    case Problem::NonFiniteSample:
        return "non-finite-sample";
    }
    return "unknown";
}

} // namespace pianolab::engine_core
