#include <pianolab/engine_core/error.hpp>
#include <pianolab/engine_core/keyboard.hpp>
#include <pianolab/engine_core/note_event.hpp>
#include <pianolab/live/audio_core.hpp>
#include <pianolab/live/service.hpp>
#include <pianolab/logging/log.hpp>
#include <pianolab/music_io/files.hpp>
#include <pianolab/pipe/messages.hpp>
#include <pianolab/settings/settings.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace pianolab::live {

// The settings library and the pipe use the same value types, so values pass straight through.
static_assert(std::is_same_v<settings::Values, pipe::SettingValues>);

namespace {

/// Why a command couldn't be carried out, for the error reply.
using Failure = std::string;

/// A command's outcome: the reply's result, or why it failed.
using Outcome = std::expected<pipe::Result, Failure>;

/// The pipe's name for a kind of setting.
std::string_view kindName(settings::Kind kind) {
    switch (kind) {
    case settings::Kind::Number:
        return "number";
    case settings::Kind::Text:
        return "text";
    case settings::Kind::TextList:
        return "textList";
    }
    return "number";
}

/// Every setting described for the pipe.
std::vector<pipe::SettingInfo> schema() {
    std::vector<pipe::SettingInfo> infos;
    for (auto& info : settings::describeSettings()) {
        infos.push_back({.name = std::move(info.name),
                         .kind = std::string(kindName(info.kind)),
                         .min = info.min,
                         .max = info.max,
                         .unit = std::move(info.unit),
                         .description = std::move(info.description),
                         .defaultValue = std::move(info.defaultValue),
                         .integer = info.integer,
                         .restartsDevices = info.restartsDevices});
    }
    return infos;
}

/// The messages of `errors`, joined with "; ".
std::string joined(const std::vector<engine_core::Error>& errors) {
    std::string text;
    for (const auto& error : errors) {
        text += (text.empty() ? "" : "; ") + error.message;
    }
    return text;
}

/// Applies `changes` (see `handleLine`), adding a `settings` event to `lines` when anything
/// changed.
Outcome changeSettings(ServiceState& state, const ServiceHooks& hooks,
                       const settings::Values& changes, Clock::time_point now,
                       std::vector<std::string>& lines) {
    auto changed = settings::withChanges(state.settings, changes);
    if (!changed) {
        return std::unexpected(joined(changed.error()));
    }
    const settings::Values before = settings::valuesOf(state.settings);
    settings::Values after = settings::valuesOf(*changed);
    if (after == before) {
        return after;
    }
    bool devices = false;
    bool piano = false;
    for (const auto& info : settings::describeSettings()) {
        if (before.at(info.name) != after.at(info.name)) {
            (info.restartsDevices ? devices : piano) = true;
        }
    }
    state.settings = std::move(*changed);
    if (devices) {
        hooks.restartDevices(state.settings); // builds the piano afresh, so it covers both
    } else if (piano) {
        hooks.changePiano(state.settings.piano);
    }
    state.saveAt = now + saveDelay;
    lines.push_back(pipe::eventLine(pipe::SettingsChanged{.values = after}));
    return after;
}

/// Carries out `command`.
Outcome carryOut(ServiceState& state, const ServiceHooks& hooks, const pipe::Command& command,
                 Clock::time_point now, std::vector<std::string>& lines) {
    if (const auto* on = std::get_if<pipe::NoteOn>(&command)) {
        hooks.playNote(
            engine_core::NoteOn{.note = on->key, .velocity = static_cast<float>(on->velocity)});
        return pipe::Result{};
    }
    if (const auto* off = std::get_if<pipe::NoteOff>(&command)) {
        hooks.playNote(engine_core::NoteOff{.note = off->key});
        return pipe::Result{};
    }
    if (std::holds_alternative<pipe::GetSettings>(command)) {
        return settings::valuesOf(state.settings);
    }
    if (std::holds_alternative<pipe::GetSchema>(command)) {
        return schema();
    }
    if (const auto* set = std::get_if<pipe::SetSettings>(&command)) {
        return changeSettings(state, hooks, set->values, now, lines);
    }
    if (std::holds_alternative<pipe::ListDevices>(command)) {
        return hooks.listDevices();
    }
    if (std::holds_alternative<pipe::StartRecording>(command)) {
        return hooks.startRecording()
            .transform([](const std::filesystem::path& file) -> pipe::Result {
                return pipe::RecordingStarted{.file = music_io::displayPath(file)};
            })
            .transform_error([](const engine_core::Error& error) { return error.message; });
    }
    if (std::holds_alternative<pipe::StopRecording>(command)) {
        return hooks.stopRecording()
            .transform([](const RecordingSummary& summary) -> pipe::Result {
                return pipe::RecordingStopped{.file = music_io::displayPath(summary.file),
                                              .seconds = summary.seconds};
            })
            .transform_error([](const engine_core::Error& error) { return error.message; });
    }
    return hooks.status();
}

} // namespace

