// The audio service's JUCE side: thin adapters from JUCE's device callbacks to the JUCE-free audio
// core, the devices behind the service's hooks, and the main loop. JUCE requires inheritance for
// its callbacks; nothing else sees it.

#include <pianolab/engine_core/config.hpp>
#include <pianolab/engine_core/error.hpp>
#include <pianolab/engine_core/note_event.hpp>
#include <pianolab/live/audio_core.hpp>
#include <pianolab/live/devices.hpp>
#include <pianolab/live/monitor.hpp>
#include <pianolab/live/pipe_io.hpp>
#include <pianolab/live/recorder.hpp>
#include <pianolab/live/server.hpp>
#include <pianolab/live/service.hpp>
#include <pianolab/logging/log.hpp>
#include <pianolab/models/piano_key.hpp>
#include <pianolab/music_io/files.hpp>
#include <pianolab/pipe/messages.hpp>
#include <pianolab/settings/settings.hpp>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace pianolab::live {

namespace {

/// How often the main loop sends events, saves and checks for a stop, in milliseconds: about 30
/// times a second, enough for a smooth level meter.
constexpr int eventIntervalMs = 33;

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

    /// The piano to build the next time the device starts. On the main thread; the piano
    /// already playing takes new settings through `AudioLinks::keyboards`.
    void setPiano(const models::PianoConfig& newPiano) {
        const std::scoped_lock lock(pianoMutex);
        piano = newPiano;
    }

    /// Before audio starts (not on the audio thread): builds the piano for the device's sample
    /// rate and scratch space for its block size. May allocate.
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override {
        models::PianoConfig config;
        {
            const std::scoped_lock lock(pianoMutex);
            config = piano;
        }
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
    std::mutex pianoMutex;     ///< guards `piano`: the device may restart on another thread
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
    std::vector<std::string> names; ///< as the system names them, for the status
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
        opened.names.push_back(utf8(device.name));
    }
    if (devices.isEmpty()) {
        logging::warning(log, "no MIDI inputs found; plug in a keyboard and choose it in the "
                              "settings, or restart piano-lab");
    }
    return opened;
}

/// Everything live that a device change rebuilds: the queues to and from the audio thread, the
/// recorder, the audio callback and the MIDI inputs, plus what the main loop last reported about
/// them. Built by `startRig`, stopped by `stopRig`.
struct Rig {
    std::unique_ptr<AudioLinks> links; ///< first, so it outlives everything using it
    std::unique_ptr<Recorder> recorder;
    std::unique_ptr<AudioCallback> callback;
    MidiInputs midi;
    std::size_t screenInput = 0; ///< the input queue for notes played on screen
    MonitorState monitor;
    EventState events;
    /// A keyboard with new settings, waiting for room in `links->keyboards`.
    std::optional<models::PianoKeyboard> pendingKeyboard;
};

/// Opens the devices `settings` name and starts playing: the whole live side, from scratch.
std::unique_ptr<Rig> startRig(juce::AudioDeviceManager& manager, const settings::Settings& settings,
                              const logging::Log& log) {
    auto rig = std::make_unique<Rig>();
    const auto midiDevices = chosenMidiInputs(settings.midi, log);
    rig->screenInput = static_cast<std::size_t>(midiDevices.size());
    rig->links = makeAudioLinks(rig->screenInput + 1);
    rig->recorder = std::make_unique<Recorder>(*rig->links);
    rig->recorder->startThread(log);
    rig->callback = std::make_unique<AudioCallback>(*rig->links, settings.piano, log);
    if (openAudio(manager, settings.audio, log)) {
        manager.addAudioCallback(rig->callback.get());
    }
    rig->midi = openMidiInputs(midiDevices, *rig->links, log);
    return rig;
}

/// Stops everything `rig` runs: the MIDI inputs, then audio, then any recording, which is
/// finished and logged. Its queues stay, for one last report.
void stopRig(juce::AudioDeviceManager& manager, Rig& rig, const logging::Log& log) {
    for (auto& input : rig.midi.inputs) {
        input->stop();
    }
    rig.midi = {};
    manager.removeAudioCallback(rig.callback.get());
    manager.closeAudioDevice();
    if (rig.recorder->recording()) {
        if (const auto summary = rig.recorder->stop()) {
            logging::info(log, "recording finished: {} ({:.1f} s)",
                          music_io::displayPath(summary->file), summary->seconds);
        } else {
            logging::error(log, "recording lost: {}", summary.error().message);
        }
    }
    rig.recorder.reset();
}

