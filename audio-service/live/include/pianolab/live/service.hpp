#pragma once

#include <pianolab/engine_core/error.hpp>
#include <pianolab/engine_core/note_event.hpp>
#include <pianolab/live/audio_core.hpp>
#include <pianolab/live/recorder.hpp>
#include <pianolab/logging/log.hpp>
#include <pianolab/models/piano_key.hpp>
#include <pianolab/pipe/messages.hpp>
#include <pianolab/settings/settings.hpp>

#include <chrono>
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// The audio service's main-thread logic: answering commands from the service pipe, keeping the
/// settings and saving them, and turning what the audio side publishes into events. Free of
/// JUCE: the devices are behind `ServiceHooks`, so tests drive it with stand-ins.
namespace pianolab::live {

/// How long after the last setting change the settings file is saved, so a slider drag is one
/// write.
inline constexpr std::chrono::milliseconds saveDelay{1'000};

/// The clock the service schedules saves on.
using Clock = std::chrono::steady_clock;

/// What the service needs from the devices side. Each is called on the main thread.
struct ServiceHooks {
    /// Plays a note from the screen.
    std::function<void(const engine_core::NoteEvent&)> playNote;
    /// Gives the playing piano new settings; notes ringing carry on with them.
    std::function<void(const models::PianoConfig&)> changePiano;
    /// Reopens the audio and MIDI devices for new settings, finishing any recording.
    std::function<void(const settings::Settings&)> restartDevices;
    /// The audio outputs and MIDI inputs there are.
    std::function<pipe::Devices()> listDevices;
    /// What's open and running.
    std::function<pipe::Status()> status;
    /// Starts recording the output; returns the new file, or why it can't.
    std::function<std::expected<std::filesystem::path, engine_core::Error>()> startRecording;
    /// Stops recording; returns the finished file, or why it can't.
    std::function<std::expected<RecordingSummary, engine_core::Error>()> stopRecording;
};

/// The main thread's state between commands.
struct ServiceState {
    settings::Settings settings;        ///< the settings in use
    std::filesystem::path settingsFile; ///< where they're saved
    /// The settings file's text as last read or written, for `settings::saveSettings`; nothing
    /// when it couldn't be read.
    std::optional<std::string> settingsText;
    /// When the settings are next saved; nothing when there's nothing to save.
    std::optional<Clock::time_point> saveAt;
};

/// Answers one line from the service pipe, arriving at `now`. Returns the lines to write to
/// stdout, in order: the reply (for requests with an id) and any events, e.g. `settings` after a
/// change. A bad line gets a JSON-RPC error reply when it has an id or isn't a request at all; a
/// bad notification (which has no one to reply to) is logged instead.
///
/// `settings.set` applies valid changes at once: device settings reopen the devices, the rest go
/// to the playing piano, and the file is saved `saveDelay` later (see `saveIfDue`). Invalid
/// changes are rejected whole, with the reasons in the error's message.
[[nodiscard]] std::vector<std::string> handleLine(ServiceState& state, const ServiceHooks& hooks,
                                                  const logging::Log& log, std::string_view line,
                                                  Clock::time_point now);

/// Saves the settings if a save is due at `now`, or if any is waiting and `force` is set (when
/// stopping). Logs where a hand-edited file was kept, and any failure; a failed save isn't
/// retried until the next change.
void saveIfDue(ServiceState& state, const logging::Log& log, Clock::time_point now,
               bool force = false);

/// What the last events said, so the next ones only report changes.
struct EventState {
    HeldKeys keys{};        ///< the keys held down, as last reported
    bool levelSent = false; ///< whether the last `level` event was above silence
};

/// The events for what the audio side published since the last call: `keys` when the keys held
/// down changed, and `level` while anything sounds (plus one at 0 when it falls silent).
[[nodiscard]] std::vector<pipe::Event> liveEvents(EventState& state, AudioLinks& links);

/// The MIDI note numbers of the keys in `keys`, lowest first.
[[nodiscard]] std::vector<int> heldNotes(const HeldKeys& keys);

/// A new recording's file in `folder`: "piano-lab <stamp>.wav", where `stamp` is the local time
/// (e.g. "2026-11-27 15-04-32"), with " (2)", " (3)", ... added if that file already exists.
[[nodiscard]] std::filesystem::path recordingPath(const std::filesystem::path& folder,
                                                  std::string_view stamp);

} // namespace pianolab::live
