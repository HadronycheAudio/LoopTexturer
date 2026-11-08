//! LoopTexturer's signal processing: the parameters, the processor that runs the signal chain described in
//! docs/signal-path.md, and the looper's bridge to the panel. The C++ side, in src/, is the VST3 classes around it,
//! and reads the parameter table from here through `hadronyche_plugin`, and the looper through src/LooperApi.h.

pub mod bridge;
pub mod ffi;
pub mod looper;
pub mod tape;

mod lfo;
mod saturator;
mod state_variable_filter;

use core::ffi::{CStr, c_void};
use core::sync::atomic::Ordering::SeqCst;
use std::sync::{Arc, Condvar, Mutex};
use std::time::Duration;

use hadronyche_dsp::{Smoother, VOLUME_SILENCE_DB, db_to_gain, dry_gain, pow2_f32, volume_to_gain, wet_gain};
use hadronyche_plugin::{
    Block, Curve, Output, ParamSpec, Params, Processor, StateReader, StateWriter, TRANSPORT_BAR_POSITION_VALID,
    TRANSPORT_PLAYING, TRANSPORT_PROJECT_TIME_MUSIC_VALID, TRANSPORT_TEMPO_VALID, TRANSPORT_TIME_SIGNATURE_VALID, Tail,
    params,
};

use bridge::{Bridge, LoadedLoop, Mode};
use lfo::{Lfo, Wave};
use looper::{Looper, Settings, Transport};
use saturator::Saturator;
use state_variable_filter::{StateVariableFilter, mode_from_index};
use tape::Tape;

// The order of these lists must match the enums in the modules: `Wave`, `SvfMode`, `saturator::Mode`, `Mode` and
// `LooperBars`. Projects save a position in them, so only ever add to the end.
const LFO_WAVES: &[&CStr] = &[c"Sine", c"Triangle", c"Square", c"Sample & hold"];
const FILTER_MODES: &[&CStr] = &[c"Low-pass", c"High-pass", c"Band-pass", c"Notch"];
const SAT_MODES: &[&CStr] = &[
    c"Hard clip",
    c"Soft saturator",
    c"Sine clip",
    c"Tube",
    c"Wavefolder",
    c"Rectifier",
];
const LOOPER_MODES: &[&CStr] = &[c"Free running", c"Tempo sync"];
const LOOPER_BAR_CHOICES: &[&CStr] = &[
    c"1/2 bar", c"1 bar", c"2 bars", c"4 bars", c"6 bars", c"8 bars", c"Custom", c"Open",
];

/// The entries of Looper bars, in order, which the C++ side gets as `LooperBarChoice`.
#[repr(i32)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum LooperBars {
    Half,
    One,
    Two,
    Four,
    Six,
    Eight,
    Custom,
    Open,
}

