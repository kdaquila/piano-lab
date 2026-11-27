#pragma once

#include <pianolab/engine_core/error.hpp>
#include <pianolab/settings/settings.hpp>

#include <cstddef>
#include <string>
#include <vector>

/// Choosing the audio and MIDI devices from the user's settings and what's plugged in. Plain
/// data in, plain data out, with no JUCE, so the rules are tested without devices.
namespace pianolab::server {

/// One kind of audio device the system offers (e.g. "Windows Audio", "ASIO") and its outputs.
struct AudioDeviceType {
    std::string name;
    std::vector<std::string> outputs; ///< the output devices' names
    std::size_t defaultOutput = 0;    ///< the index in `outputs` of the type's default output
};

/// The audio output to open.
struct AudioChoice {
    std::string type;   ///< the device type's name; empty when there's no output at all
    std::string output; ///< the output device's name; empty when there's no output at all
    std::vector<engine_core::Error> warnings; ///< each setting that couldn't be followed
};

/// Chooses the audio output for `settings` from `available` (in the system's order).
///
/// An empty `type` picks the first type with an output, and an empty `output` that type's default
/// output. An `output` named without a `type` picks the first type that has it. A named type or
/// output that isn't there (names must match exactly) is reported in
/// `warnings`, listing what is there, and the default is used instead; so is a named type with no
/// outputs. With no outputs anywhere, both names come back empty.
[[nodiscard]] AudioChoice chooseAudioOutput(const settings::AudioSettings& settings,
                                            const std::vector<AudioDeviceType>& available);

/// The MIDI inputs to open.
struct MidiChoice {
    std::vector<std::size_t> inputs;          ///< indices into the available inputs, in order
    std::vector<engine_core::Error> warnings; ///< each named input that isn't there
};

/// Chooses the MIDI inputs for `settings` from the names of the `available` inputs.
///
/// No inputs named opens every input. Otherwise every available input with a listed name opens
/// (names must match exactly; two inputs with the same name both open). A listed name that isn't
/// there is reported in `warnings`, once; if none of them is there, every input opens, so a
/// keyboard plugged into another port still plays.
[[nodiscard]] MidiChoice chooseMidiInputs(const settings::MidiSettings& settings,
                                          const std::vector<std::string>& available);

} // namespace pianolab::server
