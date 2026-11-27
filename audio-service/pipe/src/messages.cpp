// The service pipe's messages, read and written with Glaze. The only file that includes Glaze.

#include <pianolab/pipe/messages.hpp>

#include <glaze/glaze.hpp>

#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace pianolab::pipe {

/// Types Glaze reads by reflection. They can't live in an anonymous namespace: on MSVC, Glaze's
/// reflection needs types with external linkage.
namespace detail {

/// A JSON-RPC 2.0 request as it arrives, with its parameters still unread.
struct Envelope {
    std::string jsonrpc;                 ///< must be "2.0"
    std::optional<std::int64_t> id;      ///< absent for notifications
    std::string method;                  ///< e.g. "settings.set"
    std::optional<glz::raw_json> params; ///< read once the method is known
};

/// Just a request's id, as raw JSON text, for error replies. Kept as text so a broken or
/// non-numeric id can't be mistaken for a number half-read.
struct IdOnly {
    std::optional<glz::raw_json> id;
};

/// A JSON-RPC 2.0 success reply.
struct ResultReply {
    std::string jsonrpc = "2.0";
    std::int64_t id = 0;
    Result result;
};

/// A JSON-RPC 2.0 error's details.
struct ErrorDetails {
    int code = 0;
    std::string message;
};

/// A JSON-RPC 2.0 error reply. `id` is raw JSON so it can be `null` (written even then, as the
/// spec requires) when the request's id couldn't be read.
struct ErrorReply {
    std::string jsonrpc = "2.0";
    glz::raw_json id;
    ErrorDetails error;
};

/// A JSON-RPC 2.0 notification: an event, with its parameters.
template <typename P> struct Notification {
    std::string jsonrpc = "2.0";
    std::string method;
    P params;
};

} // namespace detail

namespace {

using detail::Envelope;
using detail::IdOnly;

/// Glaze's options for reading the pipe: input isn't null-terminated (lines are views into a
/// buffer), unknown and missing keys are errors, and nothing may follow the message.
struct ReadOptions : glz::opts {
    /// Reject anything after the message. Glaze looks this option up by its exact name.
    bool validate_trailing_whitespace = true; // NOLINT(readability-identifier-naming)
};

/// The options every read uses.
constexpr ReadOptions readOptions = [] {
    ReadOptions options{};
    options.null_terminated = false;
    options.error_on_unknown_keys = true;
    options.error_on_missing_keys = true;
    return options;
}();

/// The options for reading just a request's `id`, ignoring everything else, so an error reply
/// can still name the request it answers.
constexpr ReadOptions idOnlyOptions = [] {
    ReadOptions options{};
    options.null_terminated = false;
    options.error_on_unknown_keys = false;
    options.validate_trailing_whitespace = false;
    return options;
}();

/// The request's id, if the line has a readable one. Reading goes front to back, so an id that
/// comes before the point where a broken line goes wrong is still found.
std::optional<std::int64_t> readId(std::string_view line) {
    IdOnly found{};
    // The error is ignored on purpose: whatever was read before it is all we want.
    static_cast<void>(glz::read<idOnlyOptions>(found, line));
    if (!found.id) {
        return std::nullopt;
    }
    // Only a complete whole number counts.
    std::int64_t id = 0;
    if (glz::read<readOptions>(id, std::string_view(found.id->str))) {
        return std::nullopt;
    }
    return id;
}

/// An error for request `id`.
std::unexpected<RpcError> failure(std::optional<std::int64_t> id, int code, std::string message) {
    return std::unexpected(RpcError{.id = id, .code = code, .message = std::move(message)});
}

/// Reads `params` (an object; absent means `{}`) as a `T`.
template <typename T>
std::expected<T, RpcError> readParams(const std::optional<glz::raw_json>& params,
                                      std::optional<std::int64_t> id) {
    const std::string_view text = params ? std::string_view(params->str) : "{}";
    T value{};
    if (const auto error = glz::read<readOptions>(value, text)) {
        return failure(id, error_code::invalidParams,
                       "invalid params: " + glz::format_error(error, text));
    }
    return value;
}

/// Reads a method's parameters into the `Command` that method stands for.
using ParamsReader = std::function<std::expected<Command, RpcError>(
    const std::optional<glz::raw_json>&, std::optional<std::int64_t>)>;

/// A reader for a method whose parameters are a `T`.
template <typename T> ParamsReader paramsAs() {
    return [](const std::optional<glz::raw_json>& params,
              std::optional<std::int64_t> id) -> std::expected<Command, RpcError> {
        return readParams<T>(params, id).transform([](T value) {
            return Command{std::move(value)};
        });
    };
}

/// Every method, and how its parameters are read.
const std::map<std::string, ParamsReader, std::less<>>& methods() {
    static const std::map<std::string, ParamsReader, std::less<>> table{
        {"note.on", paramsAs<NoteOn>()},
        {"note.off", paramsAs<NoteOff>()},
        {"settings.get", paramsAs<GetSettings>()},
        {"settings.schema", paramsAs<GetSchema>()},
        {"settings.set",
         [](const std::optional<glz::raw_json>& params,
            std::optional<std::int64_t> id) -> std::expected<Command, RpcError> {
             return readParams<SettingValues>(params, id).transform([](SettingValues values) {
                 return Command{SetSettings{.values = std::move(values)}};
             });
         }},
        {"devices.list", paramsAs<ListDevices>()},
        {"recording.start", paramsAs<StartRecording>()},
        {"recording.stop", paramsAs<StopRecording>()},
        {"status", paramsAs<GetStatus>()},
    };
    return table;
}

/// Writes `value` as one line of JSON. Writing can only fail if something is badly wrong (out of
/// memory); then the line is a JSON-RPC error with no id, which is still valid on the pipe.
template <typename T> std::string toLine(const T& value) {
    std::string line;
    if (glz::write_json(value, line)) {
        return R"({"jsonrpc":"2.0","id":null,"error":{"code":-32603,"message":"couldn't write a message"}})";
    }
    return line;
}

/// The event's method name on the pipe.
struct EventName {
    std::string_view operator()(const Ready& /*event*/) const { return "ready"; }
    std::string_view operator()(const LogLine& /*event*/) const { return "log"; }
    std::string_view operator()(const ActiveKeys& /*event*/) const { return "keys"; }
    std::string_view operator()(const Level& /*event*/) const { return "level"; }
    std::string_view operator()(const SettingsChanged& /*event*/) const { return "settings"; }
};

} // namespace

