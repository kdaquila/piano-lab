#pragma once

// The settings file's sections, listed once for the library's own files.

#include <pianolab/engine_core/tuning.hpp>
#include <pianolab/models/sine_key.hpp>
#include <pianolab/settings/settings.hpp>

#include <string_view>
#include <tuple>

namespace pianolab::settings::detail {

/// One section of the settings file.
struct Section {
    std::string_view name;  ///< e.g. "sine"; settings are named "sine.gain"
    std::string_view about; ///< one line on what it's for, for the file
    bool devices = false;   ///< it sets up the audio or MIDI devices, not the piano
};

/// Calls `visit(section, part, fields)` for each section of `settings` (a `Settings`, const or
/// not), in the order the file lists them: `part` is the struct the section sets and `fields` its
/// field table.
template <typename S, typename F> void forEachSection(S& settings, const F& visit) {
    visit(Section{.name = "audio",
                  .about = "The audio output piano-lab plays through",
                  .devices = true},
          settings.audio, std::tuple_cat(audioTextFields, audioNumberFields));
    visit(Section{.name = "midi", .about = "The MIDI inputs piano-lab listens to", .devices = true},
          settings.midi, midiFields);
    visit(Section{.name = "tuning", .about = "How the piano is tuned", .devices = false},
          settings.piano.tuning, engine_core::tuningConfigFields);
    visit(
        Section{.name = "sine", .about = "The sine key model: one sine per key", .devices = false},
        settings.piano.sine, models::sineConfigFields);
    visit(Section{.name = "render",
                  .about = "How the engine renders (the sample rate comes from the audio device)",
                  .devices = false},
          settings.piano.render, renderFileFields);
}

} // namespace pianolab::settings::detail
