#pragma once

#include <cstdint>
#include <string_view>

namespace pianolab::engine_core {

/// What went wrong while rendering, so the backend can log it and the UI can show it.
///
/// The engine never stops for an error: it applies a fallback and records a message here. It
/// is fixed-size and allocation-free, so it can be updated on the audio thread.
struct Diagnostics {
    /// How many problems have been recorded since the keyboard was made.
    std::uint64_t total = 0;
    /// The most recent problem, or empty if there has been none.
    std::string_view last;
};

/// Records one problem. `message` must be a string literal (or otherwise live for the whole
/// program), because only a view of it is stored: building a string here would allocate.
void record(Diagnostics& diagnostics, std::string_view message) noexcept;

} // namespace pianolab::engine_core
