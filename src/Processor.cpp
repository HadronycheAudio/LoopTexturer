#include "Processor.h"

#include "Controller.h"
#include "dsp/Smoother.h"

#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#if defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#endif

using namespace Steinberg;

namespace LoopTexturer {

namespace {

constexpr double kSmoothingMs = 20.0;
constexpr double kMinQ = 0.7071;
constexpr double kMaxQ = 20.0;

// Stops very small values in the filters from slowing the CPU down.
class FlushDenormals
{
public:
#if defined(_M_X64) || defined(__x86_64__)
    FlushDenormals () : _previous (_mm_getcsr ()) { _mm_setcsr (_previous | 0x8040); }
    ~FlushDenormals () { _mm_setcsr (_previous); }

private:
    unsigned int _previous;
#endif
};

float qFromResonance (double percent)
{
    return static_cast<float> (kMinQ * std::pow (kMaxQ / kMinQ, percent / 100.0));
}

float volumeToGain (double db)
{
    return db <= kVolumeSilenceDb ? 0.0f : dbToGain (db);
}

} // namespace

Processor::Processor ()
{
    setControllerClass (Controller::getClassFUID ());

    for (Vst::ParamID id = 0; id < kParamCount; ++id)
    {
        const auto& spec = getParamSpec (id);
        _params[id] = plainToNormalized (spec, spec.defaultValue);
    }
}

tresult PLUGIN_API Processor::initialize (FUnknown* context)
{
    const tresult result = AudioEffect::initialize (context);
    if (result != kResultOk)
        return result;

    addAudioInput (STR16 ("Stereo In"), Vst::SpeakerArr::kStereo);
    addAudioOutput (STR16 ("Stereo Out"), Vst::SpeakerArr::kStereo);
    return kResultOk;
}

tresult PLUGIN_API Processor::setActive (TBool state)
{
    if (state)
        prepareDsp ();

    return AudioEffect::setActive (state);
}

tresult PLUGIN_API Processor::setBusArrangements (Vst::SpeakerArrangement* inputs, int32 numIns,
                                                  Vst::SpeakerArrangement* outputs, int32 numOuts)
{
    if (numIns == 1 && numOuts == 1 && inputs[0] == Vst::SpeakerArr::kStereo && outputs[0] == Vst::SpeakerArr::kStereo)
        return AudioEffect::setBusArrangements (inputs, numIns, outputs, numOuts);

    return kResultFalse;
}

tresult PLUGIN_API Processor::canProcessSampleSize (int32 symbolicSampleSize)
{
    return symbolicSampleSize == Vst::kSample32 ? kResultTrue : kResultFalse;
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
    return normalizedToPlain (getParamSpec (id), _params[id]);
}

void Processor::readParameterChanges (Vst::IParameterChanges* changes)
{
    if (!changes)
        return;

    const int32 count = changes->getParameterCount ();
    for (int32 i = 0; i < count; ++i)
    {
        auto* queue = changes->getParameterData (i);
        if (!queue || queue->getParameterId () >= kParamCount || queue->getPointCount () == 0)
            continue;

        // Smoothing covers the rest, so only the last value in each block is needed.
        int32 offset = 0;
        Vst::ParamValue value = 0.0;
        if (queue->getPoint (queue->getPointCount () - 1, offset, value) == kResultTrue)
            _params[queue->getParameterId ()] = value;
    }
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
}

void Processor::prepareDsp ()
{
    const double sampleRate = processSetup.sampleRate;

    for (auto* smoother : { &_gain, &_volume, &_pan, &_panLfoAmount, &_filterPitch, &_filterLfoAmount, &_filterQ })
        smoother->prepare (sampleRate, kSmoothingMs);

    _gain.snapTo (dbToGain (plain (kGain)));
    _volume.snapTo (volumeToGain (plain (kVolume)));
    _pan.snapTo (static_cast<float> (plain (kPan) / 100.0));
    _panLfoAmount.snapTo (static_cast<float> (plain (kPanLfoAmount) / 100.0));
    _filterPitch.snapTo (static_cast<float> (std::log2 (plain (kFilterFreq))));
    _filterQ.snapTo (qFromResonance (plain (kFilterResonance)));
    _filterLfoAmount.snapTo (static_cast<float> (plain (kFilterLfoAmount)));

    _panLfo.prepare (sampleRate);
    _filterLfo.prepare (sampleRate);
    _filter.prepare (sampleRate);

    _saturator.prepare (sampleRate, dbToGain (plain (kSatDrive)),
                        plain (kSatOn) > 0.5 ? static_cast<float> (plain (kSatMix) / 100.0) : 0.0f);
    _postSaturator.prepare (sampleRate, dbToGain (plain (kPostSatDrive)),
                            plain (kPostSatOn) > 0.5 ? static_cast<float> (plain (kPostSatMix) / 100.0) : 0.0f);

    applyParameters ();
}

tresult PLUGIN_API Processor::process (Vst::ProcessData& data)
{
    readParameterChanges (data.inputParameterChanges);

    if (data.numOutputs == 0 || data.numSamples == 0)
        return kResultOk;

    auto& output = data.outputs[0];
    if (output.numChannels != 2)
    {
        for (int32 c = 0; c < output.numChannels; ++c)
            std::memset (output.channelBuffers32[c], 0, sizeof (float) * static_cast<size_t> (data.numSamples));
        return kResultOk;
    }

    const FlushDenormals flushDenormals;
    applyParameters ();

    const bool hasInput = data.numInputs > 0 && data.inputs[0].numChannels == 2;
    const float* inputLeft = hasInput ? data.inputs[0].channelBuffers32[0] : nullptr;
    const float* inputRight = hasInput ? data.inputs[0].channelBuffers32[1] : nullptr;
    float* outputLeft = output.channelBuffers32[0];
    float* outputRight = output.channelBuffers32[1];

    constexpr float quarterPi = 0.7853981634f;
    constexpr float sqrt2 = 1.414213562f;

    for (int32 i = 0; i < data.numSamples; ++i)
    {
        float left = hasInput ? inputLeft[i] : 0.0f;
        float right = hasInput ? inputRight[i] : 0.0f;

        // Gain
        const float gain = _gain.next ();
        left *= gain;
        right *= gain;

        // Pan: an equal-power balance, scaled to leave the signal unchanged in the centre.
        const float panLfo = _panLfo.next ();
        const float position = std::clamp (_pan.next () + _panLfoAmount.next () * panLfo, -1.0f, 1.0f);
        const float angle = (position + 1.0f) * quarterPi;
        left *= std::cos (angle) * sqrt2;
        right *= std::sin (angle) * sqrt2;

        // Filter
        const float filterLfo = _filterLfo.next ();
        const float pitch = _filterPitch.next () + _filterLfoAmount.next () * filterLfo;
        _filter.setCoefficients (std::pow (2.0f, pitch), _filterQ.next ());
        left = _filter.process (left, 0);
        right = _filter.process (right, 1);

        // Saturator
        _saturator.process (left, right);

        // The looper will go here.

        // Post saturator
        _postSaturator.process (left, right);

        // Volume
        const float volume = _volume.next ();
        outputLeft[i] = left * volume;
        outputRight[i] = right * volume;
    }

    output.silenceFlags = 0;
    return kResultOk;
}

tresult PLUGIN_API Processor::setState (IBStream* state)
{
    if (!state)
        return kResultFalse;

    const bool ok = readParamState (state, [this] (Vst::ParamID id, double value) { _params[id] = value; });
    return ok ? kResultOk : kResultFalse;
}

tresult PLUGIN_API Processor::getState (IBStream* state)
{
    if (!state)
        return kResultFalse;

    ParamValues values;
    for (size_t id = 0; id < values.size (); ++id)
        values[id] = _params[id];

    return writeParamState (state, values) ? kResultOk : kResultFalse;
}

} // namespace LoopTexturer
