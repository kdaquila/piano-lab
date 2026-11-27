#include <pianolab/engine_core/config.hpp>
#include <pianolab/engine_core/error.hpp>
#include <pianolab/engine_core/keyboard.hpp>
#include <pianolab/engine_core/note_event.hpp>
#include <pianolab/engine_core/performance.hpp>
#include <pianolab/engine_core/render_config.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace pianolab::engine_core {

namespace {

/// The event, fixed up to be playable, or nothing if it has to be dropped. `number` is the
/// event's 1-based position in the input, for the report.
std::optional<PerformanceEvent> checkEvent(PerformanceEvent event, std::size_t number,
                                           std::vector<Error>& errors) {
    const double seconds = event.seconds;
    if (std::isnan(seconds) || seconds < 0.0 || seconds > maxPerformanceSeconds) {
        errors.push_back({std::format("event {}: time {} s is outside 0-{} s; dropped", number,
                                      formatValue(seconds), formatValue(maxPerformanceSeconds))});
        return std::nullopt;
    }
    if (auto* on = std::get_if<NoteOn>(&event.event)) {
        if (!keyIndex(on->note)) {
            errors.push_back(
                {std::format("event {} at {} s: note {} is outside the keyboard ({}-{}); dropped",
                             number, formatValue(seconds), on->note, lowestNote, highestNote)});
            return std::nullopt;
        }
        if (std::isnan(on->velocity)) {
            errors.push_back({std::format("event {} at {} s: velocity is NaN; dropped", number,
                                          formatValue(seconds))});
            return std::nullopt;
        }
        if (on->velocity < 0.0F || on->velocity > 1.0F) {
            const float clamped = std::clamp(on->velocity, 0.0F, 1.0F);
            errors.push_back({std::format("event {} at {} s: velocity {} is outside 0-1; using {}",
                                          number, formatValue(seconds), formatValue(on->velocity),
                                          formatValue(clamped))});
            on->velocity = clamped;
        }
    } else if (const auto* off = std::get_if<NoteOff>(&event.event)) {
        if (!keyIndex(off->note)) {
            return std::nullopt; // its press was reported
        }
    }
    return event;
}

/// The key an event acts on. Checked events are always on the keyboard.
std::size_t keyOf(const PerformanceEvent& event) noexcept {
    const auto* on = std::get_if<NoteOn>(&event.event);
    const auto* off = std::get_if<NoteOff>(&event.event);
    const int note = on != nullptr ? on->note : (off != nullptr ? off->note : lowestNote);
    return keyIndex(note).value_or(0);
}

/// Orders the events within each instant of `events` (already sorted by time), so that every
/// release lands where it belongs. A release that ends a note held from before goes ahead of the
/// presses, so a repeated note is re-struck instead of cut off. A release that ends a note
/// pressed at this same instant goes after its press, so a zero-length note sounds briefly
/// instead of sticking. Otherwise the input order is kept.
std::vector<PerformanceEvent> orderEachInstant(const std::vector<PerformanceEvent>& events) {
    std::vector<PerformanceEvent> ordered;
    ordered.reserve(events.size());
    std::array<int, keyCount> held{}; // presses not yet released, per key
    std::vector<PerformanceEvent> presses;
    std::vector<PerformanceEvent> lateReleases;

    for (std::size_t start = 0; start < events.size();) {
        std::size_t end = start;
        while (end < events.size() && events[end].seconds == events[start].seconds) {
            ++end;
        }

        presses.clear();
        lateReleases.clear();
        std::array<int, keyCount> releasable = held;
        for (std::size_t index = start; index < end; ++index) {
            const PerformanceEvent& event = events[index];
            const std::size_t key = keyOf(event);
            if (std::holds_alternative<NoteOn>(event.event)) {
                presses.push_back(event);
            } else if (releasable.at(key) > 0) {
                --releasable.at(key);
                ordered.push_back(event); // ends a note held from before
            } else {
                lateReleases.push_back(event);
            }
        }
        ordered.insert(ordered.end(), presses.begin(), presses.end());
        ordered.insert(ordered.end(), lateReleases.begin(), lateReleases.end());

        for (std::size_t index = start; index < end; ++index) {
            int& count = held.at(keyOf(events[index]));
            count = std::holds_alternative<NoteOn>(events[index].event) ? count + 1
                                                                        : std::max(count - 1, 0);
        }
        start = end;
    }
    return ordered;
}

} // namespace

Validated<Performance> validate(const Performance& performance) {
    std::vector<Error> errors;
    Performance checked;
    checked.events.reserve(performance.events.size());
    for (std::size_t index = 0; index < performance.events.size(); ++index) {
        if (auto event = checkEvent(performance.events[index], index + 1, errors)) {
            checked.events.push_back(*event);
        }
    }
    std::ranges::stable_sort(checked.events, {}, &PerformanceEvent::seconds);
    checked.events = orderEachInstant(checked.events);
    return {.config = detail::makeValid(std::move(checked)), .errors = std::move(errors)};
}

double lastEventSeconds(const Valid<Performance>& performance) noexcept {
    return performance->events.empty() ? 0.0 : performance->events.back().seconds;
}

std::vector<TimedEvent> toTimedEvents(const Valid<Performance>& performance,
                                      const Valid<RenderConfig>& render) {
    std::vector<TimedEvent> events;
    events.reserve(performance->events.size());
    for (const auto& event : performance->events) {
        // Validated times are 0-6 h and sample rates at most 384 kHz, so this fits easily.
        const auto at = static_cast<std::size_t>(std::llround(event.seconds * render->sampleRate));
        events.push_back({.at = at, .event = event.event});
    }
    return events;
}

} // namespace pianolab::engine_core
