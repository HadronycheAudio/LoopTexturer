//! What the processor shares with the panel and the worker thread: the looper's status, its tapes, and the mailbox
//! that the looper is reached through from other threads.
//!
//! The plug-in is not distributable, so the processor and the UI run in one process and share this object. VST3
//! messages only carry its ID, which the receiver looks up in the registry here. That way, neither side can end up
//! holding a dangling pointer, whatever order the host destroys them in. The LV2's panel, which may be in another
//! process, builds a mirror of its own from copies, as src/lv2/LooperLink.cpp describes.

use core::ptr;
use core::sync::atomic::{AtomicBool, AtomicI32, AtomicI64, AtomicPtr, AtomicU64, Ordering::SeqCst};
use std::collections::BTreeMap;
use std::sync::{Arc, Mutex, Weak};

use crate::tape::Tape;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Mode {
    Free,
    Sync,
}

/// A loop restored from a saved project. No tape means there's no loop.
#[derive(Clone, Copy, Debug)]
pub struct LoadedLoop {
    pub mode: Mode,
    /// Kept alive by the bridge's tapes, like every tape the looper holds a pointer to.
    pub tape: *const Tape,
    pub frames: i64,
    pub quarters: f64,
    pub anchor_quarters: f64,
    pub frames_per_quarter: f64,
    pub write_speed: f64,
}

// SAFETY: the pointer is only read, and only to a tape the bridge keeps.
unsafe impl Send for LoadedLoop {}
unsafe impl Sync for LoadedLoop {}

/// The looper's status, written by the audio thread once per block and read by the panel and when saving, as the
/// C++ side gets it in one piece.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct StatusCopy {
    pub is_sync_mode: bool,
    pub has_content: bool,
    pub is_recording: bool,
    pub is_overdubbing: bool,
    /// Rec is waiting for the next bar line.
    pub is_armed: bool,
    /// Recording ends at the next bar line.
    pub is_closing: bool,
    pub is_play_armed: bool,
    pub is_playing: bool,
    pub is_transport_running: bool,
    /// A recording was cancelled by a locate.
    pub transport_jumped: bool,
    /// Whether there's an active tape to draw.
    pub has_tape: bool,
    /// In cells.
    pub playhead: f64,
    /// How many cells the waveform shows.
    pub display_frames: i64,
    pub loop_frames: i64,
    pub loop_quarters: f64,
    pub anchor_quarters: f64,
    pub frames_per_quarter: f64,
    pub write_speed: f64,
    pub tempo: f64,
    pub sample_rate: f64,
}

/// A float held in an atomic, as its bits.
#[derive(Debug)]
pub struct AtomicF64(AtomicU64);

impl AtomicF64 {
    pub const fn new(value: f64) -> Self {
        Self(AtomicU64::new(value.to_bits()))
    }

    pub fn load(&self) -> f64 {
        f64::from_bits(self.0.load(SeqCst))
    }

    pub fn store(&self, value: f64) {
        self.0.store(value.to_bits(), SeqCst);
    }
}

#[derive(Debug)]
pub struct Status {
    pub is_sync_mode: AtomicBool,
    pub has_content: AtomicBool,
    pub is_recording: AtomicBool,
    pub is_overdubbing: AtomicBool,
    pub is_armed: AtomicBool,
    pub is_closing: AtomicBool,
    pub is_play_armed: AtomicBool,
    pub is_playing: AtomicBool,
    pub is_transport_running: AtomicBool,
    pub transport_jumped: AtomicBool,
    pub playhead: AtomicF64,
    pub display_frames: AtomicI64,
    pub loop_frames: AtomicI64,
    pub loop_quarters: AtomicF64,
    pub anchor_quarters: AtomicF64,
    pub frames_per_quarter: AtomicF64,
    pub write_speed: AtomicF64,
    pub tempo: AtomicF64,
    pub sample_rate: AtomicF64,
    pub active_tape: AtomicPtr<Tape>,
}

impl Default for Status {
    fn default() -> Self {
        Self {
            is_sync_mode: AtomicBool::new(false),
            has_content: AtomicBool::new(false),
            is_recording: AtomicBool::new(false),
            is_overdubbing: AtomicBool::new(false),
            is_armed: AtomicBool::new(false),
            is_closing: AtomicBool::new(false),
            is_play_armed: AtomicBool::new(false),
            is_playing: AtomicBool::new(false),
            is_transport_running: AtomicBool::new(false),
            transport_jumped: AtomicBool::new(false),
            playhead: AtomicF64::new(0.0),
            display_frames: AtomicI64::new(0),
            loop_frames: AtomicI64::new(0),
            loop_quarters: AtomicF64::new(0.0),
            anchor_quarters: AtomicF64::new(0.0),
            frames_per_quarter: AtomicF64::new(0.0),
            write_speed: AtomicF64::new(1.0),
            tempo: AtomicF64::new(0.0),
            sample_rate: AtomicF64::new(0.0),
            active_tape: AtomicPtr::new(ptr::null_mut()),
        }
    }
}

