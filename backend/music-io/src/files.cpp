#include <pianolab/engine_core/error.hpp>
#include <pianolab/music_io/files.hpp>
#include <pianolab/music_io/midi.hpp>
#include <pianolab/music_io/musicxml.hpp>
#include <pianolab/music_io/reading.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace pianolab::music_io {

using engine_core::Error;

namespace {

/// `path`'s extension in lower case, e.g. ".mid".
std::string lowerExtension(const std::filesystem::path& path) {
    std::string extension = displayPath(path.extension());
    std::ranges::transform(extension, extension.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

/// The UTF-8 bytes of `bytes` as text, without a leading byte-order mark.
std::string_view asText(std::span<const std::byte> bytes) {
    std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    constexpr std::string_view byteOrderMark = "\xEF\xBB\xBF";
    if (text.starts_with(byteOrderMark)) {
        text.remove_prefix(byteOrderMark.size());
    }
    return text;
}

} // namespace

std::string displayPath(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();
    return {text.begin(), text.end()};
}

std::expected<std::vector<std::byte>, Error> readBytes(const std::filesystem::path& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        return std::unexpected(
            Error{std::format("can't read {}: {}", displayPath(path), error.message())});
    }
    std::vector<std::byte> bytes(size);
    std::ifstream file(path, std::ios::binary);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    if (!file) {
        return std::unexpected(Error{std::format("can't read {}", displayPath(path))});
    }
    return bytes;
}

std::expected<void, Error> writeBytes(const std::filesystem::path& path,
                                      std::span<const std::byte> bytes) {
    std::ofstream file(path, std::ios::binary); // output mode truncates an existing file
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    file.close();
    if (!file) {
        return std::unexpected(Error{std::format("can't write {}", displayPath(path))});
    }
    return {};
}

std::expected<Reading, Error> readMusicFile(const std::filesystem::path& path) {
    const std::string extension = lowerExtension(path);
    const bool isMidi = extension == ".mid" || extension == ".midi";
    const bool isMusicXml = extension == ".musicxml" || extension == ".xml";
    const bool isCompressed = extension == ".mxl";
    if (!isMidi && !isMusicXml && !isCompressed) {
        return std::unexpected(Error{
            std::format("{}: unknown file type '{}'; expected .mid, .midi, .musicxml, .xml or .mxl",
                        displayPath(path), extension)});
    }

    const auto bytes = readBytes(path);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    auto reading = isMidi       ? readMidi(*bytes)
                   : isMusicXml ? readMusicXml(asText(*bytes))
                                : readCompressedMusicXml(*bytes);
    return reading.transform_error([&](const Error& error) {
        return Error{std::format("{}: {}", displayPath(path), error.message)};
    });
}

} // namespace pianolab::music_io
