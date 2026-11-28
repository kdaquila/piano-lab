#pragma once

// Counting heap allocations, for tests of the real-time rule (rendering must never allocate).
//
// count_allocations.cpp replaces the program's global operator new/delete with versions that
// count allocations while a check is running. Add that file to a test executable's sources, and
// only to executables that are about allocations: the replacement applies to the whole program.

#include <cstddef>
#include <utility>

/// Helpers shared by test executables.
namespace pianolab::testing {

/// Starts counting this thread's allocations. Checks may nest.
void beginAllocationCheck() noexcept;

/// Stops the innermost check and returns how many allocations this thread made since it began.
[[nodiscard]] std::size_t endAllocationCheck() noexcept;

/// Runs `body` and returns how many heap allocations it made on this thread.
template <typename F> [[nodiscard]] std::size_t countAllocations(F&& body) {
    beginAllocationCheck();
    std::forward<F>(body)();
    return endAllocationCheck();
}

} // namespace pianolab::testing
