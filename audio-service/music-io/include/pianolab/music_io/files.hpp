#pragma once

#include <pianolab/engine_core/error.hpp>
#include <pianolab/music_io/reading.hpp>

#include <cstddef>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace pianolab::music_io {

/// `path` as UTF-8 text, for messages.
[[nodiscard]] std::string displayPath(const std::filesystem::path& path);

/// The whole file at `path`, or an `Error` naming the file if it can't be read.
[[nodiscard]] std::expected<std::vector<std::byte>, engine_core::Error>
readBytes(const std::filesystem::path& path);

/// Writes `bytes` to `path`, replacing the file if it exists. Writes a temporary `.partial` file
/// first and renames it into place, so a failed write leaves no half-written file and an
/// existing file untouched. Returns an `Error` naming the file if it can't be written.
[[nodiscard]] std::expected<void, engine_core::Error> writeBytes(const std::filesystem::path& path,
                                                                 std::span<const std::byte> bytes);

/// Reads the music file at `path`, choosing the reader by extension: `.mid` and `.midi` (MIDI),
/// `.musicxml` and `.xml` (MusicXML), `.mxl` (compressed MusicXML). Returns an `Error` when the
/// file can't be read at all; anything it can partly read comes back with warnings.
[[nodiscard]] std::expected<Reading, engine_core::Error>
readMusicFile(const std::filesystem::path& path);

} // namespace pianolab::music_io
