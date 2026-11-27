#include <pianolab/engine_core/config.hpp>
#include <pianolab/engine_core/keyboard.hpp>
#include <pianolab/engine_core/note_event.hpp>
#include <pianolab/live/audio_core.hpp>
#include <pianolab/live/live_midi.hpp>
#include <pianolab/models/piano_key.hpp>
#include <pianolab/rt/spsc_queue.hpp>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <variant>

namespace pianolab::live {

namespace {

/// Sorts `events` by arrival time, keeping the order of events that arrived at the same time
/// (insertion sort: stable, allocation-free, and fast for the few events of one block, which
/// are mostly in order already).
void sortByArrival(std::span<LiveEvent> events) noexcept {
    for (std::size_t index = 1; index < events.size(); ++index) {
        const LiveEvent event = events[index];
        std::size_t hole = index;
        while (hole > 0 && events[hole - 1].arrivedSeconds > event.arrivedSeconds) {
            events[hole] = events[hole - 1];
            --hole;
        }
        events[hole] = event;
    }
}

/// Takes waiting events from every input queue into `state.arrived`, in arrival order, and
/// turns them into engine events at the start of the block in `state.blockEvents`.
void collectEvents(AudioState& state, AudioLinks& links) noexcept {
    const std::size_t room = std::min(state.arrived.size(), state.blockEvents.size());
    std::size_t count = 0;
    for (const auto& input : links.inputs) {
        while (count < room) {
            const auto event = input->tryPop();
            if (!event) {
                break;
            }
            state.arrived[count++] = *event;
        }
    }
    const auto arrived = std::span(state.arrived).first(count);
    sortByArrival(arrived);
    for (std::size_t index = 0; index < count; ++index) {
        state.blockEvents[index] = {.at = 0, .event = arrived[index].event};
    }
    state.eventCount = count;
}

/// Copies `out` to the recorder in chunks, tagged with `session`. Counts what doesn't fit.
void sendToRecorder(AudioLinks& links, std::uint32_t session, std::span<const float> out) noexcept {
    RecordedChunk chunk;
    chunk.session = session;
    for (std::size_t start = 0; start < out.size(); start += recordedChunkSamples) {
        const auto part = out.subspan(start, std::min(recordedChunkSamples, out.size() - start));
        std::ranges::copy(part, chunk.samples.begin());
        chunk.count = static_cast<std::uint32_t>(part.size());
        if (!links.recording.tryPush(chunk)) {
            links.droppedRecordingSamples.fetch_add(part.size(), std::memory_order_relaxed);
        }
    }
}

} // namespace

std::unique_ptr<AudioLinks> makeAudioLinks(std::size_t inputCount) {
    auto links = std::make_unique<AudioLinks>();
    for (std::size_t input = 0; input < inputCount; ++input) {
        links->inputs.push_back(std::make_unique<rt::SpscQueue<LiveEvent>>(liveEventQueueCapacity));
    }
    return links;
}

void receiveMidi(AudioLinks& links, std::size_t input, std::span<const std::uint8_t> bytes,
                 double arrivedSeconds) noexcept {
    if (input >= links.inputs.size()) {
        return;
    }
    const MidiMeaning meaning = readMidiMessage(bytes);
    if (const auto* event = std::get_if<engine_core::NoteEvent>(&meaning)) {
        if (!links.inputs[input]->tryPush(
                LiveEvent{.event = *event, .arrivedSeconds = arrivedSeconds})) {
            links.droppedEvents.fetch_add(1, std::memory_order_relaxed);
        }
    } else if (std::holds_alternative<UnsupportedPedal>(meaning)) {
        links.pedalMessages.fetch_add(1, std::memory_order_relaxed);
    }
}

AudioState makeAudioState(const engine_core::Valid<models::PianoConfig>& config) {
    return AudioState{.keyboard = models::makePianoKeyboard(config),
                      .arrived = std::vector<LiveEvent>(maxEventsPerBlock),
                      .blockEvents = std::vector<engine_core::TimedEvent>(maxEventsPerBlock),
                      .eventCount = 0,
                      .publishedProblems = 0};
}

void processBlock(AudioState& state, AudioLinks& links, std::span<float> out) noexcept {
    collectEvents(state, links);

    // Render in pieces no longer than the keyboard's maxBlock; the events go in the first.
    const std::size_t piece = std::max<std::size_t>(state.keyboard.maxBlock, 1);
    std::span<const engine_core::TimedEvent> events =
        std::span(state.blockEvents).first(state.eventCount);
    for (std::size_t start = 0; start < out.size(); start += piece) {
        engine_core::render(state.keyboard, out.subspan(start, std::min(piece, out.size() - start)),
                            events);
        events = {};
    }
    if (out.empty() && !events.empty()) {
        engine_core::render(state.keyboard, out, events); // a zero-length block still plays notes
    }

    if (const auto session = links.recordingSession.load(std::memory_order_acquire); session != 0) {
        sendToRecorder(links, session, out);
    }

    const auto& diagnostics = state.keyboard.diagnostics;
    if (diagnostics.total != state.publishedProblems && links.diagnostics.tryPush(diagnostics)) {
        state.publishedProblems = diagnostics.total;
    }
}

} // namespace pianolab::live
