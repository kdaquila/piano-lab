#include <pianolab/engine_core/config.hpp>
#include <pianolab/engine_core/error.hpp>
#include <pianolab/engine_core/performance.hpp>
#include <pianolab/engine_core/render_offline.hpp>
#include <pianolab/models/piano_key.hpp>
#include <pianolab/music_io/files.hpp>
#include <pianolab/music_io/wav.hpp>
#include <pianolab/render/render.hpp>
#include <pianolab/settings/settings.hpp>

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

/// The path named by UTF-8 `text`. (Constructing a path from `char` text would read it in the
/// system's code page on Windows, losing characters outside it.)
std::filesystem::path pathFromUtf8(std::string_view text) {
    return {std::u8string(text.begin(), text.end())};
}

/// True if `a` and `b` name the same existing file, however they're spelled.
bool sameFile(const std::filesystem::path& a, const std::filesystem::path& b) {
    std::error_code error; // e.g. one of them doesn't exist yet: then they can't be the same
    return std::filesystem::equivalent(a, b, error) && !error;
}

/// Adds `more` to the end of `warnings`.
void append(std::vector<Error>& warnings, std::vector<Error> more) {
    warnings.insert(warnings.end(), std::make_move_iterator(more.begin()),
                    std::make_move_iterator(more.end()));
}

/// The piano settings to render with, and the file they came from (empty: the built-ins).
struct PianoSettings {
    models::PianoConfig piano;
    std::filesystem::path file;
};

/// The piano settings for `options` (see `renderFile`), adding any problems to `warnings`.
/// Returns an `Error` only when the file named by `--settings` can't be used.
std::expected<PianoSettings, Error> pianoSettings(const Options& options,
                                                  const std::filesystem::path& installSettings,
                                                  std::vector<Error>& warnings) {
    if (!options.settings.empty()) {
        auto loaded = settings::loadSettings(options.settings);
        if (!loaded) {
            return std::unexpected(Error{std::format("--settings: {}", loaded.error().message)});
        }
        append(warnings, std::move(loaded->warnings));
        return PianoSettings{.piano = loaded->settings.piano, .file = options.settings};
    }
    if (installSettings.empty()) {
        return PianoSettings{};
    }
    std::error_code error;
    const bool exists = std::filesystem::exists(installSettings, error);
    if (error) {
        warnings.push_back(
            Error{std::format("can't check for {}: {}; using the built-in settings",
                              music_io::displayPath(installSettings), error.message())});
        return PianoSettings{};
    }
    if (!exists) {
        return PianoSettings{};
    }
    auto loaded = settings::loadSettings(installSettings);
    if (!loaded) {
        warnings.push_back(
            Error{std::format("{}; using the built-in settings", loaded.error().message)});
        return PianoSettings{};
    }
    append(warnings, std::move(loaded->warnings));
    return PianoSettings{.piano = loaded->settings.piano, .file = installSettings};
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
            options.output = pathFromUtf8(arguments[++index]);
        } else if (argument == "--settings") {
            if (!hasValue) {
                return std::unexpected(Error{"--settings needs a file name"});
            }
            options.settings = pathFromUtf8(arguments[++index]);
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
            options.input = pathFromUtf8(argument);
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

std::expected<Summary, Error> renderFile(const Options& options,
                                         const std::filesystem::path& installSettings) {
    if (options.output == options.input || sameFile(options.output, options.input)) {
        return std::unexpected(Error{"the output file would overwrite the input"});
    }
    Summary summary{.output = options.output, .seconds = 0.0, .settings = {}, .warnings = {}};
    auto chosen = pianoSettings(options, installSettings, summary.warnings);
    if (!chosen) {
        return std::unexpected(chosen.error());
    }
    summary.settings = chosen->file;

    auto reading = music_io::readMusicFile(options.input);
    if (!reading) {
        return std::unexpected(reading.error());
    }
    append(summary.warnings, std::move(reading->warnings));

    auto performance = engine_core::validate(reading->performance);
    append(summary.warnings, std::move(performance.errors));

    models::PianoConfig config = chosen->piano;
    if (options.sampleRate) {
        config.render.sampleRate = *options.sampleRate;
    }
    auto piano = models::validate(config);
    append(summary.warnings, std::move(piano.errors));

    auto keyboard = models::makePianoKeyboard(piano.config);
    const auto render = engine_core::validPart(piano.config, &models::PianoConfig::render);
    const double sampleRate = render->sampleRate;

    // Refuse up front, before allocating, a render that might not fit in one WAV file.
    const double longest =
        (engine_core::lastEventSeconds(performance.config) + maxTailSeconds) * sampleRate +
        static_cast<double>(render->maxBlock);
    if (longest > static_cast<double>(music_io::maxWavSamples)) {
        return std::unexpected(Error{std::format(
            "the performance is too long for one WAV file at {} Hz (its last event is at {} s; "
            "the limit is about {} s including the release); try a lower --sample-rate",
            engine_core::formatValue(sampleRate),
            engine_core::formatValue(engine_core::lastEventSeconds(performance.config)),
            engine_core::formatValue(
                std::floor(static_cast<double>(music_io::maxWavSamples) / sampleRate)))});
    }

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
