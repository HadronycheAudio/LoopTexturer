#pragma once

#include "LooperApi.h"
#include "LooperIds.h"

#include <cstdint>
#include <memory>

namespace LoopTexturer {

// The looper's status, as the Rust side gives it in one piece.
using LooperStatus = Ffi::StatusCopy;

// A tape the looper plays from, as the panel reads it: its size, and the overview the waveform is drawn from. It's
// the Rust side's, kept alive for as long as this is.
class TapeView
{
public:
    explicit TapeView (const void* tape) : _tape (tape) {}
    ~TapeView () { Ffi::looptexturer_tape_release (_tape); }
    TapeView (const TapeView&) = delete;
    TapeView& operator= (const TapeView&) = delete;

    int64_t getCapacity () const { return Ffi::looptexturer_tape_capacity (_tape); }
    int64_t getBucketFrames () const { return Ffi::looptexturer_tape_bucket_frames (_tape); }

    float getOverviewMin (int64_t bucket) const { return overview (bucket).first; }
    float getOverviewMax (int64_t bucket) const { return overview (bucket).second; }

private:
    std::pair<float, float> overview (int64_t bucket) const
    {
        float low = 0.0f;
        float high = 0.0f;
        Ffi::looptexturer_tape_overview (_tape, bucket, &low, &high);
        return { low, high };
    }

    const void* _tape;
};

// What the processor shares with the UI: the looper's status and its tapes, which live on the Rust side. The plug-in
// is not distributable, so both sides run in one process and share it directly. VST3 messages only carry its ID,
// which the receiver looks up on the Rust side. That way, neither side can end up holding a dangling pointer,
// whatever order the host destroys them in.
class LooperBridge
{
public:
    // The processor's own, from RustProcessor::shared ().
    static std::shared_ptr<LooperBridge> retain (const void* shared)
    {
        const void* bridge = Ffi::looptexturer_bridge_retain (shared);
        return bridge ? std::shared_ptr<LooperBridge> (new LooperBridge (bridge)) : nullptr;
    }

    // Null when there's no bridge with this ID any more.
    static std::shared_ptr<LooperBridge> find (int64_t id)
    {
        const void* bridge = Ffi::looptexturer_bridge_find (id);
        return bridge ? std::shared_ptr<LooperBridge> (new LooperBridge (bridge)) : nullptr;
    }

    // For the LV2's panel, which fills it from the audio side's copies. See src/lv2/LooperLink.cpp.
    static std::shared_ptr<LooperBridge> createMirror ()
    {
        return std::shared_ptr<LooperBridge> (new LooperBridge (Ffi::looptexturer_bridge_create_mirror ()));
    }

    ~LooperBridge () { Ffi::looptexturer_bridge_release (_bridge); }
    LooperBridge (const LooperBridge&) = delete;
    LooperBridge& operator= (const LooperBridge&) = delete;

    int64_t getId () const { return Ffi::looptexturer_bridge_id (_bridge); }

    LooperStatus getStatus () const
    {
        LooperStatus status {};
        Ffi::looptexturer_bridge_status (_bridge, &status);
        return status;
    }

    // A button, which the looper takes at the start of its next block.
    void sendCommand (int command) const { Ffi::looptexturer_bridge_send_command (_bridge, command); }

    // Null while the looper has no tape.
    std::unique_ptr<TapeView> getActiveTape () const
    {
        const void* tape = Ffi::looptexturer_bridge_active_tape (_bridge);
        return tape ? std::make_unique<TapeView> (tape) : nullptr;
    }

    // A mirror's status and overview, as copied from the audio side.
    void setStatus (const LooperStatus& status) const { Ffi::looptexturer_bridge_set_status (_bridge, &status); }
    void setOverview (int64_t first, const float* peaks, int64_t count) const
    {
        Ffi::looptexturer_bridge_set_overview (_bridge, first, peaks, count);
    }

    // The Rust side's, for its own functions.
    const void* raw () const { return _bridge; }

private:
    explicit LooperBridge (const void* bridge) : _bridge (bridge) {}

    const void* _bridge;
};

} // namespace LoopTexturer
