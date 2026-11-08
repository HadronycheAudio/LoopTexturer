//! The looper, modelled as a tape read and written at a variable speed by a single head. The two modes only differ
//! in what moves the head:
//!
//! - Free running: the head never stops, and the length knob sets its speed.
//! - Tempo sync: the head's position comes from the host's musical position, so a tempo change repitches the loop
//!   and the loop always stays on the grid.
//!
//! Reading and writing at the same position means overdubs line up with what's playing. Feedback works directly on
//! the stored cells instead of on an interpolated copy, so a loop at 100% feedback stays bit-exact however many
//! layers are added on top.
//!
//! The arithmetic is the C++ `Looper`'s, in the same order and precision, so that the two give the same bits.

use core::sync::atomic::Ordering::SeqCst;
use std::sync::Arc;

use hadronyche_dsp::Smoother;

use crate::bridge::{Bridge, LoadedLoop, Mode};
use crate::tape::{Tape, max, min};

/// The buttons, as bits in the bridge's pending commands.
pub const COMMAND_REC: i32 = 1 << 0;
pub const COMMAND_OVERDUB: i32 = 1 << 1;
pub const COMMAND_PLAY: i32 = 1 << 2;
pub const COMMAND_STOP: i32 = 1 << 3;
pub const COMMAND_CLEAR: i32 = 1 << 4;

pub const MAX_BARS: i64 = 64;
pub const FREE_TAPE_SECONDS: f64 = 30.0;

/// Removes clicks when punching in and out.
const ENVELOPE_SECONDS: f64 = 0.005;
const SPEED_SMOOTHING_MS: f64 = 50.0;
const FEEDBACK_SMOOTHING_MS: f64 = 20.0;
const UNITY_FEEDBACK_MARGIN: f32 = 1e-4;
const SLOWEST_PLANNED_TEMPO: f64 = 20.0;
const TAPE_HEADROOM: f64 = 1.25;
const ERASE_FRAMES_PER_BLOCK: i64 = 1 << 16;
const JUMP_TOLERANCE_SAMPLES: f64 = 64.0;
/// At normal speed. The filter runs per cell, so its corner rises with the speed.
const DC_CORNER_HZ: f64 = 5.0;
/// The free running tape length that uses DC_CORNER_HZ as it is.
const DC_REFERENCE_SECONDS: f64 = 10.0;

#[derive(Clone, Copy, Debug)]
pub struct Settings {
    pub mode: Mode,
    pub free_length_seconds: f64,
    /// 0 means open-ended: recording runs until Rec is pressed again.
    pub target_bars: f64,
    /// 1 leaves the loop unchanged.
    pub feedback: f64,
}

#[derive(Clone, Copy, Debug)]
pub struct Transport {
    pub is_valid: bool,
    pub is_playing: bool,
    pub quarters: f64,
    pub tempo: f64,
    pub bar_start_quarters: f64,
    pub numerator: i32,
    pub denominator: i32,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum SyncState {
    Empty,
    /// The first pass of a new loop, whose length may not be known yet.
    Recording,
    Loop,
}

/// Unchanged up to full scale, then bends towards a ceiling of 1.5. Above 100% feedback, this curve is what gives the
/// loop its distortion, and it keeps the level from running away.
fn soft_limit(x: f32) -> f32 {
    let magnitude = x.abs();
    if magnitude <= 1.0 {
        return x;
    }

    let limited = 1.0 + 0.5 * ((magnitude - 1.0) / 0.5).tanh();
    if x < 0.0 { -limited } else { limited }
}

fn move_towards(value: f32, target: f32, step: f32) -> f32 {
    if value < target {
        min(value + step, target)
    } else {
        max(value - step, target)
    }
}

pub struct Looper {
    bridge: Arc<Bridge>,
    /// How many samples behind the host's position the input arrives at the looper.
    input_latency: i32,
    sample_rate: f64,
    envelope_step: f32,

    mode: Mode,

    // Shared by both modes.
    is_recording: bool,
    is_overdubbing: bool,
    record_envelope: f32,
    overdub_envelope: f32,
    wet_envelope: f32,
    wet_target: f32,
    feedback: Smoother,
    current_feedback: f32,
    head: f64,
    write_speed: f64,
    previous_left: f32,
    previous_right: f32,
    last_output_left: f32,
    last_output_right: f32,
    dc_coefficient: f32,
    dc_input: [f32; 2],
    dc_output: [f32; 2],