impl LooperBars {
    pub const NAMES: [&'static str; 8] = ["Half", "One", "Two", "Four", "Six", "Eight", "Custom", "Open"];
}

params! {
    /// Parameter IDs are saved in Cubase projects and automation: never reorder or remove one, only add at the end.
    pub enum Param => PARAMS {
        Gain => ParamSpec::new(c"Gain", c"dB", -24.0, 24.0, 0.0, Curve::Linear, 1),

        Pan => ParamSpec::new(c"Pan", c"%", -100.0, 100.0, 0.0, Curve::Linear, 0),
        PanLfoRate => ParamSpec::new(c"Pan LFO rate", c"Hz", 0.1, 10.0, 1.0, Curve::Log, 2),
        PanLfoAmount => ParamSpec::new(c"Pan LFO amount", c"%", 0.0, 100.0, 0.0, Curve::Linear, 0),
        PanLfoWave => ParamSpec::list(c"Pan LFO wave", LFO_WAVES, 0),

        FilterMode => ParamSpec::list(c"Filter mode", FILTER_MODES, 0),
        FilterFreq => ParamSpec::new(c"Filter freq", c"Hz", 20.0, 20000.0, 20000.0, Curve::Log, 0),
        FilterResonance => ParamSpec::new(c"Filter resonance", c"%", 0.0, 100.0, 0.0, Curve::Linear, 0),
        FilterLfoRate => ParamSpec::new(c"Filter LFO rate", c"Hz", 0.1, 10.0, 1.0, Curve::Log, 2),
        FilterLfoAmount => ParamSpec::new(c"Filter LFO amount", c"oct", 0.0, 4.0, 0.0, Curve::Linear, 2),
        FilterLfoWave => ParamSpec::list(c"Filter LFO wave", LFO_WAVES, 0),

        SatOn => ParamSpec::toggle(c"Saturator on", true),
        SatMode => ParamSpec::list(c"Saturator mode", SAT_MODES, 1),
        SatDrive => ParamSpec::new(c"Saturator drive", c"dB", 0.0, 36.0, 0.0, Curve::Linear, 1),
        SatMix => ParamSpec::new(c"Saturator mix", c"%", 0.0, 100.0, 100.0, Curve::Linear, 0),

        PostSatOn => ParamSpec::toggle(c"Post sat on", false),
        PostSatMode => ParamSpec::list(c"Post sat mode", SAT_MODES, 1),
        PostSatDrive => ParamSpec::new(c"Post sat drive", c"dB", 0.0, 36.0, 0.0, Curve::Linear, 1),
        PostSatMix => ParamSpec::new(c"Post sat mix", c"%", 0.0, 100.0, 100.0, Curve::Linear, 0),

        Volume => ParamSpec::new(c"Volume", c"dB", VOLUME_SILENCE_DB, 6.0, 0.0, Curve::Linear, 1).silent_at_min(),

        // Changing the mode clears the loop, which automation shouldn't be able to do.
        LooperMode => ParamSpec::list(c"Looper mode", LOOPER_MODES, 0).not_automatable(),
        LooperFreeLength => ParamSpec::new(c"Looper length", c"s", 0.5, 30.0, 2.0, Curve::Log, 2),
        LooperBars => ParamSpec::list(c"Looper bars", LOOPER_BAR_CHOICES, 1),
        LooperCustomBars => ParamSpec::new(c"Looper custom bars", c"bars", 1.0, 64.0, 16.0, Curve::Integer, 0),
        LooperFeedback => ParamSpec::new(c"Looper feedback", c"%", 0.0, 200.0, 100.0, Curve::Linear, 0),
        LooperDryWet => ParamSpec::new(c"Looper dry/wet", c"%", 0.0, 100.0, 50.0, Curve::Linear, 0),
    }
}

/// Version 2 added the looper's audio after the parameters. Version 3 extended the looper's length from 10 s to
/// 30 s, which changes what its normalized value means.
const STATE_VERSION: i32 = 3;
const VERSION_2_MAX_LENGTH: f64 = 10.0;

const SMOOTHING_MS: f64 = 20.0;
/// As the C++ wrote it, which isn't quite 1 / sqrt (2).
#[allow(clippy::approx_constant)]
const MIN_Q: f64 = 0.7071;
const MAX_Q: f64 = 20.0;
const MAX_SAVED_FRAMES: i64 = 1 << 31;
const STREAM_CHUNK_FLOATS: usize = 1 << 16;
const WORKER_INTERVAL: Duration = Duration::from_millis(50);

/// The signal path is in single precision, and these gains are held in `f32` on their way to it, as the C++ held
/// them in `float`. Going through `f64` instead would change the smoothers' targets, and so the sound of saved
/// projects.
fn db_to_gain_f32(db: f64) -> f32 {
    db_to_gain(db) as f32
}

fn q_from_resonance(percent: f64) -> f32 {
    (MIN_Q * (MAX_Q / MIN_Q).powf(percent / 100.0)) as f32
}

fn get_transport(transport: &hadronyche_plugin::Transport) -> Transport {
    let mut result = Transport {
        is_valid: false,
        is_playing: false,
        quarters: 0.0,
        tempo: 0.0,
        bar_start_quarters: 0.0,
        numerator: 4,
        denominator: 4,
    };

    let flags = transport.flags;
    result.is_valid = flags & TRANSPORT_PROJECT_TIME_MUSIC_VALID != 0 && flags & TRANSPORT_TEMPO_VALID != 0;
    result.is_playing = flags & TRANSPORT_PLAYING != 0;
    result.quarters = transport.project_time_music;
    result.tempo = transport.tempo;

    if flags & TRANSPORT_TIME_SIGNATURE_VALID != 0
        && transport.time_signature_numerator > 0
        && transport.time_signature_denominator > 0
    {
        result.numerator = transport.time_signature_numerator;
        result.denominator = transport.time_signature_denominator;
    }

    let bar_length = f64::from(result.numerator) * 4.0 / f64::from(result.denominator);
    result.bar_start_quarters = if flags & TRANSPORT_BAR_POSITION_VALID != 0 {
        transport.bar_position_music
    } else {
        (result.quarters / bar_length).floor() * bar_length
    };
    result
}

/// Allocates large tapes and frees old ones, neither of which can happen on the audio thread. It wakes every
/// `WORKER_INTERVAL`, or when the processor stops it.
struct Worker {
    thread: Option<std::thread::JoinHandle<()>>,
    stop: Arc<(Mutex<bool>, Condvar)>,
}

impl Worker {
    fn start(bridge: Arc<Bridge>) -> Self {
        let stop = Arc::new((Mutex::new(false), Condvar::new()));
        let signal = Arc::clone(&stop);
        let thread = std::thread::spawn(move || Self::run(&bridge, &signal));
        Self {
            thread: Some(thread),
            stop,
        }
    }

