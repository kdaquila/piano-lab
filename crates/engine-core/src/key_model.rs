/// The sound model for one piano key.
///
/// The [`Keyboard`](crate::Keyboard) creates one instance per key at startup
/// and keeps it for the life of the engine, the way a real piano always has
/// all its strings. Pressing a key strikes that key's model, even if it is
/// still ringing.
///
/// # Real-time rules
/// Everything except [`prepare`](KeyModel::prepare) runs on the audio thread
/// and must never allocate, lock or block.
pub trait KeyModel {
    /// Setup before audio starts. Allowed to allocate.
    ///
    /// `max_block` is the largest slice [`render_add`](KeyModel::render_add)
    /// will ever be given.
    fn prepare(&mut self, sample_rate: f64, max_block: usize);

    /// The key is pressed. `velocity` is normalised to 0.0–1.0.
    ///
    /// Re-striking a key that is still ringing must be handled without a
    /// click.
    fn strike(&mut self, velocity: f32);

    /// The key is released: the damper comes down.
    fn release(&mut self);

    /// Adds this key's sound into `out`. It adds rather than overwrites, so
    /// many keys can share one buffer.
    fn render_add(&mut self, out: &mut [f32]);

    /// True when the key is quiet enough that rendering it can be skipped.
    fn is_silent(&self) -> bool;
}
