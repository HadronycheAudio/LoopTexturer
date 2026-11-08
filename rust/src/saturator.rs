//! A stereo multi-mode saturator with drive and dry/wet mix, run at four times the sample rate.

use std::sync::LazyLock;

use hadronyche_dsp::Smoother;

pub const FACTOR: usize = 4;
pub const TAPS: usize = 4 * 48 + 1;
/// The filters' delay at the base rate, which is the saturator's latency, on or off, so that switching it doesn't
/// shift the audio.
pub const LATENCY: usize = (TAPS - 1) / FACTOR;
const UP_LENGTH: usize = TAPS / FACTOR + 1;

/// A Kaiser-windowed sinc with its cutoff at the base rate's Nyquist frequency and about 90 dB of stopband
/// attenuation. Worked out once, in `prepare` rather than on the audio thread.
static KAISER_TAPS: LazyLock<[f32; TAPS]> = LazyLock::new(|| {
    let bessel_i0 = |x: f64| {
        let mut sum = 1.0;
        let mut term = 1.0;
        for k in 1..32 {
            term *= (x / (2.0 * f64::from(k))) * (x / (2.0 * f64::from(k)));
            sum += term;
        }
        sum
    };

    let pi = core::f64::consts::PI;
    let beta = 8.96;
    let middle = (TAPS - 1) as f64 / 2.0;

    let mut window = [0f64; TAPS];
    let mut sum = 0.0;
    for (n, value) in window.iter_mut().enumerate() {
        let t = (n as f64 - middle) / middle;
        let offset = (n as f64 - middle) / FACTOR as f64;
        let sinc = if offset == 0.0 {
            1.0
        } else {
            (pi * offset).sin() / (pi * offset)
        };
        *value = sinc * bessel_i0(beta * (1.0 - t * t).sqrt()) / bessel_i0(beta);
        sum += *value;
    }

    let mut result = [0f32; TAPS];
    for (value, &w) in result.iter_mut().zip(&window) {
        *value = (w / sum) as f32;
    }
    result
});

/// Runs a nonlinear function at 4x the sample rate on one channel, using linear-phase FIR filters.
#[derive(Clone, Debug)]
pub struct Oversampler {
    /// Each history is stored twice in a row, so the filters can read it without wrapping around.
    up_history: [f32; UP_LENGTH * 2],
    down_history: [f32; TAPS * 2],
    up_position: usize,
    down_position: usize,
}

impl Default for Oversampler {
    fn default() -> Self {
        Self {
            up_history: [0.0; UP_LENGTH * 2],
            down_history: [0.0; TAPS * 2],
            up_position: 0,
            down_position: 0,
        }
    }
}

impl Oversampler {
    pub fn reset(&mut self) {
        self.up_history.fill(0.0);
        self.down_history.fill(0.0);
        self.up_position = 0;
        self.down_position = 0;
    }

    pub fn process(&mut self, input: f32, function: impl Fn(f32) -> f32) -> f32 {
        self.push_up(input);

        // Keeping the sample at phase 0 gives a delay of a whole number of base rate samples.
        let output = self.push_down_and_filter(function(self.filter_up(0)));
        for phase in 1..FACTOR {
            self.push_down(function(self.filter_up(phase)));
        }
        output
    }

    fn push_up(&mut self, input: f32) {
        self.up_position = if self.up_position == 0 {
            UP_LENGTH - 1
        } else {
            self.up_position - 1
        };
        self.up_history[self.up_position] = input;
        self.up_history[self.up_position + UP_LENGTH] = input;
    }

    fn filter_up(&self, phase: usize) -> f32 {
        let taps = &*KAISER_TAPS;
        let history = &self.up_history[self.up_position..];
        let mut sum = 0f32;
        let mut n = phase;
        let mut k = 0;
        while n < TAPS {
            sum += taps[n] * history[k];
            n += FACTOR;
            k += 1;
        }
        sum * FACTOR as f32
    }

    fn push_down(&mut self, input: f32) {
        self.down_position = if self.down_position == 0 {
            TAPS - 1
        } else {
            self.down_position - 1
        };
        self.down_history[self.down_position] = input;
        self.down_history[self.down_position + TAPS] = input;
    }

    fn push_down_and_filter(&mut self, input: f32) -> f32 {
        self.push_down(input);

        let taps = &*KAISER_TAPS;
        let history = &self.down_history[self.down_position..];
        let mut sum = 0f32;
        for n in 0..TAPS {
            sum += taps[n] * history[n];
        }
        sum
    }
}

/// The order matches SAT_MODES in lib.rs, and projects save a position in it.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Mode {
    HardClip,
    Soft,
    SineClip,
    Tube,
    Wavefolder,
    Rectifier,
}

impl Mode {
    pub fn from_index(index: i32) -> Self {
        match index {
            0 => Mode::HardClip,
            2 => Mode::SineClip,
            3 => Mode::Tube,
            4 => Mode::Wavefolder,
            5 => Mode::Rectifier,
            _ => Mode::Soft,
        }
    }
}

const SILENT_MIX: f32 = 1e-5;

/// It has a constant latency of `LATENCY` samples, whether it's on or off, so switching it doesn't shift the audio.
#[derive(Clone, Debug)]
pub struct Saturator {
    oversamplers: [Oversampler; 2],
    dry_delay: [[f32; LATENCY]; 2],
    dry_position: usize,
    dc_input: [f32; 2],
    dc_output: [f32; 2],
    dc_coefficient: f32,
    drive: Smoother,
    mix: Smoother,
    mode: Mode,
}

