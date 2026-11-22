#pragma once

#include <string_view>

/// Core engine types: key models, the 88-key keyboard, note events, offline rendering, tuning.
/// Depends on nothing.
namespace pianolab::engine_core {

/// The library's name. A placeholder until the library has real contents.
[[nodiscard]] std::string_view libraryName() noexcept;

} // namespace pianolab::engine_core
