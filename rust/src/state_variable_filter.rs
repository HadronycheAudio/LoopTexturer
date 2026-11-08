//! A stereo topology-preserving state-variable filter, which stays stable under fast modulation.

use hadronyche_dsp::{SvfCoefficients, SvfMode, SvfState, svf_coefficients, svf_output, svf_tick, svf_warp};

/// The order of SvfMode matches FILTER_MODES in lib.rs, and projects save a position in it.
pub fn mode_from_index(index: i32) -> SvfMode {
    match index {
        1 => SvfMode::HighPass,
        2 => SvfMode::BandPass,
        3 => SvfMode::Notch,
        _ => SvfMode::LowPass,
    }
}

#[derive(Clone, Debug)]
pub struct StateVariableFilter {
    sample_rate: f64,
    mode: SvfMode,
    coefficients: SvfCoefficients,
    states: [SvfState; 2],
}

impl Default for StateVariableFilter {
    fn default() -> Self {
        Self {
            sample_rate: 44100.0,
            mode: SvfMode::LowPass,
            coefficients: SvfCoefficients::default(),
            states: [SvfState::default(); 2],
        }
    }
}

impl StateVariableFilter {
    pub fn prepare(&mut self, sample_rate: f64) {
        self.sample_rate = sample_rate;
        self.states = [SvfState::default(); 2];
    }

    pub fn set_mode(&mut self, mode: SvfMode) {
        self.mode = mode;
    }

    pub fn set_coefficients(&mut self, frequency: f64, q: f64) {
        self.coefficients = svf_coefficients(svf_warp(frequency, self.sample_rate), q);
    }

    pub fn process(&mut self, input: f32, channel: usize) -> f32 {
        let taps = svf_tick(input, &self.coefficients, &mut self.states[channel]);
        svf_output(self.mode, input, self.coefficients.k, taps)
    }
}