impl Status {
    pub fn copy(&self) -> StatusCopy {
        StatusCopy {
            is_sync_mode: self.is_sync_mode.load(SeqCst),
            has_content: self.has_content.load(SeqCst),
            is_recording: self.is_recording.load(SeqCst),
            is_overdubbing: self.is_overdubbing.load(SeqCst),
            is_armed: self.is_armed.load(SeqCst),
            is_closing: self.is_closing.load(SeqCst),
            is_play_armed: self.is_play_armed.load(SeqCst),
            is_playing: self.is_playing.load(SeqCst),
            is_transport_running: self.is_transport_running.load(SeqCst),
            transport_jumped: self.transport_jumped.load(SeqCst),
            has_tape: !self.active_tape.load(SeqCst).is_null(),
            playhead: self.playhead.load(),
            display_frames: self.display_frames.load(SeqCst),
            loop_frames: self.loop_frames.load(SeqCst),
            loop_quarters: self.loop_quarters.load(),
            anchor_quarters: self.anchor_quarters.load(),
            frames_per_quarter: self.frames_per_quarter.load(),
            write_speed: self.write_speed.load(),
            tempo: self.tempo.load(),
            sample_rate: self.sample_rate.load(),
        }
    }

    /// Everything but the active tape, for a mirror.
    pub fn set(&self, copy: &StatusCopy) {
        self.is_sync_mode.store(copy.is_sync_mode, SeqCst);
        self.has_content.store(copy.has_content, SeqCst);
        self.is_recording.store(copy.is_recording, SeqCst);
        self.is_overdubbing.store(copy.is_overdubbing, SeqCst);
        self.is_armed.store(copy.is_armed, SeqCst);
        self.is_closing.store(copy.is_closing, SeqCst);
        self.is_play_armed.store(copy.is_play_armed, SeqCst);
        self.is_playing.store(copy.is_playing, SeqCst);
        self.is_transport_running.store(copy.is_transport_running, SeqCst);
        self.transport_jumped.store(copy.transport_jumped, SeqCst);
        self.playhead.store(copy.playhead);
        self.display_frames.store(copy.display_frames, SeqCst);
        self.loop_frames.store(copy.loop_frames, SeqCst);
        self.loop_quarters.store(copy.loop_quarters);
        self.anchor_quarters.store(copy.anchor_quarters);
        self.frames_per_quarter.store(copy.frames_per_quarter);
        self.write_speed.store(copy.write_speed);
        self.tempo.store(copy.tempo);
        self.sample_rate.store(copy.sample_rate);
    }
}

/// How the looper is reached from other threads: the buttons, the worker's tapes, and a restored loop. The tape
/// pointers are plain, kept alive by the bridge's tapes, and atomic only so that every thread sees which are in use.
#[derive(Debug)]
pub struct Mailbox {
    /// A bitmask of `Command`s, taken at the start of the next block.
    pub pending_commands: AtomicI32,
    /// Offers from the worker. An offer must not replace one that's still there, because the audio thread may be
    /// taking it.
    pub offered_sync_tape: AtomicPtr<Tape>,
    pub offered_free_tape: AtomicPtr<Tape>,
    pub incoming_load: AtomicPtr<LoadedLoop>,
    /// What the looper plays from, written by the audio thread.
    pub free_tape: AtomicPtr<Tape>,
    pub sync_tape: AtomicPtr<Tape>,
}

impl Default for Mailbox {
    fn default() -> Self {
        Self {
            pending_commands: AtomicI32::new(0),
            offered_sync_tape: AtomicPtr::new(ptr::null_mut()),
            offered_free_tape: AtomicPtr::new(ptr::null_mut()),
            incoming_load: AtomicPtr::new(ptr::null_mut()),
            free_tape: AtomicPtr::new(ptr::null_mut()),
            sync_tape: AtomicPtr::new(ptr::null_mut()),
        }
    }
}

impl Mailbox {
    /// The tapes that must stay allocated. Offers have to be read first: the looper puts a tape in its place before
    /// withdrawing its offer, so in this order, a tape moving between the two is always seen in at least one.
    pub fn tapes_in_use(&self) -> [*const Tape; 5] {
        let offered_free = self.offered_free_tape.load(SeqCst);
        let offered_sync = self.offered_sync_tape.load(SeqCst);
        let load = self.incoming_load.load(SeqCst);
        // SAFETY: a load stays where the bridge put it, for as long as the bridge lives.
        let loaded = if load.is_null() {
            ptr::null()
        } else {
            unsafe { (*load).tape }
        };
        [
            offered_free,
            offered_sync,
            self.free_tape.load(SeqCst),
            self.sync_tape.load(SeqCst),
            loaded,
        ]
    }

    pub fn has_pending_load(&self) -> bool {
        !self.incoming_load.load(SeqCst).is_null()
    }
}

