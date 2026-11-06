#include "Looper.h"

#include <algorithm>
#include <cmath>

namespace LoopTexturer {

namespace {

constexpr double kEnvelopeSeconds = 0.005; // Removes clicks when punching in and out.
constexpr double kSpeedSmoothingMs = 50.0;
constexpr double kFeedbackSmoothingMs = 20.0;
constexpr float kUnityFeedbackMargin = 1e-4f;
constexpr double kSlowestPlannedTempo = 20.0;
constexpr double kTapeHeadroom = 1.25;
constexpr int64_t kEraseFramesPerBlock = 1 << 16;
constexpr double kJumpToleranceSamples = 64.0;
constexpr double kDcCornerHz = 5.0; // At normal speed. The filter runs per cell, so its corner rises with the speed.
constexpr double kDcReferenceSeconds = 10.0; // The free running tape length that uses kDcCornerHz as it is.

// Unchanged up to full scale, then bends towards a ceiling of 1.5. Above 100% feedback, this curve
// is what gives the loop its distortion, and it keeps the level from running away.
float softLimit (float x)
{
    const float magnitude = std::abs (x);
    if (magnitude <= 1.0f)
        return x;

    const float limited = 1.0f + 0.5f * std::tanh ((magnitude - 1.0f) / 0.5f);
    return x < 0.0f ? -limited : limited;
}

float moveTowards (float value, float target, float step)
{
    return value < target ? std::min (value + step, target) : std::max (value - step, target);
}

} // namespace

Looper::Looper (LooperBridge& bridge, int inputLatency) : _bridge (bridge), _inputLatency (inputLatency)
{
}

void Looper::prepare (double sampleRate)
{
    _sampleRate = sampleRate;
    _envelopeStep = static_cast<float> (1.0 / (kEnvelopeSeconds * sampleRate));

    _feedback.prepare (sampleRate, kFeedbackSmoothingMs);
    _speed.prepare (sampleRate, kSpeedSmoothingMs);
    _feedback.snapTo (_currentFeedback);

    const auto freeFrames = static_cast<int64_t> (kFreeTapeSeconds * sampleRate);
    auto* freeTape = _freeTape.load ();
    if (!freeTape || (!_freeHasContent && freeTape->getCapacity () != freeFrames))
    {
        auto tape = std::make_shared<Tape> (freeFrames);
        _bridge.addTape (tape);
        _freeTape.store (tape.get ());
        _head = 0.0;
    }

    _bridge.status.sampleRate.store (sampleRate);
    _hasPosition = false;
}

std::array<const Tape*, 5> Looper::getTapesInUse () const
{
    // Offers have to be read first. adoptIncoming puts a tape in its place before withdrawing its
    // offer, so in this order, a tape moving between the two is always seen in at least one of them.
    const Tape* offeredFree = _offeredFreeTape.load ();
    const Tape* offeredSync = _offeredSyncTape.load ();
    const auto* load = _incomingLoad.load ();
    return { offeredFree, offeredSync, _freeTape.load (), _syncTape.load (), load ? load->tape : nullptr };
}

// A loop restored from an older project or another sample rate keeps its own tape, so that it plays
// the same. Once it's cleared, the next recording needs a full tape: on a short one, a long loop
// would run the head slower than normal speed and skip samples of its input.
bool Looper::isWaitingForFreeTape () const
{
    const auto* tape = _freeTape.load ();
    return tape && !_freeHasContent && tape->getCapacity () != static_cast<int64_t> (kFreeTapeSeconds * _sampleRate);
}

// Above 100% feedback, this filter stops any offset from building up in the loop. In free running
// mode, its corner keeps the same place in the audio whatever the tape's size.
void Looper::updateDcCoefficient ()
{
    double cornerHz = kDcCornerHz;
    if (_mode == Mode::Free)
    {
        if (const auto* tape = _freeTape.load ())
            cornerHz *= kDcReferenceSeconds * _sampleRate / static_cast<double> (tape->getCapacity ());
    }
    _dcCoefficient = static_cast<float> (std::exp (-6.283185307179586 * cornerHz / _sampleRate));
}

void Looper::adoptIncoming ()
{
    if (auto* load = _incomingLoad.exchange (nullptr))
    {
        _mode = load->mode;
        _isRecording = false;
        _isOverdubbing = false;
        _recordEnvelope = 0.0f;
        _overdubEnvelope = 0.0f;
        _isArmed = false;
        _isClosing = false;
        _isPlayArmed = false;
        _eraseCursor = -1;
        _overviewBucket = -1;
        _freePlaying = true;

        if (load->mode == Mode::Free)
        {
            if (load->tape)
            {
                _freeTape.store (load->tape);
                _head = 0.0;
                _writeSpeed = load->writeSpeed;
                _freeHasContent = true;
            }
            else
            {
                _freeHasContent = false;
                _eraseCursor = 0;
            }
            _syncState = SyncState::Empty;
            _loopQuarters = 0.0;
            _loopFrames = 0;
        }
        else
        {
            _freeHasContent = false;
            _eraseCursor = 0;
            if (load->tape)
            {
                _syncTape.store (load->tape);
                _syncState = SyncState::Loop;
                _isPlaying = true;
                _loopFrames = load->frames;
                _loopQuarters = load->quarters;
                _anchor = load->anchorQuarters;
                _framesPerQuarter = load->framesPerQuarter;
                _writeSpeed = load->writeSpeed;
            }
            else
            {
                _syncState = SyncState::Empty;
                _isPlaying = false;
                _loopFrames = 0;
                _loopQuarters = 0.0;
            }
        }
    }

    if (!_freeHasContent)
    {
        if (auto* offered = _offeredFreeTape.load ())
        {
            _freeTape.store (offered);
            _offeredFreeTape.store (nullptr);
            _head = 0.0;
            _eraseCursor = -1; // A new tape is already blank.
            _overviewBucket = -1;
            _freeTapeWasSwapped = true;
        }
    }

    // A larger tape is only swapped in when no loop is using the current one.
    if (_syncState == SyncState::Empty)
    {
        if (auto* offered = _offeredSyncTape.load ())
        {
            auto* current = _syncTape.load ();
            if (!current || offered->getCapacity () > current->getCapacity ())
                _syncTape.store (offered);
            _offeredSyncTape.store (nullptr);
        }
    }
}

void Looper::clearLoop ()
{
    _isRecording = false;
    _isOverdubbing = false;
    _recordEnvelope = 0.0f;
    _overdubEnvelope = 0.0f;
    _overviewBucket = -1;

    _freeHasContent = false;
    _freePlaying = true;
    _eraseCursor = 0;

    _syncState = SyncState::Empty;
    _isArmed = false;
    _isClosing = false;
    _isPlayArmed = false;
    _isPlaying = false;
    _loopFrames = 0;
    _loopQuarters = 0.0;
    _writeSpeed = 1.0;

    if (auto* tape = _syncTape.load ())
        tape->clearOverview ();
    if (auto* tape = _freeTape.load ())
        tape->clearOverview ();
}

void Looper::startFreeTape ()
{
    // A stopped tape starts again from the top of the loop.
    if (!_freePlaying)
    {
        _freePlaying = true;
        _head = 0.0;
        _wetEnvelope = 0.0f;
    }
}

void Looper::handleCommands (const Settings& settings)
{
    const int commands = _pendingCommands.exchange (0);
    if (commands == 0)
        return;

    if (commands & kCommandClear)
    {
        clearLoop ();
        return;
    }

    if (_mode == Mode::Free)
    {
        if (_eraseCursor >= 0 || isWaitingForFreeTape ())
            return;

        if (commands & kCommandStop)
        {
            _freePlaying = false;
            _isRecording = false;
            _isOverdubbing = false;
        }
        if (commands & kCommandPlay)
            startFreeTape ();

        if (commands & kCommandRec)
        {
            _isRecording = !_isRecording;
            if (_isRecording)
            {
                startFreeTape ();
                _isOverdubbing = false;
                _recordedCells = 0.0;

                // On an empty tape, the loop starts where the recording starts.
                if (!_freeHasContent)
                    _head = 0.0;
            }
        }
        if (commands & kCommandOverdub)
        {
            _isOverdubbing = !_isOverdubbing;
            if (_isOverdubbing)
            {
                startFreeTape ();
                _isRecording = false;
            }
        }
        return;
    }

    if (commands & kCommandStop)
    {
        _isPlaying = false;
        _isPlayArmed = false;
        _isOverdubbing = false;
        _isArmed = false;
        if (_syncState == SyncState::Recording)
            cancelRecording ();
    }

    if (commands & kCommandPlay)
    {
        if (_syncState == SyncState::Loop && !_isPlaying)
            _isPlayArmed = true;
    }

    if (commands & kCommandRec)
    {
        if (_syncState == SyncState::Recording)
            _isClosing = true;
        else if (_isArmed)
            _isArmed = false;
        else
        {
            _isArmed = true;
            _targetBars = settings.targetBars;
        }
    }

    if (commands & kCommandOverdub)
    {
        if (_syncState == SyncState::Loop && _isPlaying)
            _isOverdubbing = !_isOverdubbing;
    }
}

void Looper::updateSyncBlock (const Transport& transport)
{
    _transportRunning = transport.isValid && transport.isPlaying && transport.tempo > 0.0;
    if (!transport.isValid || transport.tempo <= 0.0)
        return;

    _tempo = transport.tempo;
    _quartersPerSample = transport.tempo / (60.0 * _sampleRate);
    _barLength = transport.numerator * 4.0 / transport.denominator;
    _barStart = transport.barStartQuarters;

    // The input arrives inputLatency samples late, and the head has to line up with it.
    const double quarters = transport.quarters - _inputLatency * _quartersPerSample;

    // A locate, a cycle jump or a restart.
    const bool jumped = _hasPosition && std::abs (quarters - _nextExpectedQuarters) > kJumpToleranceSamples * _quartersPerSample;
    if (jumped)
    {
        if (_syncState == SyncState::Recording)
        {
            // There's no sensible way to continue a loop whose length isn't known yet.
            cancelRecording ();
            _transportJumped = true;
        }
        _wetEnvelope = 0.0f;
    }

    _quarters = quarters;

    // The next bar line at or after this position.
    const double barsAhead = std::ceil ((quarters - _barStart) / _barLength - 1e-9);
    _nextBar = _barStart + barsAhead * _barLength;

    // Plan for 64 bars at this tempo, within reason. Recording stops early if the tape runs out.
    const double plannedTempo = std::max (transport.tempo, kSlowestPlannedTempo);
    const double needed = static_cast<double> (kMaxBars) * _barLength * 60.0 / plannedTempo * _sampleRate;
    const auto* tape = _syncTape.load ();
    if (_syncState == SyncState::Empty && (!tape || static_cast<double> (tape->getCapacity ()) < needed)
        && !_offeredSyncTape.load ())
    {
        _bridge.requestedSyncFrames.store (static_cast<int64_t> (needed * kTapeHeadroom));
    }
}

void Looper::beginBlock (const Settings& settings, const Transport& transport)
{
    adoptIncoming ();

    if (settings.mode != _mode)
    {
        clearLoop ();
        _mode = settings.mode;
    }

    if (_mode == Mode::Sync)
        updateSyncBlock (transport);
    else
        _transportRunning = transport.isValid && transport.isPlaying;

    handleCommands (settings);

    _feedback.setTarget (static_cast<float> (settings.feedback));
    updateDcCoefficient ();

    if (_mode == Mode::Free)
    {
        if (isWaitingForFreeTape () && !_offeredFreeTape.load ())
            _bridge.requestedFreeFrames.store (static_cast<int64_t> (kFreeTapeSeconds * _sampleRate));

        if (auto* tape = _freeTape.load ())
        {
            const double frames = static_cast<double> (tape->getCapacity ());
            const double speed = frames / (settings.freeLengthSeconds * _sampleRate);
            _speed.setTarget (speed);

            // A blank tape has nothing to repitch, and a recording that started during a glide
            // would begin below normal speed.
            if (_freeTapeWasSwapped)
                _speed.snapTo (speed);
            _freeTapeWasSwapped = false;

            if (_eraseCursor >= 0)
                eraseSomeOfFreeTape ();
        }
    }
}

void Looper::eraseSomeOfFreeTape ()
{
    auto* tape = _freeTape.load ();
    const int64_t end = std::min (_eraseCursor + kEraseFramesPerBlock, tape->getCapacity ());
    for (int64_t i = _eraseCursor; i < end; ++i)
    {
        tape->store (0, i, 0.0f);
        tape->store (1, i, 0.0f);
    }
    _eraseCursor = end == tape->getCapacity () ? -1 : end;
}

void Looper::updateEnvelopes ()
{
    _recordEnvelope = moveTowards (_recordEnvelope, _isRecording ? 1.0f : 0.0f, _envelopeStep);
    _overdubEnvelope = moveTowards (_overdubEnvelope, _isOverdubbing ? 1.0f : 0.0f, _envelopeStep);
    _wetEnvelope = moveTowards (_wetEnvelope, _wetTarget, _envelopeStep);

    _currentFeedback = _feedback.nextFloat ();
    if (std::abs (_currentFeedback - 1.0f) < kUnityFeedbackMargin)
        _currentFeedback = 1.0f;
}

float Looper::updateDcBlocker (int channel, float input)
{
    const auto c = static_cast<size_t> (channel);
    const float output = input - _dcInput[c] + _dcCoefficient * _dcOutput[c];
    _dcInput[c] = input;
    _dcOutput[c] = output;
    return output;
}

float Looper::applyCell (float old, float input) const
{
    float value = old;
    if (_overdubEnvelope > 0.0f)
        value += input * _overdubEnvelope;
    if (_recordEnvelope > 0.0f)
        value += (input - value) * _recordEnvelope;
    return softLimit (value);
}

void Looper::noteOverview (Tape& tape, int64_t index, float left, float right)
{
    const int64_t bucketFrames = tape.getBucketFrames ();
    const int64_t bucket = index / bucketFrames;
    if (bucket >= Tape::kOverviewBuckets)
        return;

    if (bucket != _overviewBucket)
    {
        _overviewBucket = bucket;
        // Starting mid-bucket means the rest of it wasn't rewritten, so its old peaks still count.
        const bool startsAtEdge = index % bucketFrames == 0;
        _overviewMin = startsAtEdge ? 0.0f : tape.getOverviewMin (bucket);
        _overviewMax = startsAtEdge ? 0.0f : tape.getOverviewMax (bucket);
    }

    _overviewMin = std::min ({ _overviewMin, left, right });
    _overviewMax = std::max ({ _overviewMax, left, right });
    tape.setOverview (bucket, _overviewMin, _overviewMax);
}

void Looper::writeSpan (Tape& tape, double from, double to, int64_t frames, bool isNewPass, float inLeft, float inRight)
{
    const bool writesInput = _recordEnvelope > 0.0f || _overdubEnvelope > 0.0f;
    const bool changesLevel = _currentFeedback != 1.0f;
    if (to <= from || (!writesInput && !changesLevel))
        return;

    // Fills every cell the head moved past, ramping from the previous input sample to this one.
    const double span = to - from;
    for (double position = std::floor (from) + 1.0; position <= to; position += 1.0)
    {
        if (position < 0.0)
            continue;

        const auto index = static_cast<int64_t> (position) % frames;
        const auto t = static_cast<float> ((position - from) / span);
        const float left = _previousLeft + (inLeft - _previousLeft) * t;
        const float right = _previousRight + (inRight - _previousRight) * t;

        float oldLeft = isNewPass ? 0.0f : tape.load (0, index);
        float oldRight = isNewPass ? 0.0f : tape.load (1, index);
        if (changesLevel)
        {
            oldLeft = softLimit (oldLeft * _currentFeedback);
            oldRight = softLimit (oldRight * _currentFeedback);
            if (_currentFeedback > 1.0f)
            {
                oldLeft = updateDcBlocker (0, oldLeft);
                oldRight = updateDcBlocker (1, oldRight);
            }
        }

        const float newLeft = applyCell (oldLeft, left);
        const float newRight = applyCell (oldRight, right);
        tape.store (0, index, newLeft);
        tape.store (1, index, newRight);
        noteOverview (tape, index, newLeft, newRight);
    }
}

void Looper::processFree (float inLeft, float inRight, float& left, float& right)
{
    auto* tape = _freeTape.load ();
    if (!tape)
        return;

    const int64_t frames = tape->getCapacity ();
    const double speed = _speed.next ();

    if (!_freePlaying)
    {
        // The tape stands still, with the output fading out.
        _wetTarget = 0.0f;
        updateEnvelopes ();
        left = _lastOutputLeft * _wetEnvelope;
        right = _lastOutputRight * _wetEnvelope;
        _previousLeft = inLeft;
        _previousRight = inRight;
        return;
    }

    const double previous = _head;
    double unwrapped = previous + speed;
    _head = unwrapped >= static_cast<double> (frames) ? unwrapped - static_cast<double> (frames) : unwrapped;

    _wetTarget = _freeHasContent && _eraseCursor < 0 ? 1.0f : 0.0f;
    updateEnvelopes ();

    const double width = std::max (0.0, speed - _writeSpeed);
    _lastOutputLeft = tape->read (0, _head, frames, width);
    _lastOutputRight = tape->read (1, _head, frames, width);
    left = _lastOutputLeft * _wetEnvelope;
    right = _lastOutputRight * _wetEnvelope;

    if (_eraseCursor < 0)
    {
        if (_recordEnvelope > 0.0f || _overdubEnvelope > 0.0f)
        {
            _freeHasContent = true;
            _writeSpeed = speed;
        }
        if (_freeHasContent)
            writeSpan (*tape, previous, unwrapped, frames, false, inLeft, inRight);

        // After one whole pass, Rec moves on to overdubbing on top of what it just recorded.
        if (_isRecording)
        {
            _recordedCells += speed;
            if (_recordedCells >= static_cast<double> (frames))
            {
                _isRecording = false;
                _isOverdubbing = true;
            }
        }
    }

    _previousLeft = inLeft;
    _previousRight = inRight;
}

void Looper::startRecording ()
{
    auto* tape = _syncTape.load ();
    _isArmed = false;
    _isClosing = false;
    _isPlayArmed = false;
    _isOverdubbing = false;
    _overdubEnvelope = 0.0f;
    _isPlaying = false;
    _transportJumped = false;

    _syncState = SyncState::Recording;
    _isRecording = true;
    _recordEnvelope = 1.0f; // At full level right away, so the first attack in the loop is kept.
    _writeSpeed = 1.0;

    _anchor = _nextBar;
    _framesPerQuarter = _sampleRate * 60.0 / _tempo;
    _targetQuarters = _targetBars > 0.0 ? _targetBars * _barLength : 0.0;
    _loopQuarters = 0.0;
    _loopFrames = 0;
    _head = -1e-9; // Just before cell 0, so that the first span includes it.
    _wetEnvelope = 0.0f;
    _overviewBucket = -1;
    tape->clearOverview ();
}

void Looper::closeRecording (double quarters)
{
    auto* tape = _syncTape.load ();
    _loopQuarters = quarters;
    _loopFrames = std::min (static_cast<int64_t> (std::llround (quarters * _framesPerQuarter)), tape->getCapacity ());
    _syncState = SyncState::Loop;
    _isClosing = false;
    _isPlaying = true;

    // Recording fades out over the start of the loop, which crossfades the seam, and moves on to
    // overdubbing on top of what it just recorded.
    _isRecording = false;
    _isOverdubbing = true;
    _head -= static_cast<double> (_loopFrames);
}

void Looper::cancelRecording ()
{
    _isRecording = false;
    _recordEnvelope = 0.0f;
    _isClosing = false;
    _syncState = SyncState::Empty;
    _loopFrames = 0;
    _loopQuarters = 0.0;
    if (auto* tape = _syncTape.load ())
        tape->clearOverview ();
}

void Looper::processSync (float inLeft, float inRight, float& left, float& right)
{
    auto* tape = _syncTape.load ();

    if (!_transportRunning)
    {
        // Follows the transport: everything waits, with the output fading out.
        _wetTarget = 0.0f;
        updateEnvelopes ();
        left = _lastOutputLeft * _wetEnvelope;
        right = _lastOutputRight * _wetEnvelope;
        _previousLeft = inLeft;
        _previousRight = inRight;
        return;
    }

    _quarters += _quartersPerSample;

    if (_quarters >= _nextBar)
    {
        if (_isArmed && tape)
            startRecording ();
        else if (_syncState == SyncState::Recording && _isClosing && _nextBar > _anchor)
            closeRecording (_nextBar - _anchor);
        else if (_isPlayArmed)
        {
            _isPlayArmed = false;
            _isPlaying = true;
        }
        _nextBar += _barLength;
    }

    const double speed = _quartersPerSample * _framesPerQuarter;

    if (_syncState == SyncState::Recording)
    {
        const double elapsed = _quarters - _anchor;
        const double head = elapsed * _framesPerQuarter;

        if (_targetQuarters > 0.0 && elapsed >= _targetQuarters)
            closeRecording (_targetQuarters);
        else if (elapsed >= static_cast<double> (kMaxBars) * _barLength)
            closeRecording (static_cast<double> (kMaxBars) * _barLength);
        else if (head + speed + 2.0 >= static_cast<double> (tape->getCapacity ()))
        {
            const double wholeBars = std::floor (elapsed / _barLength) * _barLength;
            if (wholeBars > 0.0)
                closeRecording (wholeBars);
            else
                cancelRecording ();
        }
        else
        {
            updateEnvelopes ();
            writeSpan (*tape, _head, head, tape->getCapacity (), true, inLeft, inRight);
            _head = head;
            _previousLeft = inLeft;
            _previousRight = inRight;
            left = 0.0f;
            right = 0.0f;
            return;
        }
    }

    if (_syncState != SyncState::Loop || !tape)
    {
        _wetTarget = 0.0f;
        updateEnvelopes ();
        left = 0.0f;
        right = 0.0f;
        _previousLeft = inLeft;
        _previousRight = inRight;
        return;
    }

    // The position in the loop comes from the musical position, so it can't drift off the grid.
    const double cycles = (_quarters - _anchor) / _loopQuarters;
    const double head = (cycles - std::floor (cycles)) * static_cast<double> (_loopFrames);
    const auto frames = static_cast<double> (_loopFrames);

    double delta = head - _head;
    if (delta < 0.0)
        delta += frames;

    _wetTarget = _isPlaying ? 1.0f : 0.0f;
    updateEnvelopes ();

    const double width = std::max (0.0, speed - _writeSpeed);
    _lastOutputLeft = tape->read (0, head, _loopFrames, width);
    _lastOutputRight = tape->read (1, head, _loopFrames, width);
    left = _lastOutputLeft * _wetEnvelope;
    right = _lastOutputRight * _wetEnvelope;

    // A big step means the head was moved by a locate, so there's nothing to fill in between.
    if (_isPlaying && delta < std::max (speed * 2.0, 64.0))
    {
        const double from = _head < 0.0 ? _head + frames : _head;
        writeSpan (*tape, from, from + delta, _loopFrames, false, inLeft, inRight);
    }

    _head = head;
    _previousLeft = inLeft;
    _previousRight = inRight;
}

void Looper::processSample (float& left, float& right)
{
    const float inLeft = left;
    const float inRight = right;
    left = 0.0f;
    right = 0.0f;

    if (_mode == Mode::Free)
        processFree (inLeft, inRight, left, right);
    else
        processSync (inLeft, inRight, left, right);
}

void Looper::endBlock ()
{
    auto& status = _bridge.status;
    const bool isSync = _mode == Mode::Sync;
    const Tape* tape = isSync ? _syncTape.load () : _freeTape.load ();

    status.isSyncMode.store (isSync);
    status.isRecording.store (_isRecording);
    status.isOverdubbing.store (_isOverdubbing);
    status.isArmed.store (_isArmed);
    status.isClosing.store (_isClosing);
    status.isPlayArmed.store (_isPlayArmed);
    status.isPlaying.store (isSync ? _isPlaying : _freePlaying);
    status.isTransportRunning.store (_transportRunning);
    status.transportJumped.store (_transportJumped);
    status.activeTape.store (tape);
    status.playhead.store (_head);
    status.writeSpeed.store (_writeSpeed);
    status.tempo.store (_tempo);

    _nextExpectedQuarters = _quarters;
    _hasPosition = true;

    if (isSync)
    {
        const bool isRecording = _syncState == SyncState::Recording;
        status.hasContent.store (_syncState != SyncState::Empty);
        status.loopFrames.store (_loopFrames);
        status.loopQuarters.store (_loopQuarters);
        status.anchorQuarters.store (_anchor);
        status.framesPerQuarter.store (_framesPerQuarter);

        int64_t displayFrames = _loopFrames;
        if (isRecording)
        {
            displayFrames = _targetQuarters > 0.0 ? static_cast<int64_t> (_targetQuarters * _framesPerQuarter)
                                                  : static_cast<int64_t> (_head);
        }
        status.displayFrames.store (displayFrames);
    }
    else
    {
        const int64_t frames = tape ? tape->getCapacity () : 0;
        status.hasContent.store (_freeHasContent);
        status.loopFrames.store (frames);
        status.displayFrames.store (frames);
        status.loopQuarters.store (0.0);
    }
}

} // namespace LoopTexturer
