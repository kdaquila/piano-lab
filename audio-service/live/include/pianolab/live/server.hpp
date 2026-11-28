#pragma once

#include <pianolab/logging/log.hpp>
#include <pianolab/settings/settings.hpp>

#include <atomic>
#include <filesystem>
#include <optional>
#include <string>

namespace pianolab::live {

/// What the audio service starts with.
struct ServiceOptions {
    settings::Settings settings;        ///< the settings to start with
    std::filesystem::path settingsFile; ///< where changes to them are saved
    /// The settings file's text as read at startup, for the `.bak` safety net (see
    /// `settings::saveSettings`); nothing when it couldn't be read.
    std::optional<std::string> settingsText;
    std::filesystem::path recordingsFolder; ///< where recordings go; created when needed
    std::string version;                    ///< the program's version, for the `ready` event
};

/// Runs the audio service in the foreground, on the calling thread (which becomes JUCE's message
/// thread), until stdin ends or `stopRequested` becomes true.
///
/// Plays the piano through the audio output and from the MIDI inputs the settings name. By
/// default that's the first device type's default output (Windows Audio, shared, on Windows) at
/// 48 kHz if the device allows, with the device's own buffer size, and every MIDI input. Device
/// problems are logged and never stop it: a device that isn't there is replaced by the default,
/// with no audio device it keeps running silently, and inputs that fail to open are skipped.
///
/// Reads service pipe commands from stdin and writes replies and events to stdout (see
/// `pianolab::pipe`): a `ready` event once it's listening, then `keys` and `level` events as the
/// piano plays, and `settings` after a change. (Log messages become `log` events when `log` sends
/// them there: see `withLogEvents`.) Setting changes apply live: device settings reopen the devices
/// (finishing any recording), and the rest reach notes already ringing. They're saved to the
/// settings file a second after the last one, and on the way out.
void runService(const ServiceOptions& options, const logging::Log& log,
                const std::atomic<bool>& stopRequested);

} // namespace pianolab::live
