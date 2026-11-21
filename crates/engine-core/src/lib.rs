//! Core traits and orchestration for the piano-lab engine.
//!
//! This crate defines the interfaces every model implements (string, hammer,
//! soundboard, radiation) and runs them together. It must stay free of
//! devices, threads, files and UI so it can be tested headless.

#[cfg(test)]
mod tests {
    #[test]
    fn placeholder() {}
}
