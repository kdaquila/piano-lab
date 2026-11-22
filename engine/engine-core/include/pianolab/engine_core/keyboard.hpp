#pragma once

#include <pianolab/engine_core/config.hpp>
#include <pianolab/engine_core/diagnostics.hpp>
#include <pianolab/engine_core/note_event.hpp>
#include <pianolab/engine_core/render_config.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <optional>
#include <span>
#include <type_traits>
#include <variant>

namespace pianolab::engine_core {

/// Number of keys on a standard piano.
inline constexpr std::size_t keyCount = 88;
/// MIDI note number of the lowest key (A0).
inline constexpr int lowestNote = 21;
/// MIDI note number of the highest key (C8).
inline constexpr int highestNote = lowestNote + static_cast<int>(keyCount) - 1;

/// The key index for a MIDI note (0 = A0, 87 = C8), or nothing if the note is off the keyboard.
[[nodiscard]] constexpr std::optional<std::size_t> keyIndex(int note) noexcept {
    if (note < lowestNote || note > highestNote) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(note - lowestNote);
}

/// What a key model must provide: a copyable value plus four functions, found by argument-
/// dependent lookup in the model's namespace. Each takes the current key and returns the new one
/// (state in, state out); `renderAdd` also adds the key's sound into `out`.
///
/// Real-time rules: none of these may allocate, lock, block or do I/O, and all are noexcept.
template <typename Key>
concept KeyModel =
    std::copyable<Key> && requires(const Key& key, float velocity, std::span<float> out) {
        { strike(key, velocity) } noexcept -> std::same_as<Key>;
        { release(key) } noexcept -> std::same_as<Key>;
        { renderAdd(key, out) } noexcept -> std::same_as<Key>;
        { isSilent(key) } noexcept -> std::same_as<bool>;
    };

/// All 88 keys, each holding its model for the life of the engine, plus what went wrong so far.
///
/// There is no voice pool and no voice stealing: re-striking a key excites the same model again,
/// as on a real piano. Make one with `makeKeyboard`.
template <KeyModel Key> struct Keyboard {
    std::array<Key, keyCount> keys;
    /// The longest stretch handed to a key model at once. Longer blocks are split.
    std::size_t maxBlock = 1;
    /// What the keyboard has recovered from so far.
    Diagnostics diagnostics;
};

/// Builds a keyboard for a validated render configuration, calling `makeKey(note)` once per key
/// (A0 to C8). The factory is where each model gets prepared for the sample rate. Setup code:
/// may allocate; can't fail, because the configuration is already valid.
template <KeyModel Key, std::invocable<int> MakeKey>
    requires std::same_as<std::invoke_result_t<MakeKey&, int>, Key>
[[nodiscard]] Keyboard<Key> makeKeyboard(const Valid<RenderConfig>& render, MakeKey makeKey) {
    Keyboard<Key> keyboard{.keys = {}, .maxBlock = render->maxBlock, .diagnostics = {}};
    for (std::size_t index = 0; index < keyCount; ++index) {
        keyboard.keys[index] = makeKey(lowestNote + static_cast<int>(index));
    }
    return keyboard;
}

/// Replaces NaN and infinite samples with silence, recording each one. Returns how many it fixed.
std::size_t replaceNonFinite(std::span<float> out, Diagnostics& diagnostics) noexcept;

/// Applies one event immediately: strikes or releases the key. Notes off the keyboard are
/// ignored and velocities outside 0-1 are clamped; both are recorded.
template <KeyModel Key> void apply(Keyboard<Key>& keyboard, const NoteEvent& event) noexcept {
    if (const auto* on = std::get_if<NoteOn>(&event)) {
        const auto index = keyIndex(on->note);
        if (!index) {
            record(keyboard.diagnostics, "note outside A0-C8; ignored");
            return;
        }
        float velocity = on->velocity;
        if (std::isnan(velocity) || velocity < 0.0F || velocity > 1.0F) {
            record(keyboard.diagnostics, "velocity outside 0-1 (or NaN); clamped");
            velocity = velocity > 1.0F ? 1.0F : 0.0F;
        }
        keyboard.keys[*index] = strike(keyboard.keys[*index], velocity);
    } else if (const auto* off = std::get_if<NoteOff>(&event)) {
        const auto index = keyIndex(off->note);
        if (!index) {
            record(keyboard.diagnostics, "note outside A0-C8; ignored");
            return;
        }
        keyboard.keys[*index] = release(keyboard.keys[*index]);
    }
}

/// Adds every sounding key into `out`, handing models at most `maxBlock` samples at a time.
template <KeyModel Key> void renderSpan(Keyboard<Key>& keyboard, std::span<float> out) noexcept {
    // Keyboard is a plain struct, so guard against a hand-built one with maxBlock 0.
    const std::size_t step = std::max<std::size_t>(keyboard.maxBlock, 1);
    for (std::size_t start = 0; start < out.size(); start += step) {
        const auto chunk = out.subspan(start, std::min(step, out.size() - start));
        for (auto& key : keyboard.keys) {
            if (!isSilent(key)) {
                key = renderAdd(key, chunk);
            }
        }
    }
}

/// Renders one block into `out` (overwriting it), applying each event at its exact sample
/// offset within the block. `events` should be sorted by `at`, each no later than `out.size()`.
///
/// Real-time safe: never allocates, never throws, never stops. Problems (an oversized block,
/// unsorted or late events, non-finite samples) are fixed up and recorded in the keyboard's
/// diagnostics.
template <KeyModel Key>
void render(Keyboard<Key>& keyboard, std::span<float> out,
            std::span<const TimedEvent> events) noexcept {
    std::ranges::fill(out, 0.0F);
    if (out.size() > keyboard.maxBlock) {
        record(keyboard.diagnostics, "block longer than maxBlock; rendered in chunks");
    }

    // Render the stretch up to each event, then apply the event.
    std::size_t position = 0;
    for (const auto& timed : events) {
        std::size_t at = timed.at;
        if (at < position) {
            record(keyboard.diagnostics, "event before the previous one; moved forward");
            at = position;
        }
        if (at > out.size()) {
            record(keyboard.diagnostics, "event past the end of the block; applied at the end");
            at = out.size();
        }
        renderSpan(keyboard, out.subspan(position, at - position));
        apply(keyboard, timed.event);
        position = at;
    }
    renderSpan(keyboard, out.subspan(position));

    replaceNonFinite(out, keyboard.diagnostics);
}

} // namespace pianolab::engine_core