    overview_bucket: i64,
    overview_min: f32,
    overview_max: f32,

    // Free running.
    speed: Smoother,
    free_has_content: bool,
    free_playing: bool,
    free_tape_was_swapped: bool,
    /// How far the head has moved in the current Rec pass.
    recorded_cells: f64,
    /// Negative when the tape isn't being erased.
    erase_cursor: i64,

    // Tempo sync.
    sync_state: SyncState,
    is_armed: bool,
    is_closing: bool,
    is_play_armed: bool,
    is_playing: bool,
    transport_running: bool,
    transport_jumped: bool,
    quarters: f64,
    quarters_per_sample: f64,
    tempo: f64,
    bar_length: f64,
    bar_start: f64,
    next_bar: f64,
    next_expected_quarters: f64,
    has_position: bool,
    target_quarters: f64,
    target_bars: f64,
    anchor: f64,
    loop_quarters: f64,
    frames_per_quarter: f64,
    loop_frames: i64,
}

impl Looper {
    pub fn new(bridge: Arc<Bridge>, input_latency: i32) -> Self {
        Self {
            bridge,
            input_latency,
            sample_rate: 44100.0,
            envelope_step: 0.01,
            mode: Mode::Free,
            is_recording: false,
            is_overdubbing: false,
            record_envelope: 0.0,
            overdub_envelope: 0.0,
            wet_envelope: 0.0,
            wet_target: 0.0,
            feedback: Smoother::default(),
            current_feedback: 1.0,
            head: 0.0,
            write_speed: 1.0,
            previous_left: 0.0,
            previous_right: 0.0,
            last_output_left: 0.0,
            last_output_right: 0.0,
            dc_coefficient: 0.999,
            dc_input: [0.0; 2],
            dc_output: [0.0; 2],
            overview_bucket: -1,
            overview_min: 0.0,
            overview_max: 0.0,
            speed: Smoother::default(),
            free_has_content: false,
            free_playing: true,
            free_tape_was_swapped: false,
            recorded_cells: 0.0,
            erase_cursor: -1,
            sync_state: SyncState::Empty,
            is_armed: false,
            is_closing: false,
            is_play_armed: false,
            is_playing: false,
            transport_running: false,
            transport_jumped: false,
            quarters: 0.0,
            quarters_per_sample: 0.0,
            tempo: 0.0,
            bar_length: 4.0,
            bar_start: 0.0,
            next_bar: 0.0,
            next_expected_quarters: 0.0,
            has_position: false,
            target_quarters: 0.0,
            target_bars: 1.0,
            anchor: 0.0,
            loop_quarters: 0.0,
            frames_per_quarter: 0.0,
            loop_frames: 0,
        }
    }

    /// The tape behind a pointer the mailbox holds.
    ///
    /// SAFETY: every tape pointer in the mailbox came from `Bridge::add_tape`, and the bridge keeps the tape alive
    /// while the pointer is in use, which the worker checks through `tapes_in_use` before releasing one.
    fn tape<'a>(pointer: *const Tape) -> Option<&'a Tape> {
        unsafe { pointer.as_ref() }
    }

    // The lifetimes are the tapes' own, not this borrow's, because the looper changes while it has a tape.
    fn free_tape<'a>(&self) -> Option<&'a Tape> {
        Self::tape(self.bridge.mail.free_tape.load(SeqCst))
    }

    fn sync_tape<'a>(&self) -> Option<&'a Tape> {
        Self::tape(self.bridge.mail.sync_tape.load(SeqCst))
    }

