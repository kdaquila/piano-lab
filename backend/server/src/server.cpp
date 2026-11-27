// The server's JUCE side: thin adapters from JUCE's device callbacks to the JUCE-free audio core,
// and the main loop. JUCE requires inheritance for its callbacks; nothing else sees it.

#include <pianolab/engine_core/config.hpp>
#include <pianolab/logging/log.hpp>
#include <pianolab/models/piano_key.hpp>
#include <pianolab/server/audio_core.hpp>
#include <pianolab/server/devices.hpp>
#include <pianolab/server/monitor.hpp>
#include <pianolab/server/recorder.hpp>
#include <pianolab/server/server.hpp>
#include <pianolab/settings/settings.hpp>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace pianolab::server {

namespace {

/// How often the main loop checks for news and for a stop request, in milliseconds.
constexpr int monitorIntervalMs = 100;

/// JUCE text as UTF-8.
std::string utf8(const juce::String& text) {
    return text.toStdString();
}

/// UTF-8 text as JUCE text.
juce::String juceText(const std::string& text) {
    return juce::String::fromUTF8(text.data(), static_cast<int>(text.size()));
}

/// Plays the audio core through a JUCE audio device: renders mono and copies it to every
/// output channel.
class AudioCallback final : public juce::AudioIODeviceCallback {
public:
    /// An adapter for the audio core whose shared side is `links`, playing the piano `piano`
    /// (the user's settings, validated once the device's sample rate is known) and logging
    /// device changes.
    AudioCallback(AudioLinks& links, models::PianoConfig piano, const logging::Log& log)
        : links(links), piano(piano), log(log) {}

    /// Before audio starts (not on the audio thread): builds the piano for the device's sample
    /// rate and scratch space for its block size. May allocate.
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override {
        models::PianoConfig config = piano;
        config.render.sampleRate = device->getCurrentSampleRate();
        const auto validated = models::validate(config);
        for (const auto& problem : validated.errors) {
            logging::warning(log, "{}", problem.message);
        }
        state.emplace(makeAudioState(validated.config));
        mono.assign(static_cast<std::size_t>(std::max(device->getCurrentBufferSizeSamples(), 1)),
                    0.0F);
        const int blockSize = device->getCurrentBufferSizeSamples();
        logging::info(log, "audio: {} ({}), {} Hz, {}-sample blocks ({:.1f} ms)",
                      utf8(device->getName()), utf8(device->getTypeName()),
                      engine_core::formatValue(device->getCurrentSampleRate()), blockSize,
                      1000.0 * blockSize / device->getCurrentSampleRate());
    }

    /// On the audio thread: renders the next block into every output channel.
    void audioDeviceIOCallbackWithContext(
        const float* const* /*inputChannelData*/, int /*numInputChannels*/,
        float* const* outputChannelData, int numOutputChannels, int numSamples,
        const juce::AudioIODeviceCallbackContext& /*context*/) override {
        const auto samples = static_cast<std::size_t>(std::max(numSamples, 0));
        const std::span outputs(outputChannelData, static_cast<std::size_t>(numOutputChannels));
        if (!state || mono.empty()) {
            for (float* channel : outputs) {
                if (channel != nullptr) {
                    std::fill_n(channel, samples, 0.0F);
                }
            }
            return;
        }
        // A device may hand over more than it said it would; render in pieces that fit.
        for (std::size_t start = 0; start < samples; start += mono.size()) {
            const std::span block(mono.data(), std::min(mono.size(), samples - start));
            processBlock(*state, links, block);
            for (float* channel : outputs) {
                if (channel != nullptr) {
                    std::ranges::copy(block, channel + start);
                }
            }
        }
    }

    /// The device has stopped; nothing to do.
    void audioDeviceStopped() override {}

private:
    AudioLinks& links;
    models::PianoConfig piano; ///< the user's piano; its sample rate comes from the device
    const logging::Log& log;
    std::optional<AudioState> state; ///< built when the device starts
    std::vector<float> mono;         ///< the engine's mono output for one block
};

/// Passes one MIDI input's messages to the audio core, on that input's thread.
class MidiReceiver final : public juce::MidiInputCallback {
public:
    /// A receiver feeding input number `input` of `links`.
    MidiReceiver(AudioLinks& links, std::size_t input) : links(links), input(input) {}

