#include <pianolab/engine_core/config.hpp>
#include <pianolab/engine_core/error.hpp>
#include <pianolab/engine_core/tuning.hpp>
#include <pianolab/models/sine_key.hpp>
#include <pianolab/music_io/files.hpp>
#include <pianolab/settings/settings.hpp>

#include <cstddef>
#include <expected>
#include <filesystem>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace pianolab::settings {

using engine_core::Error;
using engine_core::Field;

namespace {

/// The starter file's opening comment.
constexpr std::string_view starterIntroduction =
    R"(# piano-lab settings
#
# Only what you change needs to be here. Everything below is commented out, so it uses the
# built-in value shown: remove the # in front of a setting to change it. Problems in this file
# are reported (in the log, and by 'piano-lab render'), and the built-in value is used instead.
#
# piano-lab wrote this file because there wasn't one. It never changes it afterwards, so delete
# it to get a fresh one listing every setting of a newer version.
)";

/// `text` as a TOML string in double quotes, with quotes, backslashes and control characters
/// escaped.
std::string tomlString(std::string_view text) {
    std::string result = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            result += '\\';
            result += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            result += std::format("\\u{:04X}", static_cast<unsigned>(c));
        } else {
            result += c;
        }
    }
    return result + '"';
}

/// The starter file's lines for one number setting: its description and range, then the
/// setting commented out with its built-in value from `defaults`.
template <typename C, typename V>
std::string starterLines(const Field<C, V>& field, const C& defaults) {
    const std::string unit = field.unit.empty() ? std::string{} : std::format(" {}", field.unit);
    return std::format("# {} ({}-{}{})\n# {} = {}\n", field.description,
                       engine_core::formatValue(field.min), engine_core::formatValue(field.max),
                       unit, field.name, engine_core::formatValue(defaults.*field.member));
}

/// See the `Field` overload.
template <typename C> std::string starterLines(const TextField<C>& field, const C& defaults) {
    return std::format("# {}\n# {} = {}\n", field.description, field.name,
                       tomlString(defaults.*field.member));
}

/// See the `Field` overload.
template <typename C> std::string starterLines(const TextListField<C>& field, const C& defaults) {
    std::string list;
    for (const auto& name : defaults.*field.member) {
        list += list.empty() ? tomlString(name) : ", " + tomlString(name);
    }
    return std::format("# {}\n# {} = [{}]\n", field.description, field.name, list);
}

/// The starter file's section `section`: a comment saying what it's for (`about`), the header,
/// and every one of `fields` (a tuple of fields for `C`) with its built-in value.
template <typename C, typename... Fs>
std::string starterSection(std::string_view section, std::string_view about,
                           const std::tuple<Fs...>& fields) {
    const C defaults{};
    std::string text = std::format("\n# {}\n[{}]\n", about, section);
    std::apply([&](const auto&... field) { ((text += "\n" + starterLines(field, defaults)), ...); },
               fields);
    return text;
}

/// `text` as bytes, for writing.
std::span<const std::byte> asBytes(std::string_view text) {
    return std::as_bytes(std::span(text.data(), text.size()));
}

/// The UTF-8 text of `bytes`, without a leading byte-order mark.
std::string_view asText(std::span<const std::byte> bytes) {
    std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    constexpr std::string_view byteOrderMark = "\xEF\xBB\xBF";
    if (text.starts_with(byteOrderMark)) {
        text.remove_prefix(byteOrderMark.size());
    }
    return text;
}

} // namespace

engine_core::Validated<AudioSettings> validate(const AudioSettings& settings) {
    std::vector<Error> errors;
    AudioSettings checked = engine_core::checkFields(settings, "audio.", audioNumberFields, errors);
    return {.config = engine_core::detail::makeValid(std::move(checked)),
            .errors = std::move(errors)};
}

std::expected<Loaded, Error> loadSettings(const std::filesystem::path& path) {
    const auto bytes = music_io::readBytes(path);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    return parseSettings(asText(*bytes), music_io::displayPath(path));
}

std::string starterSettings() {
    std::string text(starterIntroduction);
    text += starterSection<AudioSettings>("audio", "The audio output the server plays through",
                                          std::tuple_cat(audioTextFields, audioNumberFields));
    text +=
        starterSection<MidiSettings>("midi", "The MIDI inputs the server listens to", midiFields);
    text += starterSection<engine_core::TuningConfig>("tuning", "How the piano is tuned",
                                                      engine_core::tuningConfigFields);
    text += starterSection<models::SineConfig>("sine", "The sine key model: one sine per key",
                                               models::sineConfigFields);
    text += starterSection<engine_core::RenderConfig>(
        "render", "How the engine renders (the sample rate comes from the audio device)",
        renderFileFields);
    return text;
}

std::expected<void, Error> writeStarterSettings(const std::filesystem::path& path) {
    const std::string text = starterSettings();
    return music_io::writeBytes(path, asBytes(text));
}

} // namespace pianolab::settings