    /// Not on the audio thread, and not while processing. A loop that's already there is kept.
    pub fn prepare(&mut self, sample_rate: f64) {
        self.sample_rate = sample_rate;
        self.envelope_step = (1.0 / (ENVELOPE_SECONDS * sample_rate)) as f32;

        self.feedback.prepare(sample_rate, FEEDBACK_SMOOTHING_MS);
        self.speed.prepare(sample_rate, SPEED_SMOOTHING_MS);
        self.feedback.snap_to(f64::from(self.current_feedback));

        let free_frames = (FREE_TAPE_SECONDS * sample_rate) as i64;
        let needs_tape = match self.free_tape() {
            None => true,
            Some(tape) => !self.free_has_content && tape.capacity() != free_frames,
        };
        if needs_tape {
            let tape = self.bridge.add_tape(Arc::new(Tape::new(free_frames)));
            self.bridge.mail.free_tape.store(tape.cast_mut(), SeqCst);
            self.head = 0.0;
        }

        self.bridge.status.sample_rate.store(sample_rate);
        self.has_position = false;
    }

    /// A loop restored from an older project or another sample rate keeps its own tape, so that it plays the same.
    /// Once it's cleared, the next recording needs a full tape: on a short one, a long loop would run the head slower
    /// than normal speed and skip samples of its input.
    fn is_waiting_for_free_tape(&self) -> bool {
        match self.free_tape() {
            Some(tape) => !self.free_has_content && tape.capacity() != (FREE_TAPE_SECONDS * self.sample_rate) as i64,
            None => false,
        }
    }

    /// Above 100% feedback, this filter stops any offset from building up in the loop. In free running mode, its
    /// corner keeps the same place in the audio whatever the tape's size.
    fn update_dc_coefficient(&mut self) {
        let mut corner_hz = DC_CORNER_HZ;
        if self.mode == Mode::Free {
            if let Some(tape) = self.free_tape() {
                corner_hz *= DC_REFERENCE_SECONDS * self.sample_rate / tape.capacity() as f64;
            }
        }
        self.dc_coefficient = (-core::f64::consts::TAU * corner_hz / self.sample_rate).exp() as f32;
    }

    fn adopt_incoming(&mut self) {
        let incoming = self.bridge.mail.incoming_load.swap(core::ptr::null_mut(), SeqCst);
        // SAFETY: a load stays where the bridge put it, for as long as the bridge lives.
        if let Some(load) = unsafe { incoming.cast_const().as_ref() } {
            let load: LoadedLoop = *load;
            self.mode = load.mode;
            self.is_recording = false;
            self.is_overdubbing = false;
            self.record_envelope = 0.0;
            self.overdub_envelope = 0.0;
            self.is_armed = false;
            self.is_closing = false;
            self.is_play_armed = false;
            self.erase_cursor = -1;
            self.overview_bucket = -1;
            self.free_playing = true;

            if load.mode == Mode::Free {
                if !load.tape.is_null() {
                    self.bridge.mail.free_tape.store(load.tape.cast_mut(), SeqCst);
                    self.head = 0.0;
                    self.write_speed = load.write_speed;
                    self.free_has_content = true;
                } else {
                    self.free_has_content = false;
                    self.erase_cursor = 0;
                }
                self.sync_state = SyncState::Empty;
                self.loop_quarters = 0.0;
                self.loop_frames = 0;
            } else {
                self.free_has_content = false;
                self.erase_cursor = 0;
                if !load.tape.is_null() {
                    self.bridge.mail.sync_tape.store(load.tape.cast_mut(), SeqCst);
                    self.sync_state = SyncState::Loop;
                    self.is_playing = true;
                    self.loop_frames = load.frames;
                    self.loop_quarters = load.quarters;
                    self.anchor = load.anchor_quarters;
                    self.frames_per_quarter = load.frames_per_quarter;
                    self.write_speed = load.write_speed;
                } else {
                    self.sync_state = SyncState::Empty;
                    self.is_playing = false;
                    self.loop_frames = 0;
                    self.loop_quarters = 0.0;
                }
            }
        }

        if !self.free_has_content {
            let offered = self.bridge.mail.offered_free_tape.load(SeqCst);
            if !offered.is_null() {
                self.bridge.mail.free_tape.store(offered, SeqCst);
                self.bridge.mail.offered_free_tape.store(core::ptr::null_mut(), SeqCst);
                self.head = 0.0;
                self.erase_cursor = -1; // A new tape is already blank.
                self.overview_bucket = -1;
                self.free_tape_was_swapped = true;
            }
        }

        // A larger tape is only swapped in when no loop is using the current one.
        if self.sync_state == SyncState::Empty {
            let offered = self.bridge.mail.offered_sync_tape.load(SeqCst);
            if let Some(offered_tape) = Self::tape(offered) {
                let larger = match self.sync_tape() {
                    None => true,
                    Some(current) => offered_tape.capacity() > current.capacity(),
                };
                if larger {
                    self.bridge.mail.sync_tape.store(offered, SeqCst);
                }
                self.bridge.mail.offered_sync_tape.store(core::ptr::null_mut(), SeqCst);
            }
        }
    }

