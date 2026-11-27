#pragma once

#include <pianolab/logging/log.hpp>
#include <pianolab/settings/settings.hpp>

#include <atomic>

namespace pianolab::server {

/// Runs the piano server in the foreground, on the calling thread (which becomes JUCE's message
/// thread), until `stopRequested` becomes true; it's checked ten times a second.
///
/// Plays `settings.piano` through the audio output and from the MIDI inputs `settings` names.
/// By default that's the first device type's default output (Windows Audio, shared, on Windows)
/// at 48 kHz if the device allows, with the device's own buffer size, and every MIDI input.
/// Device problems are logged and never stop the server: a device that isn't there is replaced
/// by the default, with no audio device it keeps running silently, and inputs that fail to open
/// are skipped. The engine's diagnostics and any dropped notes are logged as they happen.
void runServer(const settings::Settings& settings, const logging::Log& log,
               const std::atomic<bool>& stopRequested);

} // namespace pianolab::server
