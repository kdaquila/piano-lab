#pragma once

#include <pianolab/engine_core/error.hpp>

#include <cstddef>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>

namespace pianolab::music_io {

/// A mono 32-bit float WAV file written a piece at a time, for recordings whose length isn't
/// known in advance. The same format as `encodeWav`.
///
/// It writes to `<path>.partial` and only renames that into place when finished, so a recording
/// that fails part-way never replaces an existing file with a broken one (the partial file is
/// kept, since a recording can't be made again). Not real-time safe
/// (it does file I/O): use it from a writer thread, never the audio thread.
///
/// A class rather than a plain struct because it owns an open file.
class WavFileWriter {
public:
    /// Starts a WAV file at `path` with `sampleRate` hertz. Returns an `Error` naming the file if
    /// it can't be created.
    [[nodiscard]] static std::expected<WavFileWriter, engine_core::Error>
    create(const std::filesystem::path& path, double sampleRate);

    /// Takes over `other`'s open file; `other` is left finished, with nothing to write to.
    WavFileWriter(WavFileWriter&& other) noexcept;
    /// Takes over `other`'s open file. A file this one had is closed but not renamed: it stays
    /// as its `.partial` file.
    WavFileWriter& operator=(WavFileWriter&& other) noexcept;
    WavFileWriter(const WavFileWriter&) = delete;
    WavFileWriter& operator=(const WavFileWriter&) = delete;
    /// Finishes the file if `finish` wasn't called, ignoring any error.
    ~WavFileWriter();

    /// Adds `samples` to the end of the file. Returns an `Error` if they can't be written, or if
    /// the file would grow past `maxWavSamples`; the samples written so far are kept.
    [[nodiscard]] std::expected<void, engine_core::Error> append(std::span<const float> samples);

    /// How many samples have been written so far.
    [[nodiscard]] std::size_t samplesWritten() const noexcept;

    /// Completes the file's header and moves it to its final name, replacing any file there.
    /// Returns an `Error` if that fails. Calling it again does nothing.
    [[nodiscard]] std::expected<void, engine_core::Error> finish();

private:
    struct Impl;
    explicit WavFileWriter(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl;
};

} // namespace pianolab::music_io
