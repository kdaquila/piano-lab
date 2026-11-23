#pragma once

#include <pianolab/logging/log.hpp>
#include <pianolab/server/audio_core.hpp>

#include <cstdint>

namespace pianolab::server {

/// What the monitor has already reported, so each problem is logged once, as it happens.
struct MonitorState {
    std::uint64_t engineProblems = 0;   ///< the engine's diagnostics total last logged
    std::uint64_t droppedEvents = 0;    ///< dropped live events last logged
    std::uint64_t droppedRecording = 0; ///< dropped recording samples last logged
    bool pedalsReported = false;        ///< whether "pedals aren't supported" was logged
};

/// Logs what the audio side has reported since the last call: the engine's diagnostics (how
/// many problems it recovered from, and the latest), notes or recorded audio that had to be
/// dropped, and the first pedal message. Runs on an ordinary thread every so often (the
/// server's main thread, ten times a second); never on the audio thread.
void reportNews(MonitorState& state, AudioLinks& links, const logging::Log& log);

} // namespace pianolab::server
