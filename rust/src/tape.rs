//! Stereo audio storage for the looper, with a peak overview for drawing the waveform.

use core::sync::atomic::{AtomicU32, Ordering::Relaxed};

pub const OVERVIEW_BUCKETS: i64 = 32768;
const MAX_AVERAGED_CELLS: i64 = 64;

/// The audio thread writes while the panel reads, so every value is a relaxed atomic, holding an `f32`'s bits. On
/// x64 that costs the same as a plain float.
pub struct Tape {
    capacity: i64,
    bucket_frames: i64,
    left: Box<[AtomicU32]>,
    right: Box<[AtomicU32]>,
    overview_min: Box<[AtomicU32]>,
    overview_max: Box<[AtomicU32]>,
}

fn zeros(count: usize) -> Box<[AtomicU32]> {
    let mut cells = Vec::with_capacity(count);
    cells.resize_with(count, || AtomicU32::new(0));
    cells.into_boxed_slice()
}

impl Tape {
    /// Allocating and zeroing can take a while, so this must never run on the audio thread.
    pub fn new(frames: i64) -> Self {
        let count = frames.max(0) as usize;
        Self {
            capacity: frames,
            bucket_frames: ((frames + OVERVIEW_BUCKETS - 1) / OVERVIEW_BUCKETS).max(1),
            left: zeros(count),
            right: zeros(count),
            overview_min: zeros(OVERVIEW_BUCKETS as usize),
            overview_max: zeros(OVERVIEW_BUCKETS as usize),
        }
    }

    pub fn capacity(&self) -> i64 {
        self.capacity
    }

    pub fn bucket_frames(&self) -> i64 {
        self.bucket_frames
    }

    fn channel(&self, channel: usize) -> &[AtomicU32] {
        if channel == 0 { &self.left } else { &self.right }
    }

    pub fn load(&self, channel: usize, index: i64) -> f32 {
        f32::from_bits(self.channel(channel)[index as usize].load(Relaxed))
    }

    pub fn store(&self, channel: usize, index: i64, value: f32) {
        self.channel(channel)[index as usize].store(value.to_bits(), Relaxed);
    }

    /// Reads at a fractional position in a loop of the given length. A width of one cell or more averages that many
    /// cells, which limits aliasing when playing faster than the audio was recorded.
    pub fn read(&self, channel: usize, head: f64, frames: i64, width: f64) -> f32 {
        // Rounding can put the position right on the end of the loop.
        let first = (head as i64).min(frames - 1);
        if width < 1.0 {
            let fraction = (head - first as f64) as f32;
            let next = if first + 1 >= frames { 0 } else { first + 1 };
            let a = self.load(channel, first);
            return a + (self.load(channel, next) - a) * fraction;
        }

        let count = (width as i64 + 1).min(MAX_AVERAGED_CELLS);
        let mut sum = 0f32;
        let mut index = first;
        for _ in 0..count {
            sum += self.load(channel, index);
            index = if index == 0 { frames - 1 } else { index - 1 };
        }
        sum / count as f32
    }

    pub fn overview_min(&self, bucket: i64) -> f32 {
        f32::from_bits(self.overview_min[bucket as usize].load(Relaxed))
    }

    pub fn overview_max(&self, bucket: i64) -> f32 {
        f32::from_bits(self.overview_max[bucket as usize].load(Relaxed))
    }

    pub fn set_overview(&self, bucket: i64, min: f32, max: f32) {
        self.overview_min[bucket as usize].store(min.to_bits(), Relaxed);
        self.overview_max[bucket as usize].store(max.to_bits(), Relaxed);
    }

    pub fn clear_overview(&self) {
        for bucket in 0..OVERVIEW_BUCKETS {
            self.set_overview(bucket, 0.0, 0.0);
        }
    }

    /// The current peaks, for a tape whose contents were just restored.
    pub fn build_overview(&self, frames: i64) {
        let bucket_frames = self.bucket_frames;
        let mut start = 0;
        while start < frames {
            let mut low = 0f32;
            let mut high = 0f32;
            let end = (start + bucket_frames).min(frames);
            for i in start..end {
                for channel in 0..2 {
                    let value = self.load(channel, i);
                    low = min(low, value);
                    high = max(high, value);
                }
            }
            self.set_overview(start / bucket_frames, low, high);
            start += bucket_frames;
        }
    }
}

/// `std::min` and `std::max` on floats: the second wins only when it's smaller or greater.
pub fn min(a: f32, b: f32) -> f32 {
    if b < a { b } else { a }
}

pub fn max(a: f32, b: f32) -> f32 {
    if a < b { b } else { a }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn it_reads_between_cells_and_round_the_loop() {
        let tape = Tape::new(8);
        for i in 0..4 {
            tape.store(0, i, i as f32);
        }
        assert_eq!(tape.read(0, 1.5, 4, 0.0), 1.5);
        assert_eq!(tape.read(0, 3.5, 4, 0.0), 1.5, "the end wraps to the start");
        assert_eq!(
            tape.read(0, 3.0, 4, 2.0),
            2.0,
            "a wide read averages the cells behind the head"
        );
    }

    #[test]
    fn the_overview_covers_the_tape_in_buckets() {
        let tape = Tape::new(OVERVIEW_BUCKETS * 2);
        assert_eq!(tape.bucket_frames(), 2);
        tape.store(1, 3, -0.5);
        tape.build_overview(4);
        assert_eq!((tape.overview_min(1), tape.overview_max(1)), (-0.5, 0.0));
        tape.clear_overview();
        assert_eq!(tape.overview_min(1), 0.0);
    }
}