    fn clear_loop(&mut self) {
        self.is_recording = false;
        self.is_overdubbing = false;
        self.record_envelope = 0.0;
        self.overdub_envelope = 0.0;
        self.overview_bucket = -1;

        self.free_has_content = false;
        self.free_playing = true;
        self.erase_cursor = 0;

        self.sync_state = SyncState::Empty;
        self.is_armed = false;
        self.is_closing = false;
        self.is_play_armed = false;
        self.is_playing = false;
        self.loop_frames = 0;
        self.loop_quarters = 0.0;
        self.write_speed = 1.0;

        if let Some(tape) = self.sync_tape() {
            tape.clear_overview();
        }
        if let Some(tape) = self.free_tape() {
            tape.clear_overview();
        }
    }

    fn start_free_tape(&mut self) {
        // A stopped tape starts again from the top of the loop.
        if !self.free_playing {
            self.free_playing = true;
            self.head = 0.0;
            self.wet_envelope = 0.0;
        }
    }

    fn handle_commands(&mut self, settings: &Settings) {
        let commands = self.bridge.mail.pending_commands.swap(0, SeqCst);
        if commands == 0 {
            return;
        }

        if commands & COMMAND_CLEAR != 0 {
            self.clear_loop();
            return;
        }

        if self.mode == Mode::Free {
            if self.erase_cursor >= 0 || self.is_waiting_for_free_tape() {
                return;
            }

            if commands & COMMAND_STOP != 0 {
                self.free_playing = false;
                self.is_recording = false;
                self.is_overdubbing = false;
            }
            if commands & COMMAND_PLAY != 0 {
                self.start_free_tape();
            }

            if commands & COMMAND_REC != 0 {
                self.is_recording = !self.is_recording;
                if self.is_recording {
                    self.start_free_tape();
                    self.is_overdubbing = false;
                    self.recorded_cells = 0.0;

                    // On an empty tape, the loop starts where the recording starts.
                    if !self.free_has_content {
                        self.head = 0.0;
                    }
                }
            }
            if commands & COMMAND_OVERDUB != 0 {
                self.is_overdubbing = !self.is_overdubbing;
                if self.is_overdubbing {
                    self.start_free_tape();
                    self.is_recording = false;
                }
            }
            return;
        }

        if commands & COMMAND_STOP != 0 {
            self.is_playing = false;
            self.is_play_armed = false;
            self.is_overdubbing = false;
            self.is_armed = false;
            if self.sync_state == SyncState::Recording {
                self.cancel_recording();
            }
        }

        if commands & COMMAND_PLAY != 0 && self.sync_state == SyncState::Loop && !self.is_playing {
            self.is_play_armed = true;
        }

        if commands & COMMAND_REC != 0 {
            if self.sync_state == SyncState::Recording {
                self.is_closing = true;
            } else if self.is_armed {
                self.is_armed = false;
            } else {
                self.is_armed = true;
                self.target_bars = settings.target_bars;
            }
        }

        if commands & COMMAND_OVERDUB != 0 && self.sync_state == SyncState::Loop && self.is_playing {
            self.is_overdubbing = !self.is_overdubbing;
        }
    }

