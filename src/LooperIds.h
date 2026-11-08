#pragma once

// Generated from rust/src/looper.rs and rust/src/lib.rs, by cargo test in rust/. Don't edit it here: change the
// Rust, and run HADRONYCHE_UPDATE_HEADERS=1 cargo test in rust/ to bring this up to date.

#include <cstdint>

namespace LoopTexturer {

// Buckets in a tape's overview, and the frames of the LV2 panel's mirror tape.
constexpr int64_t kOverviewBuckets = 32768;

// The looper's buttons, as bits in a looper command message.
enum LooperCommand
{
    kCommandRec = 1,
    kCommandOverdub = 2,
    kCommandPlay = 4,
    kCommandStop = 8,
    kCommandClear = 16,
};

// The entries of Looper bars, in order. Projects save a position in the list.
enum LooperBarChoice
{
    kBarsHalf,
    kBarsOne,
    kBarsTwo,
    kBarsFour,
    kBarsSix,
    kBarsEight,
    kBarsCustom,
    kBarsOpen,
};

} // namespace LoopTexturer
