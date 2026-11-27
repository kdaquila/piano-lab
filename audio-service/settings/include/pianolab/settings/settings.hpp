#pragma once

#include <pianolab/engine_core/config.hpp>
#include <pianolab/engine_core/error.hpp>
#include <pianolab/engine_core/render_config.hpp>
#include <pianolab/models/piano_key.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>
#include <vector>

/// The user's settings: `settings.toml` in the install folder, holding only what the user changed,
/// merged over the built-in settings.
///
/// Every setting is declared once, in a field table: the engine's tables for the piano, and the
/// tables below for the devices. The same tables read and write the file, and describe, read and
/// change settings by name for the web API and the UI.
namespace pianolab::settings {

/// The settings file's name. It lives in the install folder, next to the program.
inline constexpr std::string_view fileName = "settings.toml";

/// One text setting of the settings struct `C`.
template <typename C> struct TextField {
    std::string_view name;        ///< the setting's name, e.g. "output"
    std::string C::* member;      ///< the struct member it describes
    std::string_view description; ///< one line for the starter file and the UI
};

/// One setting of the settings struct `C` holding a list of names.
template <typename C> struct TextListField {
    std::string_view name;                ///< the setting's name, e.g. "inputs"
    std::vector<std::string> C::* member; ///< the struct member it describes
    std::string_view description;         ///< one line for the starter file and the UI
};

/// Which audio output the server plays through. Names are the ones the operating system shows; an
/// empty name means the default.
struct AudioSettings {
    /// The kind of audio device, e.g. "Windows Audio", "Windows Audio (Exclusive Mode)" or "ASIO".
    /// Empty: the first kind that has an output.
    std::string type;
    /// The output device of that type. Empty: the type's default output.
    std::string output;
    /// The sample rate to ask the device for, in hertz. A device that can't do it uses its
    /// closest one.
    double sampleRate = 48'000.0;
    /// The buffer size to ask the device for, in samples. 0: the device's own. A device that can't
    /// do it uses its closest one.
    std::size_t bufferSize = 0;
};

/// The `[audio]` text settings, declared once.
inline constexpr std::tuple audioTextFields{
    TextField<AudioSettings>{
        .name = "type",
        .member = &AudioSettings::type,
        .description = "Kind of audio device, e.g. \"Windows Audio\" or \"ASIO\"; empty for the "
                       "first kind with an output",
    },
    TextField<AudioSettings>{
        .name = "output",
        .member = &AudioSettings::output,
        .description = "Output device, as the system names it; empty for the default",
    },
};

/// The `[audio]` number settings, declared once.
inline constexpr std::tuple audioNumberFields{
    engine_core::Field<AudioSettings, double>{
        .name = "sampleRate",
        .member = &AudioSettings::sampleRate,
        .min = 8'000.0,
        .max = 384'000.0,
        .unit = "Hz",
        .description = "Sample rate to ask the device for; it uses its closest if it can't",
    },
    engine_core::Field<AudioSettings, std::size_t>{
        .name = "bufferSize",
        .member = &AudioSettings::bufferSize,
        .min = 0,
        .max = 8'192,
        .unit = "samples",
        .description = "Buffer size to ask the device for; 0 for the device's own",
    },
};

/// Validates the audio settings. Always returns usable ones: out-of-range numbers are replaced
/// with the built-in values, each reported as an `Error` naming the setting ("audio.…").
[[nodiscard]] engine_core::Validated<AudioSettings> validate(const AudioSettings& settings);

/// Which MIDI inputs the server listens to.
struct MidiSettings {
    /// The inputs to open, by the names the system shows. Empty: every input.
    std::vector<std::string> inputs;
};

/// The `[midi]` settings, declared once.
inline constexpr std::tuple midiFields{
    TextListField<MidiSettings>{
        .name = "inputs",
        .member = &MidiSettings::inputs,
        .description = "MIDI inputs to open, as the system names them; empty for all of them",
    },
};

/// The `[render]` settings the file can hold: the engine's own field, picked from its table.
/// `sampleRate` is left out on purpose: the audio device decides it live (asked for with
/// `audio.sampleRate`), and `piano-lab render --sample-rate` offline.
inline constexpr std::tuple renderFileFields{std::get<1>(engine_core::renderConfigFields)};

/// Everything the settings file can set. The defaults are the built-in settings.
struct Settings {
    /// The piano. Not yet validated: the user decides the values and the device the sample rate,
    /// so whoever builds the piano validates it (`models::validate`) once it knows both.
    models::PianoConfig piano;
    AudioSettings audio;
    MidiSettings midi;
};

/// Settings read from a file, with everything that had to be skipped or fixed along the way: one
/// `Error` per problem, each starting with the file and line, e.g.
/// "settings.toml:4: unknown setting 'sine.gian'; ignored", in the order of the file. Empty lists
/// mean the file was read exactly as written.
struct Loaded {
    Settings settings;
    /// The text that was read, without a byte-order mark: what `saveSettings` compares the file
    /// with to tell whether it changed since.
    std::string text;
    /// Problems with the piano's sections, or with the file as a whole (e.g. an unknown section).
    std::vector<engine_core::Error> warnings;
    /// Problems with the `[audio]` and `[midi]` sections, which only matter to the server.
    std::vector<engine_core::Error> deviceWarnings;
};

/// Reads settings from TOML `text`, merging what it sets over the built-in settings. `source`
/// names the text in messages (usually the file's path).
///
/// Never stops for a problem it can work around: an unknown setting or section is ignored, and a
/// value of the wrong type keeps the built-in value, each reported as a warning with its line.
/// Values are not range-checked here; `models::validate` and `validate(AudioSettings)` do that.
/// Returns an `Error` with the line and column only when `text` isn't valid TOML at all.
[[nodiscard]] std::expected<Loaded, engine_core::Error> parseSettings(std::string_view text,
                                                                      std::string_view source);

/// Reads the settings file at `path` (see `parseSettings`). Returns an `Error` when the file can't
/// be read (including when it doesn't exist) or isn't valid TOML.
[[nodiscard]] std::expected<Loaded, engine_core::Error>
loadSettings(const std::filesystem::path& path);

/// The settings file for `settings`: every setting with its description and valid range. Those
/// that differ from the built-in value are set; the rest are commented out, showing the built-in
/// value, so uncommenting one sets it. Generated from the field tables, so it always matches the
/// program.
[[nodiscard]] std::string settingsText(const Settings& settings);

/// The starter settings file: `settingsText` of the built-in settings, so every setting is
/// commented out.
[[nodiscard]] std::string starterSettings();

/// Writes `starterSettings()` to `path`, replacing any file there (callers check first). Returns
/// an `Error` naming the file if it can't be written.
[[nodiscard]] std::expected<void, engine_core::Error>
writeStarterSettings(const std::filesystem::path& path);

/// One setting's value: a number, a text, or a list of texts. Counts (e.g. `render.maxBlock`)
/// are numbers too.
using Value = std::variant<double, std::string, std::vector<std::string>>;

/// Settings by their full name, e.g. `{"sine.gain": 0.5}`.
using Values = std::map<std::string, Value>;

/// The kind of value a setting holds.
enum class Kind : std::uint8_t {
    Number,   ///< a `double`
    Text,     ///< a `std::string`
    TextList, ///< a `std::vector<std::string>`
};

/// One setting, described for the web API and the UI.
struct SettingInfo {
    std::string name;             ///< full name, e.g. "sine.gain"
    Kind kind = Kind::Number;     ///< the kind of `Value` it takes
    std::optional<double> min;    ///< numbers only: the smallest valid value
    std::optional<double> max;    ///< numbers only: the largest valid value
    std::string unit;             ///< e.g. "Hz"; empty when it has none
    std::string description;      ///< one line for people
    Value defaultValue;           ///< the built-in value
    bool integer = false;         ///< numbers only: whole numbers only
    bool restartsDevices = false; ///< changing it reopens the audio or MIDI devices
};

/// Every setting the file can hold, in the order the file lists them. Generated from the field
/// tables.
[[nodiscard]] std::vector<SettingInfo> describeSettings();

/// Every setting of `settings` by its full name.
[[nodiscard]] Values valuesOf(const Settings& settings);

/// `current` with `changes` applied, or every problem with them: an unknown name ("unknown
/// setting 'sine.gian'"), a value of the wrong kind ("sine.gain: expected a number, found text")
/// or out of range ("sine.gain: 2 is outside 0-1"). Unlike the file, which falls back to the
/// built-in value, a bad change is rejected, so nothing changes unless all of `changes` are valid.
[[nodiscard]] std::expected<Settings, std::vector<engine_core::Error>>
withChanges(const Settings& current, const Values& changes);

/// The name of the copy `saveSettings` keeps of a file it was about to replace: the settings
/// file's name with this added, i.e. "settings.toml.bak".
inline constexpr std::string_view backupSuffix = ".bak";

/// What `saveSettings` did.
struct Saved {
    std::string text; ///< what it wrote: pass it as `lastKnown` next time
    /// Where it copied the file it replaced, when that file had changed (see `saveSettings`).
    std::optional<std::filesystem::path> backup;
};

/// Writes `settingsText(settings)` to `path`, atomically (see `music_io::writeBytes`).
///
/// piano-lab owns the file while it runs, but someone may still edit it. So unless the file on
/// disk is exactly `lastKnown` (the text last read from or written to it) and exactly what
/// piano-lab would write for it, it is first copied to `path` + `backupSuffix`, replacing any
/// older copy. That keeps hand edits, comments and unknown settings, which the new file drops.
///
/// Returns an `Error` naming the file if the file can't be read, copied or written; then the
/// file is left as it was.
[[nodiscard]] std::expected<Saved, engine_core::Error>
saveSettings(const std::filesystem::path& path, const Settings& settings,
             const std::optional<std::string>& lastKnown);

} // namespace pianolab::settings