    fn update_sync_block(&mut self, transport: &Transport) {
        self.transport_running = transport.is_valid && transport.is_playing && transport.tempo > 0.0;
        if !transport.is_valid || transport.tempo <= 0.0 {
            return;
        }

        self.tempo = transport.tempo;
        self.quarters_per_sample = transport.tempo / (60.0 * self.sample_rate);
        self.bar_length = f64::from(transport.numerator) * 4.0 / f64::from(transport.denominator);
        self.bar_start = transport.bar_start_quarters;

        // The input arrives input_latency samples late, and the head has to line up with it.
        let quarters = transport.quarters - f64::from(self.input_latency) * self.quarters_per_sample;

        // A locate, a cycle jump or a restart.
        let jumped = self.has_position
            && (quarters - self.next_expected_quarters).abs() > JUMP_TOLERANCE_SAMPLES * self.quarters_per_sample;
        if jumped {
            if self.sync_state == SyncState::Recording {
                // There's no sensible way to continue a loop whose length isn't known yet.
                self.cancel_recording();
                self.transport_jumped = true;
            }
            self.wet_envelope = 0.0;
        }

        self.quarters = quarters;

        // The next bar line at or after this position.
        let bars_ahead = ((quarters - self.bar_start) / self.bar_length - 1e-9).ceil();
        self.next_bar = self.bar_start + bars_ahead * self.bar_length;

        // Plan for 64 bars at this tempo, within reason. Recording stops early if the tape runs out.
        let planned_tempo = transport.tempo.max(SLOWEST_PLANNED_TEMPO);
        let needed = MAX_BARS as f64 * self.bar_length * 60.0 / planned_tempo * self.sample_rate;
        let too_small = match self.sync_tape() {
            None => true,
            Some(tape) => (tape.capacity() as f64) < needed,
        };
        if self.sync_state == SyncState::Empty && too_small && self.bridge.mail.offered_sync_tape.load(SeqCst).is_null()
        {
            self.bridge
                .requested_sync_frames
                .store((needed * TAPE_HEADROOM) as i64, SeqCst);
        }
    }

    pub fn begin_block(&mut self, settings: &Settings, transport: &Transport) {
        self.adopt_incoming();

        if settings.mode != self.mode {
            self.clear_loop();
            self.mode = settings.mode;
        }

        if self.mode == Mode::Sync {
            self.update_sync_block(transport);
        } else {
            self.transport_running = transport.is_valid && transport.is_playing;
        }

        self.handle_commands(settings);

        self.feedback.set_target(f64::from(settings.feedback as f32));
        self.update_dc_coefficient();

        if self.mode == Mode::Free {
            if self.is_waiting_for_free_tape() && self.bridge.mail.offered_free_tape.load(SeqCst).is_null() {
                self.bridge
                    .requested_free_frames
                    .store((FREE_TAPE_SECONDS * self.sample_rate) as i64, SeqCst);
            }

            if let Some(tape) = self.free_tape() {
                let frames = tape.capacity() as f64;
                let speed = frames / (settings.free_length_seconds * self.sample_rate);
                self.speed.set_target(speed);

                // A blank tape has nothing to repitch, and a recording that started during a glide would begin
                // below normal speed.
                if self.free_tape_was_swapped {
                    self.speed.snap_to(speed);
                }
                self.free_tape_was_swapped = false;

                if self.erase_cursor >= 0 {
                    self.erase_some_of_free_tape();
                }
            }
        }
    }

    fn erase_some_of_free_tape(&mut self) {
        let Some(tape) = self.free_tape() else {
            return;
        };
        let end = (self.erase_cursor + ERASE_FRAMES_PER_BLOCK).min(tape.capacity());
        for i in self.erase_cursor..end {
            tape.store(0, i, 0.0);
            tape.store(1, i, 0.0);
        }
        self.erase_cursor = if end == tape.capacity() { -1 } else { end };
    }

    fn update_envelopes(&mut self) {
        let step = self.envelope_step;
        self.record_envelope = move_towards(self.record_envelope, if self.is_recording { 1.0 } else { 0.0 }, step);
        self.overdub_envelope = move_towards(self.overdub_envelope, if self.is_overdubbing { 1.0 } else { 0.0 }, step);
        self.wet_envelope = move_towards(self.wet_envelope, self.wet_target, step);

        self.current_feedback = self.feedback.step_f32();
        if (self.current_feedback - 1.0).abs() < UNITY_FEEDBACK_MARGIN {
            self.current_feedback = 1.0;
        }
    }

    fn update_dc_blocker(&mut self, channel: usize, input: f32) -> f32 {
        let output = input - self.dc_input[channel] + self.dc_coefficient * self.dc_output[channel];
        self.dc_input[channel] = input;
        self.dc_output[channel] = output;
        output
    }