    fn run(bridge: &Bridge, signal: &(Mutex<bool>, Condvar)) {
        let (stopped, wake) = signal;
        let mut guard = stopped.lock().unwrap_or_else(|poisoned| poisoned.into_inner());
        while !*guard {
            let (next, _) = wake
                .wait_timeout(guard, WORKER_INTERVAL)
                .unwrap_or_else(|poisoned| poisoned.into_inner());
            guard = next;
            if *guard {
                break;
            }
            drop(guard);

            {
                let _tape_lock = bridge
                    .tape_change
                    .lock()
                    .unwrap_or_else(|poisoned| poisoned.into_inner());

                let requested = bridge.requested_sync_frames.swap(0, SeqCst);
                if requested > 0 && bridge.mail.offered_sync_tape.load(SeqCst).is_null() {
                    let tape = bridge.add_tape(Arc::new(Tape::new(requested)));
                    bridge.mail.offered_sync_tape.store(tape.cast_mut(), SeqCst);
                }

                let requested_free = bridge.requested_free_frames.swap(0, SeqCst);
                if requested_free > 0 && bridge.mail.offered_free_tape.load(SeqCst).is_null() {
                    let tape = bridge.add_tape(Arc::new(Tape::new(requested_free)));
                    bridge.mail.offered_free_tape.store(tape.cast_mut(), SeqCst);
                }

                if !bridge.mail.has_pending_load() {
                    bridge.release_tapes_except(&bridge.mail.tapes_in_use());
                }
            }

            guard = stopped.lock().unwrap_or_else(|poisoned| poisoned.into_inner());
        }
    }
}

impl Drop for Worker {
    fn drop(&mut self) {
        {
            let (stopped, wake) = &*self.stop;
            *stopped.lock().unwrap_or_else(|poisoned| poisoned.into_inner()) = true;
            wake.notify_all();
        }
        if let Some(thread) = self.thread.take() {
            let _ = thread.join();
        }
    }
}

pub struct LoopTexturer {
    bridge: Arc<Bridge>,
    /// Dropped before the bridge, so the thread never outlives what it works on.
    _worker: Worker,

    gain: Smoother,
    volume: Smoother,
    pan: Smoother,
    pan_lfo_amount: Smoother,
    /// log2 of the frequency, so that sweeps are even across the range.
    filter_pitch: Smoother,
    filter_lfo_amount: Smoother,
    filter_q: Smoother,
    dry_gain: Smoother,
    wet_gain: Smoother,

    pan_lfo: Lfo,
    filter_lfo: Lfo,
    filter: StateVariableFilter,
    saturator: Saturator,
    post_saturator: Saturator,

    looper: Looper,

    /// Keeps the dry signal lined up with the post saturator's output.
    dry_delay: [[f32; saturator::LATENCY]; 2],
    dry_delay_position: usize,

    /// A restored loop that the audio thread hasn't picked up yet. Saved as is, if the host asks.
    last_loaded_tape: Option<Arc<Tape>>,
    last_load: Option<LoadedLoop>,
}

impl LoopTexturer {
    fn apply_parameters(&mut self, params: &Params<'_>) {
        self.gain
            .set_target(f64::from(db_to_gain_f32(params.plain(Param::Gain))));
        self.volume
            .set_target(f64::from(volume_to_gain(params.plain(Param::Volume)) as f32));

        self.pan
            .set_target(f64::from((params.plain(Param::Pan) / 100.0) as f32));
        self.pan_lfo_amount
            .set_target(f64::from((params.plain(Param::PanLfoAmount) / 100.0) as f32));
        self.pan_lfo.set_rate(params.plain(Param::PanLfoRate));
        self.pan_lfo
            .set_wave(Wave::from_index(params.plain(Param::PanLfoWave) as i32));

        self.filter
            .set_mode(mode_from_index(params.plain(Param::FilterMode) as i32));
        self.filter_pitch
            .set_target(f64::from(params.plain(Param::FilterFreq).log2() as f32));
        self.filter_q
            .set_target(f64::from(q_from_resonance(params.plain(Param::FilterResonance))));
        self.filter_lfo_amount
            .set_target(f64::from(params.plain(Param::FilterLfoAmount) as f32));
        self.filter_lfo.set_rate(params.plain(Param::FilterLfoRate));
        self.filter_lfo
            .set_wave(Wave::from_index(params.plain(Param::FilterLfoWave) as i32));

        self.saturator
            .set_mode(saturator::Mode::from_index(params.plain(Param::SatMode) as i32));
        self.saturator.set_drive(db_to_gain_f32(params.plain(Param::SatDrive)));
        self.saturator
            .set_mix(Self::saturator_mix(params, Param::SatOn, Param::SatMix));

        self.post_saturator
            .set_mode(saturator::Mode::from_index(params.plain(Param::PostSatMode) as i32));
        self.post_saturator
            .set_drive(db_to_gain_f32(params.plain(Param::PostSatDrive)));
        self.post_saturator
            .set_mix(Self::saturator_mix(params, Param::PostSatOn, Param::PostSatMix));

        self.dry_gain
            .set_target(f64::from(dry_gain(params.plain(Param::LooperDryWet)) as f32));
        self.wet_gain
            .set_target(f64::from(wet_gain(params.plain(Param::LooperDryWet)) as f32));
    }

