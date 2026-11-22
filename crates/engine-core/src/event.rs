/// Something a player does to the keyboard.
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum NoteEvent {
    /// A key is pressed. `note` is a MIDI note number (A0 = 21 … C8 = 108),
    /// `velocity` is normalised to 0.0–1.0.
    On { note: u8, velocity: f32 },
    /// A key is released.
    Off { note: u8 },
}

/// A [`NoteEvent`] stamped with the sample it happens at.
///
/// What `at` counts from depends on where the event is used:
/// - In [`Keyboard::render`](crate::Keyboard::render) it is the offset within
///   the current block.
/// - In [`render_offline`](crate::render_offline) it is the absolute sample
///   from the start of the render.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct TimedEvent {
    pub at: usize,
    pub event: NoteEvent,
}

impl TimedEvent {
    pub fn on(at: usize, note: u8, velocity: f32) -> Self {
        Self {
            at,
            event: NoteEvent::On { note, velocity },
        }
    }

    pub fn off(at: usize, note: u8) -> Self {
        Self {
            at,
            event: NoteEvent::Off { note },
        }
    }
}
