#include "sections.hpp"

#include <pianolab/engine_core/config.hpp>
#include <pianolab/engine_core/error.hpp>
#include <pianolab/music_io/files.hpp>
#include <pianolab/settings/settings.hpp>

#include <cstddef>
#include <expected>
#include <filesystem>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

namespace pianolab::settings {

using engine_core::Error;
using engine_core::Field;

namespace {

/// The settings file's opening comment.
constexpr std::string_view introduction =
    R"(# piano-lab settings
#
# Only what you change needs to be here. A setting that is commented out uses the built-in value
# shown: remove the # in front of it to change it. Problems in this file are reported (in the
# log, and by 'piano-lab render'), and the built-in value is used instead.
#
# piano-lab rewrites this file when settings change in the app, so edit it while piano-lab is
# stopped. Your own comments and anything piano-lab doesn't know aren't kept, but whenever the
# file holds something piano-lab didn't write, it first copies it to settings.toml.bak.
)";

/// `text` as a TOML string in double quotes, with quotes, backslashes and control characters
/// escaped.
std::string tomlString(std::string_view text) {
    std::string result = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            result += '\\';
            result += c;
        } else if (static_cast<unsigned char>(c) < 0x20 || c == '\x7F') {
            result += std::format("\\u{:04X}", static_cast<unsigned>(c));
        } else {
            result += c;
        }
    }
    return result + '"';
}

/// A number as TOML, in full (the shortest text that reads back exactly), so writing and reading
/// it never changes it.
template <typename V> std::string tomlValue(V value) {
    return std::format("{}", value);
}

/// See the number overload.
std::string tomlValue(const std::string& text) {
    return tomlString(text);
}

/// See the number overload.
std::string tomlValue(const std::vector<std::string>& names) {
    std::string list;
    for (const auto& name : names) {
        list += list.empty() ? tomlString(name) : ", " + tomlString(name);
    }
    return "[" + list + "]";
}

/// The comment describing a number setting: what it is, and its range.
template <typename C, typename V> std::string aboutLine(const Field<C, V>& field) {
    return std::format("# {} ({}-{}{})\n", field.description, engine_core::formatValue(field.min),
                       engine_core::formatValue(field.max), engine_core::unitSuffix(field));
}

/// See the `Field` overload.
template <typename C> std::string aboutLine(const TextField<C>& field) {
    return std::format("# {}\n", field.description);
}

/// See the `Field` overload.
template <typename C> std::string aboutLine(const TextListField<C>& field) {
    return std::format("# {}\n", field.description);
}

/// The file's lines for one setting: its description, then the setting with its value in
/// `config` when that differs from the built-in one, or else commented out with the built-in
/// value.
template <typename C, typename F> std::string settingLines(const F& field, const C& config) {
    const C defaults{};
    const auto& value = config.*field.member;
    const auto& builtIn = defaults.*field.member;
    const bool changed = value != builtIn;
    return std::format("{}{}{} = {}\n", aboutLine(field), changed ? "" : "# ", field.name,
                       tomlValue(changed ? value : builtIn));
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

/// Whether `bytes` (a settings file on disk) is safe to replace without a copy: exactly the text
/// piano-lab last read or wrote (`lastKnown`), and exactly what piano-lab writes for the settings
/// it holds, so replacing it loses nothing.
bool isOwnFile(std::span<const std::byte> bytes, const std::optional<std::string>& lastKnown) {
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (!lastKnown || text != *lastKnown) {
        return false;
    }
    const auto loaded = parseSettings(text, "");
    return loaded && settingsText(loaded->settings) == text;
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

std::string settingsText(const Settings& settings) {
    std::string text(introduction);
    detail::forEachSection(settings, [&](const detail::Section& section, const auto& part,
                                         const auto& fields) {
        text += std::format("\n# {}\n[{}]\n", section.about, section.name);
        std::apply([&](const auto&... field) { ((text += "\n" + settingLines(field, part)), ...); },
                   fields);
    });
    return text;
}

std::string starterSettings() {
    return settingsText(Settings{});
}

std::expected<void, Error> writeStarterSettings(const std::filesystem::path& path) {
    const std::string text = starterSettings();
    return music_io::writeBytes(path, asBytes(text));
}

std::expected<Saved, Error> saveSettings(const std::filesystem::path& path,
                                         const Settings& settings,
                                         const std::optional<std::string>& lastKnown) {
    Saved saved{.text = settingsText(settings), .backup = std::nullopt};

    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    if (error) {
        return std::unexpected(Error{
            std::format("couldn't check {}: {}", music_io::displayPath(path), error.message())});
    }
    if (exists) {
        const auto bytes = music_io::readBytes(path);
        if (!bytes) {
            return std::unexpected(bytes.error());
        }
        if (!isOwnFile(*bytes, lastKnown)) {
            std::filesystem::path backup = path;
            backup += backupSuffix;
            if (const auto copied = music_io::writeBytes(backup, *bytes); !copied) {
                return std::unexpected(
                    Error{std::format("didn't save {}: couldn't keep a copy "
                                      "of it first: {}",
                                      music_io::displayPath(path), copied.error().message)});
            }
            saved.backup = std::move(backup);
        }
    }
    if (const auto written = music_io::writeBytes(path, asBytes(saved.text)); !written) {
        return std::unexpected(written.error());
    }
    return saved;
}

} // namespace pianolab::settings
