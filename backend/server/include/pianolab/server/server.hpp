#pragma once

#include <pianolab/logging/log.hpp>

#include <atomic>

namespace pianolab::server {

/// Runs the piano server in the foreground, on the calling thread (which becomes JUCE's message
/// thread), until `stopRequested` becomes true; it's checked ten times a second.
///
/// Opens the default audio output of the first device type (Windows Audio, shared, on Windows)
/// at 48 kHz if the device allows, with the device's own buffer size, and every MIDI input.
/// Device problems are logged and never stop the server: with no audio device it keeps running
/// silently, and inputs that fail to open are skipped. The engine's diagnostics and any dropped
/// notes are logged as they happen.
void runServer(const logging::Log& log, const std::atomic<bool>& stopRequested);

} // namespace pianolab::server
