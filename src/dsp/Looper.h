#pragma once

#include "../LooperBridge.h"
#include "Smoother.h"
#include "Tape.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace LoopTexturer {

// The looper, modelled as a tape read and written at a variable speed by a single head. The two
// modes only differ in what moves the head:
//
// - Free running: the head never stops, and the length knob sets its speed.
// - Tempo sync: the head's position comes from the host's musical position, so a tempo change
//   repitches the loop and the loop always stays on the grid.
//
// Reading and writing at the same position means overdubs line up with what's playing. Feedback
// works directly on the stored cells instead of on an interpolated copy, so a loop at 100% feedback
// stays bit-exact however many layers are added on top.
class Looper
{
public:
    enum Command
    {
        kCommandRec = 1 << 0,
        kCommandOverdub = 1 << 1,
        kCommandPlay = 1 << 2,
        kCommandStop = 1 << 3,
        kCommandClear = 1 << 4
    };

    enum class Mode
    {
        Free,
        Sync
    };

    struct Settings
    {
        Mode mode;
        double freeLengthSeconds;
        double targetBars; // 0 means open-ended: recording runs until Rec is pressed again.
        double feedback;   // 1 leaves the loop unchanged.
    };

    struct Transport
    {
        bool isValid;
        bool isPlaying;
        double quarters;
        double tempo;
        double barStartQuarters;
        int numerator;
        int denominator;
    };

    // A loop restored from a saved project. A null tape means there's no loop.
    struct LoadedLoop
    {
        Mode mode;
        Tape* tape;
        int64_t frames;
        double quarters;
        double anchorQuarters;
        double framesPerQuarter;
        double writeSpeed;
    };

    static constexpr int64_t kMaxBars = 64;
    static constexpr double kFreeTapeSeconds = 10.0;

    // inputLatency is how many samples behind the host's position the input arrives at the looper.
    Looper (LooperBridge& bridge, int inputLatency);

    // Not on the audio thread, and not while processing. A loop that's already there is kept.
    void prepare (double sampleRate);

    // Any thread.
    void sendCommand (int command) { _pendingCommands.fetch_or (command); }
    void offerSyncTape (Tape* tape) { _offeredSyncTape.store (tape); }
    void loadLoop (const LoadedLoop* loop) { _incomingLoad.store (loop); }

    // The tapes that must stay allocated.
    std::array<const Tape*, 4> getTapesInUse () const;
    bool hasPendingLoad () const { return _incomingLoad.load () != nullptr; }

    // Audio thread.
    void beginBlock (const Settings& settings, const Transport& transport);
    void processSample (float& left, float& right);
    void endBlock ();

private:
    enum class SyncState
    {
        Empty,
        Recording, // The first pass of a new loop, whose length may not be known yet.
        Loop
    };

    void adoptIncoming ();
    void handleCommands (const Settings& settings);
    void clearLoop ();
    void updateSyncBlock (const Transport& transport);

    void processFree (float inLeft, float inRight, float& left, float& right);
    void processSync (float inLeft, float inRight, float& left, float& right);
    void startRecording ();
    void closeRecording (double quarters);
    void cancelRecording ();

    void updateEnvelopes ();
    void writeSpan (Tape& tape, double from, double to, int64_t frames, bool isNewPass, float inLeft, float inRight);
    float applyCell (float old, float input) const;
    float updateDcBlocker (int channel, float input);
    void noteOverview (Tape& tape, int64_t index, float left, float right);
    void eraseSomeOfFreeTape ();

    LooperBridge& _bridge;
    const int _inputLatency;
    double _sampleRate { 44100.0 };
    float _envelopeStep { 0.01f };

    std::atomic<int> _pendingCommands { 0 };
    std::atomic<Tape*> _offeredSyncTape { nullptr };
    std::atomic<const LoadedLoop*> _incomingLoad { nullptr };

    // Plain pointers, kept alive by the bridge. Atomic only so the worker thread can see which are in use.
    std::atomic<Tape*> _freeTape { nullptr };
    std::atomic<Tape*> _syncTape { nullptr };

    Mode _mode { Mode::Free };

    // Shared by both modes.
    bool _isRecording { false };
    bool _isOverdubbing { false };
    float _recordEnvelope { 0.0f };
    float _overdubEnvelope { 0.0f };
    float _wetEnvelope { 0.0f };
    float _wetTarget { 0.0f };
    Smoother _feedback;
    float _currentFeedback { 1.0f };
    double _head { 0.0 };
    double _writeSpeed { 1.0 };
    float _previousLeft { 0.0f };
    float _previousRight { 0.0f };
    float _lastOutputLeft { 0.0f };
    float _lastOutputRight { 0.0f };
    float _dcCoefficient { 0.999f };
    std::array<float, 2> _dcInput {};
    std::array<float, 2> _dcOutput {};

    int64_t _overviewBucket { -1 };
    float _overviewMin { 0.0f };
    float _overviewMax { 0.0f };

    // Free running.
    Smoother _speed;
    bool _freeHasContent { false };
    int64_t _eraseCursor { -1 }; // Negative when the tape isn't being erased.

    // Tempo sync.
    SyncState _syncState { SyncState::Empty };
    bool _isArmed { false };
    bool _isClosing { false };
    bool _isPlayArmed { false };
    bool _isPlaying { false };
    bool _transportRunning { false };
    bool _transportJumped { false };
    double _quarters { 0.0 };
    double _quartersPerSample { 0.0 };
    double _tempo { 0.0 };
    double _barLength { 4.0 };
    double _barStart { 0.0 };
    double _nextBar { 0.0 };
    double _nextExpectedQuarters { 0.0 };
    bool _hasPosition { false };
    double _targetQuarters { 0.0 };
    double _targetBars { 1.0 };
    double _anchor { 0.0 };
    double _loopQuarters { 0.0 };
    double _framesPerQuarter { 0.0 };
    int64_t _loopFrames { 0 };
};

} // namespace LoopTexturer
