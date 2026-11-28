#pragma once

#include <pianolab/engine_core/error.hpp>
#include <pianolab/engine_core/performance.hpp>

#include <vector>

/// Music files in and out: MIDI and MusicXML read into a `Performance`, WAV written out.
namespace pianolab::music_io {

/// A music file read into a performance, plus a warning for everything that changes the sound
/// but had to be skipped (e.g. "grace notes are not supported yet; skipped ...").
///
/// The performance is not validated yet; the engine's `validate` does that.
struct Reading {
    engine_core::Performance performance;
    std::vector<engine_core::Error> warnings;
};

} // namespace pianolab::music_io
