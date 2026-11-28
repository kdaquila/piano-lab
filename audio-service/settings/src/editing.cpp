// Settings by name: their descriptions, their values, and changing them, all through the field
// tables.

#include "sections.hpp"

#include <pianolab/engine_core/config.hpp>
#include <pianolab/engine_core/error.hpp>
#include <pianolab/settings/settings.hpp>

#include <cmath>
#include <cstddef>
#include <expected>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace pianolab::settings {

using engine_core::Error;
using engine_core::Field;

namespace {

/// A setting's full name, e.g. "sine.gain" for `gain` in section `sine`.
std::string fullName(const detail::Section& section, std::string_view name) {
    return std::format("{}.{}", section.name, name);
}

/// How `field` is described for the web API; `defaults` holds the built-in values.
template <typename C, typename V>
SettingInfo describe(const detail::Section& section, const Field<C, V>& field, const C& defaults) {
    return {.name = fullName(section, field.name),
            .kind = Kind::Number,
            .min = static_cast<double>(field.min),
            .max = static_cast<double>(field.max),
            .unit = std::string(field.unit),
            .description = std::string(field.description),
            .defaultValue = static_cast<double>(defaults.*field.member),
            .integer = std::is_integral_v<V>,
            .restartsDevices = section.devices};
}

/// See the `Field` overload.
template <typename C>
SettingInfo describe(const detail::Section& section, const TextField<C>& field, const C& defaults) {
    return {.name = fullName(section, field.name),
            .kind = Kind::Text,
            .min = std::nullopt,
            .max = std::nullopt,
            .unit = {},
            .description = std::string(field.description),
            .defaultValue = defaults.*field.member,
            .integer = false,
            .restartsDevices = section.devices};
}

/// See the `Field` overload.
template <typename C>
SettingInfo describe(const detail::Section& section, const TextListField<C>& field,
                     const C& defaults) {
    return {.name = fullName(section, field.name),
            .kind = Kind::TextList,
            .min = std::nullopt,
            .max = std::nullopt,
            .unit = {},
            .description = std::string(field.description),
            .defaultValue = defaults.*field.member,
            .integer = false,
            .restartsDevices = section.devices};
}

/// The value of the setting `field` describes, in `config`.
template <typename C, typename V> Value valueOf(const Field<C, V>& field, const C& config) {
    return static_cast<double>(config.*field.member);
}

/// See the `Field` overload.
template <typename C> Value valueOf(const TextField<C>& field, const C& config) {
    return config.*field.member;
}

/// See the `Field` overload.
template <typename C> Value valueOf(const TextListField<C>& field, const C& config) {
    return config.*field.member;
}

/// What kind of value `value` is, as people would say it.
std::string_view kindOf(const Value& value) {
    switch (value.index()) {
    case 0:
        return "a number";
    case 1:
        return "text";
    default:
        return "a list";
    }
}

/// The problem with setting `name` to `value`, when it isn't what was `expected`.
Error wrongKind(std::string_view name, std::string_view expected, const Value& value) {
    return Error{std::format("{}: expected {}, found {}", name, expected, kindOf(value))};
}

/// Sets the setting `field` describes in `config` to `value`, or returns why it can't be: the
/// wrong kind of value, not a whole number for a count, or out of range. `config` is only
/// changed when the value is valid.
template <typename C, typename V>
std::optional<Error> setValue(const detail::Section& section, const Field<C, V>& field,
                              const Value& value, C& config) {
    const std::string prefix = std::format("{}.", section.name);
    const std::string name = prefix + std::string(field.name);
    const auto* number = std::get_if<double>(&value);
    if constexpr (std::is_integral_v<V>) {
        static_assert(std::is_unsigned_v<V>, "counts are never negative");
        constexpr std::string_view expected = "a whole number, 0 or more";
        if (number == nullptr) {
            return wrongKind(name, expected, value);
        }
        if (!std::isfinite(*number) || std::trunc(*number) != *number || *number < 0.0) {
            return Error{std::format("{}: expected {}, found {}", name, expected,
                                     engine_core::formatValue(*number))};
        }
        // Checked as a number first: one too large for `V` can't be converted.
        if (*number > static_cast<double>(field.max)) {
            return Error{engine_core::outOfRangeMessage(prefix, field, *number)};
        }
        const auto count = static_cast<V>(*number);
        if (!engine_core::inRange(field, count)) {
            return Error{engine_core::outOfRangeMessage(prefix, field, count)};
        }
        config.*field.member = count;
    } else {
        if (number == nullptr) {
            return wrongKind(name, "a number", value);
        }
        if (!engine_core::inRange(field, *number)) {
            return Error{engine_core::outOfRangeMessage(prefix, field, *number)};
        }
        config.*field.member = *number;
    }
    return std::nullopt;
}

/// See the `Field` overload.
template <typename C>
std::optional<Error> setValue(const detail::Section& section, const TextField<C>& field,
                              const Value& value, C& config) {
    const auto* text = std::get_if<std::string>(&value);
    if (text == nullptr) {
        return wrongKind(fullName(section, field.name), "text", value);
    }
    config.*field.member = *text;
    return std::nullopt;
}

/// See the `Field` overload.
template <typename C>
std::optional<Error> setValue(const detail::Section& section, const TextListField<C>& field,
                              const Value& value, C& config) {
    const auto* list = std::get_if<std::vector<std::string>>(&value);
    if (list == nullptr) {
        return wrongKind(fullName(section, field.name), "a list of names", value);
    }
    config.*field.member = *list;
    return std::nullopt;
}

} // namespace

std::vector<SettingInfo> describeSettings() {
    std::vector<SettingInfo> infos;
    const Settings defaults;
    detail::forEachSection(defaults, [&](const detail::Section& section, const auto& part,
                                         const auto& fields) {
        std::apply(
            [&](const auto&... field) { (infos.push_back(describe(section, field, part)), ...); },
            fields);
    });
    return infos;
}

Values valuesOf(const Settings& settings) {
    Values values;
    detail::forEachSection(
        settings, [&](const detail::Section& section, const auto& part, const auto& fields) {
            std::apply(
                [&](const auto&... field) {
                    ((values[fullName(section, field.name)] = valueOf(field, part)), ...);
                },
                fields);
        });
    return values;
}

std::expected<Settings, std::vector<Error>> withChanges(const Settings& current,
                                                        const Values& changes) {
    Settings changed = current;
    std::vector<Error> errors;
    std::size_t known = 0;
    detail::forEachSection(
        changed, [&](const detail::Section& section, auto& part, const auto& fields) {
            const auto apply = [&](const auto& field) {
                const auto change = changes.find(fullName(section, field.name));
                if (change == changes.end()) {
                    return;
                }
                ++known;
                if (auto problem = setValue(section, field, change->second, part)) {
                    errors.push_back(std::move(*problem));
                }
            };
            std::apply([&](const auto&... field) { (apply(field), ...); }, fields);
        });
    if (known != changes.size()) {
        const Values all = valuesOf(current);
        for (const auto& [name, value] : changes) {
            if (!all.contains(name)) {
                errors.push_back(Error{std::format("unknown setting '{}'", name)});
            }
        }
    }
    if (!errors.empty()) {
        return std::unexpected(std::move(errors));
    }
    return changed;
}

} // namespace pianolab::settings
