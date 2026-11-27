#include <pianolab/engine_core/error.hpp>
#include <pianolab/live/devices.hpp>
#include <pianolab/settings/settings.hpp>

#include <algorithm>
#include <cstddef>
#include <format>
#include <string>
#include <vector>

namespace pianolab::live {

using engine_core::Error;

namespace {

/// `names` as a readable list in quotes, e.g. "A", "B"; or "none" when empty.
std::string listNames(const std::vector<std::string>& names) {
    if (names.empty()) {
        return "none";
    }
    std::string list;
    for (const auto& name : names) {
        list += std::format("{}\"{}\"", list.empty() ? "" : ", ", name);
    }
    return list;
}

/// The names of the device types in `available` that have at least one output.
std::vector<std::string> typesWithOutputs(const std::vector<AudioDeviceType>& available) {
    std::vector<std::string> names;
    for (const auto& type : available) {
        if (!type.outputs.empty()) {
            names.push_back(type.name);
        }
    }
    return names;
}

/// The default output of `type`, or its first if the default index is out of range.
std::string defaultOutput(const AudioDeviceType& type) {
    return type.defaultOutput < type.outputs.size() ? type.outputs[type.defaultOutput]
                                                    : type.outputs.front();
}

} // namespace

AudioChoice chooseAudioOutput(const settings::AudioSettings& settings,
                              const std::vector<AudioDeviceType>& available) {
    AudioChoice choice;
    const auto firstWithOutputs =
        std::ranges::find_if(available, [](const auto& type) { return !type.outputs.empty(); });
    if (firstWithOutputs == available.end()) {
        return choice;
    }

    const AudioDeviceType* type = &*firstWithOutputs;
    if (settings.type.empty() && !settings.output.empty()) {
        // Only an output named: it decides the type, e.g. an ASIO interface's name.
        const auto owner = std::ranges::find_if(available, [&](const auto& candidate) {
            return std::ranges::find(candidate.outputs, settings.output) != candidate.outputs.end();
        });
        if (owner != available.end()) {
            type = &*owner;
        }
    }
    if (!settings.type.empty()) {
        const auto named = std::ranges::find(available, settings.type, &AudioDeviceType::name);
        if (named == available.end()) {
            choice.warnings.push_back(Error{std::format(
                R"(audio.type: "{}" isn't available (types with outputs: {}); using "{}")",
                settings.type, listNames(typesWithOutputs(available)), type->name)});
        } else if (named->outputs.empty()) {
            choice.warnings.push_back(Error{std::format(
                R"(audio.type: "{}" has no outputs (types with outputs: {}); using "{}")",
                settings.type, listNames(typesWithOutputs(available)), type->name)});
        } else {
            type = &*named;
        }
    }
    choice.type = type->name;

    choice.output = defaultOutput(*type);
    if (!settings.output.empty()) {
        if (std::ranges::find(type->outputs, settings.output) != type->outputs.end()) {
            choice.output = settings.output;
        } else if (settings.type.empty()) {
            std::vector<std::string> everyOutput;
            for (const auto& candidate : available) {
                everyOutput.insert(everyOutput.end(), candidate.outputs.begin(),
                                   candidate.outputs.end());
            }
            choice.warnings.push_back(
                Error{std::format(R"(audio.output: "{}" isn't available (outputs: {}); using "{}")",
                                  settings.output, listNames(everyOutput), choice.output)});
        } else {
            choice.warnings.push_back(Error{std::format(
                R"(audio.output: "{}" isn't available as a "{}" output (outputs: {}); using "{}")",
                settings.output, type->name, listNames(type->outputs), choice.output)});
        }
    }
    return choice;
}

MidiChoice chooseMidiInputs(const settings::MidiSettings& settings,
                            const std::vector<std::string>& available) {
    MidiChoice choice;
    const auto openAll = [&] {
        choice.inputs.clear();
        for (std::size_t index = 0; index < available.size(); ++index) {
            choice.inputs.push_back(index);
        }
    };
    if (settings.inputs.empty()) {
        openAll();
        return choice;
    }

    for (std::size_t index = 0; index < available.size(); ++index) {
        if (std::ranges::find(settings.inputs, available[index]) != settings.inputs.end()) {
            choice.inputs.push_back(index);
        }
    }
    for (auto name = settings.inputs.begin(); name != settings.inputs.end(); ++name) {
        const bool reported = std::find(settings.inputs.begin(), name, *name) != name;
        if (!reported && std::ranges::find(available, *name) == available.end()) {
            choice.warnings.push_back(Error{std::format(
                R"(midi.inputs: "{}" isn't connected (inputs: {}))", *name, listNames(available))});
        }
    }
    if (choice.inputs.empty() && !available.empty()) {
        choice.warnings.push_back(
            Error{"midi.inputs: none of the listed inputs is connected; opening every input"});
        openAll();
    }
    return choice;
}

} // namespace pianolab::live