    fn apply_cell(&self, old: f32, input: f32) -> f32 {
        let mut value = old;
        if self.overdub_envelope > 0.0 {
            value += input * self.overdub_envelope;
        }
        if self.record_envelope > 0.0 {
            value += (input - value) * self.record_envelope;
        }
        soft_limit(value)
    }

    fn note_overview(&mut self, tape: &Tape, index: i64, left: f32, right: f32) {
        let bucket_frames = tape.bucket_frames();
        let bucket = index / bucket_frames;
        if bucket >= crate::tape::OVERVIEW_BUCKETS {
            return;
        }

        if bucket != self.overview_bucket {
            self.overview_bucket = bucket;
            // Starting mid-bucket means the rest of it wasn't rewritten, so its old peaks still count.
            let starts_at_edge = index % bucket_frames == 0;
            self.overview_min = if starts_at_edge { 0.0 } else { tape.overview_min(bucket) };
            self.overview_max = if starts_at_edge { 0.0 } else { tape.overview_max(bucket) };
        }

        self.overview_min = min(min(self.overview_min, left), right);
        self.overview_max = max(max(self.overview_max, left), right);
        tape.set_overview(bucket, self.overview_min, self.overview_max);
    }

    #[allow(clippy::too_many_arguments)]
    fn write_span(
        &mut self,
        tape: &Tape,
        from: f64,
        to: f64,
        frames: i64,
        is_new_pass: bool,
        in_left: f32,
        in_right: f32,
    ) {
        let writes_input = self.record_envelope > 0.0 || self.overdub_envelope > 0.0;
        let changes_level = self.current_feedback != 1.0;
        if to <= from || (!writes_input && !changes_level) {
            return;
        }

        // Fills every cell the head moved past, ramping from the previous input sample to this one.
        let span = to - from;
        let mut position = from.floor() + 1.0;
        while position <= to {
            if position >= 0.0 {
                let index = (position as i64) % frames;
                let t = ((position - from) / span) as f32;
                let left = self.previous_left + (in_left - self.previous_left) * t;
                let right = self.previous_right + (in_right - self.previous_right) * t;

                let mut old_left = if is_new_pass { 0.0 } else { tape.load(0, index) };
                let mut old_right = if is_new_pass { 0.0 } else { tape.load(1, index) };
                if changes_level {
                    old_left = soft_limit(old_left * self.current_feedback);
                    old_right = soft_limit(old_right * self.current_feedback);
                    if self.current_feedback > 1.0 {
                        old_left = self.update_dc_blocker(0, old_left);
                        old_right = self.update_dc_blocker(1, old_right);
                    }
                }

                let new_left = self.apply_cell(old_left, left);
                let new_right = self.apply_cell(old_right, right);
                tape.store(0, index, new_left);
                tape.store(1, index, new_right);
                self.note_overview(tape, index, new_left, new_right);
            }
            position += 1.0;
        }
    }

    fn process_free(&mut self, in_left: f32, in_right: f32) -> (f32, f32) {
        let Some(tape) = self.free_tape() else {
            return (0.0, 0.0);
        };

        let frames = tape.capacity();
        let speed = self.speed.step();

        if !self.free_playing {
            // The tape stands still, with the output fading out.
            self.wet_target = 0.0;
            self.update_envelopes();
            let out = (
                self.last_output_left * self.wet_envelope,
                self.last_output_right * self.wet_envelope,
            );
            self.previous_left = in_left;
            self.previous_right = in_right;
            return out;
        }

        let previous = self.head;
        let unwrapped = previous + speed;
        self.head = if unwrapped >= frames as f64 {
            unwrapped - frames as f64
        } else {
            unwrapped
        };

        self.wet_target = if self.free_has_content && self.erase_cursor < 0 {
            1.0
        } else {
            0.0
        };
        self.update_envelopes();

        let width = (speed - self.write_speed).max(0.0);
        self.last_output_left = tape.read(0, self.head, frames, width);
        self.last_output_right = tape.read(1, self.head, frames, width);
        let out = (
            self.last_output_left * self.wet_envelope,
            self.last_output_right * self.wet_envelope,
        );

        if self.erase_cursor < 0 {
            if self.record_envelope > 0.0 || self.overdub_envelope > 0.0 {
                self.free_has_content = true;
                self.write_speed = speed;
            }
            if self.free_has_content {
                self.write_span(tape, previous, unwrapped, frames, false, in_left, in_right);
            }

            // After one whole pass, Rec moves on to overdubbing on top of what it just recorded.
            if self.is_recording {
                self.recorded_cells += speed;
                if self.recorded_cells >= frames as f64 {
                    self.is_recording = false;
                    self.is_overdubbing = true;
                }
            }
        }

        self.previous_left = in_left;
        self.previous_right = in_right;
        out
    }