    fn saturator_mix(params: &Params<'_>, on: Param, mix: Param) -> f32 {
        if params.plain(on) > 0.5 {
            (params.plain(mix) / 100.0) as f32
        } else {
            0.0
        }
    }

    fn looper_settings(params: &Params<'_>) -> Settings {
        let target_bars = match params.plain(Param::LooperBars) as i32 {
            0 => 0.5,
            1 => 1.0,
            2 => 2.0,
            3 => 4.0,
            4 => 6.0,
            5 => 8.0,
            6 => params.plain(Param::LooperCustomBars),
            _ => 0.0,
        };
        Settings {
            mode: if params.plain(Param::LooperMode) > 0.5 {
                Mode::Sync
            } else {
                Mode::Free
            },
            free_length_seconds: params.plain(Param::LooperFreeLength),
            target_bars,
            feedback: params.plain(Param::LooperFeedback) / 100.0,
        }
    }

    fn smoothers(&mut self) -> [&mut Smoother; 9] {
        [
            &mut self.gain,
            &mut self.volume,
            &mut self.pan,
            &mut self.pan_lfo_amount,
            &mut self.filter_pitch,
            &mut self.filter_lfo_amount,
            &mut self.filter_q,
            &mut self.dry_gain,
            &mut self.wet_gain,
        ]
    }

