---
paths:
  - "sound-engine/**/*.{hpp,cpp,h}"
  - "audio-service/**/*.{hpp,cpp,h}"
---

# C++23: what we can use

We build as C++23. A feature may only be used if **all target compilers** support it fully, because these lists are easy to get wrong from memory (training data goes stale).

**Targets (the minimum versions):** MSVC 19.44 (VS 2022 17.14), GCC 13 / libstdc++ 13, Clang 17 / libc++ 17, Apple Clang 15.
**Source:** the cppreference C++23 compiler-support table, checked 2026-11-22. Re-check it (https://en.cppreference.com/w/cpp/compiler_support/23) before using anything not listed here. Passing the MSVC build alone doesn't prove a feature is portable, and clang-tidy's `modernize-*` suggestions don't either.

## Use freely (portable to all targets)
- **Errors and absence:** `std::expected<T, Error>` (our `Result`) and `std::optional<T>` (our `Option`), including their standard monadic methods (`and_then`, `transform`, `or_else`, `transform_error`). Don't write our own monad types.
- **Utilities:** `std::to_underlying`, `std::unreachable()`, `std::byteswap`, `std::invoke_r`, `std::is_scoped_enum`, `string::contains` / `string_view::contains`, `basic_string::resize_and_overwrite`
- **constexpr:** `std::unique_ptr`, `std::bitset`, integral `to_chars`/`from_chars`, `type_info::operator==`
- **Ranges:** `views::repeat`, `views::as_rvalue` (plus everything C++20 ranges offers)
- **Language:** `if consteval`, the `uz` literal suffix for `std::size_t`, multidimensional `operator[](i, j)`, attributes on lambdas, `()`-less lambdas with specifiers, `#elifdef`/`#elifndef`, non-literal variables in constexpr functions
- **Containers:** iterator-pair constructors for `std::stack`/`std::queue`

## Don't use yet (not supported by every target)
| Feature | Missing on |
|---|---|
| Deducing `this` (explicit object parameters) | GCC 13, Clang 17, Apple Clang 15 |
| `std::print` / `std::println` | GCC 13, Clang 17 (partial) |
| `std::ranges::to`, container range constructors | GCC 13 (needs 15) |
| `std::mdspan` | GCC 13, Clang 17 (partial) |
| `std::flat_map`, `std::flat_set` | GCC 13, Clang 17, MSVC 19.44, Apple Clang |
| `std::generator` | GCC 13, Clang 17, Apple Clang |
| `std::move_only_function` | Clang 17, Apple Clang |
| `views::zip`, `enumerate`, `chunk`, `chunk_by`, `slide`, `stride`, `join_with`, `cartesian_product`, `as_const` | Clang 17 and/or Apple Clang |
| `ranges::fold_*`, `ranges::contains`, `ranges::starts_with`/`ends_with`, `ranges::find_last` | Clang 17 and/or GCC 13 |
| Formatting ranges and containers with `std::format` | GCC 13 |
| `std::stacktrace` | GCC 13, Clang 17, Apple Clang |
| `[[assume]]`, `static operator()`/`operator[]`, `auto(x)` | one or more targets |
| constexpr `<cmath>`, `std::out_ptr`, `std::start_lifetime_as` | one or more targets |
| `import std;` (standard library modules) | GCC 13, Clang 17, Apple Clang |

When the target minimums are raised (e.g. to GCC 14, Clang 18, Apple Clang 16), update this file from the same table.
