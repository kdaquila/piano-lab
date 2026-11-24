#pragma once

#include <pianolab/engine_core/error.hpp>
#include <pianolab/logging/log.hpp>
#include <pianolab/music_io/wav_file.hpp>
#include <pianolab/server/audio_core.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <vector>

namespace pianolab::server {

/// A finished recording.
struct RecordingSummary {
    std::filesystem::path file;
    double seconds = 0.0; ///< the recording's length
};

/// Records the audio output to WAV files: the writer side of the audio → recorder queue.
///
/// The control side (later the API) starts and stops recordings; the audio thread copies its
/// output into the queue while one is running; `drain` writes what's waiting to the file. With
/// `startThread`, a background thread drains every few milliseconds; tests call `drain`
/// themselves instead, to step through deterministically.
///
/// Each recording has its own session number, stamped on every chunk, so audio from a stopped
/// recording that's still in the queue never ends up in the next one.
///
/// A class rather than a plain struct because it owns a thread and an open file, shared
/// between the control side and that thread under a mutex (neither is the audio thread).
class Recorder {
public:
    /// A recorder for the output that `links` carries. `links` must outlive it.
    explicit Recorder(AudioLinks& links) noexcept;
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;
    Recorder(Recorder&&) = delete;
    Recorder& operator=(Recorder&&) = delete;
    /// Stops the thread, then finishes any recording still running.
    ~Recorder();

    /// Starts a thread that drains the queue every few milliseconds, logging problems to `log`.
    void startThread(logging::Log log);

    /// Starts recording to a WAV file at `path`, at `sampleRate` hertz (the device's rate).
    /// Returns an `Error` if a recording is already running or the file can't be created.
    [[nodiscard]] std::expected<void, engine_core::Error> start(const std::filesystem::path& path,
                                                                double sampleRate);

    /// Stops the recording: writes everything already queued, then finishes the file. Returns an
    /// `Error` if nothing is being recorded or the file can't be finished.
    [[nodiscard]] std::expected<RecordingSummary, engine_core::Error> stop();

    /// True while a recording is running.
    [[nodiscard]] bool recording();

    /// Writes every chunk waiting in the queue to the current recording (or discards it, if it
    /// belongs to none). If writing fails, the recording is stopped and the error returned.
    [[nodiscard]] std::optional<engine_core::Error> drain();

private:
    /// Writes waiting chunks; the caller holds `mutex`.
    std::optional<engine_core::Error> drainLocked();
    /// The background thread's loop, until `stop` is requested.
    void run(const std::stop_token& stop, const logging::Log& log);

    AudioLinks& links;
    std::mutex mutex; ///< guards everything below, between the control side and the thread
    std::optional<music_io::WavFileWriter> file;
    std::filesystem::path path;
    double sampleRate = 0.0;
    std::uint32_t session = 0;     ///< the running recording's number, 0 when none
    std::uint32_t lastSession = 0; ///< the last number handed out
    std::jthread thread;           ///< last, so it stops before the rest is destroyed
};

} // namespace pianolab::server
