#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

/// The service pipe: the private link between the web server and the audio service. Messages are
/// JSON-RPC 2.0 (https://www.jsonrpc.org/specification), one per line (JSON Lines), on the audio
/// service's stdin (commands) and stdout (replies and events).
///
/// This library only reads and writes the messages; it knows nothing about devices or the engine.
/// The JSON library behind it (Glaze) is used by one `.cpp` file only.
namespace pianolab::pipe {

/// The longest line the audio service reads, in bytes. Longer lines are rejected whole: the web
/// server never sends anything near this, so a longer one means something is wrong.
inline constexpr std::size_t maxLineBytes = std::size_t{64} * 1024;

/// JSON-RPC 2.0's standard error codes, used in error replies.
namespace error_code {
/// The line isn't JSON.
inline constexpr int parseError = -32'700;
/// The line is JSON, but not a JSON-RPC 2.0 request.
inline constexpr int invalidRequest = -32'600;
/// No such method.
inline constexpr int methodNotFound = -32'601;
/// The method exists, but its parameters are wrong (missing, extra, or the wrong type).
inline constexpr int invalidParams = -32'602;
/// The request was read, but couldn't be carried out (e.g. a setting out of range, or nothing
/// to stop). The message says why.
inline constexpr int failed = -32'000;
} // namespace error_code

/// One setting's value: a number, a text, or a list of texts (e.g. MIDI input names).
using SettingValue = std::variant<double, std::string, std::vector<std::string>>;

/// Settings by name, e.g. `{"sine.gain": 0.5}`.
using SettingValues = std::map<std::string, SettingValue>;

/// `note.on`: a key pressed on screen.
struct NoteOn {
    int key = 0;           ///< MIDI note number; off-keyboard notes are ignored by the engine
    double velocity = 0.0; ///< 0 to 1; the engine clamps anything outside
};

/// `note.off`: a key released on screen.
struct NoteOff {
    int key = 0; ///< MIDI note number
};

/// `settings.get`: the current value of every setting.
struct GetSettings {};

/// `settings.schema`: every setting's description (see `SettingInfo`).
struct GetSchema {};

/// `settings.set`: change some settings. Applied live and saved to the settings file; the whole
/// set is rejected if any value is unknown or out of range.
// std::map's move constructor may allocate on MSVC, so it can only throw when out of memory.
// NOLINTNEXTLINE(bugprone-exception-escape)
struct SetSettings {
    SettingValues values;
};

/// `devices.list`: the audio outputs and MIDI inputs there are.
struct ListDevices {};

/// `recording.start`: start recording the output to a new WAV file.
struct StartRecording {};

/// `recording.stop`: stop recording and finish the file.
struct StopRecording {};

/// `status`: what's running (see `Status`).
struct GetStatus {};

/// A command the web server sends.
using Command = std::variant<NoteOn, NoteOff, GetSettings, GetSchema, SetSettings, ListDevices,
                             StartRecording, StopRecording, GetStatus>;

/// A command read from the pipe. With an `id`, the web server waits for a reply carrying the same
/// `id`; without one (a JSON-RPC notification, e.g. `note.on`), no reply is sent.
struct Request {
    std::optional<std::int64_t> id;
    Command command;
};

/// A JSON-RPC error: what went wrong, for the reply.
struct RpcError {
    /// The request's id when it could be read, so the reply reaches whoever is waiting.
    std::optional<std::int64_t> id;
    int code = error_code::invalidRequest; ///< one of `error_code`
    std::string message;                   ///< what went wrong, for people
};

/// Reads one line from the pipe (without its line ending). Returns an `RpcError` for anything
/// that isn't a valid request: not JSON, not JSON-RPC 2.0, an unknown method, or wrong
/// parameters (missing, unknown or of the wrong type). Never throws or crashes, whatever the
/// line holds.
[[nodiscard]] std::expected<Request, RpcError> readRequest(std::string_view line);

/// How one setting is described in `settings.schema`.
struct SettingInfo {
    std::string name;             ///< e.g. "sine.gain"
    std::string kind;             ///< "number", "text" or "textList"
    std::optional<double> min;    ///< numbers only: the smallest valid value
    std::optional<double> max;    ///< numbers only: the largest valid value
    std::string unit;             ///< e.g. "s" or "Hz"; empty when it has none
    std::string description;      ///< one line for people
    SettingValue defaultValue;    ///< the built-in value
    bool integer = false;         ///< numbers only: whole numbers only
    bool restartsDevices = false; ///< changing it reopens the audio or MIDI devices
};

/// One kind of audio device and its outputs, for `devices.list`.
struct AudioDeviceList {
    std::string type;                 ///< e.g. "Windows Audio" or "ASIO"
    std::vector<std::string> outputs; ///< as the system names them
};

/// The reply to `devices.list`.
struct Devices {
    std::vector<AudioDeviceList> audio;
    std::vector<std::string> midi; ///< MIDI inputs, as the system names them
};

/// The reply to `status`.
struct Status {
    std::string audioType;               ///< the open device's type; empty when none is open
    std::string audioOutput;             ///< the open output; empty when none is open
    double sampleRate = 0.0;             ///< in hertz; 0 when no device is open
    int bufferSize = 0;                  ///< in samples; 0 when no device is open
    std::vector<std::string> midiInputs; ///< the MIDI inputs that are open
    bool recording = false;
    /// Blocks the audio thread took longer to render than they last, since the device opened:
    /// each one almost certainly an audible glitch.
    std::uint64_t lateBlocks = 0;
    /// Glitches the audio driver itself counted, since the device opened; left out when the
    /// driver doesn't count them.
    std::optional<int> driverXruns;
};

/// The reply to `recording.start`.
struct RecordingStarted {
    std::string file; ///< where it's being written, UTF-8
};

/// The reply to `recording.stop`.
struct RecordingStopped {
    std::string file;     ///< the finished file, UTF-8
    double seconds = 0.0; ///< its length
};

/// A successful reply's content.
using Result = std::variant<std::monostate, SettingValues, std::vector<SettingInfo>, Devices,
                            Status, RecordingStarted, RecordingStopped>;

/// The line replying to request `id` with `result` (`std::monostate` writes `null`).
[[nodiscard]] std::string resultLine(std::int64_t id, const Result& result);

/// The line replying with `error`. JSON-RPC requires an `id`, so one that couldn't be read is
/// written as `null`.
[[nodiscard]] std::string errorLine(const RpcError& error);

/// Event: the audio service is ready for commands. Sent once; only `log` events come before it.
struct Ready {
    std::string version; ///< the program's version, e.g. "0.1.0"
};

/// Event: a line for the log ("debug", "info", "warning" or "error").
struct LogLine {
    std::string level;
    std::string message;
};

/// Event: the keys sounding right now changed.
struct ActiveKeys {
    std::vector<int> keys; ///< MIDI note numbers, lowest first
};

/// Event: the output level, a few dozen times a second while anything sounds.
struct Level {
    double peak = 0.0; ///< the loudest sample since the last one, 0 to 1 (more when clipping)
};

/// Event: settings changed (from any client, or after devices reopened). Holds every setting.
// std::map's move constructor may allocate on MSVC, so it can only throw when out of memory.
// NOLINTNEXTLINE(bugprone-exception-escape)
struct SettingsChanged {
    SettingValues values;
};

/// Something the audio service tells the web server without being asked.
using Event = std::variant<Ready, LogLine, ActiveKeys, Level, SettingsChanged>;

/// The line for `event`: a JSON-RPC notification whose method is the event's name ("ready",
/// "log", "keys", "level" or "settings").
[[nodiscard]] std::string eventLine(const Event& event);

} // namespace pianolab::pipe
