//! Model implementations behind the `engine-core` traits: strings, hammers,
//! soundboards and radiation.
//!
//! For now there is one: [`SineKey`], a single sine per key. A sine is a
//! one-mode modal string, so this is the real engine at its simplest.

mod sine;

pub use sine::{SineKey, SineParams};