    /// The loop as heard: at its current speed and through the post saturator, without the dry signal or Volume,
    /// as interleaved frames. For the panel's drag, on the UI thread. Nothing, when there's no loop to render.
    pub fn render_loop(bridge: &Bridge, settings: &ffi::ExportSettings) -> Option<Vec<f32>> {
        // Starting a little before the loop lets the saturator's filters settle on what plays just before the loop's
        // start, so that the file loops seamlessly. Its latency is skipped too.
        const PRE_ROLL_FRAMES: i64 = 4096;

        let status = bridge.status.copy();
        let tape = bridge.active_tape()?;
        let loop_frames = status.loop_frames;
        let sample_rate = status.sample_rate;
        if loop_frames <= 0 || sample_rate <= 0.0 || !status.has_content {
            return None;
        }

        // The number of frames the loop takes to play at its current speed.
        let mut output_frames = loop_frames;
        if status.is_sync_mode {
            if status.tempo > 0.0 {
                output_frames = (status.loop_quarters * 60.0 / status.tempo * sample_rate).round() as i64;
            }
        } else {
            output_frames = (settings.free_length_seconds * sample_rate).round() as i64;
        }
        if output_frames <= 0 {
            return None;
        }

        let speed = loop_frames as f64 / output_frames as f64;
        let width = (speed - status.write_speed).max(0.0);

        let read_frame = |frame: i64| {
            let wrapped = ((frame % output_frames) + output_frames) % output_frames;
            let head = wrapped as f64 * speed;
            (
                tape.read(0, head, loop_frames, width),
                tape.read(1, head, loop_frames, width),
            )
        };

        let mut interleaved = vec![0f32; output_frames as usize * 2];

        if settings.post_saturator_on {
            let mut saturator = Saturator::default();
            saturator.prepare(
                sample_rate,
                db_to_gain_f32(settings.post_saturator_drive_db),
                settings.post_saturator_mix as f32,
            );
            saturator.set_mode(saturator::Mode::from_index(settings.post_saturator_mode));

            let pre_roll = PRE_ROLL_FRAMES.min(output_frames);
            let skipped = pre_roll + saturator::LATENCY as i64;
            let total = skipped + output_frames;
            for i in 0..total {
                let (mut left, mut right) = read_frame(i - pre_roll);
                saturator.process(&mut left, &mut right);
                if i >= skipped {
                    let frame = (i - skipped) as usize;
                    interleaved[frame * 2] = left;
                    interleaved[frame * 2 + 1] = right;
                }
            }
        } else {
            for i in 0..output_frames {
                let (left, right) = read_frame(i);
                interleaved[i as usize * 2] = left;
                interleaved[i as usize * 2 + 1] = right;
            }
        }

        Some(interleaved)
    }
}

/// Saved from version 2: an `i32` has-loop flag, and if set, `i32` mode, `i64` frames, 4 `f64`s (quarters, anchor,
/// frames per quarter, write speed), then all left samples and all right samples as `f32`.
fn write_loop_state(processor: &LoopTexturer, out: &mut dyn StateWriter) -> bool {
    // A restored loop the audio thread hasn't picked up yet still counts as the current loop.
    let pending = if processor.bridge.mail.has_pending_load() {
        processor.last_load
    } else {
        None
    };
    let status = processor.bridge.status.copy();

    let tape = match pending {
        Some(_) => processor.last_loaded_tape.clone(),
        None => processor.bridge.active_tape(),
    };
    let is_sync = pending.map_or(status.is_sync_mode, |load| load.mode == Mode::Sync);
    let frames = pending.map_or(status.loop_frames, |load| load.frames);
    let quarters = pending.map_or(status.loop_quarters, |load| load.quarters);
    let has_content = pending.map_or(status.has_content, |load| !load.tape.is_null());
    let has_loop = tape.is_some() && has_content && frames > 0 && (!is_sync || quarters > 0.0);

    if !out.write(&i32::from(has_loop).to_le_bytes()) {
        return false;
    }
    let Some(tape) = tape.filter(|_| has_loop) else {
        return true;
    };

    let mode: i32 = if is_sync { 1 } else { 0 };
    let anchor = pending.map_or(status.anchor_quarters, |load| load.anchor_quarters);
    let frames_per_quarter = pending.map_or(status.frames_per_quarter, |load| load.frames_per_quarter);
    let write_speed = pending.map_or(status.write_speed, |load| load.write_speed);

    let mut header = Vec::with_capacity(4 + 8 + 4 * 8);
    header.extend_from_slice(&mode.to_le_bytes());
    header.extend_from_slice(&frames.to_le_bytes());
    for field in [quarters, anchor, frames_per_quarter, write_speed] {
        header.extend_from_slice(&field.to_le_bytes());
    }
    if !out.write(&header) {
        return false;
    }

    let mut chunk = vec![0u8; STREAM_CHUNK_FLOATS * 4];
    for channel in 0..2 {
        let mut start = 0;
        while start < frames {
            let count = (STREAM_CHUNK_FLOATS as i64).min(frames - start) as usize;
            for (i, bytes) in chunk.chunks_exact_mut(4).take(count).enumerate() {
                bytes.copy_from_slice(&tape.load(channel, start + i as i64).to_le_bytes());
            }
            if !out.write(&chunk[..count * 4]) {
                return false;
            }
            start += count as i64;
        }
    }
    true
}

fn read_exact<const N: usize>(input: &mut dyn StateReader) -> Option<[u8; N]> {
    let mut bytes = [0u8; N];
    input.read(&mut bytes).then_some(bytes)
}

/// Reads what `write_loop_state` wrote, and gives the loop to the audio thread. An older state has no loop section,
/// which reads as having no loop, and so does damaged data.
fn read_loop_state(processor: &mut LoopTexturer, params: &Params<'_>, input: &mut dyn StateReader) {
    let mut load = LoadedLoop {
        mode: if params.plain(Param::LooperMode) > 0.5 {
            Mode::Sync
        } else {
            Mode::Free
        },
        tape: core::ptr::null(),
        frames: 0,
        quarters: 0.0,
        anchor_quarters: 0.0,
        frames_per_quarter: 0.0,
        write_speed: 1.0,
    };
    let mut tape: Option<Arc<Tape>> = None;

    let has_loop = read_exact::<4>(input).map(i32::from_le_bytes);
    if has_loop == Some(1) {
        let header = (read_exact::<4>(input), read_exact::<8>(input), read_exact::<32>(input));
        if let (Some(mode), Some(frames), Some(fields)) = header {
            let mode = i32::from_le_bytes(mode);
            let frames = i64::from_le_bytes(frames);
            if frames > 0 && frames <= MAX_SAVED_FRAMES {
                let new_tape = Arc::new(Tape::new(frames));

                let mut ok = true;
                let mut chunk = vec![0u8; STREAM_CHUNK_FLOATS * 4];
                'channels: for channel in 0..2 {
                    let mut start = 0;
                    while start < frames {
                        let count = (STREAM_CHUNK_FLOATS as i64).min(frames - start) as usize;
                        if !input.read(&mut chunk[..count * 4]) {
                            ok = false;
                            break 'channels;
                        }
                        for (i, bytes) in chunk.chunks_exact(4).take(count).enumerate() {
                            let value = f32::from_le_bytes([bytes[0], bytes[1], bytes[2], bytes[3]]);
                            new_tape.store(channel, start + i as i64, if value.is_finite() { value } else { 0.0 });
                        }
                        start += count as i64;
                    }
                }

                if ok {
                    new_tape.build_overview(frames);
                    let field = |i: usize| f64::from_le_bytes(fields[i * 8..i * 8 + 8].try_into().expect("8 bytes"));
                    load.mode = if mode == 1 { Mode::Sync } else { Mode::Free };
                    load.tape = Arc::as_ptr(&new_tape);
                    load.frames = frames;
                    load.quarters = field(0);
                    load.anchor_quarters = field(1);
                    load.frames_per_quarter = field(2);
                    load.write_speed = field(3);
                    tape = Some(new_tape);
                }
            }
        }
    }

