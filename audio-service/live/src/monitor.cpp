#include <pianolab/engine_core/diagnostics.hpp>
#include <pianolab/live/audio_core.hpp>
#include <pianolab/live/monitor.hpp>
#include <pianolab/logging/log.hpp>

#include <atomic>
#include <cstdint>
#include <optional>

namespace pianolab::live {

void reportNews(MonitorState& state, AudioLinks& links, const logging::Log& log) {
    // Only the newest diagnostics matter: they carry the running total and the latest problem.
    std::optional<engine_core::Diagnostics> newest;
    while (const auto diagnostics = links.diagnostics.tryPop()) {
        newest = *diagnostics;
    }
    if (newest && newest->total > state.engineProblems) {
        logging::warning(
            log, "the engine recovered from {} more problem{} ({} so far); the latest: {}",
            newest->total - state.engineProblems,
            newest->total - state.engineProblems == 1 ? "" : "s", newest->total, newest->last);
        state.engineProblems = newest->total;
    }

    const std::uint64_t droppedEvents = links.droppedEvents.load(std::memory_order_relaxed);
    if (droppedEvents > state.droppedEvents) {
        logging::warning(log, "{} live notes were dropped because the audio side fell behind",
                         droppedEvents - state.droppedEvents);
        state.droppedEvents = droppedEvents;
    }

    const std::uint64_t droppedRecording =
        links.droppedRecordingSamples.load(std::memory_order_relaxed);
    if (droppedRecording > state.droppedRecording) {
        logging::warning(log,
                         "{} samples are missing from the recording because writing it fell "
                         "behind (a slow disk?)",
                         droppedRecording - state.droppedRecording);
        state.droppedRecording = droppedRecording;
    }

    if (!state.pedalsReported && links.pedalMessages.load(std::memory_order_relaxed) > 0) {
        logging::info(log, "pedals aren't supported yet; pedal messages are ignored");
        state.pedalsReported = true;
    }
}

} // namespace pianolab::live