std::vector<std::string> handleLine(ServiceState& state, const ServiceHooks& hooks,
                                    const logging::Log& log, std::string_view line,
                                    Clock::time_point now) {
    std::vector<std::string> lines;
    const auto request = pipe::readRequest(line);
    if (!request) {
        const auto& error = request.error();
        const bool notARequest = error.code == pipe::error_code::parseError ||
                                 error.code == pipe::error_code::invalidRequest;
        if (error.id || notARequest) {
            lines.push_back(pipe::errorLine(error));
        } else {
            logging::warning(log, "service pipe: ignored a notification: {}", error.message);
        }
        return lines;
    }

    std::vector<std::string> events;
    const Outcome outcome = carryOut(state, hooks, request->command, now, events);
    if (request->id) {
        lines.push_back(outcome ? pipe::resultLine(*request->id, *outcome)
                                : pipe::errorLine({.id = request->id,
                                                   .code = pipe::error_code::failed,
                                                   .message = outcome.error()}));
    } else if (!outcome) {
        logging::warning(log, "service pipe: {}", outcome.error());
    }
    lines.insert(lines.end(), std::make_move_iterator(events.begin()),
                 std::make_move_iterator(events.end()));
    return lines;
}

void saveIfDue(ServiceState& state, const logging::Log& log, Clock::time_point now, bool force) {
    if (!state.saveAt || (!force && now < *state.saveAt)) {
        return;
    }
    state.saveAt.reset();
    auto saved = settings::saveSettings(state.settingsFile, state.settings, state.settingsText);
    if (!saved) {
        logging::error(log, "settings not saved: {}", saved.error().message);
        return;
    }
    if (saved->backup) {
        logging::warning(log, "{} had changed outside piano-lab; kept it as {}",
                         music_io::displayPath(state.settingsFile),
                         music_io::displayPath(*saved->backup));
    }
    logging::debug(log, "settings saved to {}", music_io::displayPath(state.settingsFile));
    state.settingsText = std::move(saved->text);
}

std::vector<int> heldNotes(const HeldKeys& keys) {
    std::vector<int> notes;
    for (std::size_t key = 0; key < engine_core::keyCount; ++key) {
        if ((keys.at(key / 64) >> (key % 64) & 1U) != 0) {
            notes.push_back(engine_core::lowestNote + static_cast<int>(key));
        }
    }
    return notes;
}

std::vector<pipe::Event> liveEvents(EventState& state, AudioLinks& links) {
    std::vector<pipe::Event> events;
    const HeldKeys keys = heldKeys(links);
    if (keys != state.keys) {
        state.keys = keys;
        events.emplace_back(pipe::ActiveKeys{.keys = heldNotes(keys)});
    }
    const float peak = takePeak(links);
    if (peak > 0.0F || state.levelSent) {
        events.emplace_back(pipe::Level{.peak = static_cast<double>(peak)});
        state.levelSent = peak > 0.0F;
    }
    return events;
}

std::filesystem::path recordingPath(const std::filesystem::path& folder, std::string_view stamp) {
    const auto named = [&](std::string_view suffix) {
        const std::string name = std::format("piano-lab {}{}.wav", stamp, suffix);
        return folder / std::filesystem::path(std::u8string(name.begin(), name.end()));
    };
    std::filesystem::path path = named("");
    std::error_code error;
    for (int copy = 2; std::filesystem::exists(path, error) && copy < 1'000; ++copy) {
        path = named(std::format(" ({})", copy));
    }
    return path;
}

} // namespace pianolab::live
