// The looper, as the C++ side reaches it besides HadronycheCore's frame: the bridge that the panel reads, for its
// status, waveform and export, and that the LV2's panel mirrors. Generated from rust/src/ffi.rs by cargo test in rust/,
// so don't edit it here: change the Rust, and run HADRONYCHE_UPDATE_HEADERS=1 cargo test there to bring this up to date.

#pragma once

#include <cstdint>

namespace LoopTexturer {
namespace Ffi {

/// The looper's status, written by the audio thread once per block and read by the panel and when saving, as the
/// C++ side gets it in one piece.
struct StatusCopy {
  bool isSyncMode;
  bool hasContent;
  bool isRecording;
  bool isOverdubbing;
  /// Rec is waiting for the next bar line.
  bool isArmed;
  /// Recording ends at the next bar line.
  bool isClosing;
  bool isPlayArmed;
  bool isPlaying;
  bool isTransportRunning;
  /// A recording was cancelled by a locate.
  bool transportJumped;
  /// Whether there's an active tape to draw.
  bool hasTape;
  /// In cells.
  double playhead;
  /// How many cells the waveform shows.
  int64_t displayFrames;
  int64_t loopFrames;
  double loopQuarters;
  double anchorQuarters;
  double framesPerQuarter;
  double writeSpeed;
  double tempo;
  double sampleRate;
};

/// What the panel's drag renders with. The mode is the saturator's list's index.
struct ExportSettings {
  bool postSaturatorOn;
  int32_t postSaturatorMode;
  double postSaturatorDriveDb;
  /// 0 to 1.
  double postSaturatorMix;
  double freeLengthSeconds;
};

extern "C" {

/// The bridge with this ID, retained, or null when it's gone. The ID comes from the processor's own message.
const void *looptexturer_bridge_find(int64_t id);

/// The processor's bridge, as `RustProcessor::shared ()` gives it, retained for the C++ side to keep.
const void *looptexturer_bridge_retain(const void *shared);

void looptexturer_bridge_release(const void *bridge);

/// A bridge for the LV2's panel, which fills it from the copies the audio side sends. Its tape has one frame per
/// overview bucket, so a bucket of the real tape is a bucket of this one. Retained.
const void *looptexturer_bridge_create_mirror();

int64_t looptexturer_bridge_id(const void *bridge);

/// Fills `status` with the looper's, as it stands.
void looptexturer_bridge_status(const void *bridge, StatusCopy *status);

/// A button, as a bit or several, which the looper takes at the start of its next block.
void looptexturer_bridge_send_command(const void *bridge, int32_t command);

/// The tape the looper plays from, retained, or null while there isn't one.
const void *looptexturer_bridge_active_tape(const void *bridge);

/// For a mirror: the status as copied from the audio side. The mirror's tape stands in for the real one when the copy
/// says there is one.
void looptexturer_bridge_set_status(const void *bridge,
                                    const StatusCopy *status);

/// For a mirror: `count` buckets of the overview from `first`, as a low and a high float each.
void looptexturer_bridge_set_overview(const void *bridge,
                                      int64_t first,
                                      const float *peaks,
                                      int64_t count);

void looptexturer_tape_release(const void *tape);

int64_t looptexturer_tape_capacity(const void *tape);

/// How many frames each overview bucket covers.
int64_t looptexturer_tape_bucket_frames(const void *tape);

/// The lowest and highest sample in a bucket, for drawing the waveform.
void looptexturer_tape_overview(const void *tape, int64_t bucket, float *low, float *high);

/// How many frames the loop as heard takes, or 0 when there's nothing to render.
int64_t looptexturer_render_frames(const void *bridge, const ExportSettings *settings);

/// Renders the loop as heard into `out`, which holds `frames` interleaved stereo frames as
/// `looptexturer_render_frames` gave, and returns whether it did. On the UI thread: it reads the whole loop.
bool looptexturer_render(const void *bridge,
                         const ExportSettings *settings,
                         float *out,
                         int64_t frames);

}  // extern "C"

}  // namespace Ffi
}  // namespace LoopTexturer
