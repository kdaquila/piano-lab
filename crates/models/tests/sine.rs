//! Headless acceptance tests for the sine key model (Milestone 0, step 0.2).

use engine_core::{KeyModel, Keyboard, TimedEvent, render_offline, tuning};
use models::{SineKey, SineParams};

const SR: f64 = 48_000.0;
const BLOCK: usize = 256;
const A4: u8 = 69;

fn keyboard() -> Keyboard<SineKey> {
    Keyboard::new(SR, BLOCK, |note| {
        SineKey::new(
            tuning::equal_temperament(note, tuning::A4_HZ),
            SineParams::default(),
        )
    })
}

fn seconds(s: f64) -> usize {
    (s * SR).round() as usize
}

fn peak(samples: &[f32]) -> f32 {
    samples.iter().fold(0.0, |m, s| m.max(s.abs()))
}

fn db(amplitude: f32) -> f64 {
    20.0 * f64::from(amplitude).log10()
}

/// Frequency from the spacing of rising zero crossings, interpolated
/// between samples for sub-sample accuracy.
fn measure_frequency(samples: &[f32]) -> f64 {
    let crossings: Vec<f64> = samples
        .windows(2)
        .enumerate()
        .filter(|(_, w)| w[0] < 0.0 && w[1] >= 0.0)
        .map(|(i, w)| {
            let (a, b) = (f64::from(w[0]), f64::from(w[1]));
            i as f64 + a / (a - b)
        })
        .collect();
    assert!(crossings.len() >= 2, "not enough cycles to measure");
    let periods = (crossings.len() - 1) as f64;
    let span = crossings[crossings.len() - 1] - crossings[0];
    periods * SR / span
}

#[test]
fn a4_plays_at_440_hz() {
    let mut kb = keyboard();
    let out = render_offline(&mut kb, &[TimedEvent::on(0, A4, 1.0)], seconds(1.0));
    // Skip the first 10 ms so the attack has settled.
    let hz = measure_frequency(&out[seconds(0.01)..]);
    assert!((hz - 440.0).abs() < 0.1, "measured {hz} Hz");
}

#[test]
fn every_key_plays_its_own_pitch() {
    // A0 (27.5 Hz) up to C8 (4186 Hz): the extremes and middle C.
    for note in [21, 60, 108] {
        let expected = tuning::equal_temperament(note, tuning::A4_HZ);
        let mut kb = keyboard();
        let out = render_offline(&mut kb, &[TimedEvent::on(0, note, 1.0)], seconds(1.0));
        let hz = measure_frequency(&out[seconds(0.05)..]);
        assert!(
            (hz - expected).abs() < expected * 1e-4,
            "note {note}: measured {hz} Hz, expected {expected} Hz"
        );
    }
}

#[test]
fn output_is_neither_silent_nor_clipping() {
    let mut kb = keyboard();
    let out = render_offline(&mut kb, &[TimedEvent::on(0, A4, 1.0)], seconds(0.5));
    let p = peak(&out);
    assert!(p > 0.1, "too quiet: peak {p}");
    assert!(p < 1.0, "clipping: peak {p}");
}

#[test]
fn velocity_scales_loudness() {
    let loudness = |velocity| {
        let mut kb = keyboard();
        peak(&render_offline(
            &mut kb,
            &[TimedEvent::on(0, A4, velocity)],
            seconds(0.2),
        ))
    };
    let (soft, loud) = (loudness(0.25), loudness(1.0));
    assert!(soft > 0.0 && soft < loud * 0.5, "soft {soft}, loud {loud}");
}

#[test]
fn release_decays_to_silence() {
    let mut kb = keyboard();
    let release_at = seconds(0.5);
    let out = render_offline(
        &mut kb,
        &[TimedEvent::on(0, A4, 1.0), TimedEvent::off(release_at, A4)],
        seconds(1.5),
    );
    // Release T60 is 0.25 s, so one second after release it's long gone.
    let tail = &out[release_at + seconds(1.0) - BLOCK..];
    assert!(db(peak(tail)) < -90.0, "tail at {} dB", db(peak(tail)));
    assert!(kb.key(A4).unwrap().is_silent(), "key not reporting silent");
}

#[test]
fn note_starts_on_its_exact_sample() {
    // Deliberately not on a block boundary.
    let start = 1000;
    let mut kb = keyboard();
    let out = render_offline(&mut kb, &[TimedEvent::on(start, A4, 1.0)], seconds(0.1));
    assert!(
        out[..start].iter().all(|&s| s == 0.0),
        "sound before the strike"
    );
    assert!(out[start + 2] != 0.0, "no sound just after the strike");
}

#[test]
fn restriking_a_ringing_key_does_not_click() {
    let mut kb = keyboard();
    let restrike_at = seconds(0.3) + 123; // mid-block, arbitrary phase
    let out = render_offline(
        &mut kb,
        &[
            TimedEvent::on(0, A4, 0.3),
            TimedEvent::on(restrike_at, A4, 1.0),
        ],
        seconds(0.6),
    );
    // A smooth sine of amplitude A changes by at most 2π·f/SR·A per sample.
    // Allow twice that for the attack glide; a click would be far bigger.
    let max_amp = SineParams::default().gain;
    let limit = 2.0 * std::f64::consts::TAU * 440.0 / SR * max_amp;
    let max_step = out
        .windows(2)
        .map(|w| f64::from((w[1] - w[0]).abs()))
        .fold(0.0, f64::max);
    assert!(max_step < limit, "jump of {max_step} (limit {limit})");
}

#[test]
fn rendering_is_deterministic() {
    let events = [
        TimedEvent::on(0, 60, 0.8),
        TimedEvent::on(500, 64, 0.6),
        TimedEvent::off(20_000, 60),
    ];
    let render = || render_offline(&mut keyboard(), &events, seconds(0.5));
    assert_eq!(render(), render());
}
