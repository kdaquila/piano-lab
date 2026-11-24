// The server's JUCE side: thin adapters from JUCE's device callbacks to the JUCE-free audio core,
// and the main loop. JUCE requires inheritance for its callbacks; nothing else sees it.

#include <pianolab/engine_core/config.hpp>
#include <pianolab/logging/log.hpp>
#include <pianolab/models/piano_key.hpp>
#include <pianolab/server/audio_core.hpp>
#include <pianolab/server/monitor.hpp>
#include <pianolab/server/recorder.hpp>
#include <pianolab/server/server.hpp>

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
#include <vector>

namespace pianolab::server {

namespace {

/// The sample rate the server asks the audio device for, in hertz.
constexpr double preferredSampleRate = 48'000.0;
/// How often the main loop checks for news and for a stop request, in milliseconds.
constexpr int monitorIntervalMs = 100;

/// JUCE text as UTF-8.
std::string utf8(const juce::String& text) {
    return text.toStdString();
}

/// Plays the audio core through a JUCE audio device: renders mono and copies it to every
/// output channel.
class AudioCallback final : public juce::AudioIODeviceCallback {
public:
    /// An adapter for the audio core whose shared side is `links`, logging device changes.
    AudioCallback(AudioLinks& links, const logging::Log& log) : links(links), log(log) {}

    /// Before audio starts (not on the audio thread): builds the piano for the device's sample
    /// rate and scratch space for its block size. May allocate.
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override {
        models::PianoConfig config;
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

/// Logs every audio device type and its output devices, for reference.
void logAvailableDevices(juce::AudioDeviceManager& manager, const logging::Log& log) {
    for (auto* type : manager.getAvailableDeviceTypes()) {
        type->scanForDevices();
        const auto names = type->getDeviceNames(false);
        logging::debug(log, "audio device type {}: {}", utf8(type->getTypeName()),
                       names.isEmpty() ? std::string("(no outputs)")
                                       : utf8(names.joinIntoString(", ")));
    }
}

/// Opens the default audio output, preferring 48 kHz. Logs what happened; returns whether any
/// device opened.
bool openAudio(juce::AudioDeviceManager& manager, const logging::Log& log) {
    juce::AudioDeviceManager::AudioDeviceSetup preferred;
    preferred.sampleRate = preferredSampleRate;
    const juce::String problem = manager.initialise(0, 2, nullptr, true, {}, &preferred);
    if (manager.getCurrentAudioDevice() == nullptr) {
        logging::error(log, "no audio output could be opened{}; running silently",
                       problem.isEmpty() ? std::string{} : ": " + utf8(problem));
        return false;
    }
    if (problem.isNotEmpty()) {
        logging::warning(log, "audio output: {}", utf8(problem));
    }
    return true;
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

void runServer(const logging::Log& log, const std::atomic<bool>& stopRequested) {
    // This thread becomes JUCE's message thread: device notifications arrive through it.
    juce::MessageManager::getInstance();
    {
        const auto midiDevices = juce::MidiInput::getAvailableDevices();
        const auto links = makeAudioLinks(static_cast<std::size_t>(midiDevices.size()));

        // The recorder is ready for the control API (step 0.6) to start and stop recordings.
        Recorder recorder(*links);
        recorder.startThread(log);

        juce::AudioDeviceManager manager;
        logAvailableDevices(manager, log);
        AudioCallback callback(*links, log);
        if (openAudio(manager, log)) {
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
