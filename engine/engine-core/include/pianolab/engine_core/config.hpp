#pragma once

#include <pianolab/engine_core/error.hpp>

#include <cmath>
#include <format>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

/// The configuration system's building blocks.
///
/// Each configuration is a plain struct whose default member values are the built-in settings.
/// Next to it, a table of `Field`s declares every setting once: its name, valid range, unit and
/// description. `validate` uses the table to turn any config into a `Valid` one, replacing bad
/// values with the built-in defaults and reporting each fix as an `Error`. Engine entry points
/// only accept `Valid` configs, so engine setup can't fail.
///
/// The same tables will drive loading user settings from files and showing them in the UI.
namespace pianolab::engine_core {

/// One numeric setting of `Config`: which member it is, and what's valid for it.
template <typename Config, typename Value> struct Field {
    std::string_view name;        ///< the setting's name, e.g. "sampleRate"
    Value Config::* member;       ///< the struct member it describes
    Value min;                    ///< smallest valid value (inclusive)
    Value max;                    ///< largest valid value (inclusive)
    std::string_view unit;        ///< e.g. "Hz", "s", "samples"; empty when unitless
    std::string_view description; ///< one line for docs and the UI
};

template <typename T> class Valid;

namespace detail {
/// Wraps a value that has just been validated. Only validation code may call this; anything
/// else would defeat the guarantee `Valid` gives.
template <typename T> [[nodiscard]] Valid<T> makeValid(T value);
} // namespace detail

/// A configuration that has passed validation. Only `validate` functions (through
/// `detail::makeValid`) can create one, so a function taking `Valid<T>` knows every value is in
/// range without checking again.
///
/// A class rather than a plain struct on purpose: its private constructor is what makes the
/// guarantee hold.
template <typename T> class Valid {
public:
    /// The validated configuration.
    [[nodiscard]] const T& operator*() const noexcept { return value; }
    /// Access to the validated configuration's members.
    [[nodiscard]] const T* operator->() const noexcept { return &value; }

private:
    explicit Valid(T validated) : value(std::move(validated)) {}

    T value;

    friend Valid detail::makeValid<T>(T);
};

namespace detail {
template <typename T> Valid<T> makeValid(T value) {
    return Valid<T>(std::move(value));
}
} // namespace detail

/// The result of validation: always a usable configuration, plus an `Error` for every value
/// that had to be replaced. An empty `errors` list means the input was valid as given.
template <typename T> struct Validated {
    Valid<T> config;
    std::vector<Error> errors;
};

/// The part of a valid configuration selected by `member`. A part of a valid whole is valid,
/// because validating the whole validates each part.
template <typename Part, typename Whole>
[[nodiscard]] Valid<Part> validPart(const Valid<Whole>& whole, Part Whole::* member) {
    return detail::makeValid((*whole).*member);
}

/// A setting's value as people read it: decimals like 0.0005 or 48000 rather than 5e-04, with
/// scientific notation only for very small or large numbers (e.g. 1e-05).
template <typename Value> [[nodiscard]] std::string formatValue(Value value) {
    if constexpr (std::is_floating_point_v<Value>) {
        return std::format("{:g}", value);
    } else {
        return std::format("{}", value);
    }
}

/// Checks one field of `config`: a value outside the field's range (or NaN) is replaced with
/// the value from `defaults`, and an `Error` naming `prefix` + the field is added to `errors`.
template <typename Config, typename Value>
void checkField(Config& config, const Config& defaults, std::string_view prefix,
                const Field<Config, Value>& field, std::vector<Error>& errors) {
    Value& value = config.*field.member;
    bool isNan = false;
    if constexpr (std::is_floating_point_v<Value>) {
        isNan = std::isnan(value);
    }
    if (!isNan && value >= field.min && value <= field.max) {
        return;
    }
    const Value fallback = defaults.*field.member;
    const std::string unit = field.unit.empty() ? std::string{} : std::format(" {}", field.unit);
    errors.push_back(
        Error{std::format("{}{}: {}{} is outside {}-{}{}; using the default {}{}", prefix,
                          field.name, formatValue(value), unit, formatValue(field.min),
                          formatValue(field.max), unit, formatValue(fallback), unit)});
    value = fallback;
}

/// Checks every field in `fields` (a tuple of `Field`s for `Config`) and returns `config` with
/// bad values replaced by the built-in defaults. Each replacement is added to `errors`.
template <typename Config, typename... Fields>
[[nodiscard]] Config checkFields(Config config, std::string_view prefix,
                                 const std::tuple<Fields...>& fields, std::vector<Error>& errors) {
    const Config defaults{};
    std::apply(
        [&](const auto&... field) { (checkField(config, defaults, prefix, field, errors), ...); },
        fields);
    return config;
}

} // namespace pianolab::engine_core
