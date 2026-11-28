#pragma once

#include <string_view>

/// Calibration: fitting model parameters to measurements and listening tests.
namespace pianolab::calibration {

/// The library's name. A placeholder until the library has real contents.
[[nodiscard]] std::string_view libraryName() noexcept;

} // namespace pianolab::calibration