/// Hands the waiting keyboard, if any, to the audio thread, unless its queue is full (then it
/// waits for the next try).
void flushKeyboard(Rig& rig) {
    if (rig.pendingKeyboard && rig.links->keyboards.tryPush(*rig.pendingKeyboard)) {
        rig.pendingKeyboard.reset();
    }
}

/// The devices and what plays through them, for the service's hooks.
struct Live {
    juce::AudioDeviceManager manager;
    std::unique_ptr<Rig> rig;
    logging::Log log;
    std::filesystem::path recordingsFolder;
};

/// See `ServiceHooks::playNote`.
void playNote(Live& live, const engine_core::NoteEvent& event) {
    receiveNote(*live.rig->links, live.rig->screenInput, event,
                juce::Time::getMillisecondCounterHiRes() * 0.001); // MIDI time stamps' clock
}

/// See `ServiceHooks::changePiano`.
void changePiano(Live& live, const models::PianoConfig& piano) {
    Rig& rig = *live.rig;
    rig.callback->setPiano(piano);
    auto* device = live.manager.getCurrentAudioDevice();
    if (device == nullptr) {
        return; // nothing playing; the next device built uses the new settings
    }
    models::PianoConfig config = piano;
    config.render.sampleRate = device->getCurrentSampleRate();
    const auto validated = models::validate(config);
    for (const auto& problem : validated.errors) {
        logging::warning(live.log, "{}", problem.message);
    }
    rig.pendingKeyboard = models::makePianoKeyboard(validated.config);
    flushKeyboard(rig);
}

/// See `ServiceHooks::restartDevices`.
void restartDevices(Live& live, const settings::Settings& settings) {
    logging::info(live.log, "reopening the devices for the new settings");
    stopRig(live.manager, *live.rig, live.log);
    reportNews(live.rig->monitor, *live.rig->links, live.log);
    live.rig.reset();
    live.rig = startRig(live.manager, settings, live.log);
}

/// See `ServiceHooks::listDevices`.
pipe::Devices listDevices(Live& live) {
    pipe::Devices devices;
    for (auto& type : audioDeviceTypes(live.manager, live.log)) {
        devices.audio.push_back({.type = std::move(type.name), .outputs = std::move(type.outputs)});
    }
    for (const auto& device : juce::MidiInput::getAvailableDevices()) {
        devices.midi.push_back(utf8(device.name));
    }
    return devices;
}

/// See `ServiceHooks::status`.
pipe::Status status(Live& live) {
    pipe::Status status;
    if (auto* device = live.manager.getCurrentAudioDevice()) {
        status.audioType = utf8(device->getTypeName());
        status.audioOutput = utf8(device->getName());
        status.sampleRate = device->getCurrentSampleRate();
        status.bufferSize = device->getCurrentBufferSizeSamples();
    }
    status.midiInputs = live.rig->midi.names;
    status.recording = live.rig->recorder->recording();
    return status;
}

