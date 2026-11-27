#pragma once

#include <pianolab/engine_core/diagnostics.hpp>
#include <pianolab/engine_core/keyboard.hpp>
#include <pianolab/engine_core/note_event.hpp>

#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>

namespace pianolab::engine_core {

namespace detail {

/// `events` sorted (stably) by `at`. Unsorted input is recorded in the keyboard's diagnostics.
template <KeyModel K>
[[nodiscard]] std::vector<TimedEvent> sortedEvents(Keyboard<K>& keyboard,
                                                   std::span<const TimedEvent> events) {
    std::vector<TimedEvent> sorted(events.begin(), events.end());
    const auto byTime = [](const TimedEvent& a, const TimedEvent& b) { return a.at < b.at; };
    if (!std::ranges::is_sorted(sorted, byTime)) {
        record(keyboard.diagnostics, "events were not sorted by time; sorted them");
        std::ranges::stable_sort(sorted, byTime);
    }
    return sorted;
}

/// Renders the block `out`, which starts at absolute sample `start`, applying the events at the
/// front of `pending` that fall inside it and removing them from `pending`. `blockEvents` is
/// scratch space.
template <KeyModel K>
void renderBlockAt(Keyboard<K>& keyboard, std::span<float> out, std::size_t start,
                   std::span<const TimedEvent>& pending, std::vector<TimedEvent>& blockEvents) {
    blockEvents.clear();
    while (!pending.empty() && pending.front().at < start + out.size()) {
        blockEvents.push_back({.at = pending.front().at - start, .event = pending.front().event});
        pending = pending.subspan(1);
    }
    render(keyboard, out, blockEvents);
}

} // namespace detail

/// Renders `length` samples from a list of events, block by block (`maxBlock` at a time),
/// exactly as the real-time engine would.
///
/// Here `at` on each event is the absolute sample from the start of the render. Unsorted events
/// are sorted (stably) and recorded as out of order; events at or after `length` are ignored.
/// Deterministic: the same inputs always give the same samples.
///
/// For tests and offline rendering, so it allocates its output.
template <KeyModel K>
[[nodiscard]] std::vector<float>
renderOffline(Keyboard<K>& keyboard, std::span<const TimedEvent> events, std::size_t length) {
    const std::vector<TimedEvent> sorted = detail::sortedEvents(keyboard, events);
    std::vector<float> out(length, 0.0F);
    std::vector<TimedEvent> blockEvents;
    blockEvents.reserve(sorted.size());
    std::span<const TimedEvent> pending = sorted;
    // Keyboard is a plain struct, so guard against a hand-built one with maxBlock 0.
    const std::size_t chunk = std::max<std::size_t>(keyboard.maxBlock, 1);

    for (std::size_t start = 0; start < length; start += chunk) {
        const std::size_t size = std::min(chunk, length - start);
        detail::renderBlockAt(keyboard, std::span(out).subspan(start, size), start, pending,
                              blockEvents);
    }
    return out;
}

/// The result of `renderUntilSilent`.
struct OfflineRender {
    std::vector<float> samples;
    /// True when the piano was still sounding at the cap, so the render was cut off there.
    bool stoppedAtCap = false;
};

/// Renders every event, then keeps going until every key is silent: the natural length of the
/// performance, release tails included. Never renders more than `maxTail` samples past the last
/// event; hitting that cap (a stuck note, or a model that never decays) sets `stoppedAtCap`.
///
/// Renders whole blocks of `maxBlock` samples, so the length is a multiple of `maxBlock`.
/// Events are handled as in `renderOffline`. Deterministic. Allocates its output.
template <KeyModel K>
[[nodiscard]] OfflineRender
renderUntilSilent(Keyboard<K>& keyboard, std::span<const TimedEvent> events, std::size_t maxTail) {
    const std::vector<TimedEvent> sorted = detail::sortedEvents(keyboard, events);
    const std::size_t lastAt = sorted.empty() ? 0 : sorted.back().at;
    // Keyboard is a plain struct, so guard against a hand-built one with maxBlock 0.
    const std::size_t chunk = std::max<std::size_t>(keyboard.maxBlock, 1);

    OfflineRender result;
    std::vector<TimedEvent> blockEvents;
    blockEvents.reserve(sorted.size());
    std::span<const TimedEvent> pending = sorted;

    for (std::size_t start = 0;; start += chunk) {
        const bool allApplied = pending.empty();
        if (allApplied && allSilent(keyboard)) {
            break;
        }
        if (allApplied && start >= lastAt + maxTail) {
            result.stoppedAtCap = true;
            break;
        }
        result.samples.resize(start + chunk, 0.0F);
        detail::renderBlockAt(keyboard, std::span(result.samples).subspan(start, chunk), start,
                              pending, blockEvents);
    }
    return result;
}

} // namespace pianolab::engine_core
