#include <pianolab/engine_core/config.hpp>
#include <pianolab/engine_core/error.hpp>
#include <pianolab/engine_core/note_event.hpp>
#include <pianolab/music_io/midi.hpp>
#include <pianolab/music_io/reading.hpp>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <cstddef>
#include <expected>
#include <format>
#include <span>

namespace pianolab::music_io {

using engine_core::Error;

namespace {

/// MIDI controller number of the sustain (damper) pedal.
constexpr int sustainPedal = 64;
/// MIDI controller number of the sostenuto pedal.
constexpr int sostenutoPedal = 66;
/// MIDI controller number of the soft pedal (una corda).
constexpr int softPedal = 67;

/// True for a pedal controller message.
bool isPedal(const juce::MidiMessage& message) {
    if (!message.isController()) {
        return false;
    }
    const int controller = message.getControllerNumber();
    return controller == sustainPedal || controller == sostenutoPedal || controller == softPedal;
}

} // namespace

std::expected<Reading, Error> readMidi(std::span<const std::byte> data) {
    juce::MemoryInputStream stream(data.data(), data.size(), false);
    juce::MidiFile file;
    if (!file.readFrom(stream)) {
        return std::unexpected(Error{"not a Standard MIDI file"});
    }
    file.convertTimestampTicksToSeconds();

    Reading reading;
    std::size_t pedalEvents = 0;
    double firstPedalSeconds = 0.0;
    for (int track = 0; track < file.getNumTracks(); ++track) {
        for (const auto* holder : *file.getTrack(track)) {
            const juce::MidiMessage& message = holder->message;
            const double seconds = message.getTimeStamp();
            if (message.isNoteOn()) {
                reading.performance.events.push_back(
                    {.seconds = seconds,
                     .event = engine_core::NoteOn{.note = message.getNoteNumber(),
                                                  .velocity = message.getFloatVelocity()}});
            } else if (message.isNoteOff()) { // includes note-ons with velocity 0
                reading.performance.events.push_back(
                    {.seconds = seconds,
                     .event = engine_core::NoteOff{.note = message.getNoteNumber()}});
            } else if (isPedal(message)) {
                if (pedalEvents == 0 || seconds < firstPedalSeconds) {
                    firstPedalSeconds = seconds;
                }
                ++pedalEvents;
            }
        }
    }

    if (pedalEvents > 0) {
        reading.warnings.push_back({std::format(
            "pedals are not supported yet; skipped {} pedal events (the first at {} s)",
            pedalEvents, engine_core::formatValue(firstPedalSeconds))});
    }
    if (reading.performance.events.empty()) {
        reading.warnings.push_back({"the file has no notes"});
    }
    return reading;
}

} // namespace pianolab::music_io