pub struct Bridge {
    id: i64,
    pub status: Status,
    pub mail: Mailbox,
    /// Set by the audio thread when it needs a larger tape. The worker thread allocates it.
    pub requested_sync_frames: AtomicI64,
    pub requested_free_frames: AtomicI64,
    /// Held while tapes are being added, so the worker doesn't release one before it's in use.
    pub tape_change: Mutex<()>,
    /// Keeps tapes alive. The audio thread only ever holds plain pointers to tapes kept here.
    tapes: Mutex<Vec<Arc<Tape>>>,
    /// Restored loops, each boxed so that it stays where it is once the audio thread has been pointed at it.
    #[allow(clippy::vec_box)]
    loads: Mutex<Vec<Box<LoadedLoop>>>,
}

static REGISTRY: Mutex<BTreeMap<i64, Weak<Bridge>>> = Mutex::new(BTreeMap::new());
static NEXT_ID: AtomicI64 = AtomicI64::new(1);

fn registry() -> std::sync::MutexGuard<'static, BTreeMap<i64, Weak<Bridge>>> {
    REGISTRY.lock().unwrap_or_else(|poisoned| poisoned.into_inner())
}

impl Bridge {
    pub fn create() -> Arc<Bridge> {
        let bridge = Arc::new(Bridge {
            id: NEXT_ID.fetch_add(1, SeqCst),
            status: Status::default(),
            mail: Mailbox::default(),
            requested_sync_frames: AtomicI64::new(0),
            requested_free_frames: AtomicI64::new(0),
            tape_change: Mutex::new(()),
            tapes: Mutex::new(Vec::new()),
            loads: Mutex::new(Vec::new()),
        });

        let mut registry = registry();
        // Instances come and go over a session, so expired entries are removed here.
        registry.retain(|_, weak| weak.strong_count() > 0);
        registry.insert(bridge.id, Arc::downgrade(&bridge));
        bridge
    }

    pub fn find(id: i64) -> Option<Arc<Bridge>> {
        registry().get(&id).and_then(Weak::upgrade)
    }

    pub fn id(&self) -> i64 {
        self.id
    }

    pub fn send_command(&self, command: i32) {
        self.mail.pending_commands.fetch_or(command, SeqCst);
    }

    fn tapes(&self) -> std::sync::MutexGuard<'_, Vec<Arc<Tape>>> {
        self.tapes.lock().unwrap_or_else(|poisoned| poisoned.into_inner())
    }

    /// Keeps a tape alive, and gives the pointer the looper will hold.
    pub fn add_tape(&self, tape: Arc<Tape>) -> *const Tape {
        let pointer = Arc::as_ptr(&tape);
        self.tapes().push(tape);
        pointer
    }

    /// Releases every tape that isn't in the list given.
    pub fn release_tapes_except(&self, in_use: &[*const Tape]) {
        let released: Vec<Arc<Tape>> = {
            let mut tapes = self.tapes();
            let (kept, released) = tapes
                .drain(..)
                .partition::<Vec<_>, _>(|tape| in_use.contains(&Arc::as_ptr(tape)));
            *tapes = kept;
            released
        };
        // The memory is freed here, outside the lock, because large tapes take a moment to free.
        drop(released);
    }

    pub fn active_tape(&self) -> Option<Arc<Tape>> {
        let active = self.status.active_tape.load(SeqCst).cast_const();
        self.tapes().iter().find(|tape| Arc::as_ptr(tape) == active).cloned()
    }

    /// Hands a restored loop to the audio thread, which picks it up at its next block.
    pub fn load_loop(&self, load: LoadedLoop) {
        let mut loads = self.loads.lock().unwrap_or_else(|poisoned| poisoned.into_inner());
        loads.push(Box::new(load));
        let pointer: *const LoadedLoop = &**loads.last().expect("just pushed");
        self.mail.incoming_load.store(pointer.cast_mut(), SeqCst);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_bridge_is_found_by_its_id_while_it_lives() {
        let bridge = Bridge::create();
        let id = bridge.id();
        assert!(Arc::ptr_eq(&Bridge::find(id).unwrap(), &bridge));
        drop(bridge);
        assert!(Bridge::find(id).is_none());
        assert!(Bridge::find(-1).is_none());
    }

    #[test]
    fn tapes_live_until_released_and_the_active_one_is_found() {
        let bridge = Bridge::create();
        let kept = bridge.add_tape(Arc::new(Tape::new(16)));
        let gone = bridge.add_tape(Arc::new(Tape::new(16)));
        bridge.status.active_tape.store(kept.cast_mut(), SeqCst);
        assert!(bridge.active_tape().is_some());
        bridge.release_tapes_except(&[kept]);
        assert_eq!(bridge.tapes().len(), 1);
        bridge.status.active_tape.store(gone.cast_mut(), SeqCst);
        assert!(bridge.active_tape().is_none(), "a released tape is no longer offered");
    }

    #[test]
    fn a_status_goes_round_as_a_copy() {
        let status = Status::default();
        let copy = StatusCopy {
            is_playing: true,
            playhead: 12.5,
            loop_frames: 7,
            ..StatusCopy::default()
        };
        status.set(&copy);
        assert_eq!(status.copy(), copy);
    }
}