impl Default for Saturator {
    fn default() -> Self {
        Self {
            oversamplers: [Oversampler::default(), Oversampler::default()],
            dry_delay: [[0.0; LATENCY]; 2],
            dry_position: 0,
            dc_input: [0.0; 2],
            dc_output: [0.0; 2],
            dc_coefficient: 0.999,
            drive: Smoother::default(),
            mix: Smoother::default(),
            mode: Mode::Soft,
        }
    }
}

impl Saturator {
    pub fn prepare(&mut self, sample_rate: f64, drive: f32, mix: f32) {
        LazyLock::force(&KAISER_TAPS);
        for oversampler in &mut self.oversamplers {
            oversampler.reset();
        }
        for delay in &mut self.dry_delay {
            delay.fill(0.0);
        }
        self.dry_position = 0;
        self.dc_input = [0.0; 2];
        self.dc_output = [0.0; 2];

        // A corner frequency of 5 Hz removes the offset without touching the audible low end.
        self.dc_coefficient = (-core::f64::consts::TAU * 5.0 / sample_rate).exp() as f32;

        self.drive.prepare(sample_rate, 20.0);
        self.mix.prepare(sample_rate, 20.0);
        self.drive.snap_to(f64::from(drive));
        self.mix.snap_to(f64::from(mix));
    }

    pub fn set_mode(&mut self, mode: Mode) {
        self.mode = mode;
    }

    pub fn set_drive(&mut self, linear_gain: f32) {
        self.drive.set_target(f64::from(linear_gain));
    }

    pub fn set_mix(&mut self, mix: f32) {
        self.mix.set_target(f64::from(mix));
    }

    pub fn process(&mut self, left: &mut f32, right: &mut f32) {
        let drive = self.drive.step_f32();
        let mix = self.mix.step_f32();

        *left = self.process_channel(*left, 0, drive, mix);
        *right = self.process_channel(*right, 1, drive, mix);

        self.dry_position = if self.dry_position + 1 == LATENCY {
            0
        } else {
            self.dry_position + 1
        };
    }

    fn process_channel(&mut self, input: f32, channel: usize, drive: f32, mix: f32) -> f32 {
        let delay = &mut self.dry_delay[channel];
        let dry = delay[self.dry_position];
        delay[self.dry_position] = input;

        // Fully dry, the costly part can be skipped. The latency is unaffected, and the filters' old contents are
        // masked by the mix fading in when the saturator is switched back on.
        if mix < SILENT_MIX {
            return dry;
        }

        let mode = self.mode;
        let shaped = self.oversamplers[channel].process(input, |sample| shape(mode, sample * drive));

        // The Tube and Rectifier curves shift the signal off centre, so the offset is removed.
        let centered = shaped - self.dc_input[channel] + self.dc_coefficient * self.dc_output[channel];
        self.dc_input[channel] = shaped;
        self.dc_output[channel] = centered;

        dry + (centered - dry) * mix
    }
}

fn shape(mode: Mode, x: f32) -> f32 {
    match mode {
        Mode::HardClip => clamp(x, -1.0, 1.0),
        Mode::Soft => x.tanh(),
        Mode::SineClip => (core::f32::consts::FRAC_PI_2 * clamp(x, -1.0, 1.0)).sin(),
        Mode::Tube => {
            const BIAS: f32 = 0.25;
            (x + BIAS).tanh() - BIAS.tanh()
        }
        Mode::Wavefolder => {
            // A triangle-shaped transfer curve: unchanged up to +/-1, then folded back and forth.
            let wrapped = x + 1.0 - 4.0 * ((x + 1.0) / 4.0).floor();
            1.0 - (wrapped - 2.0).abs()
        }
        Mode::Rectifier => (0.3 * x + 0.7 * x.abs()).tanh(),
    }
}

/// `std::clamp` on floats, which lets NaN through, unlike `f32::clamp`'s checks on the bounds.
fn clamp(value: f32, low: f32, high: f32) -> f32 {
    if value < low {
        low
    } else if value > high {
        high
    } else {
        value
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_taps_add_up_to_one() {
        let sum: f64 = KAISER_TAPS.iter().map(|&tap| f64::from(tap)).sum();
        assert!((sum - 1.0).abs() < 1e-6);
        assert_eq!(LATENCY, 48);
    }

    #[test]
    fn off_it_delays_by_its_latency_and_nothing_else() {
        let mut saturator = Saturator::default();
        saturator.prepare(48000.0, 1.0, 0.0);
        let mut out = Vec::new();
        for i in 0..LATENCY + 3 {
            let (mut l, mut r) = (if i == 0 { 1.0 } else { 0.0 }, 0.0);
            saturator.process(&mut l, &mut r);
            out.push(l);
        }
        assert_eq!(out[LATENCY], 1.0);
        assert!(out[..LATENCY].iter().all(|&v| v == 0.0));
    }

    #[test]
    fn the_shapes_clip_as_named() {
        assert_eq!(shape(Mode::HardClip, 3.0), 1.0);
        assert_eq!(shape(Mode::Wavefolder, 0.5), 0.5);
        assert!((shape(Mode::Wavefolder, 1.5) - 0.5).abs() < 1e-6, "folded back");
        assert_eq!(shape(Mode::Tube, 0.0), 0.0);
    }
}