    let _lock = processor
        .bridge
        .tape_change
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner());
    if let Some(tape) = &tape {
        processor.bridge.add_tape(Arc::clone(tape));
    }

    processor.last_loaded_tape = tape;
    processor.last_load = Some(load);
    processor.bridge.load_loop(load);
}

impl Processor for LoopTexturer {
    const PARAMS: &'static [ParamSpec] = &PARAMS;
    const STATE_VERSION: i32 = STATE_VERSION;
    const HAS_AUDIO_INPUT: bool = true;

    fn new() -> Self {
        let bridge = Bridge::create();
        Self {
            _worker: Worker::start(Arc::clone(&bridge)),
            looper: Looper::new(Arc::clone(&bridge), saturator::LATENCY as i32),
            bridge,
            gain: Smoother::default(),
            volume: Smoother::default(),
            pan: Smoother::default(),
            pan_lfo_amount: Smoother::default(),
            filter_pitch: Smoother::default(),
            filter_lfo_amount: Smoother::default(),
            filter_q: Smoother::default(),
            dry_gain: Smoother::default(),
            wet_gain: Smoother::default(),
            pan_lfo: Lfo::new(0x9E37_79B9),
            filter_lfo: Lfo::new(0x85EB_CA6B),
            filter: StateVariableFilter::default(),
            saturator: Saturator::default(),
            post_saturator: Saturator::default(),
            dry_delay: [[0.0; saturator::LATENCY]; 2],
            dry_delay_position: 0,
            last_loaded_tape: None,
            last_load: None,
        }
    }

    fn prepare(&mut self, sample_rate: f64, _max_block_size: usize, params: &Params<'_>) {
        for smoother in self.smoothers() {
            smoother.prepare(sample_rate, SMOOTHING_MS);
        }

        self.gain.snap_to(f64::from(db_to_gain_f32(params.plain(Param::Gain))));
        self.volume
            .snap_to(f64::from(volume_to_gain(params.plain(Param::Volume)) as f32));
        self.pan.snap_to(f64::from((params.plain(Param::Pan) / 100.0) as f32));
        self.pan_lfo_amount
            .snap_to(f64::from((params.plain(Param::PanLfoAmount) / 100.0) as f32));
        self.filter_pitch
            .snap_to(f64::from(params.plain(Param::FilterFreq).log2() as f32));
        self.filter_q
            .snap_to(f64::from(q_from_resonance(params.plain(Param::FilterResonance))));
        self.filter_lfo_amount
            .snap_to(f64::from(params.plain(Param::FilterLfoAmount) as f32));
        self.dry_gain
            .snap_to(f64::from(dry_gain(params.plain(Param::LooperDryWet)) as f32));
        self.wet_gain
            .snap_to(f64::from(wet_gain(params.plain(Param::LooperDryWet)) as f32));

        self.pan_lfo.prepare(sample_rate);
        self.filter_lfo.prepare(sample_rate);
        self.filter.prepare(sample_rate);

        self.saturator.prepare(
            sample_rate,
            db_to_gain_f32(params.plain(Param::SatDrive)),
            Self::saturator_mix(params, Param::SatOn, Param::SatMix),
        );
        self.post_saturator.prepare(
            sample_rate,
            db_to_gain_f32(params.plain(Param::PostSatDrive)),
            Self::saturator_mix(params, Param::PostSatOn, Param::PostSatMix),
        );

        for delay in &mut self.dry_delay {
            delay.fill(0.0);
        }
        self.dry_delay_position = 0;

        {
            let _lock = self
                .bridge
                .tape_change
                .lock()
                .unwrap_or_else(|poisoned| poisoned.into_inner());
            self.looper.prepare(sample_rate);
        }

        self.apply_parameters(params);
    }