    fn start_recording(&mut self) {
        self.is_armed = false;
        self.is_closing = false;
        self.is_play_armed = false;
        self.is_overdubbing = false;
        self.overdub_envelope = 0.0;
        self.is_playing = false;
        self.transport_jumped = false;

        self.sync_state = SyncState::Recording;
        self.is_recording = true;
        self.record_envelope = 1.0; // At full level right away, so the first attack in the loop is kept.
        self.write_speed = 1.0;

        self.anchor = self.next_bar;
        self.frames_per_quarter = self.sample_rate * 60.0 / self.tempo;
        self.target_quarters = if self.target_bars > 0.0 {
            self.target_bars * self.bar_length
        } else {
            0.0
        };
        self.loop_quarters = 0.0;
        self.loop_frames = 0;
        self.head = -1e-9; // Just before cell 0, so that the first span includes it.
        self.wet_envelope = 0.0;
        self.overview_bucket = -1;
        if let Some(tape) = self.sync_tape() {
            tape.clear_overview();
        }
    }

    fn close_recording(&mut self, quarters: f64) {
        let capacity = self.sync_tape().map_or(0, Tape::capacity);
        self.loop_quarters = quarters;
        self.loop_frames = ((quarters * self.frames_per_quarter).round() as i64).min(capacity);
        self.sync_state = SyncState::Loop;
        self.is_closing = false;
        self.is_playing = true;

        // Recording fades out over the start of the loop, which crossfades the seam, and moves on to overdubbing
        // on top of what it just recorded.
        self.is_recording = false;
        self.is_overdubbing = true;
        self.head -= self.loop_frames as f64;
    }

    fn cancel_recording(&mut self) {
        self.is_recording = false;
        self.record_envelope = 0.0;
        self.is_closing = false;
        self.sync_state = SyncState::Empty;
        self.loop_frames = 0;
        self.loop_quarters = 0.0;
        if let Some(tape) = self.sync_tape() {
            tape.clear_overview();
        }
    }

