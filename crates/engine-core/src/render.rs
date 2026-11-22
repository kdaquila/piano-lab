use crate::{KeyModel, Keyboard, TimedEvent};

/// Renders `length` samples from a list of events, block by block, exactly
/// as the real-time engine would.
///
/// Here `at` on each event is the absolute sample from the start of the
/// render; events must be sorted by it. Events at or after `length` are
/// ignored. Deterministic: the same inputs always give the same samples.
///
/// This is for tests and offline rendering, so it allocates its output.
pub fn render_offline<M: KeyModel>(
    keyboard: &mut Keyboard<M>,
    events: &[TimedEvent],
    length: usize,
) -> Vec<f32> {
    assert!(
        events.windows(2).all(|w| w[0].at <= w[1].at),
        "events must be sorted by `at`"
    );

    let block = keyboard.max_block();
    let mut out = vec![0.0; length];
    let mut block_events = Vec::with_capacity(events.len());
    let mut remaining = events;

    for (index, chunk) in out.chunks_mut(block).enumerate() {
        let start = index * block;
        let end = start + chunk.len();

        // Take this block's events and make their times block-relative.
        let split = remaining.partition_point(|e| e.at < end);
        block_events.clear();
        block_events.extend(remaining[..split].iter().map(|e| TimedEvent {
            at: e.at - start,
            ..*e
        }));
        remaining = &remaining[split..];

        keyboard.render(chunk, &block_events);
    }
    out
}
