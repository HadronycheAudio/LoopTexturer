//! What the C++ side reaches the looper through, besides the frame: the bridge that the panel reads, for its status,
//! waveform and export, and that the LV2's panel mirrors. src/LooperApi.h is generated from this file by cargo test,
//! so a change here is a change to that header.
//!
//! A bridge or tape pointer given out here is retained, and the C++ side releases it. Every function takes a null
//! pointer and does nothing.
//!
//! Safety, for all of them: a pointer given to one of these came from one of them, or from `Processor::shared`, and
//! hasn't been released, and a buffer is as long as its count says.
#![allow(clippy::missing_safety_doc)]

use core::ffi::c_void;
use core::sync::atomic::Ordering::SeqCst;
use std::sync::Arc;

use crate::bridge::{Bridge, StatusCopy};
use crate::tape::{OVERVIEW_BUCKETS, Tape};

/// What the panel's drag renders with. The mode is the saturator's list's index.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct ExportSettings {
    pub post_saturator_on: bool,
    pub post_saturator_mode: i32,
    pub post_saturator_drive_db: f64,
    /// 0 to 1.
    pub post_saturator_mix: f64,
    pub free_length_seconds: f64,
}

/// SAFETY: the pointer came from one of the functions here, or from `Processor::shared`, and hasn't been released.
unsafe fn bridge<'a>(pointer: *const c_void) -> Option<&'a Bridge> {
    unsafe { pointer.cast::<Bridge>().as_ref() }
}

unsafe fn tape<'a>(pointer: *const c_void) -> Option<&'a Tape> {
    unsafe { pointer.cast::<Tape>().as_ref() }
}

fn retained<T>(arc: Arc<T>) -> *const c_void {
    Arc::into_raw(arc).cast()
}

/// The bridge with this ID, retained, or null when it's gone. The ID comes from the processor's own message.
#[unsafe(no_mangle)]
pub extern "C" fn looptexturer_bridge_find(id: i64) -> *const c_void {
    Bridge::find(id).map_or(core::ptr::null(), retained)
}

/// The processor's bridge, as `RustProcessor::shared ()` gives it, retained for the C++ side to keep.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn looptexturer_bridge_retain(shared: *const c_void) -> *const c_void {
    if shared.is_null() {
        return core::ptr::null();
    }
    // SAFETY: the pointer is a live bridge's, as the module says, and the count goes back down on release.
    unsafe {
        Arc::increment_strong_count(shared.cast::<Bridge>());
    }
    shared
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn looptexturer_bridge_release(bridge: *const c_void) {
    if !bridge.is_null() {
        // SAFETY: retained by one of the functions here, once per release.
        unsafe { drop(Arc::from_raw(bridge.cast::<Bridge>())) };
    }
}

/// A bridge for the LV2's panel, which fills it from the copies the audio side sends. Its tape has one frame per
/// overview bucket, so a bucket of the real tape is a bucket of this one. Retained.
#[unsafe(no_mangle)]
pub extern "C" fn looptexturer_bridge_create_mirror() -> *const c_void {
    let bridge = Bridge::create();
    let tape = bridge.add_tape(Arc::new(Tape::new(OVERVIEW_BUCKETS)));
    bridge.mail.free_tape.store(tape.cast_mut(), SeqCst);
    retained(bridge)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn looptexturer_bridge_id(bridge: *const c_void) -> i64 {
    // SAFETY: as the module says.
    unsafe { self::bridge(bridge) }.map_or(0, Bridge::id)
}

/// Fills `status` with the looper's, as it stands.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn looptexturer_bridge_status(bridge: *const c_void, status: *mut StatusCopy) {
    // SAFETY: as the module says, and the caller gives a struct to fill.
    if let (Some(bridge), false) = (unsafe { self::bridge(bridge) }, status.is_null()) {
        unsafe { status.write(bridge.status.copy()) };
    }
}

/// A button, as a bit or several, which the looper takes at the start of its next block.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn looptexturer_bridge_send_command(bridge: *const c_void, command: i32) {
    // SAFETY: as the module says.
    if let Some(bridge) = unsafe { self::bridge(bridge) } {
        bridge.send_command(command);
    }
}

/// The tape the looper plays from, retained, or null while there isn't one.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn looptexturer_bridge_active_tape(bridge: *const c_void) -> *const c_void {
    // SAFETY: as the module says.
    unsafe { self::bridge(bridge) }
        .and_then(Bridge::active_tape)
        .map_or(core::ptr::null(), retained)
}