    /// Called by JUCE for every complete message from the device.
    void handleIncomingMidiMessage(juce::MidiInput* /*source*/,
                                   const juce::MidiMessage& message) override {
        const std::span bytes(message.getRawData(),
                              static_cast<std::size_t>(message.getRawDataSize()));
        receiveMidi(links, input, bytes, message.getTimeStamp());
    }

private:
    AudioLinks& links;
    std::size_t input;
};

/// Ten times a second on the main thread: logs news from the audio side, and ends the message
/// loop once a stop is requested.
class MonitorTimer final : public juce::Timer {
public:
    /// A timer reporting from `links` to `log` and watching `stopRequested`.
    MonitorTimer(AudioLinks& links, const logging::Log& log, const std::atomic<bool>& stopRequested)
        : links(links), log(log), stopRequested(stopRequested) {}
    MonitorTimer(const MonitorTimer&) = delete;
    MonitorTimer& operator=(const MonitorTimer&) = delete;
    MonitorTimer(MonitorTimer&&) = delete;
    MonitorTimer& operator=(MonitorTimer&&) = delete;
    /// Stops the timer before it goes away.
    ~MonitorTimer() override { stopTimer(); }

    /// Reports news; stops the message loop when asked to.
    void timerCallback() override {
        reportNews(state, links, log);
        if (stopRequested.load()) {
            stopTimer();
            juce::MessageManager::getInstance()->stopDispatchLoop();
        }
    }

private:
    AudioLinks& links;
    const logging::Log& log;
    const std::atomic<bool>& stopRequested;
    MonitorState state;
};

/// Every audio device type JUCE offers and its outputs, in JUCE's order, scanning for devices.
/// Logs them at the debug level, for reference.
std::vector<AudioDeviceType> audioDeviceTypes(juce::AudioDeviceManager& manager,
                                              const logging::Log& log) {
    std::vector<AudioDeviceType> types;
    for (auto* type : manager.getAvailableDeviceTypes()) {
        type->scanForDevices();
        const auto names = type->getDeviceNames(false);
        AudioDeviceType found{.name = utf8(type->getTypeName()),
                              .outputs = {},
                              .defaultOutput = static_cast<std::size_t>(
                                  std::max(type->getDefaultDeviceIndex(false), 0))};
        for (const auto& name : names) {
            found.outputs.push_back(utf8(name));
        }
        logging::debug(log, "audio device type {}: {}", found.name,
                       names.isEmpty() ? std::string("(no outputs)")
                                       : utf8(names.joinIntoString(", ")));
        types.push_back(std::move(found));
    }
    return types;
}

/// Opens the output named by `choice` at the sample rate and buffer size `audio` asks for (the
/// device's closest, if it can't do them), with no fallback. Returns JUCE's error text; empty
/// when it opened.
juce::String openAudioDevice(juce::AudioDeviceManager& manager, const AudioChoice& choice,
                             const settings::AudioSettings& audio) {
    // JUCE's own saved-state format is the one way to name the device type, device, rate and
    // buffer size in a single step.
    juce::XmlElement state("DEVICESETUP");
    state.setAttribute("deviceType", juceText(choice.type));
    state.setAttribute("audioOutputDeviceName", juceText(choice.output));
    state.setAttribute("audioDeviceRate", audio.sampleRate);
    if (audio.bufferSize > 0) {
        state.setAttribute("audioDeviceBufferSize", static_cast<int>(audio.bufferSize));
    }
    return manager.initialise(0, 2, &state, false);
}

/// Opens the audio output `settings` asks for, falling back to the defaults for anything it
/// can't (see `chooseAudioOutput`), and to the default output, at the same sample rate and buffer
/// size, if the chosen one won't open. A sample rate or buffer size the device can't do becomes
/// its closest one (JUCE's choice). Logs what happened; returns whether any device opened.
bool openAudio(juce::AudioDeviceManager& manager, const settings::AudioSettings& settings,
               const logging::Log& log) {
    const auto validated = settings::validate(settings);
    for (const auto& problem : validated.errors) {
        logging::warning(log, "{}", problem.message);
    }
    const settings::AudioSettings& audio = *validated.config;
    const auto choice = chooseAudioOutput(audio, audioDeviceTypes(manager, log));
    for (const auto& problem : choice.warnings) {
        logging::warning(log, "{}", problem.message);
    }
    if (choice.type.empty()) {
        logging::error(log, "no audio output found; running silently");
        return false;
    }

    juce::String problem = openAudioDevice(manager, choice, audio);
    if (manager.getCurrentAudioDevice() == nullptr) {
        // e.g. ASIO in use by another program: try the default output, at the same rate and
        // buffer size.
        const auto fallback = chooseAudioOutput({}, audioDeviceTypes(manager, log));
        const bool isDifferent = fallback.type != choice.type || fallback.output != choice.output;
        if (!fallback.type.empty() && isDifferent) {
            logging::warning(log, R"(audio: "{}" ({}) couldn't be opened{}; trying "{}" ({}))",
                             choice.output, choice.type,
                             problem.isEmpty() ? std::string{} : ": " + utf8(problem),
                             fallback.output, fallback.type);
            problem = openAudioDevice(manager, fallback, audio);
        }
    }
    auto* device = manager.getCurrentAudioDevice();
    if (device == nullptr) {
        logging::error(log, "no audio output could be opened{}; running silently",
                       problem.isEmpty() ? std::string{} : ": " + utf8(problem));
        return false;
    }
    if (problem.isNotEmpty()) {
        logging::warning(log, "audio output: {}", utf8(problem));
    }
    if (device->getCurrentSampleRate() != audio.sampleRate) {
        logging::info(log, "audio: the device can't run at {} Hz; using its closest, {} Hz",
                      engine_core::formatValue(audio.sampleRate),
                      engine_core::formatValue(device->getCurrentSampleRate()));
    }
    if (audio.bufferSize > 0 &&
        device->getCurrentBufferSizeSamples() != static_cast<int>(audio.bufferSize)) {
        logging::info(log, "audio: the device can't use {}-sample buffers; using its closest, {}",
                      audio.bufferSize, device->getCurrentBufferSizeSamples());
    }
    return true;
}

/// The MIDI inputs `settings` asks for, out of every input connected (see `chooseMidiInputs`).
/// Logs any listed input that isn't connected.
juce::Array<juce::MidiDeviceInfo> chosenMidiInputs(const settings::MidiSettings& settings,
                                                   const logging::Log& log) {
    const auto available = juce::MidiInput::getAvailableDevices();
    std::vector<std::string> names;
    std::string list;
    for (const auto& device : available) {
        names.push_back(utf8(device.name));
        list += (list.empty() ? "" : ", ") + names.back();
    }
    logging::debug(log, "MIDI inputs: {}", names.empty() ? std::string("(none)") : list);
    const auto choice = chooseMidiInputs(settings, names);
    for (const auto& problem : choice.warnings) {
        logging::warning(log, "{}", problem.message);
    }
    juce::Array<juce::MidiDeviceInfo> chosen;
    for (const std::size_t index : choice.inputs) {
        chosen.add(available[static_cast<int>(index)]);
    }
    return chosen;
}

/// The MIDI inputs that opened, and their receivers.
struct MidiInputs {
    std::vector<std::unique_ptr<MidiReceiver>> receivers;
    std::vector<std::unique_ptr<juce::MidiInput>> inputs;
};

/// Opens every input in `devices`, feeding input `i` of `links` from device `i`. Logs each one;
/// inputs that fail to open are skipped.
MidiInputs openMidiInputs(const juce::Array<juce::MidiDeviceInfo>& devices, AudioLinks& links,
                          const logging::Log& log) {
    MidiInputs opened;
    for (int index = 0; index < devices.size(); ++index) {
        const auto& device = devices.getReference(index);
        auto receiver = std::make_unique<MidiReceiver>(links, static_cast<std::size_t>(index));
        auto input = juce::MidiInput::openDevice(device.identifier, receiver.get());
        if (input == nullptr) {
            logging::warning(log, "MIDI input {} couldn't be opened; skipped", utf8(device.name));
            continue;
        }
        input->start();
        logging::info(log, "MIDI input: {}", utf8(device.name));
        opened.receivers.push_back(std::move(receiver));
        opened.inputs.push_back(std::move(input));
    }
    if (devices.isEmpty()) {
        logging::warning(log, "no MIDI inputs found; plug in a keyboard and restart the server");
    }
    return opened;
}

} // namespace

