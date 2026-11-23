// Replaces the global allocator with one that counts allocations during checks. See
// count_allocations.hpp. Counting (instead of aborting) lets tests prove the checker really
// catches allocations, so a passing "never allocates" test means something.

#include "count_allocations.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <new>

namespace {

/// Above zero while a check is running on this thread.
thread_local std::size_t checkDepth = 0;
/// Allocations seen during checks on this thread.
thread_local std::size_t allocationCount = 0;
/// The deepest nesting of checks that's tracked; deeper checks count into the innermost one.
constexpr std::size_t maxCheckDepth = 16;
/// `allocationCount` when each running check began, outermost first. Fixed-size, so beginning a
/// check never allocates in the middle of counting.
thread_local std::array<std::size_t, maxCheckDepth> checkStarts{};

/// Counts an allocation if a check is running on this thread.
void noteAllocation() noexcept {
    if (checkDepth > 0) {
        ++allocationCount;
    }
}

/// Counts and performs an ordinary allocation. Throws std::bad_alloc when out of memory, as
/// operator new must.
void* allocate(std::size_t size) {
    noteAllocation();
    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc): this *is* the allocator
    if (void* memory = std::malloc(size == 0 ? 1 : size)) {
        return memory;
    }
    throw std::bad_alloc();
}

/// Counts and performs an over-aligned allocation (for types declared `alignas` beyond the
/// default). Throws std::bad_alloc when out of memory.
void* allocateAligned(std::size_t size, std::align_val_t alignment) {
    noteAllocation();
    const auto align = static_cast<std::size_t>(alignment);
#ifdef _MSC_VER
    void* memory = _aligned_malloc(size == 0 ? 1 : size, align);
#else
    const std::size_t rounded = ((size == 0 ? 1 : size) + align - 1) / align * align;
    void* memory = std::aligned_alloc(align, rounded);
#endif
    if (memory == nullptr) {
        throw std::bad_alloc();
    }
    return memory;
}

/// Frees memory from `allocate`.
void deallocate(void* memory) noexcept {
    std::free(memory); // NOLINT(cppcoreguidelines-no-malloc): this *is* the allocator
}

/// Frees memory from `allocateAligned`.
void deallocateAligned(void* memory) noexcept {
#ifdef _MSC_VER
    _aligned_free(memory);
#else
    std::free(memory); // NOLINT(cppcoreguidelines-no-malloc): this *is* the allocator
#endif
}

} // namespace

namespace pianolab::testing {

void beginAllocationCheck() noexcept {
    if (checkDepth < maxCheckDepth) {
        checkStarts[checkDepth] = allocationCount; // in range: checked above
    }
    ++checkDepth;
}

std::size_t endAllocationCheck() noexcept {
    if (checkDepth == 0) {
        return 0;
    }
    --checkDepth;
    const std::size_t start = checkStarts[std::min(checkDepth, maxCheckDepth - 1)];
    return allocationCount - start;
}

} // namespace pianolab::testing

/// The replaceable global allocation functions ([new.delete] in the C++ standard), one doc
/// comment for the whole set: every `new` counts and allocates through the helpers above, and
/// every `delete` frees through the matching helper.
// NOLINTBEGIN(readability-inconsistent-declaration-parameter-name)
void* operator new(std::size_t size) {
    return allocate(size);
}
void* operator new[](std::size_t size) {
    return allocate(size);
}
void* operator new(std::size_t size, std::align_val_t alignment) {
    return allocateAligned(size, alignment);
}
void* operator new[](std::size_t size, std::align_val_t alignment) {
    return allocateAligned(size, alignment);
}
void operator delete(void* memory) noexcept {
    deallocate(memory);
}
void operator delete[](void* memory) noexcept {
    deallocate(memory);
}
void operator delete(void* memory, std::size_t /*size*/) noexcept {
    deallocate(memory);
}
void operator delete[](void* memory, std::size_t /*size*/) noexcept {
    deallocate(memory);
}
void operator delete(void* memory, std::align_val_t /*alignment*/) noexcept {
    deallocateAligned(memory);
}
void operator delete[](void* memory, std::align_val_t /*alignment*/) noexcept {
    deallocateAligned(memory);
}
void operator delete(void* memory, std::size_t /*size*/, std::align_val_t /*alignment*/) noexcept {
    deallocateAligned(memory);
}
void operator delete[](void* memory, std::size_t /*size*/,
                       std::align_val_t /*alignment*/) noexcept {
    deallocateAligned(memory);
}
// NOLINTEND(readability-inconsistent-declaration-parameter-name)
