//! The real-time rule: rendering must never allocate memory.
//!
//! This test binary swaps in a checking global allocator. Any allocation
//! inside `assert_no_alloc(...)` aborts the whole test run, so a violation
//! can't slip through as a pass. On Windows the failure looks like
//! "memory allocation of N bytes failed" followed by exit code 0xc0000409
//! (Windows' code for an abort); that means this rule was broken.
//!
//! It lives in its own file because the checking allocator applies to the
//! whole test binary.

use assert_no_alloc::{AllocDisabler, assert_no_alloc};
use engine_core::{Keyboard, TimedEvent, tuning};
use models::{SineKey, SineParams};

#[global_allocator]
static ALLOCATOR: AllocDisabler = AllocDisabler;

const SR: f64 = 48_000.0;
const BLOCK: usize = 256;

#[test]
fn rendering_never_allocates() {
    // Setup may allocate: build the keyboard and buffers first.
    let mut kb = Keyboard::new(SR, BLOCK, |note| {
        SineKey::new(
            tuning::equal_temperament(note, tuning::A4_HZ),
            SineParams::default(),
        )
    });
    let mut out = vec![0.0f32; BLOCK];

    // Every key struck, held, then released: the busiest case today.
    let strikes: Vec<TimedEvent> = (21..=108)
        .map(|note| TimedEvent::on(usize::from(note - 21), note, 0.8))
        .collect();
    let releases: Vec<TimedEvent> = (21..=108)
        .map(|note| TimedEvent::off(usize::from(note - 21) * 2, note))
        .collect();

    assert_no_alloc(|| {
        kb.render(&mut out, &strikes);
        for _ in 0..100 {
            kb.render(&mut out, &[]);
        }
        kb.render(&mut out, &releases);
        for _ in 0..200 {
            kb.render(&mut out, &[]);
        }
        // Odd-sized blocks too, as audio drivers sometimes deliver.
        kb.render(&mut out[..17], &[TimedEvent::on(5, 69, 1.0)]);
    });
}
