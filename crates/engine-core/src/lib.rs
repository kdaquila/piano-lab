//! Core traits and orchestration for the piano-lab engine.
//!
//! This crate defines the interfaces every model implements and runs them
//! together. It must stay free of devices, threads, files and UI so it can be
//! tested headless.
//!
//! The main pieces:
//! - [`KeyModel`]: the sound model for one key. Implementations live in the
//!   `models` crate.
//! - [`Keyboard`]: all 88 keys, each owning its model for the life of the
//!   engine, rendered together with sample-accurate note events.
//! - [`render_offline`]: renders a whole event list to a buffer, for tests and
//!   the render CLI.

mod event;
mod key_model;
mod keyboard;
mod render;
pub mod tuning;

pub use event::{NoteEvent, TimedEvent};
pub use key_model::KeyModel;
pub use keyboard::{HIGHEST_NOTE, KEY_COUNT, Keyboard, LOWEST_NOTE, key_index};
pub use render::render_offline;
