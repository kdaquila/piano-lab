#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace pianolab::engine_core {

/// Problems the engine recovers from while rendering. The engine never stops for an error: it
/// applies a fallback and records the problem here, so the backend can log it and the UI can
/// show it (see docs/coding-style.md, "Errors are recovered and reported").
enum class Problem : std::uint8_t {
    /// A block was longer than the keyboard's maxBlock. Fallback: rendered in maxBlock chunks.
    OversizedBlock,
    /// An event came before the previous one. Fallback: moved forward to the previous one's time.
    EventOutOfOrder,
    /// An event's time was past the end of the block. Fallback: applied at the end of the block.
    EventPastBlock,
    /// A note outside A0-C8. Fallback: ignored.
    NoteOutsideKeyboard,
    /// A velocity outside 0-1, or NaN. Fallback: clamped (NaN becomes 0).
    VelocityOutOfRange,
    /// A rendered sample was NaN or infinite. Fallback: replaced with silence.
    NonFiniteSample,
};

inline constexpr std::size_t problemCount = 6;

/// Counts of each problem since the keyboard was made, plus the most recent one. Fixed-size and
/// allocation-free, so it can be updated on the audio thread.
struct Diagnostics {
    std::array<std::uint64_t, problemCount> counts{};
    std::optional<Problem> last;
};

/// Records one occurrence of a problem.
void record(Diagnostics& diagnostics, Problem problem) noexcept;

/// How many times a problem has occurred.
[[nodiscard]] std::uint64_t count(const Diagnostics& diagnostics, Problem problem) noexcept;

/// How many problems of any kind have occurred.
[[nodiscard]] std::uint64_t total(const Diagnostics& diagnostics) noexcept;

/// A short, stable name for logs and the UI, e.g. "oversized-block".
[[nodiscard]] std::string_view problemName(Problem problem) noexcept;

} // namespace pianolab::engine_core
