#pragma once

#include <pianolab/engine_core/diagnostics.hpp>
#include <pianolab/engine_core/keyboard.hpp>
#include <pianolab/engine_core/note_event.hpp>

#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>

namespace pianolab::engine_core {

/// Renders `length` samples from a list of events, block by block (`maxBlock` at a time),
/// exactly as the real-time engine would.
///
/// Here `at` on each event is the absolute sample from the start of the render. Unsorted events
/// are sorted (stably) and recorded as out of order; events at or after `length` are ignored.
/// Deterministic: the same inputs always give the same samples.
///
/// For tests and offline rendering, so it allocates its output.
template <KeyModel Key>
[[nodiscard]] std::vector<float>
renderOffline(Keyboard<Key>& keyboard, std::span<const TimedEvent> events, std::size_t length) {
    std::vector<TimedEvent> sorted(events.begin(), events.end());
    const auto byTime = [](const TimedEvent& a, const TimedEvent& b) { return a.at < b.at; };
    if (!std::ranges::is_sorted(sorted, byTime)) {
        record(keyboard.diagnostics, Problem::EventOutOfOrder);
        std::ranges::stable_sort(sorted, byTime);
    }

    std::vector<float> out(length, 0.0F);
    std::vector<TimedEvent> blockEvents;
    blockEvents.reserve(sorted.size());
    auto next = sorted.begin();

    for (std::size_t start = 0; start < length; start += keyboard.maxBlock) {
        const std::size_t size = std::min(keyboard.maxBlock, length - start);

        // This block's events, with times made relative to the block.
        blockEvents.clear();
        for (; next != sorted.end() && next->at < start + size; ++next) {
            blockEvents.push_back({.at = next->at - start, .event = next->event});
        }
        render(keyboard, std::span(out).subspan(start, size), blockEvents);
    }
    return out;
}

} // namespace pianolab::engine_core