    fn process(&mut self, block: Block<'_>) -> Output {
        self.apply_parameters(&block.params);
        self.looper
            .begin_block(&Self::looper_settings(&block.params), &get_transport(block.transport));

        let [output_left, output_right] = block.output;

        const QUARTER_PI: f32 = core::f32::consts::FRAC_PI_4;
        const SQRT2: f32 = core::f32::consts::SQRT_2;

        for i in 0..output_left.len() {
            // A broken sample upstream would otherwise stay in the filters and the loop for ever.
            let sample = |channel: usize| block.input.map_or(0.0, |input| input[channel][i]);
            let mut left = finite_or_silence(sample(0));
            let mut right = finite_or_silence(sample(1));

            // Gain
            let gain = self.gain.step_f32();
            left *= gain;
            right *= gain;

            // Pan: an equal-power balance, scaled to leave the signal unchanged in the centre.
            let pan_lfo = self.pan_lfo.step();
            let position = clamp(
                self.pan.step_f32() + self.pan_lfo_amount.step_f32() * pan_lfo,
                -1.0,
                1.0,
            );
            let angle = (position + 1.0) * QUARTER_PI;
            left *= angle.cos() * SQRT2;
            right *= angle.sin() * SQRT2;

            // Filter
            let filter_lfo = self.filter_lfo.step();
            let pitch = self.filter_pitch.step_f32() + self.filter_lfo_amount.step_f32() * filter_lfo;
            let q = self.filter_q.step_f32();
            self.filter.set_coefficients(f64::from(pow2_f32(pitch)), f64::from(q));
            left = self.filter.process(left, 0);
            right = self.filter.process(right, 1);

            // Saturator
            self.saturator.process(&mut left, &mut right);

            // The dry signal bypasses the looper and post saturator, delayed to stay aligned with them.
            let delay_index = self.dry_delay_position;
            let dry_left = self.dry_delay[0][delay_index];
            let dry_right = self.dry_delay[1][delay_index];
            self.dry_delay[0][delay_index] = left;
            self.dry_delay[1][delay_index] = right;
            self.dry_delay_position = if self.dry_delay_position + 1 == saturator::LATENCY {
                0
            } else {
                self.dry_delay_position + 1
            };

            // Looper: takes the input, and gives the loop's output.
            (left, right) = self.looper.process_sample(left, right);

            // Post saturator
            self.post_saturator.process(&mut left, &mut right);

            // Dry/wet
            let dry_gain = self.dry_gain.step_f32();
            let wet_gain = self.wet_gain.step_f32();
            left = dry_left * dry_gain + left * wet_gain;
            right = dry_right * dry_gain + right * wet_gain;

            // Volume
            let volume = self.volume.step_f32();
            output_left[i] = left * volume;
            output_right[i] = right * volume;
        }

        self.looper.end_block();
        Output::Sound
    }

    fn latency(&self, _sample_rate: f64) -> u32 {
        (saturator::LATENCY * 2) as u32
    }

    /// The looper's tape keeps running without any input, so the host must not suspend the plug-in.
    fn tail(&self, _sample_rate: f64) -> Tail {
        Tail::Infinite
    }

    fn migrate_param(index: usize, normalized: f64, version: i32) -> f64 {
        if version < 3 && index == Param::LooperFreeLength as usize {
            let spec = &PARAMS[Param::LooperFreeLength as usize];
            return spec.plain_to_normalized(spec.min * (VERSION_2_MAX_LENGTH / spec.min).powf(normalized));
        }
        normalized
    }

    fn save_state(&self, out: &mut dyn StateWriter) -> bool {
        write_loop_state(self, out)
    }

    fn load_state(&mut self, params: &Params<'_>, input: &mut dyn StateReader) -> bool {
        read_loop_state(self, params, input);
        true
    }

    fn shared(&self) -> *const c_void {
        Arc::as_ptr(&self.bridge).cast()
    }
}

fn finite_or_silence(sample: f32) -> f32 {
    if sample.is_finite() { sample } else { 0.0 }
}

/// `std::clamp` on floats, which lets NaN through.
fn clamp(value: f32, low: f32, high: f32) -> f32 {
    if value < low {
        low
    } else if value > high {
        high
    } else {
        value
    }
}

