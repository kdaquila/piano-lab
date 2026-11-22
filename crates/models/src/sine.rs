use std::f64::consts::TAU;

use engine_core::KeyModel;

/// Settings shared by every [`SineKey`].
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct SineParams {
    /// Peak amplitude at full velocity. Kept well below 1.0 so several keys
    /// can sound together without clipping.
    pub gain: f64,
    /// How fast a strike reaches its new level, as the time to settle within
    /// 60 dB. Short enough to sound instant, long enough to avoid a click.
    pub attack_t60: f64,
    /// How long the sound takes to fall by 60 dB after release (the damper).
    pub release_t60: f64,
}

impl Default for SineParams {
    fn default() -> Self {
        Self {
            gain: 0.25,
            attack_t60: 0.005,
            release_t60: 0.25,
        }
    }
}

/// Below this amplitude (−100 dB) a released key counts as silent.
const SILENCE: f64 = 1e-5;

/// One sine per key: the simplest possible key model.
///
/// The amplitude glides exponentially toward its target: quickly toward the
/// strike level, more slowly toward zero after release. The phase is never
/// reset, so re-striking a ringing key changes its level smoothly instead of
/// clicking.
#[derive(Debug, Clone)]
pub struct SineKey {
    frequency: f64,
    params: SineParams,

    // Set by `prepare`.
    phase_step: f64,
    attack_coeff: f64,
    release_coeff: f64,

    // Running state, all f64 for precision over long notes.
    phase: f64,
    amplitude: f64,
    target: f64,
    coeff: f64,
    released: bool,
}

impl SineKey {
    pub fn new(frequency: f64, params: SineParams) -> Self {
        Self {
            frequency,
            params,
            phase_step: 0.0,
            attack_coeff: 0.0,
            release_coeff: 0.0,
            phase: 0.0,
            amplitude: 0.0,
            target: 0.0,
            coeff: 0.0,
            released: true,
        }
    }

    pub fn frequency(&self) -> f64 {
        self.frequency
    }
}

/// Per-sample smoothing coefficient that closes 60 dB of the gap to the
/// target in `t60` seconds.
fn t60_coeff(t60: f64, sample_rate: f64) -> f64 {
    // ln(1000) ≈ 6.908: the natural-log size of a 60 dB drop.
    let samples = (t60 * sample_rate).max(1.0);
    1.0 - (-(1000f64.ln()) / samples).exp()
}

impl KeyModel for SineKey {
    fn prepare(&mut self, sample_rate: f64, _max_block: usize) {
        self.phase_step = self.frequency / sample_rate;
        self.attack_coeff = t60_coeff(self.params.attack_t60, sample_rate);
        self.release_coeff = t60_coeff(self.params.release_t60, sample_rate);
    }

    fn strike(&mut self, velocity: f32) {
        self.target = f64::from(velocity) * self.params.gain;
        self.coeff = self.attack_coeff;
        self.released = false;
    }

    fn release(&mut self) {
        self.target = 0.0;
        self.coeff = self.release_coeff;
        self.released = true;
    }

    fn render_add(&mut self, out: &mut [f32]) {
        for sample in out.iter_mut() {
            *sample += (self.amplitude * (TAU * self.phase).sin()) as f32;
            self.phase += self.phase_step;
            if self.phase >= 1.0 {
                self.phase -= 1.0;
            }
            self.amplitude += (self.target - self.amplitude) * self.coeff;
        }
        if self.released && self.amplitude < SILENCE {
            self.amplitude = 0.0;
        }
    }

    fn is_silent(&self) -> bool {
        self.released && self.amplitude < SILENCE
    }
}
