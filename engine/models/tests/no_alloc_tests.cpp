// The real-time rule: rendering must never allocate memory.
//
// This test binary replaces the global operator new/delete with versions that count allocations
// while a check is running. It's its own executable because the replacement applies to the
// whole program. Counting (instead of aborting) lets the first tests prove the checker really
// catches allocations, so a passing "never allocates" test means something.

#include "harness.hpp"

#include <pianolab/engine_core/diagnostics.hpp>
#include <pianolab/engine_core/keyboard.hpp>
#include <pianolab/engine_core/note_event.hpp>
#include <pianolab/models/piano_key.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <new>
#include <span>
#include <vector>

// --- Allocation counting ------------------------------------------------------------------------

namespace {

/// Above zero while a check is running on this thread.
thread_local std::size_t checkDepth = 0;
/// Allocations seen during checks on this thread.
thread_local std::size_t allocationCount = 0;

/// Counts an allocation if a check is running on this thread.
void noteAllocation() noexcept {
    if (checkDepth > 0) {
        ++allocationCount;
    }
}

/// Runs `body` and returns how many heap allocations it made on this thread.
template <typename Body> std::size_t countAllocations(Body&& body) {
    const std::size_t before = allocationCount;
    ++checkDepth;
    std::forward<Body>(body)();
    --checkDepth;
    return allocationCount - before;
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

// --- The checker works --------------------------------------------------------------------------

namespace {

// Somewhere to put results so the compiler can't optimise the allocations away.
void* volatile sink = nullptr;

struct alignas(64) CacheLineAligned {
    std::array<std::byte, 64> data;
};

} // namespace

TEST_CASE("the checker catches a deliberate allocation") {
    const auto count = countAllocations([] {
        std::vector<int> numbers(100);
        sink = numbers.data();
    });
    CHECK(count >= 1);
}

TEST_CASE("the checker catches an over-aligned allocation") {
    const auto count = countAllocations([] {
        const auto aligned = std::make_unique<CacheLineAligned>();
        sink = aligned.get();
    });
    CHECK(count == 1);
}

TEST_CASE("the checker ignores allocations outside a check") {
    const std::vector<int> numbers(100);
    sink = const_cast<int*>(numbers.data()); // NOLINT(cppcoreguidelines-pro-type-const-cast)
    CHECK(countAllocations([] {}) == 0);
}

// --- Rendering never allocates ----------------------------------------------------------------

TEST_CASE("rendering never allocates") {
    namespace ec = pianolab::engine_core;
    using pianolab::models::testing::blockSize;

    // Setup may allocate: build the keyboard, buffers and event lists first.
    auto keyboard = pianolab::models::testing::makeTestKeyboard();
    std::vector<float> out(blockSize);
    std::vector<float> oversized(blockSize * 3 + 7);
    std::vector<ec::TimedEvent> strikes;
    std::vector<ec::TimedEvent> releases;
    for (int note = ec::lowestNote; note <= ec::highestNote; ++note) {
        const auto offset = static_cast<std::size_t>(note - ec::lowestNote);
        strikes.push_back(ec::noteOn(offset, note, 0.8F));
        releases.push_back(ec::noteOff(offset * 2, note));
    }
    // Misuse the engine recovers from, so the recovery paths are checked too.
    const std::vector<ec::TimedEvent> messy{ec::noteOn(40, 60, 1.5F), ec::noteOn(10, 200, 1.0F),
                                            ec::noteOff(9999, 60)};
    const std::vector<ec::TimedEvent> oddBlock{ec::noteOn(5, 69, 1.0F)};
    const std::vector<ec::TimedEvent> none;

    const auto count = countAllocations([&] {
        // Every key struck, held, then released: the busiest case today.
        ec::render(keyboard, out, strikes);
        for (int block = 0; block < 100; ++block) {
            ec::render(keyboard, out, none);
        }
        ec::render(keyboard, out, releases);
        for (int block = 0; block < 200; ++block) {
            ec::render(keyboard, out, none);
        }
        // Odd-sized and oversized blocks, as audio drivers sometimes deliver.
        ec::render(keyboard, std::span(out).first(17), oddBlock);
        ec::render(keyboard, oversized, none);
        ec::render(keyboard, out, messy);
    });

    CHECK(count == 0);
    // The messy events and the oversized block were recovered from and recorded.
    CHECK(keyboard.diagnostics.total > 0);
}
