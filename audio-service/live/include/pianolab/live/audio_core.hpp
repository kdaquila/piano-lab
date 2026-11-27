#pragma once

#include <pianolab/engine_core/config.hpp>
#include <pianolab/engine_core/diagnostics.hpp>
#include <pianolab/engine_core/note_event.hpp>
#include <pianolab/live/live_midi.hpp>
#include <pianolab/models/piano_key.hpp>
#include <pianolab/rt/spsc_queue.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

/// The audio core: what the audio thread does every block, and how other threads talk to it.
/// Free of JUCE and devices, so tests drive it block by block exactly as a sound card would.
namespace pianolab::live {

/// How many events each live input's queue holds: far more than a pianist can play between
/// two audio blocks.
inline constexpr std::size_t liveEventQueueCapacity = 1024;
/// The most events applied in one block; any more wait for the next block.
inline constexpr std::size_t maxEventsPerBlock = 1024;
/// How many diagnostics updates can wait for the monitor.
inline constexpr std::size_t diagnosticsQueueCapacity = 64;
/// Samples per recorded chunk.
inline constexpr std::size_t recordedChunkSamples = 512;
/// How many chunks can wait for the recorder: about 11 seconds at 48 kHz, so a slow disk
/// doesn't lose audio.
inline constexpr std::size_t recordingQueueChunks = 1024;

/// A piece of the output on its way to the recorder.
struct RecordedChunk {
    std::uint32_t session = 0; ///< which recording it belongs to
    std::uint32_t count = 0;   ///< how many of `samples` are used
    std::array<float, recordedChunkSamples> samples{};
};

/// Everything the audio thread shares with other threads: one lock-free queue per pair of
/// threads, plus a few atomic numbers. Made once at startup (`makeAudioLinks`), before audio
/// starts, and never moved while the threads hold on to it.
struct AudioLinks {
    /// Live notes, one queue per input. Producer: that input's thread; consumer: audio.
    std::vector<std::unique_ptr<rt::SpscQueue<LiveEvent>>> inputs;
    /// The engine's diagnostics when they change. Producer: audio; consumer: the monitor.
    rt::SpscQueue<engine_core::Diagnostics> diagnostics{diagnosticsQueueCapacity};
    /// The output, while recording. Producer: audio; consumer: the recorder.
    rt::SpscQueue<RecordedChunk> recording{recordingQueueChunks};
    /// The recording the output belongs to, or 0 when not recording. Set by the recorder.
    std::atomic<std::uint32_t> recordingSession{0};
    /// Samples that didn't fit in the recording queue (the recorder fell behind).
    std::atomic<std::uint64_t> droppedRecordingSamples{0};
    /// Live events that didn't fit in their input's queue.
    std::atomic<std::uint64_t> droppedEvents{0};
    /// Pedal messages received, which the piano can't play yet.
    std::atomic<std::uint64_t> pedalMessages{0};
};

/// Links for `inputCount` live inputs. Setup code: allocates.
[[nodiscard]] std::unique_ptr<AudioLinks> makeAudioLinks(std::size_t inputCount);

/// On input `input`'s thread: reads one raw MIDI message and passes any note on to the audio
/// thread, stamped with `arrivedSeconds`. Pedals are counted; a full queue drops the note and
/// counts it. Never blocks or allocates. Inputs past the end of `links.inputs` are ignored.
void receiveMidi(AudioLinks& links, std::size_t input, std::span<const std::uint8_t> bytes,
                 double arrivedSeconds) noexcept;

/// What only the audio thread touches: the keyboard, and scratch space allocated up front.
struct AudioState {
    models::PianoKeyboard keyboard;
    /// This block's events, by arrival: the first `eventCount` are used. Sized to
    /// `maxEventsPerBlock` at setup and never resized, so filling it can't allocate.
    std::vector<LiveEvent> arrived;
    /// The same events as engine events, at the start of the block. Sized like `arrived`.
    std::vector<engine_core::TimedEvent> blockEvents;
    std::size_t eventCount = 0;          ///< how many of `arrived`/`blockEvents` this block uses
    std::uint64_t publishedProblems = 0; ///< the diagnostics total last sent
};

/// The audio thread's state for a piano built from `config`. Setup code: allocates.
[[nodiscard]] AudioState makeAudioState(const engine_core::Valid<models::PianoConfig>& config);

/// Renders one block of output into `out` (overwriting it). The audio callback's whole job:
///   1. takes every note waiting in the input queues and applies them at the start of the block,
///      in the order they arrived (up to `maxEventsPerBlock`; any more wait for the next block);
///   2. renders the piano, in pieces of at most the keyboard's `maxBlock`;
///   3. while recording, copies the block to the recorder (counting what doesn't fit);
///   4. sends the engine's diagnostics to the monitor when they change.
/// Real-time safe: never allocates, locks, blocks or throws.
void processBlock(AudioState& state, AudioLinks& links, std::span<float> out) noexcept;

} // namespace pianolab::live
