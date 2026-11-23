#include <pianolab/engine_core/config.hpp>
#include <pianolab/engine_core/error.hpp>
#include <pianolab/engine_core/performance.hpp>
#include <pianolab/engine_core/render_offline.hpp>
#include <pianolab/models/piano_key.hpp>
#include <pianolab/music_io/files.hpp>
#include <pianolab/music_io/wav.hpp>
#include <pianolab/render/render.hpp>

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace pianolab::render {

using engine_core::Error;

namespace {

/// `text` as a whole number of hertz, or nothing if it isn't one.
std::optional<double> parseSampleRate(std::string_view text) {
    std::uint32_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return static_cast<double>(value);
}

/// Adds `more` to the end of `warnings`.
void append(std::vector<Error>& warnings, std::vector<Error> more) {
    warnings.insert(warnings.end(), std::make_move_iterator(more.begin()),
                    std::make_move_iterator(more.end()));
}

} // namespace

std::expected<Options, Error> parseArguments(std::span<const std::string_view> arguments) {
    Options options;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const std::string_view argument = arguments[index];
        const bool hasValue = index + 1 < arguments.size();
        if (argument == "-h" || argument == "--help") {
            options.help = true;
        } else if (argument == "-o" || argument == "--out") {
            if (!hasValue) {
                return std::unexpected(Error{std::format("{} needs a file name", argument)});
            }
            options.output = arguments[++index];
        } else if (argument == "--sample-rate") {
            if (!hasValue) {
                return std::unexpected(Error{"--sample-rate needs a value in hertz"});
            }
            const std::string_view value = arguments[++index];
            options.sampleRate = parseSampleRate(value);
            if (!options.sampleRate) {
                return std::unexpected(
                    Error{std::format("--sample-rate: '{}' isn't a whole number of hertz", value)});
            }
        } else if (argument.starts_with('-')) {
            return std::unexpected(Error{std::format("unknown option '{}'", argument)});
        } else if (!options.input.empty()) {
            return std::unexpected(Error{std::format(
                "only one input file can be rendered at a time; got '{}' too", argument)});
        } else {
            options.input = argument;
        }
    }

    if (options.help) {
        return options;
    }
    if (options.input.empty()) {
        return std::unexpected(Error{"no input file given"});
    }
    if (options.output.empty()) {
        options.output = std::filesystem::path(options.input).replace_extension(".wav");
    }
    if (options.output == options.input) {
        return std::unexpected(Error{"the output file would overwrite the input"});
    }
    return options;
}

std::expected<Summary, Error> renderFile(const Options& options) {
    auto reading = music_io::readMusicFile(options.input);
    if (!reading) {
        return std::unexpected(reading.error());
    }
    Summary summary{.output = options.output, .seconds = 0.0, .warnings = {}};
    append(summary.warnings, std::move(reading->warnings));

    auto performance = engine_core::validate(reading->performance);
    append(summary.warnings, std::move(performance.errors));

    models::PianoConfig config;
    if (options.sampleRate) {
        config.render.sampleRate = *options.sampleRate;
    }
    auto piano = models::validate(config);
    append(summary.warnings, std::move(piano.errors));

    auto keyboard = models::makePianoKeyboard(piano.config);
    const auto render = engine_core::validPart(piano.config, &models::PianoConfig::render);
    const double sampleRate = render->sampleRate;
    const auto events = engine_core::toTimedEvents(performance.config, render);
    const auto maxTail = static_cast<std::size_t>(std::llround(maxTailSeconds * sampleRate));
    const auto result = engine_core::renderUntilSilent(keyboard, events, maxTail);

    if (result.stoppedAtCap) {
        summary.warnings.push_back({std::format(
            "the piano was still sounding {} s after the last event; stopped there (a note that "
            "is never released?)",
            engine_core::formatValue(maxTailSeconds))});
    }
    if (keyboard.diagnostics.total > 0) {
        summary.warnings.push_back(
            {std::format("the engine recovered from {} problems while rendering; the last: {}",
                         keyboard.diagnostics.total, keyboard.diagnostics.last)});
    }

    const auto wav = music_io::encodeWav(result.samples, sampleRate);
    if (!wav) {
        return std::unexpected(wav.error());
    }
    if (auto written = music_io::writeBytes(options.output, *wav); !written) {
        return std::unexpected(written.error());
    }
    summary.seconds = static_cast<double>(result.samples.size()) / sampleRate;
    return summary;
}

} // namespace pianolab::render