/// See `ServiceHooks::startRecording`.
std::expected<std::filesystem::path, engine_core::Error> startRecording(Live& live) {
    auto* device = live.manager.getCurrentAudioDevice();
    if (device == nullptr) {
        return std::unexpected(
            engine_core::Error{"no audio output is open, so there's nothing to record"});
    }
    std::error_code error;
    std::filesystem::create_directories(live.recordingsFolder, error);
    if (error) {
        return std::unexpected(engine_core::Error{std::format(
            "can't create {}: {}", music_io::displayPath(live.recordingsFolder), error.message())});
    }
    auto path = recordingPath(live.recordingsFolder,
                              utf8(juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M-%S")));
    if (auto started = live.rig->recorder->start(path, device->getCurrentSampleRate()); !started) {
        return std::unexpected(std::move(started.error()));
    }
    logging::info(live.log, "recording to {}", music_io::displayPath(path));
    return path;
}

/// See `ServiceHooks::stopRecording`.
std::expected<RecordingSummary, engine_core::Error> stopRecording(Live& live) {
    auto summary = live.rig->recorder->stop();
    if (summary) {
        logging::info(live.log, "recording finished: {} ({:.1f} s)",
                      music_io::displayPath(summary->file), summary->seconds);
    }
    return summary;
}

/// Writes `lines` to stdout.
void writeLines(const std::vector<std::string>& lines) {
    for (const auto& line : lines) {
        static_cast<void>(writeLine(line));
    }
}

/// The main thread's loop: answers commands as soon as they arrive (woken by the stdin reader),
/// and about 30 times a second sends events, saves settings that are due, logs news from the
/// audio side, and stops once asked to or once stdin ends.
class MainLoop final : public juce::Timer, public juce::AsyncUpdater {
public:
    /// A loop serving `live` and `state` through `hooks`, with commands from `inbox`.
    MainLoop(Live& live, ServiceState& state, const ServiceHooks& hooks, Inbox& inbox,
             const std::atomic<bool>& stopRequested)
        : live(live), state(state), hooks(hooks), inbox(inbox), stopRequested(stopRequested) {}
    MainLoop(const MainLoop&) = delete;
    MainLoop& operator=(const MainLoop&) = delete;
    MainLoop(MainLoop&&) = delete;
    MainLoop& operator=(MainLoop&&) = delete;
    /// Stops the timer and drops any wake-up still pending.
    ~MainLoop() override {
        stopTimer();
        cancelPendingUpdate();
    }

    /// Woken by the stdin reader: answers what arrived.
    void handleAsyncUpdate() override { serve(); }

    /// About 30 times a second: see the class.
    void timerCallback() override {
        serve();
        Rig& rig = *live.rig;
        flushKeyboard(rig);
        reportNews(rig.monitor, *rig.links, live.log);
        for (const auto& event : liveEvents(rig.events, *rig.links)) {
            static_cast<void>(writeLine(pipe::eventLine(event)));
        }
        saveIfDue(state, live.log, Clock::now());
        if (stopRequested.load() || inputClosed(inbox)) {
            stopTimer();
            juce::MessageManager::getInstance()->stopDispatchLoop();
        }
    }

private:
    /// Answers every line waiting.
    void serve() {
        for (const auto& line : takeLines(inbox)) {
            writeLines(handleLine(state, hooks, live.log, line, Clock::now()));
        }
    }

    Live& live;
    ServiceState& state;
    const ServiceHooks& hooks;
    Inbox& inbox;
    const std::atomic<bool>& stopRequested;
};

} // namespace

void runService(const ServiceOptions& options, const logging::Log& log,
                const std::atomic<bool>& stopRequested) {
    // This thread becomes JUCE's message thread: device notifications arrive through it.
    juce::MessageManager::getInstance();
    {
        Live live;
        live.log = log;
        live.recordingsFolder = options.recordingsFolder;
        live.rig = startRig(live.manager, options.settings, log);

        ServiceState state{.settings = options.settings,
                           .settingsFile = options.settingsFile,
                           .settingsText = options.settingsText,
                           .saveAt = std::nullopt};
        const ServiceHooks hooks{
            .playNote = [&live](const engine_core::NoteEvent& event) { playNote(live, event); },
            .changePiano = [&live](const models::PianoConfig& piano) { changePiano(live, piano); },
            .restartDevices =
                [&live](const settings::Settings& settings) { restartDevices(live, settings); },
            .listDevices = [&live] { return listDevices(live); },
            .status = [&live] { return status(live); },
            .startRecording = [&live] { return startRecording(live); },
            .stopRecording = [&live] { return stopRecording(live); },
        };

        const auto inbox = std::make_shared<Inbox>();
        MainLoop loop(live, state, hooks, *inbox, stopRequested);
        setWake(*inbox, [&loop] { loop.triggerAsyncUpdate(); });
        startStdinReader(inbox);
        static_cast<void>(writeLine(pipe::eventLine(pipe::Ready{.version = options.version})));
        logging::info(log, "the piano is ready; press Ctrl+C to stop");
        loop.startTimer(eventIntervalMs);
        juce::MessageManager::getInstance()->runDispatchLoop();

        setWake(*inbox, {}); // the reader may outlive the loop; it mustn't wake it any more
        logging::info(log, "stopping");
        saveIfDue(state, log, Clock::now(), true);
        stopRig(live.manager, *live.rig, log);
        reportNews(live.rig->monitor, *live.rig->links, log); // the audio side's last moments
        live.rig.reset();
    }
    juce::MessageManager::deleteInstance();
    logging::info(log, "stopped");
}

} // namespace pianolab::live
