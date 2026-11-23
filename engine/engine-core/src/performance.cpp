#include <pianolab/engine_core/config.hpp>
#include <pianolab/engine_core/error.hpp>
#include <pianolab/engine_core/keyboard.hpp>
#include <pianolab/engine_core/note_event.hpp>
#include <pianolab/engine_core/performance.hpp>
#include <pianolab/engine_core/render_config.hpp>

#include <algorithm>
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

/// Sort order: by time, and at the same instant releases come before presses.
bool playsBefore(const PerformanceEvent& a, const PerformanceEvent& b) noexcept {
    if (a.seconds != b.seconds) {
        return a.seconds < b.seconds;
    }
    const bool aIsOff = std::holds_alternative<NoteOff>(a.event);
    const bool bIsOff = std::holds_alternative<NoteOff>(b.event);
    return aIsOff && !bIsOff;
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
    std::ranges::stable_sort(checked.events, playsBefore);
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
