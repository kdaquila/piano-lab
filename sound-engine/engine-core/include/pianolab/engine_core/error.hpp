#pragma once

#include <string>

namespace pianolab::engine_core {

/// The one error type: a human-readable message saying what went wrong and what was done about
/// it, e.g. "sine.attackT60: -0.1 s is outside 0.0005-1 s; using the default 0.005 s".
///
/// Errors are values: a function that can fail returns `std::expected<T, Error>`, and validation
/// returns a list of them alongside its result. Our code never throws them.
struct Error {
    std::string message;
};

} // namespace pianolab::engine_core
