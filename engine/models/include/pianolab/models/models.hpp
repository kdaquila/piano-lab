#pragma once

#include <string_view>

/// Physical models of piano keys: strings, hammers, bridge, soundboard.
namespace pianolab::models {

/// The library's name. A placeholder until the library has real contents.
[[nodiscard]] std::string_view libraryName() noexcept;

} // namespace pianolab::models