    fn process_sync(&mut self, in_left: f32, in_right: f32) -> (f32, f32) {
        let tape = self.sync_tape();

        if !self.transport_running {
            // Follows the transport: everything waits, with the output fading out.
            self.wet_target = 0.0;
            self.update_envelopes();
            let out = (
                self.last_output_left * self.wet_envelope,
                self.last_output_right * self.wet_envelope,
            );
            self.previous_left = in_left;
            self.previous_right = in_right;
            return out;
        }

        self.quarters += self.quarters_per_sample;

        if self.quarters >= self.next_bar {
            if self.is_armed && tape.is_some() {
                self.start_recording();
            } else if self.sync_state == SyncState::Recording && self.is_closing && self.next_bar > self.anchor {
                self.close_recording(self.next_bar - self.anchor);
            } else if self.is_play_armed {
                self.is_play_armed = false;
                self.is_playing = true;
            }
            self.next_bar += self.bar_length;
        }

        let speed = self.quarters_per_sample * self.frames_per_quarter;

        if self.sync_state == SyncState::Recording {
            let elapsed = self.quarters - self.anchor;
            let head = elapsed * self.frames_per_quarter;
            let capacity = tape.map_or(0, Tape::capacity);

            if self.target_quarters > 0.0 && elapsed >= self.target_quarters {
                self.close_recording(self.target_quarters);
            } else if elapsed >= MAX_BARS as f64 * self.bar_length {
                self.close_recording(MAX_BARS as f64 * self.bar_length);
            } else if head + speed + 2.0 >= capacity as f64 {
                let whole_bars = (elapsed / self.bar_length).floor() * self.bar_length;
                if whole_bars > 0.0 {
                    self.close_recording(whole_bars);
                } else {
                    self.cancel_recording();
                }
            } else {
                self.update_envelopes();
                if let Some(tape) = tape {
                    self.write_span(tape, self.head, head, capacity, true, in_left, in_right);
                }
                self.head = head;
                self.previous_left = in_left;
                self.previous_right = in_right;
                return (0.0, 0.0);
            }
        }

        let Some(tape) = tape.filter(|_| self.sync_state == SyncState::Loop) else {
            self.wet_target = 0.0;
            self.update_envelopes();
            self.previous_left = in_left;
            self.previous_right = in_right;
            return (0.0, 0.0);
        };

        // The position in the loop comes from the musical position, so it can't drift off the grid.
        let cycles = (self.quarters - self.anchor) / self.loop_quarters;
        let head = (cycles - cycles.floor()) * self.loop_frames as f64;
        let frames = self.loop_frames as f64;

        let mut delta = head - self.head;
        if delta < 0.0 {
            delta += frames;
        }

        self.wet_target = if self.is_playing { 1.0 } else { 0.0 };
        self.update_envelopes();

        let width = (speed - self.write_speed).max(0.0);
        self.last_output_left = tape.read(0, head, self.loop_frames, width);
        self.last_output_right = tape.read(1, head, self.loop_frames, width);
        let out = (
            self.last_output_left * self.wet_envelope,
            self.last_output_right * self.wet_envelope,
        );

        // A big step means the head was moved by a locate, so there's nothing to fill in between.
        if self.is_playing && delta < (speed * 2.0).max(64.0) {
            let from = if self.head < 0.0 { self.head + frames } else { self.head };
            self.write_span(tape, from, from + delta, self.loop_frames, false, in_left, in_right);
        }

        self.head = head;
        self.previous_left = in_left;
        self.previous_right = in_right;
        out
    }

    /// Takes the input, and gives the loop's output.
    pub fn process_sample(&mut self, left: f32, right: f32) -> (f32, f32) {
        if self.mode == Mode::Free {
            self.process_free(left, right)
        } else {
            self.process_sync(left, right)
        }
    }

    pub fn end_block(&mut self) {
        let status = &self.bridge.status;
        let is_sync = self.mode == Mode::Sync;
        let tape = if is_sync {
            self.bridge.mail.sync_tape.load(SeqCst)
        } else {
            self.bridge.mail.free_tape.load(SeqCst)
        };

        status.is_sync_mode.store(is_sync, SeqCst);
        status.is_recording.store(self.is_recording, SeqCst);
        status.is_overdubbing.store(self.is_overdubbing, SeqCst);
        status.is_armed.store(self.is_armed, SeqCst);
        status.is_closing.store(self.is_closing, SeqCst);
        status.is_play_armed.store(self.is_play_armed, SeqCst);
        status
            .is_playing
            .store(if is_sync { self.is_playing } else { self.free_playing }, SeqCst);
        status.is_transport_running.store(self.transport_running, SeqCst);
        status.transport_jumped.store(self.transport_jumped, SeqCst);
        status.active_tape.store(tape, SeqCst);
        status.playhead.store(self.head);
        status.write_speed.store(self.write_speed);
        status.tempo.store(self.tempo);

        self.next_expected_quarters = self.quarters;
        self.has_position = true;

        if is_sync {
            let is_recording = self.sync_state == SyncState::Recording;
            status.has_content.store(self.sync_state != SyncState::Empty, SeqCst);
            status.loop_frames.store(self.loop_frames, SeqCst);
            status.loop_quarters.store(self.loop_quarters);
            status.anchor_quarters.store(self.anchor);
            status.frames_per_quarter.store(self.frames_per_quarter);

            let mut display_frames = self.loop_frames;
            if is_recording {
                display_frames = if self.target_quarters > 0.0 {
                    (self.target_quarters * self.frames_per_quarter) as i64
                } else {
                    self.head as i64
                };
            }
            status.display_frames.store(display_frames, SeqCst);
        } else {
            let frames = Self::tape(tape).map_or(0, Tape::capacity);
            status.has_content.store(self.free_has_content, SeqCst);
            status.loop_frames.store(frames, SeqCst);
            status.display_frames.store(frames, SeqCst);
            status.loop_quarters.store(0.0);
        }
    }
}
