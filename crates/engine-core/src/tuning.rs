//! Note-to-frequency helpers.

/// Standard concert pitch for A4 (MIDI note 69).
pub const A4_HZ: f64 = 440.0;

/// The frequency of a MIDI note in 12-tone equal temperament, given the
/// pitch of A4.
///
/// Real pianos are stretch-tuned; that belongs to calibration later. This is
/// the plain textbook starting point.
pub fn equal_temperament(note: u8, a4_hz: f64) -> f64 {
    a4_hz * 2f64.powf((f64::from(note) - 69.0) / 12.0)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn close(a: f64, b: f64) -> bool {
        (a - b).abs() < 1e-3
    }

    #[test]
    fn reference_pitches() {
        assert!(close(equal_temperament(69, A4_HZ), 440.0));
        assert!(close(equal_temperament(21, A4_HZ), 27.5)); // A0
        assert!(close(equal_temperament(60, A4_HZ), 261.626)); // middle C
        assert!(close(equal_temperament(108, A4_HZ), 4186.009)); // C8
    }

    #[test]
    fn octave_doubles() {
        assert!(close(
            equal_temperament(81, A4_HZ),
            2.0 * equal_temperament(69, A4_HZ)
        ));
    }
}