void runServer(const settings::Settings& settings, const logging::Log& log,
               const std::atomic<bool>& stopRequested) {
    // This thread becomes JUCE's message thread: device notifications arrive through it.
    juce::MessageManager::getInstance();
    {
        const auto midiDevices = chosenMidiInputs(settings.midi, log);
        const auto links = makeAudioLinks(static_cast<std::size_t>(midiDevices.size()));

        // The recorder is ready for the control API (step 0.6) to start and stop recordings.
        Recorder recorder(*links);
        recorder.startThread(log);

        juce::AudioDeviceManager manager;
        AudioCallback callback(*links, settings.piano, log);
        if (openAudio(manager, settings.audio, log)) {
            manager.addAudioCallback(&callback);
        }
        auto midi = openMidiInputs(midiDevices, *links, log);

        logging::info(log, "the piano is ready; press Ctrl+C to stop");
        MonitorTimer monitor(*links, log, stopRequested);
        monitor.startTimer(monitorIntervalMs);
        juce::MessageManager::getInstance()->runDispatchLoop();

        logging::info(log, "stopping");
        for (auto& input : midi.inputs) {
            input->stop();
        }
        midi.inputs.clear();
        manager.removeAudioCallback(&callback);
        manager.closeAudioDevice();
        monitor.timerCallback(); // anything the audio side reported in its last moments
    }
    juce::MessageManager::deleteInstance();
    logging::info(log, "stopped");
}

} // namespace pianolab::server
