// Reading settings from TOML. The one place that uses toml++: it's header-only, so it's included
// here alone and configured on the CMake target (no exceptions: parsing returns a result).

#include <pianolab/engine_core/config.hpp>
#include <pianolab/engine_core/error.hpp>
#include <pianolab/engine_core/tuning.hpp>
#include <pianolab/models/sine_key.hpp>
#include <pianolab/settings/settings.hpp>

#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace pianolab::settings {

using engine_core::Error;
using engine_core::Field;

namespace {

/// The sections a settings file can have, in no particular order.
constexpr std::array<std::string_view, 5> sectionNames{"render", "tuning", "sine", "audio", "midi"};

/// A problem found while reading, and the line it's on.
struct Problem {
    toml::source_index line = 0;
    Error error;
};

/// Where the text being read came from, and the problems found so far.
struct Reader {
    std::string_view source;       ///< names the text in messages, e.g. its path
    std::vector<Problem> problems; ///< one per problem, in the order found
};

/// Adds a problem to `reader`, its message starting with the source and the line of `where`.
void report(Reader& reader, const toml::source_region& where, std::string_view message) {
    reader.problems.push_back(
        {.line = where.begin.line,
         .error = Error{std::format("{}:{}: {}", reader.source, where.begin.line, message)}});
}

/// The problems `reader` found, in the order of their lines in the file.
std::vector<Error> warnings(Reader reader) {
    std::ranges::stable_sort(reader.problems, {}, &Problem::line);
    std::vector<Error> result;
    result.reserve(reader.problems.size());
    for (auto& problem : reader.problems) {
        result.push_back(std::move(problem.error));
    }
    return result;
}

/// What kind of value `node` holds, as people would say it.
std::string_view describe(const toml::node& node) {
    switch (node.type()) {
    case toml::node_type::string:
        return "text";
    case toml::node_type::integer:
        return node.as_integer()->get() < 0 ? "a negative whole number" : "a whole number";
    case toml::node_type::floating_point:
        return "a number";
    case toml::node_type::boolean:
        return "true or false";
    case toml::node_type::array:
        return "a list";
    case toml::node_type::table:
        return "a section";
    case toml::node_type::date:
    case toml::node_type::time:
    case toml::node_type::date_time:
        return "a date or time";
    case toml::node_type::none:
        break;
    }
    return "nothing";
}

/// A number setting: any TOML number (`440` and `440.0` alike), or nothing for anything else.
std::optional<double> numberValue(const toml::node& node) {
    if (const auto* whole = node.as_integer()) {
        return static_cast<double>(whole->get());
    }
    if (const auto* number = node.as_floating_point()) {
        return number->get();
    }
    return std::nullopt;
}

/// A count setting: a whole number of 0 or more, or nothing for anything else.
std::optional<std::size_t> countValue(const toml::node& node) {
    if (const auto* whole = node.as_integer(); whole != nullptr && whole->get() >= 0) {
        return static_cast<std::size_t>(whole->get());
    }
    return std::nullopt;
}

/// Reads `value` from `node`, if it holds the right kind of value for `V`; returns whether it
/// did. These overloads are the only kinds of setting there are.
bool readValue(const toml::node& node, double& value) {
    const auto number = numberValue(node);
    if (number) {
        value = *number;
    }
    return number.has_value();
}

/// See the `double` overload.
bool readValue(const toml::node& node, std::size_t& value) {
    const auto count = countValue(node);
    if (count) {
        value = *count;
    }
    return count.has_value();
}

/// See the `double` overload.
bool readValue(const toml::node& node, std::string& value) {
    const auto* text = node.as_string();
    if (text != nullptr) {
        value = text->get();
    }
    return text != nullptr;
}

/// See the `double` overload. Every entry must be text, or the whole list is rejected.
bool readValue(const toml::node& node, std::vector<std::string>& value) {
    const auto* list = node.as_array();
    if (list == nullptr) {
        return false;
    }
    std::vector<std::string> names;
    for (const auto& entry : *list) {
        const auto* text = entry.as_string();
        if (text == nullptr) {
            return false;
        }
        names.push_back(text->get());
    }
    value = std::move(names);
    return true;
}

/// What a setting of type `V` must be, for messages.
template <typename V> constexpr std::string_view expectation() {
    if constexpr (std::is_same_v<V, double>) {
        return "a number";
    } else if constexpr (std::is_same_v<V, std::size_t>) {
        return "a whole number, 0 or more";
    } else if constexpr (std::is_same_v<V, std::string>) {
        return "text in quotes";
    } else {
        return R"(a list of names in quotes, like ["A", "B"])";
    }
}

/// The member of `C` that `field` describes, and the type of its value. Overloaded for each kind
/// of field.
template <typename C, typename V> V C::*memberOf(const Field<C, V>& field) {
    return field.member;
}

/// See the `Field` overload.
template <typename C> std::string C::*memberOf(const TextField<C>& field) {
    return field.member;
}

/// See the `Field` overload.
template <typename C> std::vector<std::string> C::*memberOf(const TextListField<C>& field) {
    return field.member;
}

/// Reads the setting `field` describes from `table` (the section `section`) into `config`, if
/// it's there. A value of the wrong kind is reported and the setting keeps its current value.
template <typename C, typename F>
void readField(Reader& reader, const toml::table& table, std::string_view section, const F& field,
               C& config) {
    const toml::node* node = table.get(field.name);
    if (node == nullptr) {
        return;
    }
    auto& value = config.*memberOf(field);
    if (!readValue(*node, value)) {
        using V = std::remove_cvref_t<decltype(value)>;
        report(reader, node->source(),
               std::format("{}.{}: expected {}, found {}; using the built-in value", section,
                           field.name, expectation<V>(), describe(*node)));
    }
}

/// Reads section `section` of `root` into `config`, using `fields` (a tuple of fields for `C`).
/// A missing section leaves `config` as it is. Anything in the section that isn't one of
/// `fields` is reported and ignored, as is a section that isn't a table.
template <typename C, typename... Fs>
void readSection(Reader& reader, const toml::table& root, std::string_view section, C& config,
                 const std::tuple<Fs...>& fields) {
    const toml::node* node = root.get(section);
    if (node == nullptr) {
        return;
    }
    const toml::table* table = node->as_table();
    if (table == nullptr) {
        report(reader, node->source(),
               std::format("{}: expected a section ([{}]), found {}; ignored", section, section,
                           describe(*node)));
        return;
    }

    std::apply(
        [&](const auto&... field) { (readField(reader, *table, section, field, config), ...); },
        fields);

    const std::array<std::string_view, sizeof...(Fs)> known = std::apply(
        [](const auto&... field) {
            return std::array<std::string_view, sizeof...(Fs)>{field.name...};
        },
        fields);
    for (const auto& [key, value] : *table) {
        const std::string_view name = key.str();
        if (std::ranges::find(known, name) != known.end()) {
            continue;
        }
        if (section == "render" && name == "sampleRate") {
            report(reader, key.source(),
                   "render.sampleRate can't be set here: the audio device decides it (ask for one "
                   "with audio.sampleRate), and 'piano-lab render --sample-rate' offline; ignored");
        } else {
            report(reader, key.source(),
                   std::format("unknown setting '{}.{}'; ignored", section, name));
        }
    }
}

} // namespace

std::expected<Loaded, Error> parseSettings(std::string_view text, std::string_view source) {
    const toml::parse_result result = toml::parse(text, source);
    if (!result) {
        const toml::parse_error& error = result.error();
        return std::unexpected(
            Error{std::format("{}:{}:{}: {}", source, error.source().begin.line,
                              error.source().begin.column, error.description())});
    }
    const toml::table& root = result.table();

    Reader reader{.source = source, .problems = {}};
    Settings settings;
    readSection(reader, root, "render", settings.piano.render, renderFileFields);
    readSection(reader, root, "tuning", settings.piano.tuning, engine_core::tuningConfigFields);
    readSection(reader, root, "sine", settings.piano.sine, models::sineConfigFields);
    readSection(reader, root, "audio", settings.audio,
                std::tuple_cat(audioTextFields, audioNumberFields));
    readSection(reader, root, "midi", settings.midi, midiFields);

    for (const auto& [key, value] : root) {
        if (std::ranges::find(sectionNames, key.str()) == sectionNames.end()) {
            report(reader, key.source(),
                   std::format("unknown section or setting '{}'; ignored", key.str()));
        }
    }
    return Loaded{.settings = std::move(settings), .warnings = warnings(std::move(reader))};
}

} // namespace pianolab::settings