hadronyche_plugin::export_plugin!(LoopTexturer);

#[cfg(test)]
mod tests {
    use super::*;
    use hadronyche_plugin::header;
    use std::path::Path;

    #[test]
    fn param_ids_header_is_current() {
        let path = Path::new(env!("CARGO_MANIFEST_DIR")).join("../src/ParamIds.h");
        if let Err(problem) = header::check(&path, &header::param_ids("LoopTexturer", &Param::NAMES)) {
            panic!("{problem}");
        }
    }

    /// The looper's commands and the bars list, for the C++ side and the tests, written as ParamIds.h is.
    #[test]
    fn looper_ids_header_is_current() {
        use std::fmt::Write;

        let mut text = String::new();
        text.push_str("#pragma once\n\n");
        text.push_str("// Generated from rust/src/looper.rs and rust/src/lib.rs, by cargo test in rust/. Don't edit it here: change the\n");
        text.push_str("// Rust, and run HADRONYCHE_UPDATE_HEADERS=1 cargo test in rust/ to bring this up to date.\n\n");
        text.push_str("#include <cstdint>\n\n");
        text.push_str("namespace LoopTexturer {\n\n");
        text.push_str("// Buckets in a tape's overview, and the frames of the LV2 panel's mirror tape.\n");
        let _ = writeln!(
            text,
            "constexpr int64_t kOverviewBuckets = {};\n",
            tape::OVERVIEW_BUCKETS
        );
        text.push_str("// The looper's buttons, as bits in a looper command message.\n");
        text.push_str("enum LooperCommand\n{\n");
        for (name, value) in [
            ("Rec", looper::COMMAND_REC),
            ("Overdub", looper::COMMAND_OVERDUB),
            ("Play", looper::COMMAND_PLAY),
            ("Stop", looper::COMMAND_STOP),
            ("Clear", looper::COMMAND_CLEAR),
        ] {
            let _ = writeln!(text, "    kCommand{name} = {value},");
        }
        text.push_str("};\n\n");
        text.push_str("// The entries of Looper bars, in order. Projects save a position in the list.\n");
        text.push_str("enum LooperBarChoice\n{\n");
        for name in LooperBars::NAMES {
            let _ = writeln!(text, "    kBars{name},");
        }
        text.push_str("};\n\n} // namespace LoopTexturer\n");

        let path = Path::new(env!("CARGO_MANIFEST_DIR")).join("../src/LooperIds.h");
        if let Err(problem) = header::check(&path, &text) {
            panic!("{problem}");
        }
    }

    /// src/LooperApi.h is what cbindgen makes of ffi.rs, so that the C++ side can never disagree with it.
    #[test]
    fn looper_api_header_is_current() {
        let crate_dir = Path::new(env!("CARGO_MANIFEST_DIR"));
        let generated = cbindgen::Builder::new()
            .with_crate(crate_dir)
            .with_config(cbindgen::Config::from_file(crate_dir.join("cbindgen.toml")).expect("cbindgen.toml reads"))
            .generate()
            .expect("cbindgen makes the header");
        let mut text = Vec::new();
        generated.write(&mut text);
        let text = String::from_utf8(text).expect("the header is text");

        let path = crate_dir.join("../src/LooperApi.h");
        if let Err(problem) = header::check(&path, &text) {
            panic!("{problem}");
        }
    }

    #[test]
    fn the_defaults_are_what_projects_have_always_started_from() {
        let plain: Vec<f64> = PARAMS.iter().map(|spec| spec.default).collect();
        assert_eq!(
            plain,
            [
                0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 20000.0, 0.0, 1.0, 0.0, 0.0, 1.0, 1.0, 0.0, 100.0, 0.0, 1.0, 0.0, 100.0,
                0.0, 0.0, 2.0, 1.0, 16.0, 100.0, 50.0
            ]
        );
        assert!(!PARAMS[Param::LooperMode as usize].is_automatable);
    }

    #[test]
    fn an_older_length_is_converted_to_the_longer_range() {
        // Full on the 10 s knob is 10 s on the 30 s knob.
        let spec = &PARAMS[Param::LooperFreeLength as usize];
        let converted = LoopTexturer::migrate_param(Param::LooperFreeLength as usize, 1.0, 2);
        assert!((spec.normalized_to_plain(converted) - 10.0).abs() < 1e-9);
        assert_eq!(
            LoopTexturer::migrate_param(Param::LooperFreeLength as usize, 1.0, 3),
            1.0
        );
        assert_eq!(LoopTexturer::migrate_param(Param::Gain as usize, 0.25, 2), 0.25);
    }
}
