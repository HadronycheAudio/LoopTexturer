//! A free-running LFO with a bipolar output, from -1 to 1.

use hadronyche_dsp::Smoother;

/// The order matches LFO_WAVES in lib.rs, and projects save a position in it.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Wave {
    Sine,
    Triangle,
    Square,
    SampleAndHold,
}

impl Wave {
    pub fn from_index(index: i32) -> Self {
        match index {
            1 => Wave::Triangle,
            2 => Wave::Square,
            3 => Wave::SampleAndHold,
            _ => Wave::Sine,
        }
    }
}

#[derive(Clone, Debug)]
pub struct Lfo {
    slew: Smoother,
    wave: Wave,
    sample_rate: f64,
    phase: f64,
    increment: f64,
    held: f32,
    random_state: u32,
}

impl Lfo {
    pub fn new(random_seed: u32) -> Self {
        Self {
            slew: Smoother::default(),
            wave: Wave::Sine,
            sample_rate: 44100.0,
            phase: 0.0,
            increment: 0.0,
            held: 0.0,
            random_state: random_seed,
        }
    }

    pub fn prepare(&mut self, sample_rate: f64) {
        self.sample_rate = sample_rate;
        self.phase = 0.0;
        self.held = self.next_random();
        // Rounds off the jumps of the square and sample & hold waves, which would otherwise click.
        self.slew.prepare(sample_rate, 2.0);
        self.slew.snap_to(f64::from(self.shape()));
    }

    pub fn set_rate(&mut self, hz: f64) {
        self.increment = hz / self.sample_rate;
    }

    pub fn set_wave(&mut self, wave: Wave) {
        self.wave = wave;
    }

    pub fn step(&mut self) -> f32 {
        self.phase += self.increment;
        if self.phase >= 1.0 {
            self.phase -= 1.0;
            self.held = self.next_random();
        }

        self.slew.set_target(f64::from(self.shape()));
        self.slew.step_f32()
    }

    fn shape(&self) -> f32 {
        let phase = self.phase as f32;
        match self.wave {
            Wave::Sine => (core::f32::consts::TAU * phase).sin(),
            Wave::Triangle => {
                if phase < 0.25 {
                    4.0 * phase
                } else if phase < 0.75 {
                    2.0 - 4.0 * phase
                } else {
                    4.0 * phase - 4.0
                }
            }
            Wave::Square => {
                if phase < 0.5 {
                    1.0
                } else {
                    -1.0
                }
            }
            Wave::SampleAndHold => self.held,
        }
    }

    /// xorshift32: fast and safe on the audio thread.
    fn next_random(&mut self) -> f32 {
        self.random_state ^= self.random_state << 13;
        self.random_state ^= self.random_state >> 17;
        self.random_state ^= self.random_state << 5;
        self.random_state as f32 / 2147483648.0 - 1.0
    }
}
