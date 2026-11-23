#pragma once

#include <pianolab/engine_core/error.hpp>
#include <pianolab/music_io/reading.hpp>

#include <cstddef>
#include <expected>
#include <span>
#include <string_view>

namespace pianolab::music_io {

/// Reads a MusicXML score (partwise, as MuseScore and most programs write it) into a
/// performance. Deliberately lossy: it keeps what affects the sound and drops the rest.
///
/// Supported: notes, rests, chords, ties, voices and staves (`backup`/`forward`), several parts
/// (all on the one piano), tempo (`<sound tempo>` or a metronome mark; 120 quarter notes per
/// minute by default) and dynamics (a note's `dynamics` attribute, `<sound dynamics>`, or a
/// marking such as `mf`; mf when there are none). Visual-only notation is ignored silently.
/// Notation that changes the sound but isn't supported yet (repeats, grace notes, pedals,
/// hairpins, articulations, ...) is skipped with one warning per kind, saying where it first
/// appears. Returns an `Error` if `xml` isn't a MusicXML score.
[[nodiscard]] std::expected<Reading, engine_core::Error> readMusicXml(std::string_view xml);

/// Reads a compressed MusicXML file (`.mxl`: a zip holding the score) like `readMusicXml`.
/// Returns an `Error` if `data` isn't a zip or holds no score.
[[nodiscard]] std::expected<Reading, engine_core::Error>
readCompressedMusicXml(std::span<const std::byte> data);

} // namespace pianolab::music_io
