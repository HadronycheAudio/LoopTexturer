#include "Processor.h"

#include "Controller.h"
#include "Messages.h"
#include "dsp/FloatGain.h"

#include "hadronyche/dsp/Gain.h"

#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <algorithm>
#include <chrono>
#include <cmath>

using namespace Steinberg;

namespace LoopTexturer {

namespace {

constexpr double kSmoothingMs = 20.0;
constexpr double kMinQ = 0.7071;
constexpr double kMaxQ = 20.0;
constexpr int64 kMaxSavedFrames = int64 (1) << 31;
constexpr int kStreamChunkFloats = 1 << 16;
constexpr auto kWorkerInterval = std::chrono::milliseconds (50);

float qFromResonance (double percent)
{
    return static_cast<float> (kMinQ * std::pow (kMaxQ / kMinQ, percent / 100.0));
}

float volumeToGain (double db)
{
    return static_cast<float> (Hadronyche::volumeToGain (db));
}

// Full dry and full wet meet in the middle, so nothing drops in level while the loop is empty.
float dryGainFromMix (double percent)
{
    return static_cast<float> (Hadronyche::dryGain (percent));
}

float wetGainFromMix (double percent)
{
    return static_cast<float> (Hadronyche::wetGain (percent));
}

Looper::Transport getTransport (const Vst::ProcessContext* context)
{
    Looper::Transport transport {};
    transport.numerator = 4;
    transport.denominator = 4;
    if (!context)
        return transport;

    transport.isValid = (context->state & Vst::ProcessContext::kProjectTimeMusicValid) != 0
                        && (context->state & Vst::ProcessContext::kTempoValid) != 0;
    transport.isPlaying = (context->state & Vst::ProcessContext::kPlaying) != 0;
    transport.quarters = context->projectTimeMusic;
    transport.tempo = context->tempo;

    if ((context->state & Vst::ProcessContext::kTimeSigValid) != 0 && context->timeSigNumerator > 0 && context->timeSigDenominator > 0)
    {
        transport.numerator = context->timeSigNumerator;
        transport.denominator = context->timeSigDenominator;
    }

    const double barLength = transport.numerator * 4.0 / transport.denominator;
    transport.barStartQuarters = (context->state & Vst::ProcessContext::kBarPositionValid) != 0
                                     ? context->barPositionMusic
                                     : std::floor (transport.quarters / barLength) * barLength;
    return transport;
}

// The current peaks, for a tape whose contents were just restored.
void buildOverview (Tape& tape, int64_t frames)
{
    const int64_t bucketFrames = tape.getBucketFrames ();
    for (int64_t start = 0; start < frames; start += bucketFrames)
    {
        float low = 0.0f;
        float high = 0.0f;
        const int64_t end = std::min (start + bucketFrames, frames);
        for (int64_t i = start; i < end; ++i)
        {
            for (int channel = 0; channel < 2; ++channel)
            {
                const float value = tape.load (channel, i);
                low = std::min (low, value);
                high = std::max (high, value);
            }
        }
        tape.setOverview (start / bucketFrames, low, high);
    }
}

} // namespace

Processor::Processor ()
: StereoProcessor (Controller::getClassFUID (), true, getParamSpec,
                   [] (IBStream* state, const Hadronyche::ParamValueSink& onValue) { return readParamState (state, onValue); }, writeParamState),
  _bridge (LooperBridge::create ()), _looper (*_bridge, Saturator::kLatency)
{
    processContextRequirements.needTransportState ().needProjectTimeMusic ().needBarPositionMusic ().needTempo ().needTimeSignature ();
}

tresult PLUGIN_API Processor::initialize (FUnknown* context)
{
    const tresult result = StereoProcessor::initialize (context);
    if (result != kResultOk)
        return result;

    _stopWorker = false;
    _worker = std::thread ([this] { runWorker (); });
    return kResultOk;
}

tresult PLUGIN_API Processor::terminate ()
{
    {
        const std::lock_guard<std::mutex> lock (_workerMutex);
        _stopWorker = true;
    }
    _workerWake.notify_all ();
    if (_worker.joinable ())
        _worker.join ();

    return AudioEffect::terminate ();
}

void Processor::runWorker ()
{
    std::unique_lock<std::mutex> lock (_workerMutex);
    while (!_stopWorker)
    {
        _workerWake.wait_for (lock, kWorkerInterval);
        if (_stopWorker)
            break;
        lock.unlock ();

        const std::lock_guard<std::mutex> tapeLock (_tapeChangeMutex);

        const int64_t requested = _bridge->requestedSyncFrames.exchange (0);
        if (requested > 0 && _looper.canOfferSyncTape ())
        {
            auto tape = std::make_shared<Tape> (requested);
            _bridge->addTape (tape);
            _looper.offerSyncTape (tape.get ());
        }

        const int64_t requestedFree = _bridge->requestedFreeFrames.exchange (0);
        if (requestedFree > 0 && _looper.canOfferFreeTape ())
        {
            auto tape = std::make_shared<Tape> (requestedFree);
            _bridge->addTape (tape);
            _looper.offerFreeTape (tape.get ());
        }

        if (!_looper.hasPendingLoad ())
        {
            const auto inUse = _looper.getTapesInUse ();
            _bridge->releaseTapesExcept ({ inUse.begin (), inUse.end () });
        }

        lock.lock ();
    }
}

tresult PLUGIN_API Processor::connect (Vst::IConnectionPoint* other)
{
    const tresult result = AudioEffect::connect (other);
    if (result == kResultOk)
    {
        if (auto* message = allocateMessage ())
        {
            message->setMessageID (kMessageLooperBridge);
            message->getAttributes ()->setInt (kAttributeBridgeId, _bridge->getId ());
            sendMessage (message);
            message->release ();
        }
    }
    return result;
}

tresult PLUGIN_API Processor::notify (Vst::IMessage* message)
{
    if (message && FIDStringsEqual (message->getMessageID (), kMessageLooperCommand))
    {
        int64 command = 0;
        if (message->getAttributes ()->getInt (kAttributeCommand, command) == kResultOk)
            _looper.sendCommand (static_cast<int> (command));
        return kResultOk;
    }
    return AudioEffect::notify (message);
}

uint32 PLUGIN_API Processor::getLatencySamples ()
{
    return static_cast<uint32> (Saturator::kLatency * 2);
}

// The looper's tape keeps running without any input, so the host must not suspend the plug-in.
uint32 PLUGIN_API Processor::getTailSamples ()
{
    return Vst::kInfiniteTail;
}

double Processor::plain (ParamId id) const
{
    return _params.plain (id);
}

void Processor::applyParameters ()
{
    _gain.setTarget (dbToGain (plain (kGain)));
    _volume.setTarget (volumeToGain (plain (kVolume)));

    _pan.setTarget (static_cast<float> (plain (kPan) / 100.0));
    _panLfoAmount.setTarget (static_cast<float> (plain (kPanLfoAmount) / 100.0));
    _panLfo.setRate (plain (kPanLfoRate));
    _panLfo.setWave (static_cast<Lfo::Wave> (static_cast<int> (plain (kPanLfoWave))));

    _filter.setMode (static_cast<StateVariableFilter::Mode> (static_cast<int> (plain (kFilterMode))));
    _filterPitch.setTarget (static_cast<float> (std::log2 (plain (kFilterFreq))));
    _filterQ.setTarget (qFromResonance (plain (kFilterResonance)));
    _filterLfoAmount.setTarget (static_cast<float> (plain (kFilterLfoAmount)));
    _filterLfo.setRate (plain (kFilterLfoRate));
    _filterLfo.setWave (static_cast<Lfo::Wave> (static_cast<int> (plain (kFilterLfoWave))));

    _saturator.setMode (static_cast<Saturator::Mode> (static_cast<int> (plain (kSatMode))));
    _saturator.setDrive (dbToGain (plain (kSatDrive)));
    _saturator.setMix (plain (kSatOn) > 0.5 ? static_cast<float> (plain (kSatMix) / 100.0) : 0.0f);

    _postSaturator.setMode (static_cast<Saturator::Mode> (static_cast<int> (plain (kPostSatMode))));
    _postSaturator.setDrive (dbToGain (plain (kPostSatDrive)));
    _postSaturator.setMix (plain (kPostSatOn) > 0.5 ? static_cast<float> (plain (kPostSatMix) / 100.0) : 0.0f);

    _dryGain.setTarget (dryGainFromMix (plain (kLooperDryWet)));
    _wetGain.setTarget (wetGainFromMix (plain (kLooperDryWet)));
}

Looper::Settings Processor::getLooperSettings () const
{
    Looper::Settings settings {};
    settings.mode = plain (kLooperMode) > 0.5 ? Looper::Mode::Sync : Looper::Mode::Free;
    settings.freeLengthSeconds = plain (kLooperFreeLength);
    settings.feedback = plain (kLooperFeedback) / 100.0;

    switch (static_cast<int> (plain (kLooperBars)))
    {
        case kBarsHalf: settings.targetBars = 0.5; break;
        case kBarsOne: settings.targetBars = 1.0; break;
        case kBarsTwo: settings.targetBars = 2.0; break;
        case kBarsFour: settings.targetBars = 4.0; break;
        case kBarsSix: settings.targetBars = 6.0; break;
        case kBarsEight: settings.targetBars = 8.0; break;
        case kBarsCustom: settings.targetBars = plain (kLooperCustomBars); break;
        case kBarsOpen: settings.targetBars = 0.0; break;
    }
    return settings;
}

void Processor::prepareDsp ()
{
    const double sampleRate = processSetup.sampleRate;

    for (auto* smoother : { &_gain, &_volume, &_pan, &_panLfoAmount, &_filterPitch, &_filterLfoAmount, &_filterQ, &_dryGain, &_wetGain })
        smoother->prepare (sampleRate, kSmoothingMs);

    _gain.snapTo (dbToGain (plain (kGain)));
    _volume.snapTo (volumeToGain (plain (kVolume)));
    _pan.snapTo (static_cast<float> (plain (kPan) / 100.0));
    _panLfoAmount.snapTo (static_cast<float> (plain (kPanLfoAmount) / 100.0));
    _filterPitch.snapTo (static_cast<float> (std::log2 (plain (kFilterFreq))));
    _filterQ.snapTo (qFromResonance (plain (kFilterResonance)));
    _filterLfoAmount.snapTo (static_cast<float> (plain (kFilterLfoAmount)));
    _dryGain.snapTo (dryGainFromMix (plain (kLooperDryWet)));
    _wetGain.snapTo (wetGainFromMix (plain (kLooperDryWet)));

    _panLfo.prepare (sampleRate);
    _filterLfo.prepare (sampleRate);
    _filter.prepare (sampleRate);

    _saturator.prepare (sampleRate, dbToGain (plain (kSatDrive)),
                        plain (kSatOn) > 0.5 ? static_cast<float> (plain (kSatMix) / 100.0) : 0.0f);
    _postSaturator.prepare (sampleRate, dbToGain (plain (kPostSatDrive)),
                            plain (kPostSatOn) > 0.5 ? static_cast<float> (plain (kPostSatMix) / 100.0) : 0.0f);

    for (auto& delay : _dryDelay)
        delay.fill (0.0f);
    _dryDelayPosition = 0;

    {
        const std::lock_guard<std::mutex> lock (_tapeChangeMutex);
        _looper.prepare (sampleRate);
    }

    applyParameters ();
}

void Processor::renderBlock (Vst::ProcessData& data, Vst::AudioBusBuffers& output)
{
    applyParameters ();
    _looper.beginBlock (getLooperSettings (), getTransport (data.processContext));

    const bool hasInput = data.numInputs > 0 && data.inputs[0].numChannels == 2;
    const float* inputLeft = hasInput ? data.inputs[0].channelBuffers32[0] : nullptr;
    const float* inputRight = hasInput ? data.inputs[0].channelBuffers32[1] : nullptr;
    float* outputLeft = output.channelBuffers32[0];
    float* outputRight = output.channelBuffers32[1];

    constexpr float quarterPi = 0.7853981634f;
    constexpr float sqrt2 = 1.414213562f;

    for (int32 i = 0; i < data.numSamples; ++i)
    {
        // A broken sample upstream would otherwise stay in the filters and the loop for ever.
        float left = hasInput && std::isfinite (inputLeft[i]) ? inputLeft[i] : 0.0f;
        float right = hasInput && std::isfinite (inputRight[i]) ? inputRight[i] : 0.0f;

        // Gain
        const float gain = _gain.nextFloat ();
        left *= gain;
        right *= gain;

        // Pan: an equal-power balance, scaled to leave the signal unchanged in the centre.
        const float panLfo = _panLfo.next ();
        const float position = std::clamp (_pan.nextFloat () + _panLfoAmount.nextFloat () * panLfo, -1.0f, 1.0f);
        const float angle = (position + 1.0f) * quarterPi;
        left *= std::cos (angle) * sqrt2;
        right *= std::sin (angle) * sqrt2;

        // Filter
        const float filterLfo = _filterLfo.next ();
        const float pitch = _filterPitch.nextFloat () + _filterLfoAmount.nextFloat () * filterLfo;
        _filter.setCoefficients (std::pow (2.0f, pitch), _filterQ.nextFloat ());
        left = _filter.process (left, 0);
        right = _filter.process (right, 1);

        // Saturator
        _saturator.process (left, right);

        // The dry signal bypasses the looper and post saturator, delayed to stay aligned with them.
        const auto delayIndex = static_cast<size_t> (_dryDelayPosition);
        const float dryLeft = _dryDelay[0][delayIndex];
        const float dryRight = _dryDelay[1][delayIndex];
        _dryDelay[0][delayIndex] = left;
        _dryDelay[1][delayIndex] = right;
        _dryDelayPosition = _dryDelayPosition + 1 == Saturator::kLatency ? 0 : _dryDelayPosition + 1;

        // Looper: takes the input, and leaves the loop's output in left and right.
        _looper.processSample (left, right);

        // Post saturator
        _postSaturator.process (left, right);

        // Dry/wet
        const float dryGain = _dryGain.nextFloat ();
        const float wetGain = _wetGain.nextFloat ();
        left = dryLeft * dryGain + left * wetGain;
        right = dryRight * dryGain + right * wetGain;

        // Volume
        const float volume = _volume.nextFloat ();
        outputLeft[i] = left * volume;
        outputRight[i] = right * volume;
    }

    _looper.endBlock ();

    output.silenceFlags = 0;
}

bool Processor::writeLoopState (IBStream* state)
{
    // A restored loop the audio thread hasn't picked up yet still counts as the current loop.
    const bool isPending = _looper.hasPendingLoad ();
    const auto& status = _bridge->status;

    const auto tape = isPending ? _lastLoadedTape : _bridge->getActiveTape ();
    const bool isSync = isPending ? _lastLoad.mode == Looper::Mode::Sync : status.isSyncMode.load ();
    const int64 frames = isPending ? _lastLoad.frames : status.loopFrames.load ();
    const double quarters = isPending ? _lastLoad.quarters : status.loopQuarters.load ();
    const bool hasContent = isPending ? _lastLoad.tape != nullptr : status.hasContent.load ();
    const bool hasLoop = tape && hasContent && frames > 0 && (!isSync || quarters > 0.0);

    const int32 hasLoopFlag = hasLoop ? 1 : 0;
    if (state->write (const_cast<int32*> (&hasLoopFlag), sizeof (hasLoopFlag)) != kResultOk)
        return false;
    if (!hasLoop)
        return true;

    const int32 mode = isSync ? 1 : 0;
    const double anchor = isPending ? _lastLoad.anchorQuarters : status.anchorQuarters.load ();
    const double framesPerQuarter = isPending ? _lastLoad.framesPerQuarter : status.framesPerQuarter.load ();
    const double writeSpeed = isPending ? _lastLoad.writeSpeed : status.writeSpeed.load ();

    int64 frameCount = frames;
    double fields[] = { quarters, anchor, framesPerQuarter, writeSpeed };
    if (state->write (const_cast<int32*> (&mode), sizeof (mode)) != kResultOk
        || state->write (&frameCount, sizeof (frameCount)) != kResultOk
        || state->write (fields, sizeof (fields)) != kResultOk)
        return false;

    std::vector<float> chunk (kStreamChunkFloats);
    for (int channel = 0; channel < 2; ++channel)
    {
        for (int64 start = 0; start < frames; start += kStreamChunkFloats)
        {
            const int count = static_cast<int> (std::min<int64> (kStreamChunkFloats, frames - start));
            for (int i = 0; i < count; ++i)
                chunk[static_cast<size_t> (i)] = tape->load (channel, start + i);
            if (state->write (chunk.data (), count * static_cast<int32> (sizeof (float))) != kResultOk)
                return false;
        }
    }
    return true;
}

void Processor::readLoopState (IBStream* state)
{
    auto load = std::make_unique<Looper::LoadedLoop> ();
    load->mode = plain (kLooperMode) > 0.5 ? Looper::Mode::Sync : Looper::Mode::Free;
    load->tape = nullptr;
    load->writeSpeed = 1.0;

    std::shared_ptr<Tape> tape;

    const auto readValue = [state] (void* value, int32 size) {
        int32 got = 0;
        return state->read (value, size, &got) == kResultOk && got == size;
    };

    int32 hasLoop = 0;
    if (readValue (&hasLoop, sizeof (hasLoop)) && hasLoop == 1)
    {
        int32 mode = 0;
        int64 frames = 0;
        double fields[4] {};
        if (readValue (&mode, sizeof (mode)) && readValue (&frames, sizeof (frames)) && readValue (fields, sizeof (fields))
            && frames > 0 && frames <= kMaxSavedFrames)
        {
            tape = std::make_shared<Tape> (frames);

            bool ok = true;
            std::vector<float> chunk (kStreamChunkFloats);
            for (int channel = 0; ok && channel < 2; ++channel)
            {
                for (int64 start = 0; ok && start < frames; start += kStreamChunkFloats)
                {
                    const int count = static_cast<int> (std::min<int64> (kStreamChunkFloats, frames - start));
                    ok = readValue (chunk.data (), count * static_cast<int32> (sizeof (float)));
                    for (int i = 0; ok && i < count; ++i)
                        tape->store (channel, start + i, std::isfinite (chunk[static_cast<size_t> (i)]) ? chunk[static_cast<size_t> (i)] : 0.0f);
                }
            }

            if (ok)
            {
                buildOverview (*tape, frames);
                load->mode = mode == 1 ? Looper::Mode::Sync : Looper::Mode::Free;
                load->tape = tape.get ();
                load->frames = frames;
                load->quarters = fields[0];
                load->anchorQuarters = fields[1];
                load->framesPerQuarter = fields[2];
                load->writeSpeed = fields[3];
            }
            else
            {
                tape.reset ();
            }
        }
    }

    const std::lock_guard<std::mutex> lock (_tapeChangeMutex);
    if (tape)
        _bridge->addTape (tape);

    _lastLoadedTape = tape;
    _lastLoad = *load;
    _loads.push_back (std::move (load));
    _looper.loadLoop (_loads.back ().get ());
}

tresult PLUGIN_API Processor::setState (IBStream* state)
{
    if (!state || !restoreParams (state))
        return kResultFalse;

    // Older states have no looper section, which reads as having no loop.
    readLoopState (state);
    return kResultOk;
}

tresult PLUGIN_API Processor::getState (IBStream* state)
{
    if (!state || !saveParams (state))
        return kResultFalse;

    return writeLoopState (state) ? kResultOk : kResultFalse;
}

} // namespace LoopTexturer
