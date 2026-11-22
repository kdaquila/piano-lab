#pragma once

#include <string_view>

/// Real-time plumbing shared by the engine and the backend, such as lock-free queues. Standard C++
/// only.
namespace pianolab::rt {

/// The library's name. A placeholder until the library has real contents.
[[nodiscard]] std::string_view libraryName() noexcept;

} // namespace pianolab::rt