std::expected<Request, RpcError> readRequest(std::string_view line) {
    if (line.size() > maxLineBytes) {
        return failure(std::nullopt, error_code::invalidRequest, "line too long");
    }
    Envelope envelope{};
    if (const auto error = glz::read<readOptions>(envelope, line)) {
        const bool isJson = !glz::validate_json(line);
        return failure(readId(line), isJson ? error_code::invalidRequest : error_code::parseError,
                       (isJson ? "not a JSON-RPC 2.0 request: " : "not JSON: ") +
                           glz::format_error(error, line));
    }
    if (envelope.jsonrpc != "2.0") {
        return failure(envelope.id, error_code::invalidRequest, R"(jsonrpc must be "2.0")");
    }
    const auto& table = methods();
    const auto method = table.find(envelope.method);
    if (method == table.end()) {
        return failure(envelope.id, error_code::methodNotFound,
                       "unknown method '" + envelope.method + "'");
    }
    return method->second(envelope.params, envelope.id).transform([&](Command command) {
        return Request{.id = envelope.id, .command = std::move(command)};
    });
}

std::string resultLine(std::int64_t id, const Result& result) {
    return toLine(detail::ResultReply{.id = id, .result = result});
}

std::string errorLine(const RpcError& error) {
    return toLine(detail::ErrorReply{
        .id = glz::raw_json{error.id ? std::to_string(*error.id) : std::string("null")},
        .error = {.code = error.code, .message = error.message},
    });
}

std::string eventLine(const Event& event) {
    return std::visit(
        [](const auto& params) {
            using P = std::remove_cvref_t<decltype(params)>;
            return toLine(detail::Notification<P>{.method = std::string(EventName{}(params)),
                                                  .params = params});
        },
        event);
}

} // namespace pianolab::pipe
