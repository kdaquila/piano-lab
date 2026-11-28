#include <pianolab/engine_core/config.hpp>
#include <pianolab/engine_core/error.hpp>
#include <pianolab/engine_core/render_config.hpp>

#include <string_view>
#include <utility>
#include <vector>

namespace pianolab::engine_core {

Validated<RenderConfig> validate(const RenderConfig& config, std::string_view prefix) {
    std::vector<Error> errors;
    RenderConfig checked = checkFields(config, prefix, renderConfigFields, errors);
    return {.config = detail::makeValid(checked), .errors = std::move(errors)};
}

} // namespace pianolab::engine_core
