use crate::{KeyModel, NoteEvent, TimedEvent};

/// Number of keys on a standard piano.
pub const KEY_COUNT: usize = 88;
/// MIDI note number of the lowest key (A0).
pub const LOWEST_NOTE: u8 = 21;
/// MIDI note number of the highest key (C8).
pub const HIGHEST_NOTE: u8 = LOWEST_NOTE + (KEY_COUNT as u8) - 1;

/// Maps a MIDI note number to a key index (0 = A0, 87 = C8), or `None` if
/// the note is off the keyboard.
pub fn key_index(note: u8) -> Option<usize> {
    (LOWEST_NOTE..=HIGHEST_NOTE)
        .contains(&note)
        .then(|| usize::from(note - LOWEST_NOTE))
}

/// All 88 keys, each owning its [`KeyModel`] for the life of the engine.
///
/// There is no voice pool and no voice stealing: re-striking a key excites
/// the same model again, as on a real piano.
pub struct Keyboard<M> {
    keys: Vec<M>,
    max_block: usize,
}

impl<M: KeyModel> Keyboard<M> {
    /// Builds the keyboard, calling `make` once per key with its MIDI note
    /// number, then prepares every model. Allocates; call before audio starts.
    pub fn new(sample_rate: f64, max_block: usize, mut make: impl FnMut(u8) -> M) -> Self {
        assert!(max_block > 0, "max_block must be at least 1");
        let keys = (LOWEST_NOTE..=HIGHEST_NOTE)
            .map(|note| {
                let mut model = make(note);
                model.prepare(sample_rate, max_block);
                model
            })
            .collect();
        Self { keys, max_block }
    }

    /// The largest block [`render`](Self::render) accepts.
    pub fn max_block(&self) -> usize {
        self.max_block
    }

    /// The model for a MIDI note, if it's on the keyboard.
    pub fn key(&self, note: u8) -> Option<&M> {
        key_index(note).map(|i| &self.keys[i])
    }

    /// Applies one event immediately. Notes off the keyboard are ignored.
    pub fn apply(&mut self, event: NoteEvent) {
        match event {
            NoteEvent::On { note, velocity } => {
                if let Some(i) = key_index(note) {
                    self.keys[i].strike(velocity.clamp(0.0, 1.0));
                }
            }
            NoteEvent::Off { note } => {
                if let Some(i) = key_index(note) {
                    self.keys[i].release();
                }
            }
        }
    }

    /// Renders one block into `out` (overwriting it), applying each event at
    /// its exact sample offset within the block.
    ///
    /// `events` must be sorted by `at`, with every `at` no greater than
    /// `out.len()`. `out.len()` must not exceed [`max_block`](Self::max_block).
    ///
    /// Real-time safe: does not allocate.
    pub fn render(&mut self, out: &mut [f32], events: &[TimedEvent]) {
        assert!(
            out.len() <= self.max_block,
            "block of {} samples exceeds max_block {}",
            out.len(),
            self.max_block
        );
        out.fill(0.0);

        // Render the stretch between consecutive events, then apply the event.
        let mut pos = 0;
        for timed in events {
            debug_assert!(timed.at >= pos, "events must be sorted by `at`");
            let at = timed.at.min(out.len());
            self.render_span(&mut out[pos..at]);
            self.apply(timed.event);
            pos = at;
        }
        self.render_span(&mut out[pos..]);
    }

    fn render_span(&mut self, out: &mut [f32]) {
        if out.is_empty() {
            return;
        }
        for key in self.keys.iter_mut().filter(|k| !k.is_silent()) {
            key.render_add(out);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Outputs a constant 1.0 while struck, nothing otherwise.
    #[derive(Default)]
    struct Constant {
        on: bool,
    }

    impl KeyModel for Constant {
        fn prepare(&mut self, _: f64, _: usize) {}
        fn strike(&mut self, _: f32) {
            self.on = true;
        }
        fn release(&mut self) {
            self.on = false;
        }
        fn render_add(&mut self, out: &mut [f32]) {
            out.iter_mut().for_each(|s| *s += 1.0);
        }
        fn is_silent(&self) -> bool {
            !self.on
        }
    }

    fn keyboard() -> Keyboard<Constant> {
        Keyboard::new(48_000.0, 64, |_| Constant::default())
    }

    #[test]
    fn key_range() {
        assert_eq!(HIGHEST_NOTE, 108);
        assert_eq!(key_index(20), None);
        assert_eq!(key_index(21), Some(0));
        assert_eq!(key_index(108), Some(87));
        assert_eq!(key_index(109), None);
    }

    #[test]
    fn events_land_on_their_exact_sample() {
        let mut kb = keyboard();
        let mut out = [0.0; 64];
        kb.render(
            &mut out,
            &[TimedEvent::on(10, 60, 1.0), TimedEvent::off(40, 60)],
        );
        assert!(out[..10].iter().all(|&s| s == 0.0));
        assert!(out[10..40].iter().all(|&s| s == 1.0));
        assert!(out[40..].iter().all(|&s| s == 0.0));
    }

    #[test]
    fn keys_sum_into_one_buffer() {
        let mut kb = keyboard();
        let mut out = [0.0; 8];
        kb.render(
            &mut out,
            &[TimedEvent::on(0, 60, 1.0), TimedEvent::on(0, 64, 1.0)],
        );
        assert!(out.iter().all(|&s| s == 2.0));
    }

    #[test]
    fn notes_off_the_keyboard_are_ignored() {
        let mut kb = keyboard();
        let mut out = [0.0; 8];
        kb.render(
            &mut out,
            &[TimedEvent::on(0, 5, 1.0), TimedEvent::on(0, 120, 1.0)],
        );
        assert!(out.iter().all(|&s| s == 0.0));
    }

    #[test]
    #[should_panic(expected = "exceeds max_block")]
    fn oversized_block_is_rejected() {
        let mut kb = keyboard();
        let mut out = [0.0; 65];
        kb.render(&mut out, &[]);
    }
}