/// For a mirror: the status as copied from the audio side. The mirror's tape stands in for the real one when the copy
/// says there is one.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn looptexturer_bridge_set_status(bridge: *const c_void, status: *const StatusCopy) {
    // SAFETY: as the module says, and the caller gives a filled struct.
    if let (Some(bridge), Some(status)) = (unsafe { self::bridge(bridge) }, unsafe { status.as_ref() }) {
        bridge.status.set(status);
        let tape = if status.has_tape {
            bridge.mail.free_tape.load(SeqCst)
        } else {
            core::ptr::null_mut()
        };
        bridge.status.active_tape.store(tape, SeqCst);
    }
}

/// For a mirror: `count` buckets of the overview from `first`, as a low and a high float each.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn looptexturer_bridge_set_overview(
    bridge: *const c_void,
    first: i64,
    peaks: *const f32,
    count: i64,
) {
    // SAFETY: as the module says.
    let Some(bridge) = (unsafe { self::bridge(bridge) }) else {
        return;
    };
    let Some(tape) = bridge.active_tape() else {
        return;
    };
    if peaks.is_null() || first < 0 || count < 0 || first > OVERVIEW_BUCKETS || count > OVERVIEW_BUCKETS - first {
        return;
    }
    // SAFETY: the caller gives two floats per bucket.
    let peaks = unsafe { core::slice::from_raw_parts(peaks, count as usize * 2) };
    for (i, pair) in peaks.chunks_exact(2).enumerate() {
        tape.set_overview(first + i as i64, pair[0], pair[1]);
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn looptexturer_tape_release(tape: *const c_void) {
    if !tape.is_null() {
        // SAFETY: retained by one of the functions here, once per release.
        unsafe { drop(Arc::from_raw(tape.cast::<Tape>())) };
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn looptexturer_tape_capacity(tape: *const c_void) -> i64 {
    // SAFETY: as the module says.
    unsafe { self::tape(tape) }.map_or(0, Tape::capacity)
}

/// How many frames each overview bucket covers.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn looptexturer_tape_bucket_frames(tape: *const c_void) -> i64 {
    // SAFETY: as the module says.
    unsafe { self::tape(tape) }.map_or(1, Tape::bucket_frames)
}

/// The lowest and highest sample in a bucket, for drawing the waveform.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn looptexturer_tape_overview(tape: *const c_void, bucket: i64, low: *mut f32, high: *mut f32) {
    // SAFETY: as the module says, and the caller gives two floats to fill.
    if let Some(tape) = unsafe { self::tape(tape) } {
        if (0..OVERVIEW_BUCKETS).contains(&bucket) && !low.is_null() && !high.is_null() {
            unsafe {
                low.write(tape.overview_min(bucket));
                high.write(tape.overview_max(bucket));
            }
        }
    }
}

/// How many frames the loop as heard takes, or 0 when there's nothing to render.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn looptexturer_render_frames(bridge: *const c_void, settings: *const ExportSettings) -> i64 {
    // SAFETY: as the module says, and the caller gives filled settings.
    match (unsafe { self::bridge(bridge) }, unsafe { settings.as_ref() }) {
        (Some(bridge), Some(settings)) => {
            crate::LoopTexturer::render_loop(bridge, settings).map_or(0, |frames| frames.len() as i64 / 2)
        }
        _ => 0,
    }
}

/// Renders the loop as heard into `out`, which holds `frames` interleaved stereo frames as
/// `looptexturer_render_frames` gave, and returns whether it did. On the UI thread: it reads the whole loop.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn looptexturer_render(
    bridge: *const c_void,
    settings: *const ExportSettings,
    out: *mut f32,
    frames: i64,
) -> bool {
    // SAFETY: as the module says, and the caller gives filled settings and a buffer of frames stereo frames.
    let (Some(bridge), Some(settings)) = (unsafe { self::bridge(bridge) }, unsafe { settings.as_ref() }) else {
        return false;
    };
    if out.is_null() || frames <= 0 {
        return false;
    }
    let Some(rendered) = crate::LoopTexturer::render_loop(bridge, settings) else {
        return false;
    };
    if rendered.len() != frames as usize * 2 {
        return false;
    }
    unsafe { core::slice::from_raw_parts_mut(out, rendered.len()) }.copy_from_slice(&rendered);
    true
}
