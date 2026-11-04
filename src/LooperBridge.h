#pragma once

#include "dsp/Tape.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace LoopTexturer {

// What the processor shares with the UI: the looper's status and its tapes. The plug-in is not
// distributable, so both sides run in one process and can share this object directly.
//
// VST3 messages only carry its ID, which the receiver looks up. That way, neither side can end up
// holding a dangling pointer, whatever order the host destroys them in.
class LooperBridge
{
public:
    static std::shared_ptr<LooperBridge> create ();
    static std::shared_ptr<LooperBridge> find (int64_t id);

    int64_t getId () const { return _id; }

    // Written by the audio thread once per block, read by the UI and when saving.
    struct Status
    {
        std::atomic<bool> isSyncMode { false };
        std::atomic<bool> hasContent { false };
        std::atomic<bool> isRecording { false };
        std::atomic<bool> isOverdubbing { false };
        std::atomic<bool> isArmed { false };   // Rec is waiting for the next bar line.
        std::atomic<bool> isClosing { false }; // Recording ends at the next bar line.
        std::atomic<bool> isPlayArmed { false };
        std::atomic<bool> isPlaying { false };
        std::atomic<bool> isTransportRunning { false };
        std::atomic<bool> transportJumped { false }; // A recording was cancelled by a locate.
        std::atomic<double> playhead { 0.0 };        // In cells.
        std::atomic<int64_t> displayFrames { 0 };    // How many cells the waveform shows.
        std::atomic<int64_t> loopFrames { 0 };
        std::atomic<double> loopQuarters { 0.0 };
        std::atomic<double> anchorQuarters { 0.0 };
        std::atomic<double> framesPerQuarter { 0.0 };
        std::atomic<double> writeSpeed { 1.0 };
        std::atomic<double> tempo { 0.0 };
        std::atomic<double> sampleRate { 0.0 };
        std::atomic<const Tape*> activeTape { nullptr };
    };

    Status status;

    // Set by the audio thread when it needs a larger tape. The processor's worker thread allocates it.
    std::atomic<int64_t> requestedSyncFrames { 0 };

    // Keeps a tape alive. The audio thread only ever holds plain pointers to tapes kept here.
    void addTape (const std::shared_ptr<Tape>& tape);

    // Releases every tape that isn't in the list given.
    void releaseTapesExcept (const std::vector<const Tape*>& inUse);

    std::shared_ptr<Tape> getActiveTape () const;

private:
    LooperBridge () = default;

    int64_t _id { 0 };
    mutable std::mutex _tapesMutex;
    std::vector<std::shared_ptr<Tape>> _tapes;
};

} // namespace LoopTexturer
