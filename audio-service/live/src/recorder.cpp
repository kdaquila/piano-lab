#include <pianolab/engine_core/error.hpp>
#include <pianolab/live/audio_core.hpp>
#include <pianolab/live/recorder.hpp>
#include <pianolab/logging/log.hpp>
#include <pianolab/music_io/files.hpp>
#include <pianolab/music_io/wav_file.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <thread>
#include <utility>

namespace pianolab::live {

using engine_core::Error;

namespace {

/// How often the background thread empties the queue. The queue holds about 11 s, so this
/// leaves a wide margin for a slow disk.
constexpr auto drainInterval = std::chrono::milliseconds(20);

} // namespace

Recorder::Recorder(AudioLinks& links) noexcept : links(links) {}

Recorder::~Recorder() {
    try {
        if (thread.joinable()) {
            thread.request_stop();
            thread.join();
        }
        if (recording()) {
            // Nobody is left to tell; whatever was recorded is kept either way.
            [[maybe_unused]] const auto finished = stop();
        }
    } catch (...) { // NOLINT(bugprone-empty-catch): a destructor must not throw
    }
}

void Recorder::startThread(logging::Log log) {
    thread =
        std::jthread([this, log = std::move(log)](const std::stop_token& stop) { run(stop, log); });
}

void Recorder::run(const std::stop_token& stop, const logging::Log& log) {
    while (!stop.stop_requested()) {
        if (const auto problem = drain()) {
            logging::error(log, "recording stopped: {}", problem->message);
        }
        std::this_thread::sleep_for(drainInterval);
    }
}

std::expected<void, Error> Recorder::start(const std::filesystem::path& newPath,
                                           double newSampleRate) {
    const std::scoped_lock lock(mutex);
    if (session != 0) {
        return std::unexpected(
            Error{std::format("already recording to {}", music_io::displayPath(path))});
    }
    auto writer = music_io::WavFileWriter::create(newPath, newSampleRate);
    if (!writer) {
        return std::unexpected(writer.error());
    }
    (void)drainLocked(); // discard leftovers from an earlier recording
    file.emplace(std::move(*writer));
    path = newPath;
    sampleRate = newSampleRate;
    lastSession = lastSession == std::numeric_limits<std::uint32_t>::max() ? 1 : lastSession + 1;
    session = lastSession;
    links.recordingSession.store(session, std::memory_order_release);
    return {};
}

std::expected<RecordingSummary, Error> Recorder::stop() {
    const std::scoped_lock lock(mutex);
    if (session == 0) {
        return std::unexpected(Error{"nothing is being recorded"});
    }
    // Stop the audio thread adding to this recording, then write what it already queued. A
    // block it was in the middle of may still arrive; it's discarded later, by session number.
    links.recordingSession.store(0, std::memory_order_release);
    auto problem = drainLocked();
    session = 0;
    if (!file) { // writing failed while draining: the file was already closed
        return std::unexpected(problem.value_or(Error{"the recording was lost"}));
    }
    const double seconds = static_cast<double>(file->samplesWritten()) / sampleRate;
    auto finished = file->finish();
    file.reset();
    if (!finished) {
        return std::unexpected(finished.error());
    }
    if (problem) {
        return std::unexpected(*problem);
    }
    return RecordingSummary{.file = path, .seconds = seconds};
}

bool Recorder::recording() {
    const std::scoped_lock lock(mutex);
    return session != 0;
}

std::optional<Error> Recorder::drain() {
    const std::scoped_lock lock(mutex);
    return drainLocked();
}

std::optional<Error> Recorder::drainLocked() {
    while (const auto chunk = links.recording.tryPop()) {
        if (!file || chunk->session != session) {
            continue; // from a recording that has stopped
        }
        auto written = file->append(std::span(chunk->samples).first(chunk->count));
        if (!written) {
            // Keep what was written: finish the file (a failure there is the same disk
            // problem, already being reported), and stop recording.
            links.recordingSession.store(0, std::memory_order_release);
            [[maybe_unused]] const auto finished = file->finish();
            file.reset();
            session = 0;
            return written.error();
        }
    }
    return std::nullopt;
}

} // namespace pianolab::live
