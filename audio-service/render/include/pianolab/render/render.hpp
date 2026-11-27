#pragma once

#include <pianolab/engine_core/error.hpp>

#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

/// `piano-lab render`: renders a music file with the piano engine to a WAV file, with no audio
/// device. Built for AI agents and tests: deterministic, and it never stops for a problem it can
/// work around.
namespace pianolab::render {

/// The command's help text.
inline constexpr std::string_view usage =
    R"(usage: piano-lab render <input> [-o <output.wav>] [--sample-rate <hz>] [--settings <file>]

Renders a music file with the piano engine to a WAV file (32-bit float, mono). The render runs
until the piano falls silent after the last note. The piano sounds as it does live: it uses the
piano settings in settings.toml, next to piano-lab, if there is one.

  <input>              a MIDI file (.mid, .midi) or MusicXML score (.musicxml, .xml, .mxl)
  -o, --out <file>     where to write the WAV (default: the input's path with .wav)
  --sample-rate <hz>   the sample rate in hertz, a whole number (default: 48000)
  --settings <file>    use this settings file instead of piano-lab's own settings.toml
  -h, --help           show this help

Anything that had to be skipped or fixed is reported on stderr, one line each. The exit code is
0 when a WAV was written, and 1 when nothing could be written.
)";

/// What to render, from the command line.
struct Options {
    std::filesystem::path input;
    std::filesystem::path output;
    std::optional<double> sampleRate; ///< hertz; the built-in setting when not given
    /// The settings file to use (`--settings`). Empty: the install folder's `settings.toml`, if
    /// there is one.
    std::filesystem::path settings;
    bool help = false; ///< show the help text instead of rendering
};

/// Parses the command-line arguments (without the program name). Returns an `Error` saying
/// what's wrong for anything it can't make sense of: a missing input, an unknown option, a
/// missing or malformed value, or an output path that would overwrite the input.
[[nodiscard]] std::expected<Options, engine_core::Error>
parseArguments(std::span<const std::string_view> arguments);

/// How long a render may continue after the last event before it's cut off, in seconds.
inline constexpr double maxTailSeconds = 30.0;

/// What a finished render did.
struct Summary {
    std::filesystem::path output;
    double seconds = 0.0;           ///< the length of the WAV written
    std::filesystem::path settings; ///< the settings file used; empty: the built-in settings
    std::vector<engine_core::Error> warnings; ///< everything skipped or fixed along the way
};

/// Reads the input file, renders it until the piano falls silent (at most `maxTailSeconds` after
/// the last event), and writes the WAV.
///
/// The piano uses the piano sections of `options.settings`, or else of `installSettings` (the
/// install folder's `settings.toml`) if that file exists, or else the built-in settings.
///
/// Problems it can work around (unsupported notation, invalid events, problems in the settings,
/// an install settings file that can't be read, a render cut off at the cap) become warnings.
/// Returns an `Error`, having written nothing, when the output names the input file (however
/// it's spelled), the input can't be read, the `--settings` file can't be read or isn't valid
/// TOML, the render would be too long for one WAV file (checked before rendering), or the output
/// can't be written.
[[nodiscard]] std::expected<Summary, engine_core::Error>
renderFile(const Options& options, const std::filesystem::path& installSettings);

} // namespace pianolab::render
