#include <pianolab/engine_core/note_event.hpp>
#include <pianolab/server/live_midi.hpp>

#include <cstdint>
#include <span>

namespace pianolab::server {

namespace {

/// Status nibble of a note-off message.
constexpr std::uint8_t noteOffStatus = 0x80;
/// Status nibble of a note-on message.
constexpr std::uint8_t noteOnStatus = 0x90;
/// Status nibble of a controller-change message.
constexpr std::uint8_t controllerStatus = 0xB0;
/// MIDI controller number of the sustain (damper) pedal.
constexpr int sustainPedal = 64;
/// MIDI controller number of the sostenuto pedal.
constexpr int sostenutoPedal = 66;
/// MIDI controller number of the soft pedal (una corda).
constexpr int softPedal = 67;
/// The largest MIDI data value (7 bits).
constexpr float maxDataValue = 127.0F;

} // namespace

MidiMeaning readMidiMessage(std::span<const std::uint8_t> bytes) noexcept {
    if (bytes.size() < 3) {
        return IgnoredMidi{}; // no channel message we use is shorter
    }
    const std::uint8_t kind = bytes[0] & 0xF0U;
    const int first = static_cast<int>(bytes[1] & 0x7FU);
    const int second = static_cast<int>(bytes[2] & 0x7FU);
    if (kind == noteOnStatus && second > 0) {
        return engine_core::NoteEvent{engine_core::NoteOn{
            .note = first, .velocity = static_cast<float>(second) / maxDataValue}};
    }
    if (kind == noteOnStatus || kind == noteOffStatus) {
        return engine_core::NoteEvent{engine_core::NoteOff{.note = first}};
    }
    if (kind == controllerStatus &&
        (first == sustainPedal || first == sostenutoPedal || first == softPedal)) {
        return UnsupportedPedal{.controller = first};
    }
    return IgnoredMidi{};
}

} // namespace pianolab::server
